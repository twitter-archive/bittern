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

void sm_read_hit_copy_from_cache_start(struct bittern_cache *bc,
				       struct work_item *wi)
{
	struct bio *bio = wi->wi_original_bio;
	struct cache_block *cache_block = wi->wi_cache_block;

	M_ASSERT(bio != NULL);
	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT(wi->wi_original_bio != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(bio == wi->wi_original_bio);
	ASSERT(wi->wi_cache == bc);
	ASSERT(cache_block->bcb_state == S_CLEAN_READ_HIT_CPF_CACHE_START ||
	       cache_block->bcb_state == S_DIRTY_READ_HIT_CPF_CACHE_START);
	ASSERT(wi->wi_original_cache_block == NULL);

	if (cache_block->bcb_state == S_CLEAN_READ_HIT_CPF_CACHE_START) {
		cache_state_transition3(bc,
					cache_block,
					TS_READ_HIT_WTWB_CLEAN,
					S_CLEAN_READ_HIT_CPF_CACHE_START,
					S_CLEAN_READ_HIT_CPF_CACHE_END);
	} else {
		ASSERT(cache_block->bcb_state ==
		       S_DIRTY_READ_HIT_CPF_CACHE_START);
		cache_state_transition3(bc,
					cache_block,
					TS_READ_HIT_WB_DIRTY,
					S_DIRTY_READ_HIT_CPF_CACHE_START,
					S_DIRTY_READ_HIT_CPF_CACHE_END);
	}

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "start_async_read (get_page_read): wi=%p, bc=%p, cache_block=%p, bio=%p",
		 wi, bc, cache_block, bio);
	pmem_data_get_page_read(bc,
				cache_block,
				&wi->wi_pmem_ctx,
				wi, /*callback context */
				cache_get_page_read_callback);
}

void sm_read_hit_copy_from_cache_end(struct bittern_cache *bc,
				     struct work_item *wi,
				     int err)
{
	struct bio *bio = wi->wi_original_bio;
	struct cache_block *cache_block = wi->wi_cache_block;
	enum cache_state original_state = cache_block->bcb_state;
	unsigned long cache_flags;

	M_ASSERT(bio != NULL);
	M_ASSERT_FIXME(err == 0);

	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT(wi->wi_original_bio != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(bio == wi->wi_original_bio);
	ASSERT(wi->wi_cache == bc);
	ASSERT(cache_block->bcb_state == S_CLEAN_READ_HIT_CPF_CACHE_END ||
	       cache_block->bcb_state == S_DIRTY_READ_HIT_CPF_CACHE_END);
	ASSERT(wi->wi_original_cache_block == NULL);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "bio_copy_from_cache");

	ASSERT(bc->bc_enable_extra_checksum_check == 0 ||
	       bc->bc_enable_extra_checksum_check == 1);
	if (bc->bc_enable_extra_checksum_check != 0) {
		uint128_t hash_data;
		/*
		 * copy bio from cache, aka userland reads
		 */
		bio_copy_from_cache(wi, bio, &hash_data);

		/* this is a read hit, verify that hash data is correct */
		cache_verify_hash_data(bc, cache_block, hash_data);
	} else {
		/*
		 * copy bio from cache, aka userland reads
		 */
		bio_copy_from_cache_nohash(wi, bio);
	}

	/*
	 * check crc32c
	 */
	cache_track_hash_check(bc, cache_block, cache_block->bcb_hash_data);

	/*
	 * release cache page
	 */
	pmem_data_put_page_read(bc,
				cache_block,
				&wi->wi_pmem_ctx);

	ASSERT_WORK_ITEM(wi, bc);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);

	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "io-done");
	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);

	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);
	if (cache_block->bcb_state == S_DIRTY_READ_HIT_CPF_CACHE_END) {
		cache_state_transition_final(bc, cache_block,
						     TS_NONE,
						     S_DIRTY);
	} else {
		ASSERT(cache_block->bcb_state ==
		       S_CLEAN_READ_HIT_CPF_CACHE_END);
		cache_state_transition_final(bc, cache_block,
						     TS_NONE,
						     S_CLEAN);
	}
	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);

	cache_put_update_age(bc, cache_block, 1);

	cache_timer_add(&bc->bc_timer_reads, wi->wi_ts_started);
	if (original_state == S_CLEAN_READ_HIT_CPF_CACHE_END) {
		cache_timer_add(&bc->bc_timer_read_hits,
					wi->wi_ts_started);
		cache_timer_add(&bc->bc_timer_read_clean_hits,
					wi->wi_ts_started);
	} else {
		ASSERT(original_state == S_DIRTY_READ_HIT_CPF_CACHE_END);
		cache_timer_add(&bc->bc_timer_read_hits,
					wi->wi_ts_started);
		cache_timer_add(&bc->bc_timer_read_dirty_hits,
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
	wakeup_deferred(bc);
	bio_endio(bio, 0);

	ASSERT_BITTERN_CACHE(bc);
}

void sm_read_miss_copy_from_device_start(struct bittern_cache *bc,
					 struct work_item *wi)
{
	struct bio *bio = wi->wi_original_bio;
	struct cache_block *cache_block = wi->wi_cache_block;
	int val;
	struct page *cache_page;

	M_ASSERT(bio != NULL);
	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT(wi->wi_original_bio != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(bio == wi->wi_original_bio);
	ASSERT(cache_block->bcb_state ==
	       S_CLEAN_READ_MISS_CPF_DEVICE_START);
	ASSERT(wi->wi_original_cache_block == NULL);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "copy-from-device-0");

	pmem_data_get_page_write(bc,
				 cache_block,
				 &wi->wi_pmem_ctx);

	cache_page = pmem_context_data_page(&wi->wi_pmem_ctx);

	atomic_inc(&bc->bc_read_cached_device_requests);
	val = atomic_inc_return(&bc->bc_pending_cached_device_requests);
	atomic_set_if_higher(&bc->bc_highest_pending_cached_device_requests,
			     val);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "copy-from-device");
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);

	cache_state_transition3(bc,
				cache_block,
				TS_READ_MISS_WTWB_CLEAN,
				S_CLEAN_READ_MISS_CPF_DEVICE_START,
				S_CLEAN_READ_MISS_CPF_DEVICE_END);

	/*
	 * we are in the first state -- process context
	 */
	M_ASSERT(!in_irq() && !in_softirq());
	wi->wi_ts_workqueue = current_kernel_time_nsec();
	cached_dev_do_make_request(bc,
				   wi,
				   READ, /* datadir */
				   false); /* do not set original bio */
}

void sm_read_miss_copy_from_device_end(struct bittern_cache *bc,
				       struct work_item *wi,
				       int err)
{
	struct bio *bio = wi->wi_original_bio;
	struct cache_block *cache_block = wi->wi_cache_block;
	uint128_t hash_data;

	M_ASSERT_FIXME(err == 0);
	M_ASSERT(bio != NULL);

	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT(wi->wi_original_bio != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(bio == wi->wi_original_bio);
	ASSERT(cache_block->bcb_state == S_CLEAN_READ_MISS_CPF_DEVICE_END);
	ASSERT(wi->wi_original_cache_block == NULL);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "endio - copy to userland");

	atomic_dec(&bc->bc_pending_cached_device_requests);

	/*
	 * copy bio from cache, aka userland reads
	 */
	bio_copy_from_cache(wi, bio, &hash_data);

	/* this is a read miss, so we need to update the hash */
	cache_block->bcb_hash_data = hash_data;

	/*
	 * Check/update hash. This is a bit counter-intuitive.
	 * The tracked hash, if valid, still has the old hash,
	 * so we can check it.
	 * The update is needed so to update the new hash.
	 */
	cache_track_hash_check(bc, cache_block,
					 cache_block->bcb_hash_data);
	cache_track_hash_set(bc, cache_block,
				       cache_block->bcb_hash_data);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "endio - release cache page");

	cache_state_transition3(bc,
				cache_block,
				TS_READ_MISS_WTWB_CLEAN,
				S_CLEAN_READ_MISS_CPF_DEVICE_END,
				S_CLEAN_READ_MISS_CPT_CACHE_END);

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

void sm_read_miss_copy_to_cache_end(struct bittern_cache *bc,
				    struct work_item *wi,
				    int err)
{
	struct bio *bio = wi->wi_original_bio;
	struct cache_block *cache_block = wi->wi_cache_block;
	unsigned long cache_flags;

	M_ASSERT_FIXME(err == 0);

	M_ASSERT(bio != NULL);
	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT(wi->wi_original_bio != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(bio == wi->wi_original_bio);
	ASSERT(cache_block->bcb_state == S_CLEAN_READ_MISS_CPT_CACHE_END);
	ASSERT(wi->wi_original_cache_block == NULL);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "end");

	ASSERT_WORK_ITEM(wi, bc);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);

	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "io-done");

	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);
	cache_state_transition_final(bc,
				     cache_block,
				     TS_NONE,
				     S_CLEAN);
	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	cache_put_update_age(bc, cache_block, 1);

	cache_timer_add(&bc->bc_timer_reads, wi->wi_ts_started);
	cache_timer_add(&bc->bc_timer_read_misses, wi->wi_ts_started);

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

	ASSERT_BITTERN_CACHE(bc);
}
