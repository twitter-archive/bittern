/*
 * Bittern Cache.
 *
 * Copyright(c) 2013, 2014, 2015, Twitter, Inc., All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

/*! \file */

#include "bittern_cache.h"

void cache_bgwriter_io_end(struct bittern_cache *bc,
			   struct work_item *wi,
			   struct cache_block *cache_block)
{
	ASSERT(cache_block != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(wi->wi_cache_block == cache_block);
	ASSERT(wi->wi_original_bio == NULL);
	ASSERT(wi->wi_cloned_bio == NULL);
	ASSERT(wi->wi_io_xid != 0);

	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, cache_block, NULL, NULL,
		 "writeback-done");
	ASSERT(cache_block->bcb_state ==
	       S_DIRTY_WRITEBACK_UPD_METADATA_END ||
	       cache_block->bcb_state ==
	       S_DIRTY_WRITEBACK_INV_UPD_METADATA_END);

	ASSERT((wi->wi_flags & WI_FLAG_BIO_NOT_CLONED) != 0);
	ASSERT(cache_block_is_held(bc, cache_block));

	if (cache_block->bcb_state ==
	    S_DIRTY_WRITEBACK_INV_UPD_METADATA_END) {
		/*
		 * move to invalid list
		 */
		cache_move_to_invalid(bc, cache_block, 1);
		atomic_dec(&bc->bc_pending_invalidate_requests);
	} else {
		/*
		 * move to clean list
		 */
		cache_move_to_clean(bc, cache_block);
	}

	cache_timer_add(&bc->bc_timer_writebacks, wi->wi_ts_started);

	work_item_free(bc, wi);

	atomic_dec(&bc->bc_pending_writeback_requests);
	atomic_dec(&bc->bc_pending_requests);
	atomic_inc(&bc->bc_completed_requests);
	atomic_inc(&bc->bc_completed_writebacks);

	/*
	 * wakeup bgwriter
	 * (other possible waiters are woken up in cache_move_to_*)
	 */
	wake_up_interruptible(&bc->bc_bgwriter_wait);
}

/*
 * start one writeback, possibly using sector_hint.
 * returns 1 if writeback was started, 0 otherwise.
 */
int cache_bgwriter_io_start_one(struct bittern_cache *bc,
					sector_t sector_hint,
					sector_t *o_sector_hint)
{
	unsigned long flags, cache_flags;
	struct work_item *wi;
	struct cache_block *cache_block = NULL;
	int ret;
	enum cache_state update_state;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT(is_sector_number_valid(sector_hint) ||
	       sector_hint == SECTOR_NUMBER_INVALID);
	ASSERT(o_sector_hint != NULL);
	*o_sector_hint = SECTOR_NUMBER_INVALID;

	if (sector_hint == SECTOR_NUMBER_INVALID) {
		ret = cache_get_dirty_from_head(bc,
						&cache_block,
						bc->
						bc_bgwriter_curr_min_age_secs);
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
			 "ret=%d (m=%u, q=%u, p=%u, pw=%u)", ret,
			 bc->bc_bgwriter_curr_min_age_secs,
			 bc->bc_bgwriter_curr_queue_depth,
			 atomic_read(&bc->bc_pending_requests),
			 atomic_read(&bc->bc_pending_writeback_requests));
		switch (ret) {
		case -EAGAIN:
			/*
			 * this should almost never happen, as we only get
			 * called when we are below the threshold for
			 * invalid (free) blocks
			 */
			return 0;
		case -EBUSY:
			atomic_inc(&bc->bc_writebacks_stalls);
			bc->bc_bgwriter_stalls_count++;
			bc->bc_bgwriter_cache_block_busy_count++;
			msleep(1);
			return 0;
		case -ETIME:
			/* not considered a write stall */
			bc->bc_bgwriter_too_young_count++;
			msleep(2);
			return 0;
		case 0:
			break;
		default:
			ASSERT("unexpected case value" == 0);
			BUG();
		}
	} else {
		ret = cache_get(bc,
					sector_hint,
					CACHE_FL_HIT,
					&cache_block);
		ASSERT_CACHE_GET_RET(ret);
		switch (ret) {
		case CACHE_GET_RET_HIT_IDLE:
			ASSERT(cache_block != NULL);
			ASSERT_CACHE_BLOCK(cache_block, bc);
			if (cache_block->bcb_state == S_CLEAN) {
				/*
				 * block is not dirty, release
				 */
				bc->bc_bgwriter_hint_block_clean_count++;
				cache_put(bc, cache_block, 1);
				return 0;
			}
			break;
		case CACHE_GET_RET_HIT_BUSY:
			bc->bc_bgwriter_cache_block_busy_count++;
			return 0;
		case CACHE_GET_RET_MISS:
			bc->bc_bgwriter_hint_no_block_count++;
			return 0;
		default:
			ASSERT("unexpected value of cache_get()" ==
			       NULL);
			BUG();
		}
	}

	ASSERT(cache_block_is_held(bc, cache_block));

	bc->bc_bgwriter_ready_count++;

	/*
	 * it's significantly cheaper for the bgwriter to flush and
	 * invalidate a cache block rather than flush to a clean
	 * state and then let the invalidator thread do the invalidation.
	 * so if we are very close to kick the invalidator, we'd rather
	 * flush and invalidate from here.
	 */
	if (cache_invalidator_has_work_schmitt(bc))
		update_state = S_INVALID;
	else
		update_state = S_CLEAN;

	ASSERT(cache_block->bcb_state == S_DIRTY);
	ASSERT(atomic_read(&cache_block->bcb_refcount) > 0);
	ASSERT(is_sector_number_valid(cache_block->bcb_sector));

	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);
	/*
	 * remove from dirty list.
	 * we need to remove it now to avoid bgwriter to hit the same block
	 * during the next scan (which means in the end_io we do nothing)
	 */
	list_del_init(&cache_block->bcb_entry_cleandirty);
	if (update_state == S_INVALID) {
		/* also remove from valid list */
		list_del_init(&cache_block->bcb_entry);
		/*
		 * VALID_DIRTY --> VALID_DIRTY_WRITEBACK_INV_CPT_DEVICE
		 */
		cache_state_transition_initial(bc,
					cache_block,
					TS_WRITEBACK_INV_WB,
					S_DIRTY_WRITEBACK_INV_CPF_CACHE_START);
	} else {
		/*
		 * VALID_DIRTY --> VALID_DIRTY_WRITEBACK_CPT_DEVICE
		 */
		cache_state_transition_initial(bc,
					cache_block,
					TS_WRITEBACK_WB,
					S_DIRTY_WRITEBACK_CPF_CACHE_START);
	}
	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		 "writeback dirty block (m=%u, q=%u, p=%u, pw=%u)",
		 bc->bc_bgwriter_curr_min_age_secs,
		 bc->bc_bgwriter_curr_queue_depth,
		 atomic_read(&bc->bc_pending_requests),
		 atomic_read(&bc->bc_pending_writeback_requests));

	/*
	 * allocate work_item and initialize it
	 */
	wi = work_item_allocate(bc,
				cache_block,
				NULL,
				(WI_FLAG_BIO_NOT_CLONED |
				 WI_FLAG_XID_USE_CACHE_BLOCK));
	M_ASSERT_FIXME(wi != NULL);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(wi->wi_io_xid != 0);
	ASSERT(wi->wi_io_xid == cache_block->bcb_xid);
	ASSERT(wi->wi_original_bio == NULL);
	ASSERT(wi->wi_cloned_bio == NULL);
	ASSERT(wi->wi_cache == bc);

	ret = pmem_context_setup(bc,
				 bc->bc_kmem_threads,
				 cache_block,
				 NULL,
				 &wi->wi_pmem_ctx);
	M_ASSERT_FIXME(ret == 0);

	wi->wi_ts_started = current_kernel_time_nsec();

	atomic_inc(&bc->bc_writebacks);
	atomic_inc(&bc->bc_pending_writeback_requests);
	atomic_inc(&bc->bc_pending_requests);
	if (update_state == S_INVALID) {
		int val;
		atomic_inc(&bc->bc_writebacks_invalid);
		atomic_inc(&bc->bc_invalidations);
		atomic_inc(&bc->bc_invalidations_writeback);
		val = atomic_inc_return(&bc->bc_pending_invalidate_requests);
		atomic_set_if_higher(
				&bc->bc_highest_pending_invalidate_requests,
				val);
	} else {
		atomic_inc(&bc->bc_writebacks_clean);
	}

	/*
	 * set hint for next block writeback
	 * don't bother checking for going beyond the end of the cached device,
	 * because we cannot be caching it in the first place.
	 */
	*o_sector_hint = cache_block->bcb_sector + SECTORS_PER_CACHE_BLOCK;

	/*
	 * kick off state machine to write this out.
	 * cache_bgwriter_io_endio() will be called on completion.
	 */
	if (update_state == S_INVALID)
		work_item_add_pending_io(bc,
					 wi,
					 "writeback-invalidate",
					 cache_block->bcb_sector,
					 WRITE);
	else
		work_item_add_pending_io(bc,
					 wi,
					 "writeback-flush",
					 cache_block->bcb_sector,
					 WRITE);
	ASSERT(wi->wi_cache_block == cache_block);
	cache_state_machine(bc, wi, 0);

	return 1;
}

/*
 * wait for needed writeback resources (queue and buffers).
 * return -EWOULDBLOCK if waiting would be needed and we indicated we do not
 * want to wait, 0 otherwise.
 */
int cache_bgwriter_wait_for_resources(struct bittern_cache *bc,
					      bool do_wait)
{
	unsigned int pending = atomic_read(&bc->bc_pending_writeback_requests);
	int needs_to_wait = false;

	ASSERT(do_wait == false || do_wait == true);

	if (pending == 0)
		return 0;
	if (pending >= bc->bc_bgwriter_curr_queue_depth) {
		/*
		 * writeback queue busy
		 */
		bc->bc_bgwriter_queue_full_count++;
		needs_to_wait = true;
	}
	if (needs_to_wait) {
		if (!do_wait) {
			bc->bc_bgwriter_stalls_nowait_count++;
			return -EWOULDBLOCK;
		}
		atomic_inc(&bc->bc_writebacks_stalls);
		bc->bc_bgwriter_stalls_count++;
		/*
		 * wait for at least one writeback to complete
		 */
		wait_event_interruptible(bc->bc_bgwriter_wait,
					 pending !=
					 atomic_read(&bc->
						     bc_pending_writeback_requests));
		if (signal_pending(current))
			flush_signals(current);
	}
	return 0;
}

/*
 * start a batch of sequential writebacks.
 * returns count of started writebacks.
 */
int cache_bgwriter_io_start_batch(struct bittern_cache *bc)
{
	int ret, count;
	sector_t sector_hint = SECTOR_NUMBER_INVALID;

	/*
	 * wait for resources
	 */
	ret = cache_bgwriter_wait_for_resources(bc, true);
	ASSERT(ret == 0);

	/* printk_debug("bgwriter: hint[0]=%lu\n", sector_hint); */
	ret =
	    cache_bgwriter_io_start_one(bc, sector_hint, &sector_hint);
	ASSERT(ret == 0 || ret == 1);
	if (ret == 0)
		return 0;
	count = 1;

	/*
	 * FIXME: should take max queue depth into account?
	 */
	if (sector_hint != SECTOR_NUMBER_INVALID) {
		while (count < bc->bc_bgwriter_conf_cluster_size) {
			sector_t last_hint = sector_hint;

			/* shutoff compiler warning (used in dev build) */
			last_hint = last_hint;
			/*
			 * wait for resources
			 * FIXME: wait flag should be policy controlled
			 */
			ret =
			    cache_bgwriter_wait_for_resources(bc,
							      false);
			if (ret < 0)
				break;
			ASSERT(sector_hint != SECTOR_NUMBER_INVALID);
			ret = cache_bgwriter_io_start_one(bc,
							  sector_hint,
							  &sector_hint);
			ASSERT(ret == 0 || ret == 1);
			if (ret == 0) {
				ASSERT(sector_hint == SECTOR_NUMBER_INVALID);
				break;
			}
			ASSERT(sector_hint != SECTOR_NUMBER_INVALID);
			count++;
			BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
				 "hint[%d]=%lu (last_hint=%lu)",
				 count, sector_hint, last_hint);
			ASSERT(last_hint + SECTORS_PER_CACHE_BLOCK ==
			       sector_hint);
		}
	}

	return count;
}

void cache_bgwriter_start_io(struct bittern_cache *bc)
{
	unsigned long jiffies_begin_msecs;
	unsigned int msleep_sum = 0;
	unsigned int msleep_ms = 0;
	unsigned int wb_block_count = 0;
	unsigned int wb_cluster_count = 0;

	BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL, "enter");
	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);

	if (bc->bc_bgwriter_curr_rate_per_sec > 0) {
		/*
		 * rate limiting. in no case we'll issue more than 1000 iops
		 * if rate limiting is enabled.
		 */
		msleep_ms = 1000 / bc->bc_bgwriter_curr_rate_per_sec;
		if (msleep_ms == 0)
			msleep_ms = 1;
	} else {
		/*
		 * not rate limited.
		 */
		msleep_ms = 0;
	}

	jiffies_begin_msecs = jiffies_to_msecs(jiffies);

	while (atomic_read(&bc->bc_valid_entries_dirty) > 0) {
		int count;

		/*
		 * start writeback batch
		 */
		count = cache_bgwriter_io_start_batch(bc);

		if (msleep_ms > 0) {
			msleep(msleep_ms * count);
			msleep_sum += msleep_ms * count;
		}
		wb_block_count += count;
		wb_cluster_count++;
		/*
		 * don't be too busy otherwise we won't be able to recalculate
		 * the writeback tunables. this affects print stats info
		 * (not hugely relevant).
		 * this also affects how often we recalculate writeback
		 * parameters (relevant).
		 * any "reasonable" number for either will work, provided that
		 * they are not too low.
		 * in this case we break out of this loop whenever we queue
		 * at least a certain number of writeback requests or if
		 * we have been inside this loop for at least 200 msecs.
		 *
		 * FIXME: should make this tunable?
		 *
		 * note that now we have the option of recomputing this
		 * on each request (or on each batch of requests), the exit
		 * condition from here is no longer really critical in any
		 * way, so we probably no longer need this to be tunable.
		 */
		if (wb_block_count >= 100)
			break;
		if (jiffies_to_msecs(jiffies) - jiffies_begin_msecs >= 100)
			break;

		/*
		 * recompute policy parameters (fast plug)
		 *
		 * FIXME: should fast plug be also inside the cluster loop?
		 */
		cache_bgwriter_compute_policy_fast(bc);
	}

	if (wb_block_count > 0) {
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
			 "pend_wb=%u, msleep=%u, msleep_s=%u, wb_block_count=%u",
			 atomic_read(&bc->bc_pending_writeback_requests),
			 msleep_ms, msleep_sum, wb_block_count);
		bc->bc_bgwriter_curr_block_count += wb_block_count;
		bc->bc_bgwriter_curr_block_count_sum += wb_block_count;
		bc->bc_bgwriter_curr_cluster_count += wb_cluster_count;
		bc->bc_bgwriter_curr_cluster_count_sum += wb_cluster_count;
		bc->bc_bgwriter_curr_msecs_elapsed_start_io +=
		    (jiffies_to_msecs(jiffies) - jiffies_begin_msecs);
		bc->bc_bgwriter_curr_msecs_slept_start_io += msleep_sum;
	}
}

void cache_bgwriter_wait_io(struct bittern_cache *bc)
{
	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	printk_info
	    ("enter: pending_writeback_requests=%u, pending_cached_device_requests=%u, valid_entries_dirty=%u\n",
	     atomic_read(&bc->bc_pending_writeback_requests),
	     atomic_read(&bc->bc_pending_cached_device_requests),
	     atomic_read(&bc->bc_valid_entries_dirty));

	while (atomic_read(&bc->bc_pending_writeback_requests) > 0) {
		int ret;
		/*
		 * here we are quitting, so kthread_should() will always be true
		 */
		ret = wait_event_interruptible_timeout(bc->bc_bgwriter_wait,
						       atomic_read(&bc->bc_pending_writeback_requests) == 0,
						       msecs_to_jiffies(500));
		if (signal_pending(current))
			flush_signals(current);
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
			 "loop: pending_writeback_requests=%u, pending_cached_device_requests=%u, valid_entries_dirty=%u",
			 atomic_read(&bc->bc_pending_writeback_requests),
			 atomic_read(&bc->bc_pending_cached_device_requests),
			 atomic_read(&bc->bc_valid_entries_dirty));
		msleep(10);
	}

	printk_info
	    ("exit: pending_writeback_requests=%u, pending_cached_device_requests=%u, valid_entries_dirty=%u\n",
	     atomic_read(&bc->bc_pending_writeback_requests),
	     atomic_read(&bc->bc_pending_cached_device_requests),
	     atomic_read(&bc->bc_valid_entries_dirty));
	ASSERT(atomic_read(&bc->bc_pending_cached_device_requests) == 0);
	ASSERT(atomic_read(&bc->bc_pending_writeback_requests) == 0);
}

/*
 *
 * this function is only meant to be called by the _dtr() code,
 * as it changes the writeback state.
 */
void cache_bgwriter_flush_dirty_blocks(struct bittern_cache *bc)
{
	/*!
	 * set writethrough mode.
	 * this will start flushing all the buffers very aggressively.
	 * as things stand now there seem to be no need for explicit
	 * notification that we are exiting given there is no need to
	 * do any special case from this.
	 * this function is only meant to be called by the _dtr() code,
	 * as it changes the writeback state.
	 * note that the only two meaningful variables to set are
	 * @ref bc_cache_mode_writeback and @ref bc_max_pending_requests,
	 * as the other meaningful parameters are set in
	 * @ref cache_bgwriter_compute_policy_common.
	 */
	bc->bc_cache_mode_writeback = 0;
	bc->bc_max_pending_requests = CACHE_MAX_PENDING_REQUESTS_MAX;

	printk_info("waiting for flush completion - dirty blocks = %u\n",
		    atomic_read(&bc->bc_valid_entries_dirty));
	while (atomic_read(&bc->bc_valid_entries_dirty) > 0) {
		unsigned int d = atomic_read(&bc->bc_valid_entries_dirty);
		/* using msleep instead of event wait so print flushing rate */
		msleep(1000);
		printk_info("flushing - dirty blocks = %u (%u/sec)\n",
			    atomic_read(&bc->bc_valid_entries_dirty),
			    (d - atomic_read(&bc->bc_valid_entries_dirty)));
	}
	printk_info("done waiting for flush completion - dirty blocks = %u\n",
		    atomic_read(&bc->bc_valid_entries_dirty));
	M_ASSERT(atomic_read(&bc->bc_valid_entries_dirty) == 0);
}

static int cache_bgwriter_has_work(struct bittern_cache *bc)
{
	return atomic_read(&bc->bc_valid_entries_dirty) > 0 &&
	    atomic_read(&bc->bc_pending_writeback_requests) <
	    bc->bc_bgwriter_curr_queue_depth;
}

int cache_bgwriter_kthread(void *__bc)
{
	struct bittern_cache *bc = (struct bittern_cache *)__bc;

	set_user_nice(current, CACHE_BACKGROUND_WRITER_THREAD_NICE);

	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL,
		 "enter, nice=%d", CACHE_BACKGROUND_WRITER_THREAD_NICE);

	while (!kthread_should_stop()) {
		int ret;

		ASSERT(bc != NULL);
		ASSERT_BITTERN_CACHE(bc);

		/*
		 * we get woken up at each cache fill or completed writeback
		 * however we still do need the timeout, as we poll for the age
		 * of the oldest block to write out
		 */
		ret = wait_event_interruptible_timeout(bc->bc_bgwriter_wait,
						       cache_bgwriter_has_work(bc)
						       || kthread_should_stop(),
						       msecs_to_jiffies(1));
		if (signal_pending(current))
			flush_signals(current);

		if (atomic_read(&bc->bc_valid_entries_dirty) > 0) {

			/*
			 * recompute policy parameters (slow plug)
			 */
			cache_bgwriter_compute_policy_slow(bc);

			/*
			 * sanity check
			 */
			ASSERT(bc->bc_bgwriter_curr_queue_depth >= 1 &&
			       bc->bc_bgwriter_curr_queue_depth <=
			       CACHE_MAX_PENDING_REQUESTS_MAX);
			ASSERT(bc->bc_bgwriter_curr_rate_per_sec >= 0
			       && bc->bc_bgwriter_curr_rate_per_sec <= 300);
			ASSERT(bc->bc_bgwriter_curr_min_age_secs >= 0
			       && bc->bc_bgwriter_curr_min_age_secs <= 60);

			/*
			 * start writebacks, the number and delays being
			 * determined by the recomputed policy parameters
			 */
			cache_bgwriter_start_io(bc);

			bc->bc_bgwriter_work_count++;

			/*
			 * still work to do, but pause a bit
			 */
			schedule();

			/*
			 * wait for resources
			 */
			cache_bgwriter_wait_for_resources(bc, true);

		} else {
			/*
			 * there is nothing dirty -- not a writeback stall
			 */
			bc->bc_bgwriter_no_work_count++;
			msleep(5);
		}

		bc->bc_bgwriter_loop_count++;

		schedule();
	}

	/*
	 * do a sync drain of pending writebacks before exiting
	 */
	cache_bgwriter_wait_io(bc);

	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL, "exit");

	bc->bc_bgwriter_task = NULL;
	return 0;
}
