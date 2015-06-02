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

void sm_pwrite_miss_copy_from_device_startio(struct bittern_cache *bc,
						   struct work_item *wi,
						   struct bio *bio)
{
	struct cache_block *cache_block;
	int val;
	struct page *cache_page;

	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT((wi->wi_flags & WI_FLAG_HAS_ENDIO) == 0);
	ASSERT((wi->wi_flags & WI_FLAG_MAP_IO) != 0);
	ASSERT(wi->wi_io_endio == NULL);
	ASSERT(wi->wi_original_bio != NULL);
	cache_block = wi->wi_cache_block;

	ASSERT(bio != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(bio == wi->wi_original_bio);
	ASSERT(cache_block->bcb_state ==
	       S_CLEAN_P_WRITE_MISS_CPF_DEVICE_STARTIO
	       || cache_block->bcb_state ==
	       S_DIRTY_P_WRITE_MISS_CPF_DEVICE_STARTIO);
	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);

	pmem_data_get_page_write(bc,
				 cache_block,
				 &wi->wi_pmem_ctx);

	cache_page = pmem_context_data_page(&wi->wi_pmem_ctx);

	/* set up args for cache_make_request */
	wi->bi_datadir = READ;
	wi->bi_sector = cache_block->bcb_sector;
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	wi->bi_endio = cache_state_machine_endio;
	wi->bi_page = cache_page;
	wi->bi_set_original_bio = false;
	wi->bi_set_cloned_bio = true;

	atomic_inc(&bc->bc_read_cached_device_requests);
	val = atomic_inc_return(&bc->bc_pending_cached_device_requests);
	atomic_set_if_higher(&bc->bc_highest_pending_cached_device_requests,
			     val);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "copy-from-device");
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);

	if (cache_block->bcb_state == S_CLEAN_P_WRITE_MISS_CPF_DEVICE_STARTIO) {
		cache_state_transition3(bc,
					cache_block,
					TS_P_WRITE_MISS_WT,
					S_CLEAN_P_WRITE_MISS_CPF_DEVICE_STARTIO,
					S_CLEAN_P_WRITE_MISS_CPF_DEVICE_ENDIO);
	} else {
		ASSERT(cache_block->bcb_state ==
		       S_DIRTY_P_WRITE_MISS_CPF_DEVICE_STARTIO);
		cache_state_transition3(bc,
					cache_block,
					TS_P_WRITE_MISS_WB,
					S_DIRTY_P_WRITE_MISS_CPF_DEVICE_STARTIO,
					S_DIRTY_P_WRITE_MISS_CPF_DEVICE_ENDIO);
	}

	/*
	 * we are in the first state -- process context
	 */
	M_ASSERT(!in_irq() && !in_softirq());
	wi->wi_ts_workqueue = current_kernel_time_nsec();
	cache_do_make_request(bc, wi);
}

void sm_pwrite_miss_copy_from_device_endio(struct bittern_cache *bc,
						 struct work_item *wi,
						 struct bio *bio)
{
	uint128_t hash_data;
	struct cache_block *cache_block;
	char *cache_vaddr;
	struct page *cache_page;

	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT((wi->wi_flags & WI_FLAG_HAS_ENDIO) == 0);
	ASSERT((wi->wi_flags & WI_FLAG_MAP_IO) != 0);
	ASSERT(wi->wi_io_endio == NULL);
	ASSERT(wi->wi_original_bio != NULL);
	cache_block = wi->wi_cache_block;

	ASSERT(bio != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(bio == wi->wi_original_bio);
	ASSERT(cache_block->bcb_state ==
	       S_CLEAN_P_WRITE_MISS_CPF_DEVICE_ENDIO
	       || cache_block->bcb_state ==
	       S_DIRTY_P_WRITE_MISS_CPF_DEVICE_ENDIO);
	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL, "endio");

	cache_vaddr = pmem_context_data_vaddr(&wi->wi_pmem_ctx);
	cache_page = pmem_context_data_page(&wi->wi_pmem_ctx);

	atomic_dec(&bc->bc_pending_cached_device_requests);

	/*
	 * we can check the original hash
	 */
	cache_track_hash_check_buffer(bc, cache_block, cache_vaddr);

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

	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);

	if (cache_block->bcb_state ==
	    S_CLEAN_P_WRITE_MISS_CPF_DEVICE_ENDIO) {
		int val;

		/* set up args for cache_make_request */
		wi->bi_datadir = WRITE;
		wi->bi_sector = cache_block->bcb_sector;
		ASSERT(cache_block->bcb_sector ==
		       bio_sector_to_cache_block_sector(bio));
		wi->bi_endio = cache_state_machine_endio;
		wi->bi_page = cache_page;
		wi->bi_set_original_bio = false;
		wi->bi_set_cloned_bio = true;

		BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio,
			 wi->wi_cloned_bio, "copy-to-device");

		atomic_inc(&bc->bc_write_cached_device_requests);
		val = atomic_inc_return(&bc->bc_pending_cached_device_requests);
		atomic_set_if_higher(
				&bc->bc_highest_pending_cached_device_requests,
				val);

		ASSERT_BITTERN_CACHE(bc);
		ASSERT_CACHE_BLOCK(cache_block, bc);
		ASSERT_WORK_ITEM(wi, bc);

		cache_state_transition3(bc,
					cache_block,
					TS_P_WRITE_MISS_WT,
					S_CLEAN_P_WRITE_MISS_CPF_DEVICE_ENDIO,
					S_CLEAN_P_WRITE_MISS_CPT_DEVICE_ENDIO);

		/*
		 * we are in the first state -- process context
		 */
		M_ASSERT(!in_irq() && !in_softirq());
		wi->wi_ts_workqueue = current_kernel_time_nsec();
		cache_do_make_request(bc, wi);

	} else {

		BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio,
			 wi->wi_cloned_bio, "copy-to-cache");
		/*
		 * for writeback we commit to cache and then we are done
		 */
		cache_state_transition3(bc,
					cache_block,
					TS_P_WRITE_MISS_WB,
					S_DIRTY_P_WRITE_MISS_CPF_DEVICE_ENDIO,
					S_DIRTY_P_WRITE_MISS_CPT_CACHE_END);

		pmem_data_put_page_write(bc,
					 cache_block,
					 &wi->wi_pmem_ctx,
					 wi, /*callback context */
					 cache_put_page_write_callback,
					 S_DIRTY);

	}
}

void sm_pwrite_miss_copy_to_device_endio(struct bittern_cache *bc,
					       struct work_item *wi,
					       struct bio *bio)
{
	struct cache_block *cache_block;

	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT((wi->wi_flags & WI_FLAG_HAS_ENDIO) == 0);
	ASSERT((wi->wi_flags & WI_FLAG_MAP_IO) != 0);
	ASSERT(wi->wi_io_endio == NULL);
	ASSERT(wi->wi_original_bio != NULL);
	cache_block = wi->wi_cache_block;

	ASSERT(bio != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(bio == wi->wi_original_bio);
	ASSERT(cache_block->bcb_state ==
	       S_CLEAN_P_WRITE_MISS_CPT_DEVICE_ENDIO);
	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);
	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "copy-to-device-endio");

	atomic_dec(&bc->bc_pending_cached_device_requests);

	/*
	 * for writeback we commit to cache and then we are done
	 */
	cache_state_transition3(bc,
				cache_block,
				TS_P_WRITE_MISS_WB,
				S_DIRTY_P_WRITE_MISS_CPF_DEVICE_ENDIO,
				S_DIRTY_P_WRITE_MISS_CPT_CACHE_END);

	pmem_data_put_page_write(bc,
				 cache_block,
				 &wi->wi_pmem_ctx,
				 wi, /*callback context */
				 cache_put_page_write_callback,
				 S_CLEAN);
}

void sm_pwrite_miss_copy_to_cache_end(struct bittern_cache *bc,
					    struct work_item *wi,
					    struct bio *bio)
{
	unsigned long cache_flags;
	enum cache_state original_state;
	struct cache_block *cache_block;

	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT((wi->wi_flags & WI_FLAG_HAS_ENDIO) == 0);
	ASSERT((wi->wi_flags & WI_FLAG_MAP_IO) != 0);
	ASSERT(wi->wi_io_endio == NULL);
	ASSERT(wi->wi_original_bio != NULL);
	cache_block = wi->wi_cache_block;
	original_state = cache_block->bcb_state;

	ASSERT(bio != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(bio == wi->wi_original_bio);
	ASSERT(cache_block->bcb_state ==
	       S_CLEAN_P_WRITE_MISS_CPT_CACHE_END
	       || cache_block->bcb_state ==
	       S_DIRTY_P_WRITE_MISS_CPT_CACHE_END);
	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "copy-to-cache-end");

	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);

	ASSERT_CACHE_STATE(cache_block);
	ASSERT((wi->wi_flags & WI_FLAG_MAP_IO) != 0);
	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT_CACHE_BLOCK(cache_block, bc);

	if (cache_block->bcb_state == S_CLEAN_P_WRITE_MISS_CPT_CACHE_END) {
		spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);
		cache_state_transition_final(bc,
					     cache_block,
					     TS_NONE,
					     S_CLEAN);
		spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	} else {
		ASSERT(cache_block->bcb_state ==
		       S_DIRTY_P_WRITE_MISS_CPT_CACHE_END);
		spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);
		cache_state_transition_final(bc,
					     cache_block,
					     TS_NONE,
					     S_DIRTY);
		spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	}

	cache_put_update_age(bc, cache_block, 1);

	cache_timer_add(&bc->bc_timer_writes, wi->wi_ts_started);
	cache_timer_add(&bc->bc_timer_write_misses, wi->wi_ts_started);
	if (original_state == S_CLEAN_P_WRITE_MISS_CPT_CACHE_END) {
		cache_timer_add(&bc->bc_timer_write_clean_misses,
				wi->wi_ts_started);
	} else {
		ASSERT(original_state == S_DIRTY_P_WRITE_MISS_CPT_CACHE_END);
		cache_timer_add(&bc->bc_timer_write_dirty_misses,
				wi->wi_ts_started);
	}

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
	cache_wakeup_deferred(bc);
	bio_endio(bio, 0);
}
