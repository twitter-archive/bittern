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

const char *cache_get_ret_to_str(enum cache_get_ret ret)
{
	switch (ret) {
	case CACHE_GET_RET_HIT_IDLE:
		return "CACHE_GET_RET_HIT_IDLE";
	case CACHE_GET_RET_HIT_BUSY:
		return "CACHE_GET_RET_HIT_BUSY";
	case CACHE_GET_RET_MISS_INVALID_IDLE:
		return "CACHE_GET_RET_INVALID_IDLE";
	case CACHE_GET_RET_MISS:
		return "CACHE_GET_RET_MISS";
	case CACHE_GET_RET_INVALID:
		return "CACHE_GET_RET_INVALID";
	default:
		return "CACHE_GET_RET_UNKNOWN";
	}
}

unsigned int __cache_block_pseudo_random(unsigned int previous_pseudo_random)
{
	/* linear congruent generator -- constants from ANSI C */
	return (previous_pseudo_random * 1103515245 + 12345) % 0x7fffffff;
}

int cache_get_clean(struct bittern_cache *bc,
		    struct cache_block **o_cache_block)
{
	unsigned long flags, cache_flags;
	struct cache_block *cache_block = NULL;
	int replacement_mode;
	int block_hold_ret;

	ASSERT_BITTERN_CACHE(bc);
	ASSERT(o_cache_block != NULL);
	*o_cache_block = NULL;

	/*
	 * we need to get a local copy of replacement_mode, as its value is
	 * tunable, and can change while we execute this code.
	 * it does not matter if it gets changed, but for this code to work, the
	 * value we use must stay constant
	 */
	replacement_mode = bc->bc_replacement_mode;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_REPLACEMENT_MODE(replacement_mode);

	spin_lock_irqsave(&bc->bc_entries_lock, flags);

	/*
	 * handle RANDOM replacement mode
	 */
	if (replacement_mode == CACHE_REPLACEMENT_MODE_RANDOM) {
		unsigned int random_value;
		unsigned random_cache_block_id, scan_count;

		ASSERT(cache_block == NULL);
		/*
		 * nothing found -- select a random block, regardless of its
		 * status
		 *
		 * use a decent random generator for the start,
		 * then use linear congruent generator within the loop
		 */
		random_value = (unsigned int)get_random_int();
		random_cache_block_id =
		    (random_value % atomic_read(&bc->bc_total_entries)) + 1;
		ASSERT(random_cache_block_id > 0);
		ASSERT(random_cache_block_id <=
		       atomic_read(&bc->bc_total_entries));

		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
			 "find-random, random_value=%u, random_cache_block_id=%d",
			 random_value,
			 random_cache_block_id);

		for (scan_count = 0;
		     scan_count <
		     CACHE_REPLACEMENT_MODE_RANDOM_MAX_SCANS;
		     scan_count++) {
			cache_block =
			    &bc->bc_cache_blocks[random_cache_block_id - 1];
			ASSERT(cache_block->bcb_block_id ==
			       random_cache_block_id);

			spin_lock_irqsave(&cache_block->bcb_spinlock,
					  cache_flags);
			BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL,
				 NULL,
				 "find-random, random_value=%u, random_cache_block_id=%d, scan_count=%d: cache_block->bcb_block_id=%d",
				 random_value,
				 random_cache_block_id, scan_count,
				 cache_block->bcb_block_id);
			block_hold_ret = cache_block_hold(bc, cache_block);
			if (block_hold_ret == 1
			    && cache_block->bcb_state == S_CLEAN) {
				/*
				 * found suitable cache block.
				 * note we need to keep the cache_block spinlock
				 * held as we exit from here.
				 */
				atomic_inc(&bc->bc_invalidations);
				atomic_inc(&bc->bc_idle_invalidations);
				goto replacement_cache_block_found;
			}
			cache_block_release(bc, cache_block);
			spin_unlock_irqrestore(&cache_block->bcb_spinlock,
					       cache_flags);

			random_value =
				__cache_block_pseudo_random(random_value);
			random_cache_block_id =
			    (random_value %
			     atomic_read(&bc->bc_total_entries)) + 1;
			ASSERT(random_cache_block_id > 0);
			ASSERT(random_cache_block_id <=
			       atomic_read(&bc->bc_total_entries));
		}

		/*
		 * note we are no longer holding any cache_block spinlock,
		 * only the global spinlock
		 */
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
			 "find-random, random_value=%u, random_cache_block_id=%d: no replacement cache block found",
			 random_value,
			 random_cache_block_id);
		cache_block = NULL;
		goto replacement_cache_block_not_found;
	}

	/*
	 * handle LRU and FIFO replacement mode
	 */
	if (replacement_mode == CACHE_REPLACEMENT_MODE_FIFO
	    || replacement_mode == CACHE_REPLACEMENT_MODE_LRU) {
		ASSERT(cache_block == NULL);

		if (list_non_empty(&bc->bc_valid_entries_list))
			cache_block =
			    list_first_entry(&bc->bc_valid_entries_list,
					     struct cache_block,
					     bcb_entry);
		/*
		 * this should almost never happen, as we only get called when
		 * we are below the threshold for invalid (free) blocks
		 */
		if (cache_block == NULL)
			goto replacement_cache_block_not_found;
		/*
		 * cache miss, get the first block in the list
		 */
		spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);
		ASSERT(cache_block != NULL);
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
			 "found-first-cache-entry (%s)",
			 cache_replacement_mode_to_str(replacement_mode));
		ASSERT_CACHE_BLOCK(cache_block, bc);
		block_hold_ret = cache_block_hold(bc, cache_block);
		if (block_hold_ret == 1
		    && cache_block->bcb_state == S_CLEAN) {
			/*
			 * found suitable cache block.
			 * note we need to keep the cache_block spinlock held
			 * as we exit from here.
			 */
			atomic_inc(&bc->bc_invalidations);
			atomic_inc(&bc->bc_idle_invalidations);
			goto replacement_cache_block_found;
		}
		cache_block_release(bc, cache_block);
		spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);

		/*
		 * blocks are busy, replacement not found
		 */
		cache_block = NULL;
		goto replacement_cache_block_not_found;
	}

 replacement_cache_block_not_found:

	/*
	 * we didn't find a suitable block.
	 * try list of clean blocks as a last resort.
	 */
	ASSERT(cache_block == NULL);

	if (list_non_empty(&bc->bc_valid_entries_clean_list))
		cache_block =
		    list_first_entry(&bc->bc_valid_entries_clean_list,
				     struct cache_block,
				     bcb_entry_cleandirty);
	if (cache_block == NULL) {
		/*
		 * cache_block not found
		 */
		spin_unlock_irqrestore(&bc->bc_entries_lock, flags);
		ASSERT(cache_block == NULL);
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
			 "cache-blocks-not-found (%s)",
			 cache_replacement_mode_to_str(replacement_mode));
		atomic_inc(&bc->bc_no_invalidations_all_blocks_busy);
		ASSERT_BITTERN_CACHE(bc);
		return CACHE_GET_RET_MISS;
	}

	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);
	ASSERT(cache_block != NULL);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	block_hold_ret = cache_block_hold(bc, cache_block);
	if (block_hold_ret == 1
	    && cache_block->bcb_state == S_CLEAN) {
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
			 "found-cache-block-clean-list (%s)",
			 cache_replacement_mode_to_str(replacement_mode));
		/*
		 * found suitable cache block.
		 * note we need to keep the cache_block spinlock held as
		 * we exit from here.
		 */
		atomic_inc(&bc->bc_invalidations);
		atomic_inc(&bc->bc_busy_invalidations);
		goto replacement_cache_block_found;
	}

	/*
	 * block is busy
	 */
	cache_block_release(bc, cache_block);

	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		 "cache-block-busy (%s)",
		 cache_replacement_mode_to_str(replacement_mode));
	atomic_inc(&bc->bc_no_invalidations_all_blocks_busy);
	ASSERT_BITTERN_CACHE(bc);

	return CACHE_GET_RET_MISS;

 replacement_cache_block_found:
	/*
	 * cache_block valid and held.
	 */
	ASSERT(cache_block != NULL);
	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		 "found-cache-block (%s)",
		 cache_replacement_mode_to_str(replacement_mode));

	ASSERT(cache_block->bcb_state == S_CLEAN);
	ASSERT(atomic_read(&cache_block->bcb_refcount) > 0);

	/*
	 * check hash
	 */
	cache_track_hash_check(bc, cache_block, cache_block->bcb_hash_data);

	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	*o_cache_block = cache_block;
	BT_TRACE(BT_LEVEL_TRACE4, bc, NULL, NULL, NULL, NULL, "ret-hit-idle");

	return CACHE_GET_RET_HIT_IDLE;
}

int cache_get_invalid_block_locked(struct bittern_cache *bc,
				   sector_t cache_block_sector,
				   int cleandirty_iflag,
				   struct cache_block **o_cache_block)
{
	unsigned long cache_flags;
	struct cache_block *cache_block = NULL;
	int replacement_mode;

	ASSERT_BITTERN_CACHE(bc);
	ASSERT(cleandirty_iflag == CACHE_FL_CLEAN
	       || cleandirty_iflag == CACHE_FL_DIRTY);
	ASSERT(o_cache_block != NULL);
	*o_cache_block = NULL;

	if (atomic_read(&bc->bc_invalid_entries) <
	    bc->bc_invalidator_conf_min_invalid_count) {
		BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL,
			 "get-invalid-block-wakeup-invalidator");
		wake_up_interruptible(&bc->bc_invalidator_wait);
	}

	ASSERT(cache_block_sector ==
	       sector_to_cache_block_sector(cache_block_sector));
	replacement_mode = bc->bc_replacement_mode;

	if (list_non_empty(&bc->bc_invalid_entries_list)) {
		int block_hold_ret;

		cache_block =
		    list_first_entry(&bc->bc_invalid_entries_list,
				     struct cache_block, bcb_entry);
		ASSERT(cache_block != NULL);
		spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);
		BT_TRACE(BT_LEVEL_TRACE4, bc, NULL, cache_block, NULL, NULL,
			 "get-invalid-block");
		ASSERT_CACHE_BLOCK(cache_block, bc);

		block_hold_ret = cache_block_hold(bc, cache_block);
		if (block_hold_ret == 1) {
			ASSERT(cache_block->bcb_cache_transition ==
			       TS_NONE);
			ASSERT(cache_block->bcb_state == S_INVALID);
			ASSERT(is_sector_number_invalid
			       (cache_block->bcb_sector));
		} else {
			/*
			 * block is busy
			 */
			cache_block_release(bc, cache_block);
			spin_unlock_irqrestore(&cache_block->bcb_spinlock,
					       cache_flags);
			atomic_inc(&bc->bc_invalid_blocks_busy);
			goto cache_miss_all_invalid_blocks_busy;
		}

		/*
		 * found suitable cache block.
		 */
		ASSERT(cache_block->bcb_cache_transition ==
		       TS_NONE);
		ASSERT(cache_block->bcb_state == S_INVALID);
		ASSERT(atomic_read(&cache_block->bcb_refcount) > 0);
		ASSERT(is_sector_number_invalid(cache_block->bcb_sector));

		BT_TRACE(BT_LEVEL_TRACE4, bc, NULL, cache_block, NULL, NULL,
			 "get-invalid-block-found");

		/*
		 * make valid no data
		 */
		cache_block->bcb_sector = cache_block_sector;
		if (cleandirty_iflag == CACHE_FL_CLEAN)
			cache_block->bcb_state = S_CLEAN_NO_DATA;
		else {
			ASSERT(cleandirty_iflag == CACHE_FL_DIRTY);
			cache_block->bcb_state = S_DIRTY_NO_DATA;
		}
		ASSERT(is_sector_number_valid(cache_block->bcb_sector));

		/* insert into red-black tree */
		RB_CLEAR_NODE(&cache_block->bcb_rb_node);
		cache_rb_insert(bc, cache_block);

		/*
		 * INVALID -> VALID
		 */
		atomic_inc(&bc->bc_valid_entries);
		if (cleandirty_iflag == CACHE_FL_CLEAN)
			atomic_inc(&bc->bc_valid_entries_clean);
		else {
			ASSERT(cleandirty_iflag == CACHE_FL_DIRTY);
			atomic_inc(&bc->bc_valid_entries_dirty);
		}
		atomic_dec(&bc->bc_invalid_entries);

		M_ASSERT(atomic_read(&bc->bc_invalid_entries) >= 0);
		M_ASSERT(atomic_read(&bc->bc_valid_entries_dirty) <=
			 atomic_read(&bc->bc_total_entries));
		M_ASSERT(atomic_read(&bc->bc_valid_entries_clean) <=
			 atomic_read(&bc->bc_total_entries));
		M_ASSERT(atomic_read(&bc->bc_valid_entries) <=
			 atomic_read(&bc->bc_total_entries));
		M_ASSERT(atomic_read(&bc->bc_invalid_entries) <=
			 atomic_read(&bc->bc_total_entries));

		/* LRU, FIFO, RANDOM */
		list_del_init(&cache_block->bcb_entry);
		list_add_tail(&cache_block->bcb_entry,
			      &bc->bc_valid_entries_list);
		/* all replacement modes */
		list_del_init(&cache_block->bcb_entry_cleandirty);
		if (cache_block->bcb_state == S_CLEAN_NO_DATA) {
			list_add_tail(&cache_block->bcb_entry_cleandirty,
				      &bc->bc_valid_entries_clean_list);
		} else {
			ASSERT(cache_block->bcb_state ==
			       S_DIRTY_NO_DATA);
			list_add_tail(&cache_block->bcb_entry_cleandirty,
				      &bc->bc_valid_entries_dirty_list);
		}

		cache_block->bcb_last_modify = jiffies_to_secs(jiffies);

		ASSERT_CACHE_BLOCK(cache_block, bc);
		ASSERT_BITTERN_CACHE(bc);

		spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);

		*o_cache_block = cache_block;
		BT_TRACE(BT_LEVEL_TRACE4, bc, NULL, *o_cache_block, NULL, NULL,
			 "get-invalid-block-done-ok");

		return CACHE_GET_RET_MISS_INVALID_IDLE;
	}

 cache_miss_all_invalid_blocks_busy:

	BT_TRACE(BT_LEVEL_TRACE4, bc, NULL, NULL, NULL, NULL,
		 "get-invalid-block-no-invalid-blocks");
	ASSERT_BITTERN_CACHE(bc);
	*o_cache_block = NULL;
	return CACHE_GET_RET_MISS;
}

int cache_get_invalid_block(struct bittern_cache *bc,
			    sector_t cache_block_sector,
			    int cleandirty_iflag,
			    struct cache_block **o_cache_block)
{
	int ret;
	unsigned long flags;

	ASSERT_BITTERN_CACHE(bc);
	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	ret = cache_get_invalid_block_locked(bc,
					     cache_block_sector,
					     cleandirty_iflag,
					     o_cache_block);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);
	return ret;
}

/*
 * if block is found, refcount is incremented and cache block is returned.
 * the caller who gets a block with a refcount == 1 becomes the owner and is
 * the only one allowed to change it.
 * other callers need to release the block, or leave it unchanged.
 * when caller is done, it needs to call cache_put to decrement use count.
 */
enum cache_get_ret cache_get(struct bittern_cache *bc,
			     sector_t cache_block_sector,
			     const int iflags,
			     struct cache_block **o_cache_block)
{
	struct cache_block *cache_block = NULL;
	unsigned long flags;
	unsigned long cache_flags = 0UL; /* shut up compiler */
	int replacement_mode;
	int cleandirty_iflag = iflags & CACHE_FL_CLEANDIRTY_MASK;
	int ret;

	/*
	 * we need to get a local copy of replacement_mode, as its value is
	 * tunable, and can change while we execute this code.
	 * it does not matter if it gets changed, but for this code to work,
	 * the value we use must stay constant
	 */
	ASSERT(o_cache_block != NULL);
	ASSERT_BITTERN_CACHE(bc);

	cache_block_sector = sector_to_cache_block_sector(cache_block_sector);

	replacement_mode = bc->bc_replacement_mode;
	ASSERT_CACHE_REPLACEMENT_MODE(replacement_mode);
	BT_TRACE(BT_LEVEL_TRACE3, bc, NULL, NULL, NULL, NULL,
		 "enter iflags=0x%x (%s %s %s %s %s)", iflags,
		 ((iflags & CACHE_FL_HIT) ? "hit" : ""),
		 ((iflags & CACHE_FL_MISS) ? "miss" : ""),
		 ((iflags & CACHE_FL_CLEAN) ? "clean" : ""),
		 ((iflags & CACHE_FL_DIRTY) ? "dirty" : ""),
		 cache_replacement_mode_to_str(replacement_mode));

	ASSERT((iflags & ~CACHE_FL_MASK) == 0);

	/* with the current code, the caller always needs to want a hit */
	ASSERT((iflags & CACHE_FL_HIT) != 0);

	if ((iflags & CACHE_FL_MISS) != 0) {
		/* either CLEAN or DIRTY flag is required */
		ASSERT(cleandirty_iflag == CACHE_FL_CLEAN
		       || cleandirty_iflag == CACHE_FL_DIRTY);
	} else {
		ASSERT(cleandirty_iflag == 0);
	}

	*o_cache_block = NULL;

	/*
	 * this whole function must hold the global lock thru out in order to
	 * guarantee atomicity
	 */
	spin_lock_irqsave(&bc->bc_entries_lock, flags);

	/*
	 * first do a red-black tree lookup among valid (clean/dirty) blocks
	 */
	cache_block = cache_rb_lookup(bc, cache_block_sector);
	BT_TRACE(BT_LEVEL_TRACE4, bc, NULL, cache_block, NULL, NULL,
		 "lookup red-black tree, cache_block_sector=%lu, cache_block=%p,",
		 cache_block_sector, cache_block);

	ASSERT_BITTERN_CACHE(bc);

	if (cache_block != NULL) {
		int block_hold_ret;

		spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);

		/*
		 * handle cache lookup hit
		 */
		ASSERT_CACHE_BLOCK(cache_block, bc);
		ASSERT_BITTERN_CACHE(bc);
		ASSERT(cache_block->bcb_state != S_INVALID);
		ASSERT(cache_block_sector == cache_block->bcb_sector);

		block_hold_ret = cache_block_hold(bc, cache_block);
		if (block_hold_ret == 1) {
			/*
			 * The following two asserts holds true if we own
			 * the block, the opposite does not hold true though.
			 */
			ASSERT(cache_block->bcb_cache_transition ==
			       TS_NONE);
			/*
			 * this holds true if we own the block --
			 * the opposite does not hold true
			 */
			ASSERT(cache_block->bcb_state == S_CLEAN ||
			       cache_block->bcb_state == S_DIRTY);
			/*
			 * check hash
			 */
			cache_track_hash_check(bc,
					       cache_block,
					       cache_block->bcb_hash_data);
		} else {
			BT_TRACE(BT_LEVEL_TRACE4, bc, NULL, cache_block, NULL,
				 NULL, "get-block-cache-hit-busy: %s",
				 cache_get_ret_to_str
				 (CACHE_GET_RET_HIT_BUSY));
			cache_block_release(bc, cache_block);
			spin_unlock_irqrestore(&cache_block->bcb_spinlock,
					       cache_flags);
			spin_unlock_irqrestore(&bc->bc_entries_lock, flags);
			*o_cache_block = NULL;
			return CACHE_GET_RET_HIT_BUSY;
		}

		ASSERT(atomic_read(&cache_block->bcb_refcount) > 0);
		ASSERT(is_sector_number_valid(cache_block->bcb_sector));

		BT_TRACE(BT_LEVEL_TRACE4, bc, NULL, cache_block, NULL, NULL,
			 "get-block-cache-hit: %s",
			 cache_get_ret_to_str
			 (CACHE_GET_RET_HIT_IDLE));

		if (replacement_mode == CACHE_REPLACEMENT_MODE_LRU) {
			/* LRU */
			/*
			 * push element to the end of the invalid/valid list
			 * -- this implements a simple and dumb LRU scheme
			 */
			list_del_init(&cache_block->bcb_entry);
			list_add_tail(&cache_block->bcb_entry,
				      &bc->bc_valid_entries_list);
		}

		/*
		 * push element to the end of the clean/dirty list for all
		 * replacements modes
		 */
		list_del_init(&cache_block->bcb_entry_cleandirty);
		if (cache_block->bcb_state == S_CLEAN) {
			list_add_tail(&cache_block->bcb_entry_cleandirty,
				      &bc->bc_valid_entries_clean_list);
		} else {
			ASSERT(cache_block->bcb_state == S_DIRTY);
			list_add_tail(&cache_block->bcb_entry_cleandirty,
				      &bc->bc_valid_entries_dirty_list);
		}

		ASSERT_CACHE_BLOCK(cache_block, bc);
		ASSERT_BITTERN_CACHE(bc);

		spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
		spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

		*o_cache_block = cache_block;
		return CACHE_GET_RET_HIT_IDLE;
	}

	/*
	 * cache lookup miss, part #1.
	 * if we want hits only, bail out.
	 */

	BT_TRACE(BT_LEVEL_TRACE4, bc, NULL, NULL, NULL, NULL,
		 "get-block-cache-miss");
	if ((iflags & CACHE_FL_MISS) == 0) {
		BT_TRACE(BT_LEVEL_TRACE4, bc, NULL, NULL, NULL, NULL,
			 "caller only wants hits, return MISS");
		*o_cache_block = NULL;

		spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

		return CACHE_GET_RET_MISS;
	}

	ASSERT_BITTERN_CACHE(bc);
	ASSERT(cache_block == NULL);
	ASSERT_CACHE_REPLACEMENT_MODE(replacement_mode);

	/*
	 * cache lookup miss, part #2.
	 * we want to reallocate an invalid block.
	 */

	cache_block = NULL;
	ret = cache_get_invalid_block_locked(bc,
					     cache_block_sector,
					     cleandirty_iflag,
					     &cache_block);
	ASSERT(ret == CACHE_GET_RET_MISS_INVALID_IDLE ||
	       ret == CACHE_GET_RET_MISS);

	if (ret == CACHE_GET_RET_MISS_INVALID_IDLE) {
		atomic_inc(&bc->bc_invalidations_map);
		spin_unlock_irqrestore(&bc->bc_entries_lock, flags);
		/* wake up bgwriter task */
		wake_up_interruptible(&bc->bc_bgwriter_wait);
		/* found block */
		ASSERT(cache_block != NULL);
		*o_cache_block = cache_block;
		return ret;
	}

	ASSERT(cache_block == NULL);

	/*
	 * cache miss.
	 * no available invalid blocks.
	 */

	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	BT_TRACE(BT_LEVEL_TRACE4, bc, NULL, NULL, NULL, NULL,
		 "get-block-cache-miss-all-blocks-busy");
	/* wake up bgwriter task */
	wake_up_interruptible(&bc->bc_bgwriter_wait);
	ASSERT_BITTERN_CACHE(bc);
	*o_cache_block = NULL;
	return CACHE_GET_RET_MISS;
}

/*
 * get a dirty block from the head of the dirty list.
 * if block_age is 0 it will always return a dirty block (if it exists),
 * otherwise it will only return it if the block is at least as old as
 * "block_age".
 * return values are:
 * 0 for success
 * -EBUSY for block busy
 * -ETIMER for block too young
 * -EAGAIN for dirty list empty
 */
int cache_get_dirty_from_head(struct bittern_cache *bc,
			      struct cache_block  **o_cache_block,
			      int requested_block_age)
{
	unsigned int block_age_secs;
	unsigned long flags, cache_flags;
	struct cache_block *cache_block = NULL;
	int block_hold_ret;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT(o_cache_block != NULL);
	*o_cache_block = NULL;
	/*
	 * block age is either -1 or a valid block age
	 */
	ASSERT(requested_block_age >= 0);

	spin_lock_irqsave(&bc->bc_entries_lock, flags);

	if (list_non_empty(&bc->bc_valid_entries_dirty_list)) {
		cache_block = list_first_entry(&bc->bc_valid_entries_dirty_list,
					       struct cache_block,
					       bcb_entry_cleandirty);
		ASSERT(cache_block != NULL);
		ASSERT_CACHE_BLOCK(cache_block, bc);
	} else {
		ASSERT(cache_block == NULL);
		spin_unlock_irqrestore(&bc->bc_entries_lock, flags);
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
			 "dirty list is empty");
		return -EAGAIN;
	}

	ASSERT(cache_block != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);

	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);
	block_hold_ret = cache_block_hold(bc, cache_block);
	if (block_hold_ret == 1 &&
	    cache_block->bcb_state == S_DIRTY) {
		/*
		 * we now own the block
		 */
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
			 "holding cache_block");
	} else {
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block,
			 NULL, NULL, "dirty cache_block is busy");
		cache_block_release(bc, cache_block);
		spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
		spin_unlock_irqrestore(&bc->bc_entries_lock, flags);
		return -EBUSY;
	}

	block_age_secs =
		jiffies_to_secs(jiffies) - cache_block->bcb_last_modify;
	ASSERT(block_age_secs >= 0);
	if (block_age_secs < requested_block_age) {
		/*
		 * dirty block, but too young, nothing to do
		 */
		BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, cache_block, NULL, NULL,
			 "dirty block is too young (%u/%u)",
			 block_age_secs, bc->bc_bgwriter_curr_min_age_secs);
		cache_block_release(bc, cache_block);
		spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
		spin_unlock_irqrestore(&bc->bc_entries_lock, flags);
		return -ETIME;
	}

	ASSERT(cache_block->bcb_state == S_DIRTY);
	ASSERT(atomic_read(&cache_block->bcb_refcount) > 0);

	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	*o_cache_block = cache_block;
	return 0;
}

enum cache_get_ret cache_get_clone(struct bittern_cache *bc,
				   struct cache_block *original_cache_block,
				   struct cache_block **o_cache_block,
				   bool is_dirty)
{
	int ret;
	int cache_fl;

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, original_cache_block, NULL, NULL,
		 "enter, is_dirty=%d", is_dirty);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(original_cache_block, bc);
	ASSERT(o_cache_block != NULL);
	ASSERT(is_dirty == false || is_dirty == true);

	*o_cache_block = NULL;

	cache_fl = (is_dirty ? CACHE_FL_DIRTY : CACHE_FL_CLEAN);
	ret = cache_get_invalid_block(bc,
				      original_cache_block->bcb_sector,
				      cache_fl,
				      o_cache_block);
	ASSERT(ret == CACHE_GET_RET_MISS_INVALID_IDLE ||
	       ret == CACHE_GET_RET_MISS);

	if (ret == CACHE_GET_RET_MISS_INVALID_IDLE) {
		ASSERT(*o_cache_block != NULL);
		atomic_inc(&bc->bc_dirty_write_clone_alloc_ok);
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, *o_cache_block, NULL, NULL,
			 "get-block-hit");
	} else {
		ASSERT(*o_cache_block == NULL);
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
			 "get-block-fail");
		atomic_inc(&bc->bc_dirty_write_clone_alloc_fail);
	}

	return ret;
}

enum cache_get_ret cache_get_by_id(struct bittern_cache *bc,
				   int cache_block_id,
				   struct cache_block **o_cache_block)
{
	struct cache_block *cache_block;
	unsigned long flags, cache_flags;
	int block_hold_ret;

	ASSERT(bc != NULL);
	ASSERT(o_cache_block != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT(cache_block_id > 0);
	ASSERT(cache_block_id <= atomic_read(&bc->bc_total_entries));

	*o_cache_block = NULL;

	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	cache_block = &bc->bc_cache_blocks[cache_block_id - 1];
	ASSERT(cache_block->bcb_block_id == cache_block_id);
	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);

	ASSERT_CACHE_BLOCK(cache_block, bc);

	BT_TRACE(BT_LEVEL_TRACE4, bc, NULL, cache_block, NULL, NULL,
		 "block id #%d", cache_block_id);

	block_hold_ret = cache_block_hold(bc, cache_block);
	if (block_hold_ret == 1) {
		/*
		 * block is free
		 */
		if (cache_block->bcb_state == S_INVALID) {
			/*
			 * block is invalid
			 */
			cache_block_release(bc, cache_block);
			spin_unlock_irqrestore(&cache_block->bcb_spinlock,
					       cache_flags);
			spin_unlock_irqrestore(&bc->bc_entries_lock, flags);
			return CACHE_GET_RET_INVALID;
		}
		/*
		 * block is valid and free
		 */
		ASSERT(cache_block->bcb_cache_transition ==
		       TS_NONE);
		ASSERT(cache_block->bcb_state == S_CLEAN
		       || cache_block->bcb_state == S_DIRTY);
		ASSERT(atomic_read(&cache_block->bcb_refcount) > 0);
		/*
		 * check hash
		 */
		cache_track_hash_check(bc,
				       cache_block,
				       cache_block->bcb_hash_data);
		*o_cache_block = cache_block;
		spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
		spin_unlock_irqrestore(&bc->bc_entries_lock, flags);
		return CACHE_GET_RET_HIT_IDLE;
	}

	/*
	 * block is busy
	 */
	*o_cache_block = NULL;
	cache_block_release(bc, cache_block);
	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);
	return CACHE_GET_RET_HIT_BUSY;
}

/*
 * right now we have to way to check is is_owner value is actually valid.
 * once we put an i/o context we will be able to verify it.
 */
void __cache_put(struct bittern_cache *bc, struct cache_block *cache_block,
		 int is_owner, int update_age)
{
	unsigned long flags;

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	spin_lock_irqsave(&cache_block->bcb_spinlock, flags);
	BT_TRACE(BT_LEVEL_TRACE4, bc, NULL, cache_block, NULL, NULL,
		 "put-block");
	if (is_owner) {
		ASSERT(cache_block->bcb_cache_transition ==
		       TS_NONE);
		ASSERT(cache_block->bcb_state == S_INVALID ||
		       cache_block->bcb_state == S_CLEAN ||
		       cache_block->bcb_state == S_DIRTY);
		if (cache_block->bcb_state == S_CLEAN ||
		    cache_block->bcb_state == S_DIRTY) {
			/*
			 * check hash
			 */
			cache_track_hash_check(bc, cache_block,
						 cache_block->bcb_hash_data);
		}
	}
	if (update_age)
		cache_block->bcb_last_modify = jiffies_to_secs(jiffies);
	cache_block_release(bc, cache_block);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	spin_unlock_irqrestore(&cache_block->bcb_spinlock, flags);
}

void cache_move_to_invalid(struct bittern_cache *bc,
			   struct cache_block *cache_block, int is_dirty)
{
	unsigned long flags, cache_flags;

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(is_dirty == 0 || is_dirty == 1);
	ASSERT(cache_block_is_held(bc, cache_block));

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL, "enter");

	/*
	 * cache_block needs to be in a transitional state
	 */
	ASSERT(cache_block->bcb_state != S_DIRTY &&
	       cache_block->bcb_state != S_CLEAN &&
	       cache_block->bcb_state != S_INVALID);

	ASSERT_CACHE_BLOCK(cache_block, bc);

	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);

	/* remove from red-black tree */
	cache_rb_remove(bc, cache_block);

	/* remove from dirty list */
	list_del_init(&cache_block->bcb_entry_cleandirty);
	/* remove from valid list */
	list_del_init(&cache_block->bcb_entry);

	cache_block->bcb_hash_data = UINT128_ZERO;
	cache_block->bcb_sector = SECTOR_NUMBER_INVALID;

	cache_state_transition_final(bc,
				     cache_block,
				     TS_NONE,
				     S_INVALID);
	if (is_dirty)
		atomic_dec(&bc->bc_valid_entries_dirty);
	else
		atomic_dec(&bc->bc_valid_entries_clean);
	atomic_dec(&bc->bc_valid_entries);
	atomic_inc(&bc->bc_invalid_entries);

	M_ASSERT(atomic_read(&bc->bc_valid_entries_dirty) >= 0);
	M_ASSERT(atomic_read(&bc->bc_valid_entries_clean) >= 0);
	M_ASSERT(atomic_read(&bc->bc_valid_entries) >= 0);
	M_ASSERT(atomic_read(&bc->bc_valid_entries_dirty) <=
		 atomic_read(&bc->bc_total_entries));
	M_ASSERT(atomic_read(&bc->bc_valid_entries_clean) <=
		 atomic_read(&bc->bc_total_entries));
	M_ASSERT(atomic_read(&bc->bc_valid_entries) <=
		 atomic_read(&bc->bc_total_entries));
	M_ASSERT(atomic_read(&bc->bc_invalid_entries) <=
		 atomic_read(&bc->bc_total_entries));

	/* remove from valid list, add to invalid list */
	list_del_init(&cache_block->bcb_entry);
	list_add_tail(&cache_block->bcb_entry, &bc->bc_invalid_entries_list);

	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	cache_put(bc, cache_block, 1);

	/*
	 * we are not really completing a request here, but we are freeing up
	 * resources which may be needed by the deferred thread.
	 */
	wakeup_deferred(bc);

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL, "exit");
}

void cache_move_to_clean(struct bittern_cache *bc,
			 struct cache_block *cache_block)
{
	unsigned long flags, cache_flags;

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(cache_block_is_held(bc, cache_block));

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		 "move to clean");
	/*
	 * cache_block needs to be in a transitional state
	 */
	ASSERT(cache_block->bcb_state != S_DIRTY &&
	       cache_block->bcb_state != S_CLEAN &&
	       cache_block->bcb_state != S_INVALID);
	/*
	 * DIRTY STATE --> VALID_CLEAN
	 */
	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);
	/*
	 * block has already been removed in start_io
	 * see comment in start_io to understand why.
	 */
	cache_state_transition_final(bc,
				     cache_block,
				     TS_NONE,
				     S_CLEAN);
	/* move to clean list */
	list_del_init(&cache_block->bcb_entry_cleandirty);
	list_add_tail(&cache_block->bcb_entry_cleandirty,
		      &bc->bc_valid_entries_clean_list);
	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	atomic_dec(&bc->bc_valid_entries_dirty);
	atomic_inc(&bc->bc_valid_entries_clean);

	M_ASSERT(atomic_read(&bc->bc_valid_entries_dirty) >= 0);
	M_ASSERT(atomic_read(&bc->bc_valid_entries_dirty) <=
		 atomic_read(&bc->bc_total_entries));
	M_ASSERT(atomic_read(&bc->bc_valid_entries_clean) <=
		 atomic_read(&bc->bc_total_entries));
	M_ASSERT(atomic_read(&bc->bc_valid_entries) <=
		 atomic_read(&bc->bc_total_entries));
	M_ASSERT(atomic_read(&bc->bc_invalid_entries) <=
		 atomic_read(&bc->bc_total_entries));

	cache_put(bc, cache_block, 1);

	/*
	 * wakeup possible waiters
	 */
	wakeup_deferred(bc);
}
