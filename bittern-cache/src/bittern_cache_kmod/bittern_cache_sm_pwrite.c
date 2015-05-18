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
	struct bio *cloned_bio;
	int ret;
	struct cache_block *cache_block;
	int val;

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
	       CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO
	       || cache_block->bcb_state ==
	       CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO);
	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);

	ASSERT(atomic_read(&wi->wi_cache_data.di_busy) == 0);
	ret = pmem_data_get_page_write(bc,
							   cache_block->
							   bcb_block_id,
							   cache_block,
							   &wi->wi_cache_data);
	M_ASSERT_FIXME(ret == 0);
	ASSERT(wi->wi_cache_data.di_page != NULL);
	ASSERT(atomic_read(&wi->wi_cache_data.di_busy) == 1);

	cloned_bio = bio_alloc(GFP_ATOMIC, 1);
	M_ASSERT_FIXME(cloned_bio != NULL);

	ASSERT(wi->wi_original_bio == bio);
	ASSERT(wi->wi_cloned_bio == NULL);
	wi->wi_cloned_bio = cloned_bio;
	ASSERT(wi->wi_cache == bc);
	ASSERT(wi->wi_cache_block == cache_block);

	/*
	 * here we always read the full page.
	 * in the next state we copy only the portion of the page that is requested
	 */

	/*
	 * we are reading from the cached device into the cache
	 */
	bio_set_data_dir_read(cloned_bio);
	ASSERT(bio_data_dir(cloned_bio) == READ);

	cloned_bio->bi_iter.bi_sector = bio_sector_to_cache_block_sector(bio);
	cloned_bio->bi_iter.bi_size = PAGE_SIZE;
	cloned_bio->bi_bdev = bc->bc_dev->bdev;
	cloned_bio->bi_end_io = cache_state_machine_endio;
	cloned_bio->bi_private = wi;
	cloned_bio->bi_io_vec[0].bv_page = wi->wi_cache_data.di_page;
	cloned_bio->bi_io_vec[0].bv_len = PAGE_SIZE;
	cloned_bio->bi_io_vec[0].bv_offset = 0;
	cloned_bio->bi_vcnt = 1;

	if (bio->bi_iter.bi_size == PAGE_SIZE) {
		ASSERT(bio->bi_iter.bi_sector == cloned_bio->bi_iter.bi_sector);
		ASSERT(bio->bi_iter.bi_size == cloned_bio->bi_iter.bi_size);
	}

	atomic_inc(&bc->bc_read_cached_device_requests);
	val = atomic_inc_return(&bc->bc_pending_cached_device_requests);
	atomic_set_if_higher(&bc->bc_highest_pending_cached_device_requests,
			     val);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "copy-from-device");
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);

	if (cache_block->bcb_state ==
	    CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO)
	{
		cache_state_transition3(bc, cache_block,
						CACHE_TRANSITION_PATH_PARTIAL_WRITE_MISS_WT,
						CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO,
						CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO);
	} else {
		ASSERT(cache_block->bcb_state ==
		       CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO);
		cache_state_transition3(bc, cache_block,
						CACHE_TRANSITION_PATH_PARTIAL_WRITE_MISS_WB,
						CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO,
						CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO);
	}

	/*
	 * first state -- this is called from process context
	 */
	wi->wi_ts_physio = current_kernel_time_nsec();
	generic_make_request(cloned_bio);
}

void sm_pwrite_miss_copy_from_device_endio(struct bittern_cache *bc,
						 struct work_item *wi,
						 struct bio *bio)
{
	uint128_t hash_data;
	int ret;
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
	       CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO
	       || cache_block->bcb_state ==
	       CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO);
	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL, "endio");

	atomic_dec(&bc->bc_pending_cached_device_requests);

	/*
	 * we can check the original hash
	 */
	ASSERT(wi->wi_cache_data.di_buffer != NULL);
	ASSERT(wi->wi_cache_data.di_page != NULL);
	cache_track_hash_check_buffer(bc,
					cache_block,
					wi->wi_cache_data.di_buffer);

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
	    CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO)
	{
		struct bio *cloned_bio;
		int val;

		ASSERT(atomic_read(&wi->wi_cache_data.di_busy) == 1);

		cloned_bio = bio_alloc(GFP_ATOMIC, 1);
		M_ASSERT_FIXME(cloned_bio != NULL);

		ASSERT(wi->wi_original_bio == bio);
		ASSERT(wi->wi_cloned_bio == NULL);
		wi->wi_cloned_bio = cloned_bio;
		ASSERT(wi->wi_cache == bc);
		ASSERT(wi->wi_cache_block == cache_block);

		/*
		 * here we always write the full page.
		 * for partial writes, we previously read-in the whole page, then modified a part of it with data from userland.
		 */

		/*
		 * we are writing from cache into the device
		 */
		bio_set_data_dir_write(cloned_bio);
		ASSERT(bio_data_dir(cloned_bio) == WRITE);

		cloned_bio->bi_iter.bi_sector =
		    bio_sector_to_cache_block_sector(bio);
		cloned_bio->bi_iter.bi_size = PAGE_SIZE;
		cloned_bio->bi_bdev = bc->bc_dev->bdev;
		cloned_bio->bi_end_io = cache_state_machine_endio;
		cloned_bio->bi_private = wi;
		cloned_bio->bi_io_vec[0].bv_page = wi->wi_cache_data.di_page;
		cloned_bio->bi_io_vec[0].bv_len = PAGE_SIZE;
		cloned_bio->bi_io_vec[0].bv_offset = 0;
		cloned_bio->bi_vcnt = 1;

		if (bio->bi_iter.bi_size == PAGE_SIZE) {
			ASSERT(bio->bi_iter.bi_sector ==
			       cloned_bio->bi_iter.bi_sector);
			ASSERT(bio->bi_iter.bi_size ==
			       cloned_bio->bi_iter.bi_size);
		}

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
						CACHE_TRANSITION_PATH_PARTIAL_WRITE_MISS_WT,
						CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO,
						CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_TO_DEVICE_ENDIO);

		wi->wi_ts_physio = current_kernel_time_nsec();
		generic_make_request(cloned_bio);

	} else {

		BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio,
			 wi->wi_cloned_bio, "copy-to-cache");
		/*
		 * for writeback we commit to cache and then we are done
		 */
		cache_state_transition3(bc,
						cache_block,
						CACHE_TRANSITION_PATH_PARTIAL_WRITE_MISS_WB,
						CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO,
						CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END);

		ret = pmem_data_put_page_write(bc, cache_block->bcb_block_id, cache_block, &wi->wi_cache_data, &wi->wi_async_context, wi,	/*callback context */
								   cache_put_page_write_callback,	/*callback function */
								   CACHE_VALID_DIRTY);
		M_ASSERT_FIXME(ret == 0);

	}
}

void sm_pwrite_miss_copy_to_device_endio(struct bittern_cache *bc,
					       struct work_item *wi,
					       struct bio *bio)
{
	int ret;
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
	       CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_TO_DEVICE_ENDIO);
	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);
	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "copy-to-device-endio");

	atomic_dec(&bc->bc_pending_cached_device_requests);

	ASSERT(atomic_read(&wi->wi_cache_data.di_busy) == 1);

	/*
	 * for writeback we commit to cache and then we are done
	 */
	cache_state_transition3(bc,
					cache_block,
					CACHE_TRANSITION_PATH_PARTIAL_WRITE_MISS_WB,
					CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO,
					CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END);

	ret = pmem_data_put_page_write(bc, cache_block->bcb_block_id, cache_block, &wi->wi_cache_data, &wi->wi_async_context, wi,	/*callback context */
							   cache_put_page_write_callback,	/*callback function */
							   CACHE_VALID_CLEAN);
	M_ASSERT_FIXME(ret == 0);
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
	       CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END
	       || cache_block->bcb_state ==
	       CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END);
	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "copy-to-cache-end");

	ASSERT(atomic_read(&wi->wi_cache_data.di_busy) == 0);

	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);

	ASSERT_CACHE_STATE(cache_block);
	ASSERT((wi->wi_flags & WI_FLAG_MAP_IO) != 0);
	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT_CACHE_BLOCK(cache_block, bc);

	if (cache_block->bcb_state ==
	    CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END) {
		spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);
		cache_state_transition_final(bc,
						     cache_block,
						     CACHE_TRANSITION_PATH_NONE,
						     CACHE_VALID_CLEAN);
		spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	} else {
		ASSERT(cache_block->bcb_state ==
		       CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END);
		spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);
		cache_state_transition_final(bc,
						     cache_block,
						     CACHE_TRANSITION_PATH_NONE,
						     CACHE_VALID_DIRTY);
		spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	}

	cache_put_update_age(bc, cache_block, 1);

	cache_timer_add(&bc->bc_timer_writes, wi->wi_ts_started);
	cache_timer_add(&bc->bc_timer_write_misses, wi->wi_ts_started);
	if (original_state ==
	    CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END) {
		cache_timer_add(&bc->bc_timer_write_clean_misses,
					wi->wi_ts_started);
	} else {
		ASSERT(original_state ==
		       CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END);
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
