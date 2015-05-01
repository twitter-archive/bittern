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

void sm_writeback_copy_from_cache_start(struct bittern_cache *bc,
					      struct work_item *wi,
					      struct bio *bio)
{
	/*
	 * there is no bio in this case.
	 * clone bio, start i/o to write data to device.
	 */
	int ret;
	struct cache_block *cache_block;

	ASSERT(wi->wi_original_bio == NULL);
	ASSERT(wi->wi_cloned_bio == NULL);
	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);
	cache_block = wi->wi_cache_block;

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "writeback-copy-from-cache-start");

	ASSERT(cache_block->bcb_state ==
	       CACHE_VALID_DIRTY_WRITEBACK_COPY_FROM_CACHE_START
	       || cache_block->bcb_state ==
	       CACHE_VALID_DIRTY_WRITEBACK_INV_COPY_FROM_CACHE_START);
	if (cache_block->bcb_state ==
	    CACHE_VALID_DIRTY_WRITEBACK_COPY_FROM_CACHE_START)
		cache_state_transition3(
			bc,
			cache_block,
			CACHE_TRANSITION_PATH_WRITEBACK_WB,
			CACHE_VALID_DIRTY_WRITEBACK_COPY_FROM_CACHE_START,
			CACHE_VALID_DIRTY_WRITEBACK_COPY_FROM_CACHE_END);
	else
		cache_state_transition3(
			bc,
			cache_block,
			CACHE_TRANSITION_PATH_WRITEBACK_INV_WB,
			CACHE_VALID_DIRTY_WRITEBACK_INV_COPY_FROM_CACHE_START,
			CACHE_VALID_DIRTY_WRITEBACK_INV_COPY_FROM_CACHE_END);

	ret = pmem_data_get_page_read(bc,
				      cache_block->bcb_block_id,
				      cache_block,
				      &wi->wi_cache_data,
				      &wi->wi_async_context,
				      wi, /*callback context */
				      cache_get_page_read_callback);
	M_ASSERT_FIXME(ret == 0);
}

void sm_writeback_copy_from_cache_end(struct bittern_cache *bc,
					    struct work_item *wi,
					    struct bio *bio)
{
	/*
	 * there is no bio in this case.
	 * clone bio, start i/o to write data to device.
	 */
	int ret;
	struct cache_block *cache_block;
	int val;

	ASSERT(bio == NULL);
	ASSERT(wi->wi_original_bio == NULL);
	ASSERT(wi->wi_cloned_bio == NULL);
	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);
	cache_block = wi->wi_cache_block;

	ASSERT(cache_block->bcb_state ==
	       CACHE_VALID_DIRTY_WRITEBACK_COPY_FROM_CACHE_END
	       || cache_block->bcb_state ==
	       CACHE_VALID_DIRTY_WRITEBACK_INV_COPY_FROM_CACHE_END);
	ASSERT(atomic_read(&wi->wi_cache_data.di_busy) == 1);
	ASSERT(wi->wi_cache_data.di_page != NULL);
	ASSERT(wi->wi_cache_data.di_buffer != NULL);

	ASSERT(bc->bc_enable_extra_checksum_check == 0
	       || bc->bc_enable_extra_checksum_check == 1);
	if (bc->bc_enable_extra_checksum_check != 0) {
		/* verify that crc32c is correct */
		cache_verify_hash_data_buffer(bc,
						cache_block,
						wi->wi_cache_data.di_buffer);
	}

	/*
	 * check crc32c
	 */
	cache_track_hash_check(bc, cache_block,
					 cache_block->bcb_hash_data);

	bio = bio_alloc(GFP_ATOMIC, 1);
	M_ASSERT_FIXME(bio != NULL);

	ASSERT(wi->wi_cache_data.di_page != NULL);
	bio_set_data_dir_write(bio);
	ASSERT(bio_data_dir(bio) == WRITE);
	bio->bi_iter.bi_sector = cache_block->bcb_sector;
	bio->bi_iter.bi_size = PAGE_SIZE;
	bio->bi_bdev = bc->bc_dev->bdev;
	bio->bi_end_io = cache_state_machine_endio;
	bio->bi_private = wi;
	bio->bi_vcnt = 1;
	bio->bi_io_vec[0].bv_page = wi->wi_cache_data.di_page;
	bio->bi_io_vec[0].bv_len = PAGE_SIZE;
	bio->bi_io_vec[0].bv_offset = 0;
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));

	wi->wi_original_bio = bio;
	wi->wi_cloned_bio = bio;
	ASSERT(wi->wi_cache == bc);
	ASSERT(wi->wi_cache_block == cache_block);

	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "writeback-to-device");

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_WORK_ITEM(wi, bc);

	if (cache_block->bcb_state ==
	    CACHE_VALID_DIRTY_WRITEBACK_COPY_FROM_CACHE_END)
		cache_state_transition3(
			bc,
			cache_block,
			CACHE_TRANSITION_PATH_WRITEBACK_WB,
			CACHE_VALID_DIRTY_WRITEBACK_COPY_FROM_CACHE_END,
			CACHE_VALID_DIRTY_WRITEBACK_COPY_TO_DEVICE_ENDIO);
	else
		cache_state_transition3(
			bc,
			cache_block,
			CACHE_TRANSITION_PATH_WRITEBACK_INV_WB,
			CACHE_VALID_DIRTY_WRITEBACK_INV_COPY_FROM_CACHE_END,
			CACHE_VALID_DIRTY_WRITEBACK_INV_COPY_TO_DEVICE_ENDIO);

	atomic_inc(&bc->bc_write_cached_device_requests);
	val = atomic_inc_return(&bc->bc_pending_cached_device_requests);
	atomic_set_if_higher(&bc->bc_highest_pending_cached_device_requests,
			     val);

	/*
	 * we are not in a process context here, so use work queues to defer
	 * calling generic_make_request() to a thread
	 */
	/*
	 * FIXME: add API to tell us if asyncread is sync or async and decide
	 * based on it
	 */
	wi->wi_ts_workqueue = current_kernel_time_nsec();
	INIT_WORK(&wi->wi_work, cache_make_request_worker);
	ret = queue_work(bc->bc_make_request_wq, &wi->wi_work);
	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "queue_work=%d", ret);
	ASSERT(ret == 1);
}

void sm_writeback_copy_to_device_endio(struct bittern_cache *bc,
					     struct work_item *wi,
					     struct bio *bio)
{
	/*
	 * there is no bio in this case.
	 * clone bio, start i/o to write data to device.
	 */
	int ret;
	enum cache_state metadata_state;
	struct cache_block *cache_block;

	ASSERT(bio == NULL);
	ASSERT(wi->wi_original_bio == NULL);
	ASSERT(wi->wi_cloned_bio == NULL);
	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);
	cache_block = wi->wi_cache_block;

	atomic_dec(&bc->bc_pending_cached_device_requests);

	ASSERT(cache_block->bcb_state ==
	       CACHE_VALID_DIRTY_WRITEBACK_COPY_TO_DEVICE_ENDIO
	       || cache_block->bcb_state ==
	       CACHE_VALID_DIRTY_WRITEBACK_INV_COPY_TO_DEVICE_ENDIO);
	ASSERT(wi->wi_cache_data.di_page != NULL);
	ASSERT(wi->wi_cache_data.di_buffer != NULL);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "writeback-copy-to-device-endio");

	/*
	 * release cache page
	 */
	ASSERT(atomic_read(&wi->wi_cache_data.di_busy) == 1);
	pmem_data_put_page_read(bc,
						    cache_block->bcb_block_id,
						    cache_block,
						    &wi->wi_cache_data);
	ASSERT(atomic_read(&wi->wi_cache_data.di_busy) == 0);

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_WORK_ITEM(wi, bc);

	if (cache_block->bcb_state ==
	    CACHE_VALID_DIRTY_WRITEBACK_COPY_TO_DEVICE_ENDIO)
		metadata_state = CACHE_VALID_CLEAN;
	else
		metadata_state = CACHE_INVALID;

	if (cache_block->bcb_state ==
	    CACHE_VALID_DIRTY_WRITEBACK_COPY_TO_DEVICE_ENDIO)
		cache_state_transition3(
			bc, cache_block,
			CACHE_TRANSITION_PATH_WRITEBACK_WB,
			CACHE_VALID_DIRTY_WRITEBACK_COPY_TO_DEVICE_ENDIO,
			CACHE_VALID_DIRTY_WRITEBACK_UPDATE_METADATA_END);
	else
		cache_state_transition3(
			bc,
			cache_block,
			CACHE_TRANSITION_PATH_WRITEBACK_INV_WB,
			CACHE_VALID_DIRTY_WRITEBACK_INV_COPY_TO_DEVICE_ENDIO,
			CACHE_VALID_DIRTY_WRITEBACK_INV_UPDATE_METADATA_END);

	/*
	 * start updating metadata
	 */
	ret = pmem_metadata_async_write(bc,
					cache_block->bcb_block_id,
					cache_block,
					&wi->wi_cache_data,
					&wi->wi_async_context,
					wi, /* callback context */
					cache_metadata_write_callback,
					metadata_state);
	M_ASSERT_FIXME(ret == 0);
}

void sm_writeback_update_metadata_end(struct bittern_cache *bc,
					    struct work_item *wi,
					    struct bio *bio)
{
	struct cache_block *cache_block;
	/*
	 * there is no bio in this case.
	 * clone bio, start i/o to write data to device.
	 */
	ASSERT(bio == NULL);
	ASSERT(wi->wi_original_bio == NULL);
	ASSERT(wi->wi_cloned_bio == NULL);
	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);
	cache_block = wi->wi_cache_block;

	ASSERT(cache_block->bcb_state ==
	       CACHE_VALID_DIRTY_WRITEBACK_UPDATE_METADATA_END
	       || cache_block->bcb_state ==
	       CACHE_VALID_DIRTY_WRITEBACK_INV_UPDATE_METADATA_END);
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "writeback-update-metadata-end, wi_io_endio=%p",
		 wi->wi_io_endio);

	/*
	 * the endio function is responsible for deallocating the work_item
	 */

	/*
	 * given this is an externally initiated request, we expect callback
	 * to be valid
	 */
	ASSERT((wi->wi_flags & WI_FLAG_HAS_ENDIO) != 0);
	ASSERT(wi->wi_io_endio != NULL);
	(*wi->wi_io_endio) (bc, wi, cache_block);
}
