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

/*! \todo does this really belong here and not in cache_getput ? */
void cache_invalidate_block_io_end(struct bittern_cache *bc,
				   struct work_item *wi,
				   struct cache_block *cache_block)
{
	ASSERT(cache_block != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(wi->wi_cache_block == cache_block);
	ASSERT(wi->wi_original_bio == NULL);
	ASSERT(wi->wi_cloned_bio == NULL);
	ASSERT(wi->wi_io_xid != 0);

	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, cache_block, NULL, NULL,
		 "invalidate done");
	ASSERT(cache_block->bcb_state == S_CLEAN_INVALIDATE_END ||
	       cache_block->bcb_state == S_DIRTY_INVALIDATE_END);
	ASSERT(is_sector_number_valid(cache_block->bcb_sector));

	if (cache_block->bcb_state == S_DIRTY_INVALIDATE_END)
		cache_move_to_invalid(bc, cache_block, 1);
	else
		cache_move_to_invalid(bc, cache_block, 0);

	cache_timer_add(&bc->bc_timer_invalidations, wi->wi_ts_started);

	work_item_free(bc, wi);

	atomic_inc(&bc->bc_completed_requests);
	atomic_inc(&bc->bc_completed_invalidations);
	atomic_dec(&bc->bc_pending_invalidate_requests);

	/*
	 * wakeup possible waiters
	 */
	wakeup_deferred(bc);
	wake_up_interruptible(&bc->bc_invalidator_wait);
}

/*! \todo does this really belong here and not in cache_getput ? */
void cache_invalidate_block_io_start(struct bittern_cache *bc,
				     struct cache_block *cache_block)
{
	unsigned long flags;
	unsigned long cache_flags;
	struct work_item *wi;
	int val;
	int ret;

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL, "enter");
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);

	ASSERT(cache_block->bcb_state == S_CLEAN ||
	       cache_block->bcb_state == S_DIRTY);
	ASSERT(cache_block->bcb_cache_transition ==
	       TS_NONE);
	ASSERT(is_sector_number_valid(cache_block->bcb_sector));
	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		 "invalidating clean block id #%d", cache_block->bcb_block_id);

	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);

	/*
	 * VALID_CLEAN -> CLEAN_INVALIDATE_START
	 */
	if (cache_block->bcb_state == S_CLEAN)
		cache_state_transition_initial(bc, cache_block,
				TS_CLEAN_INVALIDATION_WTWB,
				S_CLEAN_INVALIDATE_START);
	else
		cache_state_transition_initial(bc, cache_block,
				TS_DIRTY_INVALIDATION_WB,
				S_DIRTY_INVALIDATE_START);

	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

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

	val = atomic_inc_return(&bc->bc_pending_invalidate_requests);
	atomic_set_if_higher(&bc->bc_highest_pending_invalidate_requests, val);

	/*
	 * kick off state machine to write this out.
	 * cache_bgwriter_io_endio() will be called on completion.
	 */
	work_item_add_pending_io(bc,
				 wi,
				 "invalidate",
				 cache_block->bcb_sector,
				 WRITE);
	ASSERT(wi->wi_cache_block == cache_block);
	cache_state_machine(bc, wi, 0);
}

/*!
 * This function is used as a wakeup condition by the invalidator thread.
 * The wakeup conditions are (1) there has to be work to do (2) there have
 * be enough resources to do the work.
 */
static inline int cache_invalidator_has_work(struct bittern_cache *bc)
{
	unsigned int min_count;

	min_count = bc->bc_invalidator_conf_min_invalid_count;
	min_count += bc->bc_max_pending_requests;
	ASSERT(min_count > 0);

	/* bail out if there are no clean entries to invalidate */
	if (atomic_read(&bc->bc_valid_entries_clean) == 0)
		return 0;

	/*
	 * There is work to do if number of invalid entries
	 * is below the minimum threshold.
	 */
	return atomic_read(&bc->bc_invalid_entries) < min_count;
}

/*!
 * This is the Schmitt's trigger version than the above function,
 * without the check for too many requests.
 */
int cache_invalidator_has_work_schmitt(struct bittern_cache *bc)
{
	unsigned int min_count_s;

	min_count_s = bc->bc_invalidator_conf_min_invalid_count;
	min_count_s += bc->bc_max_pending_requests;
	ASSERT(min_count_s > 0);

	/* bail out if there are no clean entries to invalidate */
	if (atomic_read(&bc->bc_valid_entries_clean) == 0)
		return 0;

	/*
	 * There is work to do if number of invalid entries
	 * is below the minimum threshold.
	 * This is the Schmitt's trigger version of the above.
	 */
	min_count_s = min_count_s + min_count_s / 4;
	return atomic_read(&bc->bc_invalid_entries) < min_count_s;
}

void cache_invalidate_clean_blocks(struct bittern_cache *bc)
{
	int did_work = 0;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);

	BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL,
		 "entr: bc_min_invalid_count=%d, bc_invalid_blocks=%d, %d/%d",
		 bc->bc_invalidator_conf_min_invalid_count,
		 atomic_read(&bc->bc_invalid_entries),
		 bc->bc_invalidator_work_count,
		 bc->bc_invalidator_no_work_count);

	while (cache_invalidator_has_work_schmitt(bc)) {
		struct cache_block *cache_block;
		int ret;

		ret = cache_get_clean(bc, &cache_block);
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
			 "kthread_should_stop=%d, has_work=%d, pending=%d",
			 kthread_should_stop(),
			 cache_invalidator_has_work(bc),
			 atomic_read(&bc->bc_pending_invalidate_requests));
		if (ret == CACHE_GET_RET_HIT_IDLE) {
			/* found a clean block, start async invalidation */
			ASSERT(cache_block != NULL);
			cache_invalidate_block_io_start(bc, cache_block);
			atomic_inc(&bc->bc_invalidations_invalidator);
			did_work = 1;
		} else {
			/* no blocks, bail out */
			break;
		}
	}
	if (did_work)
		bc->bc_invalidator_work_count++;
	else
		bc->bc_invalidator_no_work_count++;

	BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL,
		 "exit: bc_min_invalid_count=%d, bc_invalid_blocks=%d, %d/%d",
		 bc->bc_invalidator_conf_min_invalid_count,
		 atomic_read(&bc->bc_invalid_entries),
		 bc->bc_invalidator_work_count,
		 bc->bc_invalidator_no_work_count);
}

int cache_invalidator_kthread(void *__bc)
{
	struct bittern_cache *bc = (struct bittern_cache *)__bc;

	set_user_nice(current, S_INVALIDATOR_THREAD_NICE);

	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL,
		 "enter, nice=%d", S_INVALIDATOR_THREAD_NICE);

	while (!kthread_should_stop()) {
		int ret;

		ASSERT(bc != NULL);
		ASSERT_BITTERN_CACHE(bc);

		ret = wait_event_interruptible(bc->bc_invalidator_wait,
					       (cache_invalidator_has_work
						(bc) || kthread_should_stop()));
		if (signal_pending(current))
			flush_signals(current);

		cache_invalidate_clean_blocks(bc);

		schedule();
	}

	/*
	 * wait for any pending invalidations to complete before quitting
	 */
	while (atomic_read(&bc->bc_pending_invalidate_requests) != 0) {
		int ret;

		ret = wait_event_interruptible(bc->bc_invalidator_wait,
			atomic_read(&bc->bc_pending_invalidate_requests)
			< bc->bc_max_pending_requests);
		if (signal_pending(current))
			flush_signals(current);
		BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL,
			 "wait: kthread_should_stop=%d, has_work=%d, pending=%d",
			 kthread_should_stop(),
			 cache_invalidator_has_work(bc),
			 atomic_read(&bc->bc_pending_invalidate_requests));
	}

	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL, "exit");

	bc->bc_invalidator_task = NULL;
	return 0;
}

/*! \todo does this really belong here and not in cache_getput ? */
void cache_invalidate_clean_block(struct bittern_cache *bc,
				  struct cache_block *cache_block)
{
	/*
	 * wait for resources, then start async invalidate
	 */
	wait_event_interruptible(bc->bc_invalidator_wait,
				 atomic_read(&bc->
					     bc_pending_invalidate_requests) <
				 bc->bc_max_pending_requests);
	/*
	 * because this is called by userland, we need to bail out if there any
	 * signals pending
	 */
	if (signal_pending(current))
		return;
	/*
	 * start async invalidation
	 */
	cache_invalidate_block_io_start(bc, cache_block);
	atomic_inc(&bc->bc_invalidations_invalidator);
}
