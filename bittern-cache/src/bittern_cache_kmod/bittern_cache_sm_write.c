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

void
sm_clean_pwrite_hit_copy_from_cache_start(struct bittern_cache *bc,
					  struct work_item *wi)
{
	struct bio *bio = wi->wi_original_bio;
	struct cache_block *cache_block = wi->wi_cache_block;
	struct cache_block *original_cache_block = wi->wi_original_cache_block;

	M_ASSERT(bio != NULL);
	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT(wi->wi_original_bio != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(wi->wi_cache == bc);
	ASSERT(cache_block->bcb_state == S_CLEAN_P_WRITE_HIT_CPF_O_CACHE_START);
	ASSERT(original_cache_block->bcb_state == S_CLEAN_INVALIDATE_START);
	ASSERT(wi->wi_original_cache_block != NULL);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_CACHE_BLOCK(original_cache_block, bc);

	cache_state_transition3(bc,
				cache_block,
				TS_P_WRITE_HIT_WT,
				S_CLEAN_P_WRITE_HIT_CPF_O_CACHE_START,
				S_CLEAN_P_WRITE_HIT_CPT_DEVICE_START);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "start_async_read (get_page_read): wi=%p, bc=%p, cache_block=%p, bio=%p",
		 wi, bc, original_cache_block, bio);
	pmem_data_get_page_read(bc,
				original_cache_block,
				&wi->wi_pmem_ctx,
				wi, /*callback context */
				cache_get_page_read_callback);
}

void sm_dirty_write_miss_copy_to_cache_start(struct bittern_cache *bc,
					     struct work_item *wi)
{
	struct bio *bio = wi->wi_original_bio;
	struct cache_block *cache_block = wi->wi_cache_block;
	uint128_t hash_data;

	M_ASSERT(bio != NULL);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "wi=%p, bc=%p, cache_block=%p, bio=%p", wi, bc, cache_block,
		 bio);

	ASSERT(wi->wi_original_cache_block == NULL);
	ASSERT(wi->wi_cache == bc);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(cache_block->bcb_state == S_DIRTY_WRITE_MISS_CPT_CACHE_START);

	/*
	 * get page for write
	 */
	pmem_data_get_page_write(bc,
				 cache_block,
				 &wi->wi_pmem_ctx);

	/*
	 * copy to cache from bio, aka userland writes
	 */
	bio_copy_to_cache(wi, bio, &hash_data);

	/* update hash */
	cache_block->bcb_hash_data = hash_data;

	/*
	 * update hash
	 */
	cache_track_hash_set(bc, cache_block, cache_block->bcb_hash_data);

	cache_state_transition3(bc,
				cache_block,
				TS_WRITE_MISS_WB,
				S_DIRTY_WRITE_MISS_CPT_CACHE_START,
				S_DIRTY_WRITE_MISS_CPT_CACHE_END);

	/*
	 * release cache page
	 */

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "start_async_write (put_page_write): callback_context/wi=%p, bc=%p, cache_block=%p, bio=%p",
		 wi, bc, cache_block, bio);
	pmem_data_put_page_write(bc,
				 cache_block,
				 &wi->wi_pmem_ctx,
				 wi, /*callback context */
				 cache_put_page_write_callback,
				 S_DIRTY);

	ASSERT_BITTERN_CACHE(bc);
}

void sm_dirty_write_miss_copy_to_cache_end(struct bittern_cache *bc,
					   struct work_item *wi,
					   int err)
{
	struct bio *bio = wi->wi_original_bio;
	struct cache_block *cache_block = wi->wi_cache_block;
	unsigned long cache_flags;

	M_ASSERT_FIXME(err == 0);

	M_ASSERT(bio != NULL);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "wi=%p, bc=%p, cache_block=%p, bio=%p", wi, bc, cache_block,
		 bio);

	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT(wi->wi_original_bio != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(cache_block->bcb_state == S_DIRTY_WRITE_MISS_CPT_CACHE_END);
	ASSERT(wi->wi_original_cache_block == NULL);
	ASSERT_CACHE_STATE(cache_block);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);

	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);
	cache_state_transition_final(bc,
				     cache_block,
				     TS_NONE,
				     S_DIRTY);
	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	cache_put_update_age(bc, cache_block, 1);

	cache_timer_add(&bc->bc_timer_writes, wi->wi_ts_started);
	cache_timer_add(&bc->bc_timer_write_misses, wi->wi_ts_started);
	cache_timer_add(&bc->bc_timer_write_dirty_misses, wi->wi_ts_started);

	work_item_free(bc, wi);

	atomic_dec(&bc->bc_pending_requests);
	if (bio_data_dir(bio) == WRITE) {
		atomic_dec(&bc->bc_pending_write_requests);
		atomic_inc(&bc->bc_completed_write_requests);
	} else {
		atomic_dec(&bc->bc_pending_read_requests);
		atomic_inc(&bc->bc_completed_read_requests);
	}
	atomic_inc(&bc->bc_completed_requests);
	/*
	 * wakeup possible waiters
	 */
	wakeup_deferred(bc);
	bio_endio(bio, 0);
}

void sm_clean_write_miss_copy_to_device_start(struct bittern_cache *bc,
					      struct work_item *wi)
{
	struct bio *bio;
	uint128_t hash_data;
	struct cache_block *cache_block;
	struct cache_block *original_cache_block;
	int val;
	struct page *cache_page;

	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT(wi->wi_original_bio != NULL);
	cache_block = wi->wi_cache_block;
	original_cache_block = wi->wi_original_cache_block;
	bio = wi->wi_original_bio;

	ASSERT(bio != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(bio == wi->wi_original_bio);
	ASSERT(cache_block->bcb_state == S_CLEAN_WRITE_MISS_CPT_DEVICE_START ||
	       cache_block->bcb_state == S_CLEAN_WRITE_HIT_CPT_DEVICE_START ||
	       cache_block->bcb_state == S_CLEAN_P_WRITE_HIT_CPT_DEVICE_START);
	if (cache_block->bcb_state != S_CLEAN_WRITE_MISS_CPT_DEVICE_START) {
		ASSERT(wi->wi_original_cache_block != NULL);
	}

	if (cache_block->bcb_state == S_CLEAN_P_WRITE_HIT_CPT_DEVICE_START) {
		char *cache_vaddr;

		ASSERT(original_cache_block->bcb_state ==
		       S_CLEAN_INVALIDATE_START);
		cache_vaddr = pmem_context_data_vaddr(&wi->wi_pmem_ctx);
		ASSERT(bc->bc_enable_extra_checksum_check == 0 ||
		       bc->bc_enable_extra_checksum_check == 1);
		if (bc->bc_enable_extra_checksum_check != 0) {
			/* verify that hash is correct */
			cache_verify_hash_data_buffer(bc,
						original_cache_block,
						cache_vaddr);
		}

		/* check hash */
		cache_track_hash_check(bc,
				       original_cache_block,
				       original_cache_block->bcb_hash_data);

		/*
		 * cloned read page to write page
		 */
		pmem_data_clone_read_to_write(bc,
					      original_cache_block,
					      cache_block,
					      &wi->wi_pmem_ctx);

	} else {
		ASSERT(cache_block->bcb_state ==
		       S_CLEAN_WRITE_MISS_CPT_DEVICE_START ||
		       cache_block->bcb_state ==
		       S_CLEAN_WRITE_HIT_CPT_DEVICE_START);
		/*
		 * get page for write
		 */
		pmem_data_get_page_write(bc,
					 cache_block,
					 &wi->wi_pmem_ctx);

	}

	cache_page = pmem_context_data_page(&wi->wi_pmem_ctx);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "di_page=%p",
		 cache_page);

	/*
	 * copy to cache from bio, aka userland writes
	 */
	bio_copy_to_cache(wi, bio, &hash_data);

	/* update hash */
	cache_block->bcb_hash_data = hash_data;

	/*
	 * update hash
	 */
	cache_track_hash_set(bc, cache_block, cache_block->bcb_hash_data);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "copy-to-device");

	atomic_inc(&bc->bc_write_cached_device_requests);
	val = atomic_inc_return(&bc->bc_pending_cached_device_requests);
	atomic_set_if_higher(&bc->bc_highest_pending_cached_device_requests,
			     val);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "copy-to-device-starting-io");

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_WORK_ITEM(wi, bc);

	if (cache_block->bcb_state == S_CLEAN_WRITE_MISS_CPT_DEVICE_START) {
		cache_state_transition3(bc,
					cache_block,
					TS_WRITE_MISS_WT,
					S_CLEAN_WRITE_MISS_CPT_DEVICE_START,
					S_CLEAN_WRITE_MISS_CPT_DEVICE_END);
		/*
		 * first step in state machine -- in a process context
		 */
		M_ASSERT(!in_irq() && !in_softirq());
		wi->wi_ts_workqueue = current_kernel_time_nsec();
		cached_dev_do_make_request(bc,
					   wi,
					   WRITE, /* datadir */
					   false); /* do not set original bio */
	} else if (cache_block->bcb_state ==
		   S_CLEAN_WRITE_HIT_CPT_DEVICE_START) {
		cache_state_transition3(bc,
					cache_block,
					TS_WRITE_HIT_WT,
					S_CLEAN_WRITE_HIT_CPT_DEVICE_START,
					S_CLEAN_WRITE_HIT_CPT_DEVICE_END);
		/*
		 * first step in state machine -- in a process context
		 */
		M_ASSERT(!in_irq() && !in_softirq());
		wi->wi_ts_workqueue = current_kernel_time_nsec();
		cached_dev_do_make_request(bc,
					   wi,
					   WRITE, /* datadir */
					   false); /* do not set original bio */
	} else {
		ASSERT(cache_block->bcb_state ==
		       S_CLEAN_P_WRITE_HIT_CPT_DEVICE_START);
		cache_state_transition3(bc,
					cache_block,
					TS_P_WRITE_HIT_WT,
					S_CLEAN_P_WRITE_HIT_CPT_DEVICE_START,
					S_CLEAN_P_WRITE_HIT_CPT_DEVICE_END);
		/*
		 * can be in softirq
		 */
		cached_dev_make_request_defer(bc,
					      wi,
					      WRITE, /* datadir */
					      false); /* do not set orig bio */
	}
}

void sm_clean_write_miss_copy_to_device_end(struct bittern_cache *bc,
					    struct work_item *wi,
					    int err)
{
	struct bio *bio = wi->wi_original_bio;
	struct cache_block *cache_block = wi->wi_cache_block;

	M_ASSERT_FIXME(err == 0);

	M_ASSERT(bio != NULL);

	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(cache_block->bcb_state == S_CLEAN_WRITE_MISS_CPT_DEVICE_END ||
	       cache_block->bcb_state == S_CLEAN_WRITE_HIT_CPT_DEVICE_END ||
	       cache_block->bcb_state == S_CLEAN_P_WRITE_HIT_CPT_DEVICE_END);
	if (cache_block->bcb_state != S_CLEAN_WRITE_MISS_CPT_DEVICE_END)
		ASSERT(wi->wi_original_cache_block != NULL);

	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, cache_block, bio, NULL,
		 "copy-to-device-io-done");

	ASSERT_CACHE_STATE(cache_block);
	ASSERT_CACHE_BLOCK(cache_block, bc);

	atomic_dec(&bc->bc_pending_cached_device_requests);

	if (cache_block->bcb_state == S_CLEAN_WRITE_MISS_CPT_DEVICE_END) {
		cache_state_transition3(bc,
					cache_block,
					TS_WRITE_MISS_WT,
					S_CLEAN_WRITE_MISS_CPT_DEVICE_END,
					S_CLEAN_WRITE_MISS_CPT_CACHE_END);
	} else if (cache_block->bcb_state ==
		   S_CLEAN_WRITE_HIT_CPT_DEVICE_END) {
		cache_state_transition3(bc,
					cache_block,
					TS_WRITE_HIT_WT,
					S_CLEAN_WRITE_HIT_CPT_DEVICE_END,
					S_CLEAN_WRITE_HIT_CPT_CACHE_END);
	} else {
		ASSERT(cache_block->bcb_state ==
		       S_CLEAN_P_WRITE_HIT_CPT_DEVICE_END);
		cache_state_transition3(bc,
					cache_block,
					TS_P_WRITE_HIT_WT,
					S_CLEAN_P_WRITE_HIT_CPT_DEVICE_END,
					S_CLEAN_P_WRITE_HIT_CPT_CACHE_END);
	}

	/*
	 * release cache page
	 */

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "start_async_write (put_page_write): callback_context/wi=%p, bc=%p, cache_block=%p, bio=%p",
		 wi, bc, cache_block, bio);
	pmem_data_put_page_write(bc,
				 cache_block,
				 &wi->wi_pmem_ctx,
				 wi, /*callback context */
				 cache_put_page_write_callback,
				 S_CLEAN);

	ASSERT_BITTERN_CACHE(bc);
}

void sm_clean_write_miss_copy_to_cache_end(struct bittern_cache *bc,
					   struct work_item *wi,
					   int err)
{
	struct bio *bio = wi->wi_original_bio;
	struct cache_block *cache_block = wi->wi_cache_block;
	enum cache_state original_state = cache_block->bcb_state;
	struct cache_block *original_cache_block = wi->wi_original_cache_block;
	unsigned long cache_flags;
	int val;

	M_ASSERT_FIXME(err == 0);

	M_ASSERT(bio != NULL);

	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT(wi->wi_original_bio != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(cache_block->bcb_state == S_CLEAN_WRITE_MISS_CPT_CACHE_END ||
	       cache_block->bcb_state == S_CLEAN_WRITE_HIT_CPT_CACHE_END ||
	       cache_block->bcb_state == S_CLEAN_P_WRITE_HIT_CPT_CACHE_END);
	if (cache_block->bcb_state != S_CLEAN_WRITE_MISS_CPT_CACHE_END)
		ASSERT(wi->wi_original_cache_block != NULL);

	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_CACHE_BLOCK(original_cache_block, bc);

	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, cache_block, bio, NULL,
		 "copy-to-cache-end");

	/*
	 * STEP #1 -- complete new cache_block write request
	 */

	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);
	cache_state_transition_final(bc,
				     cache_block,
				     TS_NONE,
				     S_CLEAN);
	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	cache_put_update_age(bc, cache_block, 1);

	cache_timer_add(&bc->bc_timer_writes, wi->wi_ts_started);
	if (original_state == S_CLEAN_WRITE_MISS_CPT_CACHE_END) {
		cache_timer_add(&bc->bc_timer_write_misses,
					wi->wi_ts_started);
		cache_timer_add(&bc->bc_timer_write_clean_misses,
					wi->wi_ts_started);
	} else {
		ASSERT(original_state == S_CLEAN_WRITE_HIT_CPT_CACHE_END ||
		       original_state == S_CLEAN_P_WRITE_HIT_CPT_CACHE_END);
		cache_timer_add(&bc->bc_timer_write_hits,
					wi->wi_ts_started);
		cache_timer_add(&bc->bc_timer_write_clean_hits,
					wi->wi_ts_started);
	}

	atomic_dec(&bc->bc_pending_requests);
	if (bio_data_dir(bio) == WRITE) {
		atomic_dec(&bc->bc_pending_write_requests);
		atomic_inc(&bc->bc_completed_write_requests);
	} else {
		atomic_dec(&bc->bc_pending_read_requests);
		atomic_inc(&bc->bc_completed_read_requests);
	}
	atomic_inc(&bc->bc_completed_requests);

	work_item_del_pending_io(bc, wi);

	bio_endio(bio, 0);

	/*
	 * wakeup possible waiters
	 */
	wakeup_deferred(bc);

	if (original_state == S_CLEAN_WRITE_MISS_CPT_CACHE_END) {
		ASSERT(original_cache_block == NULL);
		/*
		 * write miss does not do cloning.
		 */
		work_item_free(bc, wi);
		return;
	}

	/*
	 * STEP #2 -- now start an async metadata invalidation
	 */

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, original_cache_block, NULL, NULL,
		 "copy_to_cache_end-2");

	ASSERT(original_cache_block->bcb_cache_transition ==
	       TS_CLEAN_INVALIDATION_WTWB);
	ASSERT(original_cache_block->bcb_state == S_CLEAN_INVALIDATE_START);

	work_item_reallocate(bc,
			     original_cache_block,
			     wi,
			     NULL,
			     (WI_FLAG_BIO_NOT_CLONED |
			      WI_FLAG_XID_USE_CACHE_BLOCK));

	wi->wi_ts_started = current_kernel_time_nsec();

	atomic_inc(&bc->bc_invalidations_map);
	val = atomic_inc_return(&bc->bc_pending_invalidate_requests);
	atomic_set_if_higher(&bc->bc_highest_pending_invalidate_requests, val);

	/*
	 * kick off state machine to write this out.
	 * cache_bgwriter_io_end() will be called on completion.
	 */
	work_item_add_pending_io(bc,
				 wi,
				 "wmiss-clone-invalidate-original",
				 original_cache_block->bcb_sector,
				 WRITE);
	ASSERT(wi->wi_cache_block == original_cache_block);
	cache_state_machine(bc, wi, 0);
}

void
sm_dirty_pwrite_hit_copy_from_cache_start(struct bittern_cache *bc,
					  struct work_item *wi)
{
	struct bio *bio = wi->wi_original_bio;
	struct cache_block *cache_block = wi->wi_cache_block;
	struct cache_block *original_cache_block = wi->wi_original_cache_block;

	M_ASSERT(bio != NULL);

	ASSERT(wi->wi_original_cache_block != NULL);
	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(wi->wi_cache == bc);
	ASSERT(cache_block->bcb_state ==
	       S_C2_DIRTY_P_WRITE_HIT_CPF_O_CACHE_START ||
	       cache_block->bcb_state ==
	       S_DIRTY_P_WRITE_HIT_CPF_O_CACHE_START);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_CACHE_BLOCK(original_cache_block, bc);
	ASSERT(original_cache_block->bcb_state == S_CLEAN_INVALIDATE_START ||
	       original_cache_block->bcb_state == S_DIRTY_INVALIDATE_START);
	ASSERT(original_cache_block->bcb_sector == cache_block->bcb_sector);

	if (cache_block->bcb_state == S_C2_DIRTY_P_WRITE_HIT_CPF_O_CACHE_START)
		cache_state_transition3(bc,
				cache_block,
				TS_P_WRITE_HIT_WB_C2_DIRTY,
				S_C2_DIRTY_P_WRITE_HIT_CPF_O_CACHE_START,
				S_C2_DIRTY_P_WRITE_HIT_CPT_CACHE_START);
	else
		cache_state_transition3(bc,
				cache_block,
				TS_P_WRITE_HIT_WB_DIRTY,
				S_DIRTY_P_WRITE_HIT_CPF_O_CACHE_START,
				S_DIRTY_P_WRITE_HIT_CPT_CACHE_START);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "wi=%p, bc=%p, cache_block=%p, bio=%p", wi, bc, cache_block,
		 bio);
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, original_cache_block, bio, NULL,
		 "start_async_read (get_page_read): wi=%p, bc=%p, original_cache_block=%p, bio=%p",
		 wi, bc, original_cache_block, bio);
	pmem_data_get_page_read(bc,
				original_cache_block,
				&wi->wi_pmem_ctx,
				wi, /*callback context */
				cache_get_page_read_callback);
}

void
sm_dirty_pwrite_hit_copy_to_cache_start(struct bittern_cache *bc,
					struct work_item *wi,
					int err)
{
	struct bio *bio = wi->wi_original_bio;
	struct cache_block *cache_block = wi->wi_cache_block;
	struct cache_block *original_cache_block = wi->wi_original_cache_block;
	uint128_t hash_data;
	char *cache_vaddr;

	M_ASSERT_FIXME(err == 0);

	M_ASSERT(bio != NULL);

	/*
	 * for dirty write hit case, cache_block here is cloned cache block
	 */
	ASSERT(wi->wi_original_cache_block != NULL);
	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(cache_block->bcb_state == S_DIRTY_P_WRITE_HIT_CPT_CACHE_START ||
	       cache_block->bcb_state ==
	       S_C2_DIRTY_P_WRITE_HIT_CPT_CACHE_START);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_CACHE_BLOCK(original_cache_block, bc);
	ASSERT(original_cache_block->bcb_state == S_DIRTY_INVALIDATE_START ||
	       original_cache_block->bcb_state == S_CLEAN_INVALIDATE_START);
	ASSERT(original_cache_block->bcb_sector == cache_block->bcb_sector);
	if (cache_block->bcb_state == S_DIRTY_P_WRITE_HIT_CPT_CACHE_START)
		ASSERT(original_cache_block->bcb_state ==
			       S_DIRTY_INVALIDATE_START);
	else
		ASSERT(original_cache_block->bcb_state ==
		       S_CLEAN_INVALIDATE_START);

	ASSERT(bc->bc_enable_extra_checksum_check == 0 ||
	       bc->bc_enable_extra_checksum_check == 1);
	if (bc->bc_enable_extra_checksum_check != 0) {
		cache_vaddr = pmem_context_data_vaddr(&wi->wi_pmem_ctx);
		/* verify that hash is correct */
		cache_verify_hash_data_buffer(bc,
					      original_cache_block,
					      cache_vaddr);
	}

	/* check hash */
	cache_track_hash_check(bc,
			       original_cache_block,
			       original_cache_block->bcb_hash_data);

	/*
	 * clone read page to write page
	 */
	pmem_data_clone_read_to_write(bc,
				      original_cache_block,
				      cache_block,
				      &wi->wi_pmem_ctx);


	/*
	 * copy to cache from bio, aka userland writes
	 */
	bio_copy_to_cache(wi, bio, &hash_data);

	/* update hash */
	cache_block->bcb_hash_data = hash_data;

	/*
	 * update hash
	 */
	cache_track_hash_set(bc, cache_block, cache_block->bcb_hash_data);

	if (cache_block->bcb_state == S_DIRTY_P_WRITE_HIT_CPT_CACHE_START) {
		cache_state_transition3(bc,
					cache_block,
					TS_P_WRITE_HIT_WB_DIRTY,
					S_DIRTY_P_WRITE_HIT_CPT_CACHE_START,
					S_DIRTY_P_WRITE_HIT_CPT_CACHE_END);
	} else {
		ASSERT(cache_block->bcb_state ==
		       S_C2_DIRTY_P_WRITE_HIT_CPT_CACHE_START);
		cache_state_transition3(bc,
					cache_block,
					TS_P_WRITE_HIT_WB_C2_DIRTY,
					S_C2_DIRTY_P_WRITE_HIT_CPT_CACHE_START,
					S_C2_DIRTY_P_WRITE_HIT_CPT_CACHE_END);
	}

	/*
	 * release page
	 */
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "start_async_write (put_page_write): callback_context/wi=%p, bc=%p, cache_block=%p, bio=%p",
		 wi, bc, cache_block, bio);
	pmem_data_put_page_write(bc,
				 cache_block,
				 &wi->wi_pmem_ctx,
				 wi, /*callback context */
				 cache_put_page_write_callback,
				 S_DIRTY);

	ASSERT_BITTERN_CACHE(bc);
}

void
sm_dirty_write_hit_copy_to_cache_start(struct bittern_cache *bc,
				       struct work_item *wi)
{
	struct bio *bio = wi->wi_original_bio;
	struct cache_block *cache_block = wi->wi_cache_block;
	struct cache_block *original_cache_block = wi->wi_original_cache_block;
	uint128_t hash_data;

	M_ASSERT(bio != NULL);

	/*
	 * for dirty write hit case, cache_block here is cloned cache block
	 */
	ASSERT(wi->wi_original_cache_block != NULL);
	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(cache_block->bcb_state == S_DIRTY_WRITE_HIT_CPT_CACHE_START ||
	       cache_block->bcb_state == S_C2_DIRTY_WRITE_HIT_CPT_CACHE_START);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_CACHE_BLOCK(original_cache_block, bc);
	ASSERT(original_cache_block->bcb_state == S_DIRTY_INVALIDATE_START ||
	       original_cache_block->bcb_state == S_CLEAN_INVALIDATE_START);
	ASSERT(original_cache_block->bcb_sector == cache_block->bcb_sector);
	ASSERT(cache_block->bcb_state == S_DIRTY_WRITE_HIT_CPT_CACHE_START ||
	       cache_block->bcb_state == S_C2_DIRTY_WRITE_HIT_CPT_CACHE_START);
	if (cache_block->bcb_state == S_DIRTY_WRITE_HIT_CPT_CACHE_START)
		ASSERT(original_cache_block->bcb_state ==
		       S_DIRTY_INVALIDATE_START);
	else
		ASSERT(original_cache_block->bcb_state ==
		       S_CLEAN_INVALIDATE_START);
	/*
	 * get page for write
	 */
	pmem_data_get_page_write(bc, cache_block, &wi->wi_pmem_ctx);

	/*
	 * copy to cache from bio, aka userland writes
	 */
	bio_copy_to_cache(wi, bio, &hash_data);

	/* update hash */
	cache_block->bcb_hash_data = hash_data;

	/*
	 * update hash
	 */
	cache_track_hash_set(bc, cache_block, cache_block->bcb_hash_data);

	if (cache_block->bcb_state == S_DIRTY_WRITE_HIT_CPT_CACHE_START) {
		cache_state_transition3(bc,
					cache_block,
					TS_WRITE_HIT_WB_DIRTY,
					S_DIRTY_WRITE_HIT_CPT_CACHE_START,
					S_DIRTY_WRITE_HIT_CPT_CACHE_END);
	} else {
		ASSERT(cache_block->bcb_state ==
		       S_C2_DIRTY_WRITE_HIT_CPT_CACHE_START);
		cache_state_transition3(bc,
					cache_block,
					TS_WRITE_HIT_WB_C2_DIRTY,
					S_C2_DIRTY_WRITE_HIT_CPT_CACHE_START,
					S_C2_DIRTY_WRITE_HIT_CPT_CACHE_END);
	}

	/*
	 * release page
	 */
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "start_async_write (put_page_write): callback_context/wi=%p, bc=%p, cache_block=%p, bio=%p",
		 wi, bc, cache_block, bio);
	pmem_data_put_page_write(bc,
				 cache_block,
				 &wi->wi_pmem_ctx,
				 wi, /*callback context */
				 cache_put_page_write_callback,
				 S_DIRTY);

	ASSERT_BITTERN_CACHE(bc);
}

void sm_dirty_write_hit_copy_to_cache_end(struct bittern_cache *bc,
					  struct work_item *wi,
					  int err)
{
	struct bio *bio = wi->wi_original_bio;
	struct cache_block *cache_block = wi->wi_cache_block;
	struct cache_block *original_cache_block = wi->wi_original_cache_block;
	unsigned long cache_flags;
	int val;

	M_ASSERT_FIXME(err == 0);

	M_ASSERT(bio != NULL);

	/*
	 * for dirty write hit case, cache_block here is cloned cache block
	 */

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "copy_to_cache_end");

	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT(wi->wi_original_bio != NULL);
	ASSERT(wi->wi_original_cache_block != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(cache_block->bcb_state ==
	       S_DIRTY_WRITE_HIT_CPT_CACHE_END ||
	       cache_block->bcb_state ==
	       S_C2_DIRTY_WRITE_HIT_CPT_CACHE_END ||
	       cache_block->bcb_state ==
	       S_DIRTY_P_WRITE_HIT_CPT_CACHE_END ||
	       cache_block->bcb_state ==
	       S_C2_DIRTY_P_WRITE_HIT_CPT_CACHE_END);
	ASSERT(wi->wi_original_cache_block != NULL);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_CACHE_BLOCK(original_cache_block, bc);
	ASSERT(original_cache_block->bcb_state ==
	       S_DIRTY_INVALIDATE_START ||
	       original_cache_block->bcb_state ==
	       S_CLEAN_INVALIDATE_START);
	ASSERT(original_cache_block->bcb_sector == cache_block->bcb_sector);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "copy_to_cache_end-1");

	/*
	 * STEP #1 -- complete new cache_block write request
	 */

	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);
	cache_state_transition_final(bc,
				     cache_block,
				     TS_NONE,
				     S_DIRTY);
	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	cache_put_update_age(bc, cache_block, 1);

	cache_timer_add(&bc->bc_timer_writes, wi->wi_ts_started);
	cache_timer_add(&bc->bc_timer_write_hits, wi->wi_ts_started);
	cache_timer_add(&bc->bc_timer_write_dirty_hits,
				wi->wi_ts_started);

	atomic_dec(&bc->bc_pending_requests);
	if (bio_data_dir(bio) == WRITE) {
		atomic_dec(&bc->bc_pending_write_requests);
		atomic_inc(&bc->bc_completed_write_requests);
	} else {
		atomic_dec(&bc->bc_pending_read_requests);
		atomic_inc(&bc->bc_completed_read_requests);
	}
	atomic_inc(&bc->bc_completed_requests);

	work_item_del_pending_io(bc, wi);

	bio_endio(bio, 0);

	/*
	 * wakeup possible waiters
	 */
	wakeup_deferred(bc);

	/*
	 * STEP #2 -- now start an async metadata invalidation
	 */

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, original_cache_block, NULL, NULL,
		 "copy_to_cache_end-2");

	ASSERT(original_cache_block->bcb_cache_transition ==
	       TS_CLEAN_INVALIDATION_WTWB ||
	       original_cache_block->bcb_cache_transition ==
	       TS_DIRTY_INVALIDATION_WB);
	ASSERT(original_cache_block->bcb_state == S_CLEAN_INVALIDATE_START ||
	       original_cache_block->bcb_state == S_DIRTY_INVALIDATE_START);

	work_item_reallocate(bc,
			     original_cache_block,
			     wi,
			     NULL,
			     (WI_FLAG_BIO_NOT_CLONED |
			      WI_FLAG_XID_USE_CACHE_BLOCK));

	wi->wi_ts_started = current_kernel_time_nsec();

	atomic_inc(&bc->bc_invalidations_map);
	val = atomic_inc_return(&bc->bc_pending_invalidate_requests);
	atomic_set_if_higher(&bc->bc_highest_pending_invalidate_requests, val);

	/*
	 * kick off state machine to write this out.
	 * cache_bgwriter_io_end() will be called on completion.
	 */
	work_item_add_pending_io(bc,
				 wi,
				 "whit-clone-invalidate-original",
				 original_cache_block->bcb_sector,
				 WRITE);
	ASSERT(wi->wi_cache_block == original_cache_block);
	cache_state_machine(bc, wi, 0);
}
