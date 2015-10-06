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
					struct work_item *wi)
{
	struct cache_block *cache_block = wi->wi_cache_block;

	M_ASSERT(wi->wi_original_bio == NULL);
	M_ASSERT(wi->wi_cloned_bio == NULL);
	M_ASSERT(wi->wi_original_cache_block == NULL);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, NULL, NULL,
		 "writeback-copy-from-cache-start");

	ASSERT(cache_block->bcb_state == S_DIRTY_WRITEBACK_CPF_CACHE_START ||
	       cache_block->bcb_state == S_DIRTY_WRITEBACK_INV_CPF_CACHE_START);
	if (cache_block->bcb_state == S_DIRTY_WRITEBACK_CPF_CACHE_START)
		cache_state_transition3(bc,
					cache_block,
					TS_WRITEBACK_WB,
					S_DIRTY_WRITEBACK_CPF_CACHE_START,
					S_DIRTY_WRITEBACK_CPF_CACHE_END);
	else
		cache_state_transition3(bc,
					cache_block,
					TS_WRITEBACK_INV_WB,
					S_DIRTY_WRITEBACK_INV_CPF_CACHE_START,
					S_DIRTY_WRITEBACK_INV_CPF_CACHE_END);

	pmem_data_get_page_read(bc,
				cache_block,
				&wi->wi_pmem_ctx,
				wi, /*callback context */
				cache_get_page_read_callback);
}

void sm_writeback_copy_from_cache_end(struct bittern_cache *bc,
				      struct work_item *wi,
				      int err)
{
	struct cache_block *cache_block = wi->wi_cache_block;
	struct page *cache_page;
	int val;

	M_ASSERT_FIXME(err == 0);

	M_ASSERT(wi->wi_original_bio == NULL);
	M_ASSERT(wi->wi_cloned_bio == NULL);
	M_ASSERT(wi->wi_original_cache_block == NULL);

	ASSERT(cache_block->bcb_state == S_DIRTY_WRITEBACK_CPF_CACHE_END ||
	       cache_block->bcb_state == S_DIRTY_WRITEBACK_INV_CPF_CACHE_END);
	ASSERT(bc->bc_enable_extra_checksum_check == 0 ||
	       bc->bc_enable_extra_checksum_check == 1);

	cache_page = pmem_context_data_page(&wi->wi_pmem_ctx);

	if (bc->bc_enable_extra_checksum_check != 0) {
		char *cache_vaddr;

		cache_vaddr = pmem_context_data_vaddr(&wi->wi_pmem_ctx);
		/* verify that crc32c is correct */
		cache_verify_hash_data_buffer(bc, cache_block, cache_vaddr);
	}

	/*
	 * check crc32c
	 */
	cache_track_hash_check(bc, cache_block,
					 cache_block->bcb_hash_data);

	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, cache_block, NULL, NULL,
		 "writeback-to-device");

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_WORK_ITEM(wi, bc);

	if (cache_block->bcb_state == S_DIRTY_WRITEBACK_CPF_CACHE_END)
		cache_state_transition3(bc,
					cache_block,
					TS_WRITEBACK_WB,
					S_DIRTY_WRITEBACK_CPF_CACHE_END,
					S_DIRTY_WRITEBACK_CPT_DEVICE_END);
	else
		cache_state_transition3(bc,
					cache_block,
					TS_WRITEBACK_INV_WB,
					S_DIRTY_WRITEBACK_INV_CPF_CACHE_END,
					S_DIRTY_WRITEBACK_INV_CPT_DEVICE_END);

	atomic_inc(&bc->bc_write_cached_device_requests);
	val = atomic_inc_return(&bc->bc_pending_cached_device_requests);
	atomic_set_if_higher(&bc->bc_highest_pending_cached_device_requests,
			     val);

	/*
	 * potentially in a softirq
	 */
	cached_dev_make_request_defer(bc,
				      wi,
				      WRITE, /* datadir */
				      true); /* set original bio */
}

void sm_writeback_copy_to_device_end(struct bittern_cache *bc,
				     struct work_item *wi,
				     int err)
{
	struct cache_block *cache_block = wi->wi_cache_block;
	enum cache_state metadata_state;

	M_ASSERT_FIXME(err == 0);

	M_ASSERT(wi->wi_original_bio == NULL);
	M_ASSERT(wi->wi_cloned_bio == NULL);
	M_ASSERT(wi->wi_original_cache_block == NULL);

	atomic_dec(&bc->bc_pending_cached_device_requests);

	ASSERT(cache_block->bcb_state == S_DIRTY_WRITEBACK_CPT_DEVICE_END ||
	       cache_block->bcb_state ==
	       S_DIRTY_WRITEBACK_INV_CPT_DEVICE_END);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, NULL, NULL,
		 "writeback-copy-to-device-endio");

	/*
	 * release cache page
	 */
	pmem_data_put_page_read(bc,
				cache_block,
				&wi->wi_pmem_ctx);

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_WORK_ITEM(wi, bc);

	if (cache_block->bcb_state == S_DIRTY_WRITEBACK_CPT_DEVICE_END)
		metadata_state = S_CLEAN;
	else
		metadata_state = S_INVALID;

	if (cache_block->bcb_state == S_DIRTY_WRITEBACK_CPT_DEVICE_END)
		cache_state_transition3(bc,
				cache_block,
				TS_WRITEBACK_WB,
				S_DIRTY_WRITEBACK_CPT_DEVICE_END,
				S_DIRTY_WRITEBACK_UPD_METADATA_END);
	else
		cache_state_transition3(bc,
				cache_block,
				TS_WRITEBACK_INV_WB,
				S_DIRTY_WRITEBACK_INV_CPT_DEVICE_END,
				S_DIRTY_WRITEBACK_INV_UPD_METADATA_END);

	/*
	 * start updating metadata
	 */
	pmem_metadata_async_write(bc,
				  cache_block,
				  &wi->wi_pmem_ctx,
				  wi, /* callback context */
				  cache_metadata_write_callback,
				  metadata_state);
}

void sm_writeback_update_metadata_end(struct bittern_cache *bc,
				      struct work_item *wi,
				      int err)
{
	struct bio *bio = wi->wi_original_bio;
	struct cache_block *cache_block = wi->wi_cache_block;

	M_ASSERT_FIXME(err == 0);

	M_ASSERT(wi->wi_original_bio == NULL);
	M_ASSERT(wi->wi_cloned_bio == NULL);
	M_ASSERT(wi->wi_original_cache_block == NULL);

	bio = bio; /* shutoff compiler warning (used in dev build) */
	ASSERT(cache_block->bcb_state ==
	       S_DIRTY_WRITEBACK_UPD_METADATA_END ||
	       cache_block->bcb_state ==
	       S_DIRTY_WRITEBACK_INV_UPD_METADATA_END);
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "writeback-update-metadata-end");

	/*
	 * The bgwriter's endio function is responsible for
	 * deallocating the work_item.
	 */
	cache_bgwriter_io_end(bc, wi, cache_block);
}
