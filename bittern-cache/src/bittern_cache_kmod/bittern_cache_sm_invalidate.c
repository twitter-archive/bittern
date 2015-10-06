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

void sm_invalidate_start(struct bittern_cache *bc, struct work_item *wi)
{
	struct cache_block *cache_block = wi->wi_cache_block;

	M_ASSERT(wi->wi_original_bio == NULL);
	M_ASSERT(wi->wi_cloned_bio == NULL);
	M_ASSERT(wi->wi_original_cache_block == NULL);

	ASSERT(cache_block->bcb_state == S_CLEAN_INVALIDATE_START ||
	       cache_block->bcb_state == S_DIRTY_INVALIDATE_START);
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, NULL, NULL,
		 "invalidate-startio");

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_WORK_ITEM(wi, bc);

	/* we don't do dirty invalidation yet */
	if (cache_block->bcb_state == S_CLEAN_INVALIDATE_START)
		cache_state_transition3(bc,
					cache_block,
					TS_CLEAN_INVALIDATION_WTWB,
					S_CLEAN_INVALIDATE_START,
					S_CLEAN_INVALIDATE_END);
	else
		cache_state_transition3(bc,
					cache_block,
					TS_DIRTY_INVALIDATION_WB,
					S_DIRTY_INVALIDATE_START,
					S_DIRTY_INVALIDATE_END);

	/*
	 * start updating metadata
	 */
	pmem_metadata_async_write(bc,
				  cache_block,
				  &wi->wi_pmem_ctx,
				  wi, /* callback context */
				  cache_metadata_write_callback,
				  S_INVALID);
}

void sm_invalidate_end(struct bittern_cache *bc,
		       struct work_item *wi,
		       int err)
{
	struct cache_block *cache_block = wi->wi_cache_block;

	M_ASSERT_FIXME(err == 0);

	M_ASSERT(wi->wi_original_bio == NULL);
	M_ASSERT(wi->wi_cloned_bio == NULL);
	M_ASSERT(wi->wi_original_cache_block == NULL);

	ASSERT(cache_block->bcb_state == S_CLEAN_INVALIDATE_END ||
	       cache_block->bcb_state == S_DIRTY_INVALIDATE_END);
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, NULL, NULL,
		 "invalidate-end");

	/*
	 * The invalidator's endio function is responsible for
	 * deallocating the work_item.
	 */
	cache_invalidate_block_io_end(bc, wi, cache_block);
}
