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

#include "bittern_cache.h"

void sm_invalidate_start(struct bittern_cache *bc,
			 struct work_item *wi,
			 struct bio *bio)
{
	struct cache_block *cache_block;
	/*
	 * there is no bio in this case.
	 * clone bio, start i/o to write data to device.
	 */
	int ret;

	ASSERT(bio == NULL);
	ASSERT(wi->wi_original_bio == NULL);
	ASSERT(wi->wi_cloned_bio == NULL);
	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT((wi->wi_flags & WI_FLAG_HAS_ENDIO) != 0);
	ASSERT(wi->wi_original_cache_block == NULL);
	cache_block = wi->wi_cache_block;

	ASSERT(cache_block->bcb_state ==
	       CACHE_VALID_CLEAN_INVALIDATE_START
	       || cache_block->bcb_state ==
	       CACHE_VALID_DIRTY_INVALIDATE_START);
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "invalidate-startio");

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_WORK_ITEM(wi, bc);

	/* we don't do dirty invalidation yet */
	if (cache_block->bcb_state ==
	    CACHE_VALID_CLEAN_INVALIDATE_START)
		cache_state_transition3(
			bc,
			cache_block,
			CACHE_TRANSITION_PATH_CLEAN_INVALIDATION_WTWB,
			CACHE_VALID_CLEAN_INVALIDATE_START,
			CACHE_VALID_CLEAN_INVALIDATE_END);
	else
		cache_state_transition3(
			bc,
			cache_block,
			CACHE_TRANSITION_PATH_DIRTY_INVALIDATION_WB,
			CACHE_VALID_DIRTY_INVALIDATE_START,
			CACHE_VALID_DIRTY_INVALIDATE_END);

	/*
	 * start updating metadata
	 */
	ret = pmem_metadata_async_write(bc,
					cache_block,
					&wi->wi_pmem_ctx,
					wi, /* callback context */
					cache_metadata_write_callback,
					CACHE_INVALID);
	M_ASSERT_FIXME(ret == 0);
}

void sm_invalidate_end(struct bittern_cache *bc,
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
	ASSERT((wi->wi_flags & WI_FLAG_HAS_ENDIO) != 0);
	ASSERT(wi->wi_original_cache_block == NULL);
	cache_block = wi->wi_cache_block;

	ASSERT(cache_block->bcb_state ==
	       CACHE_VALID_CLEAN_INVALIDATE_END
	       || cache_block->bcb_state ==
	       CACHE_VALID_DIRTY_INVALIDATE_END);
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "invalidate-end: %p", wi->wi_io_endio);

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
