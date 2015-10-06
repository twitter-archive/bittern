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
#include <linux/sort.h>

/*! \file */

/* \todo need to actually implement this function or compleletely remove it */
void cache_zero_stats(struct bittern_cache *bc)
{
	ASSERT(bc != NULL);
	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL, "enter");
	ASSERT_BITTERN_CACHE(bc);

	/* many stats cannot be invalidated */

	/*FIXME: need to actually implement this or removed */

	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL, "done");
}

/*
 * FIXME: should add start "offset", so we can slowly print all blocks
 */
void cache_dump_blocks_cache_state(struct bittern_cache *bc,
					   int cache_state,
					   char *cache_state_str,
					   unsigned int start_offset)
{
	unsigned long flags;
	unsigned int curr_offset = 0, dump_count = 0;
	int block_id;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	printk_debug("dump_%s_start[start_offset=%u]\n", cache_state_str,
		     start_offset);

	if (start_offset > atomic_read(&bc->bc_total_entries))
		start_offset = atomic_read(&bc->bc_total_entries);
	if (start_offset == 0)
		start_offset = 1;
	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	for (block_id = start_offset;
	     block_id <= atomic_read(&bc->bc_total_entries); block_id++) {
		struct cache_block *cache_block;
		unsigned long cache_flags;
		int do_print = 0;

		cache_block = &bc->bc_cache_blocks[block_id - 1];
		spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);
		ASSERT(cache_block->bcb_block_id == block_id);
		ASSERT_CACHE_BLOCK(cache_block, bc);

		/*
		 * -1 means we want to dump busy blocks
		 */
		if (cache_state == -1 &&
		    cache_block->bcb_state != S_INVALID &&
		    cache_block->bcb_state != S_CLEAN &&
		    cache_block->bcb_state != S_DIRTY)
			do_print = 1;
		else if (cache_state == cache_block->bcb_state)
			do_print = 1;

		/*
		 * use regular printk to avoid printing too much stuff
		 */
		if (do_print) {
			++dump_count;
			printk(KERN_DEBUG "%s[%u]: s=%lu, %d(%s)\n",
			       cache_state_str,
			       block_id,
			       cache_block->bcb_sector,
			       cache_block->bcb_state,
			       cache_state_to_str(cache_block->
							  bcb_state));
		}

		ASSERT_CACHE_BLOCK(cache_block, bc);
		spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);

		if (dump_count >= 10000)
			break;
	}
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);
	curr_offset = block_id;

	printk_debug("dump_%s_done[start_offset=%u, current_offset=%u, dump_count=%u]\n",
		     cache_state_str, start_offset, curr_offset, dump_count);
}

void cache_dump_deferred(struct bittern_cache *bc,
			 struct deferred_queue *queue,
			 const char *queue_name,
			 unsigned int start_offset)
{
	unsigned long flags;
	unsigned int curr_offset = 0, dump_count = 0;
	struct bio *bio;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	printk_debug("dump_%s_start[start_offset=%u]\n", queue_name,
		     start_offset);

	spin_lock_irqsave(&bc->defer_lock, flags);
	bio_list_for_each(bio, &queue->list) {
		if (curr_offset++ < start_offset)
			continue;
		printk(KERN_DEBUG
		       "%s[%u]: dir=%s, s=%lu %s%s%s\n",
		       queue_name,
		       curr_offset,
		       bio_data_dir(bio) == READ ? "read" : "write",
		       bio->bi_iter.bi_sector,
		       ((bio->bi_rw & REQ_FLUSH) != 0 ? "F" : ""),
		       ((bio->bi_rw & REQ_FUA) != 0 ? "U" : ""),
		       ((bio->bi_rw & REQ_DISCARD) != 0 ? "D" : ""));
		if (++dump_count >= 10000)
			break;
	}
	spin_unlock_irqrestore(&bc->defer_lock, flags);

	printk_debug("dump_%s_done[start_offset=%u, current_offset=%u, dump_count=%u]\n",
	     queue_name, start_offset, curr_offset, dump_count);
}

void cache_dump_pending(struct bittern_cache *bc,
			unsigned int start_offset)
{
	unsigned long flags;
	unsigned int curr_offset = 0, dump_count = 0;
	struct work_item *wi;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	printk_debug("dump_pending_start[start_offset=%u]\n", start_offset);

	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	list_for_each_entry(wi,
			    &bc->bc_pending_requests_list,
			    wi_pending_io_list) {
		struct cache_block *cache_block = wi->wi_cache_block;

		if (curr_offset++ < start_offset)
			continue;
		if (cache_block != NULL)
			printk(KERN_DEBUG
			       "%s[%u]: op=%s, dir=%s, s=%lu %c%c%c: %d:%lu: %d(%s)\n",
			       "pending/cache_block",
			       curr_offset,
			       wi->wi_op_type,
			       data_dir_read(wi->wi_op_rw) ? "read" : "write",
			       wi->wi_op_sector,
			       ((wi->wi_op_rw & REQ_FLUSH) != 0 ? 'F' : ' '),
			       ((wi->wi_op_rw & REQ_FUA) != 0 ? 'U' : ' '),
			       ((wi->wi_op_rw & REQ_DISCARD) != 0 ? 'D' : ' '),
			       cache_block->bcb_block_id,
			       cache_block->bcb_sector,
			       cache_block->bcb_state,
			       cache_state_to_str(cache_block->bcb_state));
		else
			printk(KERN_DEBUG
			       "%s[%u]: op=%s, dir=%s, s=%lu %c%c%c\n",
			       "pending",
			       curr_offset,
			       wi->wi_op_type,
			       data_dir_read(wi->wi_op_rw) ? "read" : "write",
			       wi->wi_op_sector,
			       ((wi->wi_op_rw & REQ_FLUSH) != 0 ? 'F' : ' '),
			       ((wi->wi_op_rw & REQ_FUA) != 0 ? 'U' : ' '),
			       ((wi->wi_op_rw & REQ_DISCARD) != 0 ? 'D' : ' '));
		if (++dump_count >= 10000)
			break;
	}
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	printk_debug("dump_pending_done[start_offset=%u, current_offset=%u, dump_count=%u]\n",
		     start_offset,
		     curr_offset,
		     dump_count);
}

/*! WARNING: spinlock is kept held while calling this function */
void __cache_dump_devio_pending(struct bittern_cache *bc,
				struct list_head *pending_list,
				const char *pending_list_name,
				int *curr_offset)
{
	struct work_item *wi;
	list_for_each_entry(wi, pending_list, devio_pending_list) {
		struct cache_block *cache_block = wi->wi_cache_block;

		if (cache_block != NULL)
			printk(KERN_DEBUG
			       "%s[%u]: op=%s, dir=%s, s=%lu, gennum=%llu: %d:%lu: %d(%s)\n",
			       pending_list_name,
			       *curr_offset,
			       wi->wi_op_type,
			       data_dir_read(wi->wi_op_rw) ? "read" : "write",
			       wi->wi_op_sector,
			       wi->devio_gennum,
			       cache_block->bcb_block_id,
			       cache_block->bcb_sector,
			       cache_block->bcb_state,
			       cache_state_to_str(cache_block->bcb_state));
		else
			printk(KERN_DEBUG
			       "%s[%u]: op=%s, dir=%s, s=%lu, gennum=%llu\n",
			       pending_list_name,
			       *curr_offset,
			       wi->wi_op_type,
			       data_dir_read(wi->wi_op_rw) ? "read" : "write",
			       wi->wi_op_sector,
			       wi->devio_gennum);
		(*curr_offset)++;
	}
}

void cache_dump_devio_pending(struct bittern_cache *bc,
			      unsigned int start_offset)
{
	unsigned long flags;
	int curr_offset = 0;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	printk_debug("dump_devio_pending_start[start_offset=%u]\n",
		     curr_offset);

	spin_lock_irqsave(&bc->devio.spinlock, flags);

	__cache_dump_devio_pending(bc,
				   &bc->devio.pending_list,
				   "devio_pending/write",
				   &curr_offset);
	__cache_dump_devio_pending(bc,
				   &bc->devio.flush_pending_list,
				   "devio_pending/flush",
				   &curr_offset);

	spin_unlock_irqrestore(&bc->devio.spinlock, flags);

	/* prints redundant stuff so that it can keep the same output format */
	printk_debug("dump_devio_pending_done[start_offset=%u, current_offset=%u, dump_count=%u]\n",
		     0,
		     curr_offset,
		     curr_offset);
}

int cache_dump_blocks(struct bittern_cache *bc,
		      const char *dump_op,
		      unsigned int dump_offset)
{
	ASSERT_BITTERN_CACHE(bc);
	ASSERT(dump_op != NULL);
	/*
	 * we accept both "dump_cmd offset" and "dump_cmd", so ignore
	 * return value from sscanf.
	 * if scanf succeeds, dump_offset will contain the value.
	 */
	if (strcmp(dump_op, "clean") == 0)
		cache_dump_blocks_cache_state(bc,
					      S_CLEAN,
					      "clean",
					      dump_offset);
	else if (strcmp(dump_op, "dirty") == 0)
		cache_dump_blocks_cache_state(bc,
					      S_DIRTY,
					      "dirty",
					      dump_offset);
	else if (strcmp(dump_op, "busy") == 0)
		cache_dump_blocks_cache_state(bc,
					      -1,
					      "busy",
					      dump_offset);
	else if (strcmp(dump_op, "pending") == 0)
		cache_dump_pending(bc, dump_offset);
	else if (strcmp(dump_op, "devio_pending") == 0)
		cache_dump_devio_pending(bc, dump_offset);
	else if (strcmp(dump_op, "deferred") == 0) {
		cache_dump_deferred(bc,
				    &bc->defer_busy,
				    "deferred_wait_busy",
				    dump_offset);
		cache_dump_deferred(bc,
				    &bc->defer_page,
				    "deferred_wait_page",
				    dump_offset);
	} else if (strcmp(dump_op, "deferred_wait_busy") == 0)
		cache_dump_deferred(bc,
				    &bc->defer_busy,
				    "deferred_wait_busy",
				    dump_offset);
	else if (strcmp(dump_op, "deferred_wait_page") == 0)
		cache_dump_deferred(bc,
				    &bc->defer_page,
				    "deferred_wait_page",
				    dump_offset);
	else
		return -EINVAL;
	return 0;
}

void cache_walk_redblack(struct bittern_cache *bc)
{
	unsigned int valid_clean_count = 0, valid_dirty_count =
	    0, valid_busy_count = 0;
	unsigned int valid_total = 0;
	unsigned long flags;
	struct cache_block *cache_block;

	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL, "enter");
	ASSERT_BITTERN_CACHE(bc);

	spin_lock_irqsave(&bc->bc_entries_lock, flags);

	for (cache_block = cache_rb_first(bc); cache_block != NULL;
	     cache_block = cache_rb_next(bc, cache_block)) {
		unsigned long cache_flags;

		spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);
		ASSERT_CACHE_BLOCK(cache_block, bc);
		BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, cache_block, NULL, NULL,
			 "cache_block walk");
		if (cache_block->bcb_state == S_CLEAN)
			valid_clean_count++;
		else if (cache_block->bcb_state == S_DIRTY)
			valid_dirty_count++;
		else
			valid_busy_count++;
		valid_total++;
		spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	}

	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL,
		 "valid=%u(%u+%u+%u)", valid_total, valid_clean_count,
		 valid_dirty_count, valid_busy_count);

	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL, "done");
}

#define __cache_walk_list(__count, __bc, __bc_entries, __bc_entries_list, __bcb_entry, __name) ({       \
	struct cache_block *cache_block;				      \
	unsigned long flags;						      \
									      \
	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL,		      \
			"walking list '%s' (bc_entries=%u)",		      \
			(__name), atomic_read(&(__bc)->__bc_entries));	      \
	ASSERT_BITTERN_CACHE(__bc);					      \
	(__count) = 0;							      \
	spin_lock_irqsave(&(__bc)->bc_entries_lock, flags);		      \
	list_for_each_entry(cache_block, &(__bc)->__bc_entries_list, __bcb_entry) {                             \
		unsigned long cache_flags;				      \
									      \
		if ((__count) >= 10000000) {				      \
			BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL, \
				 "too many entries, will not continue");      \
			break;						      \
		}							      \
		spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);   \
		ASSERT_CACHE_BLOCK(cache_block, (__bc));		      \
		(__count)++;						      \
		spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);				\
	}								      \
	spin_unlock_irqrestore(&(__bc)->bc_entries_lock, flags);	      \
	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL,		      \
		"walked list '%s' (bc_entries=%u): count=%d",		      \
		(__name), atomic_read(&(__bc)->__bc_entries), (__count));     \
	ASSERT_BITTERN_CACHE(__bc);					      \
})

void cache_walk(struct bittern_cache *bc)
{
	unsigned int valid_count, invalid_count;
	unsigned int valid_dirty_count, valid_clean_count;
	unsigned int total, valid;

	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL, "enter");
	ASSERT_BITTERN_CACHE(bc);

	__cache_walk_list(valid_count, bc, bc_valid_entries,
			  bc_valid_entries_list, bcb_entry,
			  "valid_entries");
	__cache_walk_list(invalid_count, bc, bc_invalid_entries,
			  bc_invalid_entries_list, bcb_entry,
			  "invalid_entries");
	__cache_walk_list(valid_clean_count, bc, bc_valid_entries_clean,
			  bc_valid_entries_clean_list,
			  bcb_entry_cleandirty, "valid_entries_clean");
	__cache_walk_list(valid_dirty_count, bc, bc_valid_entries_dirty,
			  bc_valid_entries_dirty_list,
			  bcb_entry_cleandirty, "valid_entries_dirty");

	total = valid_count + invalid_count;
	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL,
		 "total=%u(%u+%u) [%u]", total, valid_count, invalid_count,
		 atomic_read(&bc->bc_total_entries));
	valid = valid_dirty_count + valid_clean_count;
	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL,
		 "valid=%u(%u+%u) [%u]", valid, valid_dirty_count,
		 valid_clean_count, atomic_read(&bc->bc_valid_entries));

	cache_walk_redblack(bc);

	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL, "done");
}

void cache_invalidate_blocks(struct bittern_cache *bc)
{
	int invalidated_count = 0;
	int invalid_count = 0;
	int busy_count = 0;
	int block_id;

	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL, "enter");
	ASSERT_BITTERN_CACHE(bc);

	for (block_id = 1; block_id <= atomic_read(&bc->bc_total_entries);
	     block_id++) {
		struct cache_block *cache_block;
		int ret;

		ASSERT(bc != NULL);
		ASSERT_BITTERN_CACHE(bc);

		ret = cache_get_by_id(bc, block_id, &cache_block);
		BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL,
			 "invalidate block #%d: %d(%s)", block_id, ret,
			 cache_get_ret_to_str(ret));
		ASSERT_CACHE_GET_RET(ret);
		switch (ret) {
		case CACHE_GET_RET_HIT_IDLE:
			invalidated_count++;
			ASSERT(cache_block != NULL);
			ASSERT_CACHE_BLOCK(cache_block, bc);
			if (cache_block->bcb_state == S_CLEAN) {
				BT_TRACE(BT_LEVEL_TRACE1, bc, NULL,
					 cache_block, NULL, NULL,
					 "invalidating clean block id #%d",
					 cache_block->bcb_block_id);
				cache_invalidate_clean_block(bc,
							     cache_block);
			} else {
				BT_TRACE(BT_LEVEL_TRACE1, bc, NULL,
					 cache_block, NULL, NULL,
					 "block id #%d is dirty -- skipping",
					 cache_block->bcb_block_id);
				ASSERT(cache_block->bcb_state ==
				       S_DIRTY);
				cache_put(bc, cache_block, 1);
			}
			schedule();
			break;
		case CACHE_GET_RET_HIT_BUSY:
			ASSERT(cache_block == NULL);
			busy_count++;
			BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL,
				 NULL, "block #%d busy -- skipping",
				 block_id);
			schedule();
			break;
		case CACHE_GET_RET_INVALID:
			ASSERT(cache_block == NULL);
			invalid_count++;
			BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL,
				 NULL, "block #%d invalid -- skipping",
				 block_id);
			schedule();
			break;
		default:
			M_ASSERT("unexpected case value" == 0);
		}
	}

	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL,
		 "done, invalidated_count=%d, invalid_count=%d, busy_count=%d",
		 invalidated_count, invalid_count, busy_count);
}

#ifdef ENABLE_TRACK_CRC32C

static void __cache_track_hash_set(struct bittern_cache *bc,
				   struct cache_block *cache_block,
				   unsigned long sector,
				   uint128_t hash_value)
{
	unsigned long index;

	ASSERT(sector >= 0);
	index = sector / (unsigned long)SECTORS_PER_CACHE_BLOCK;
	ASSERT(uint128_eq(bc->bc_tracked_hashes[0], CACHE_TRACK_HASH_MAGIC0));
	ASSERT(uint128_eq(bc->bc_tracked_hashes[1], CACHE_TRACK_HASH_MAGIC1));
	ASSERT(uint128_eq(bc->bc_tracked_hashes[bc->bc_tracked_hashes_num + 2],
			  CACHE_TRACK_HASH_MAGICN));
	ASSERT(uint128_eq(bc->bc_tracked_hashes[bc->bc_tracked_hashes_num + 3],
			  CACHE_TRACK_HASH_MAGICN1));
	if (index < bc->bc_tracked_hashes_num) {
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
			 "set - sector=%lu, index=%lu, hash=" UINT128_FMT,
			 sector, index, UINT128_ARG(hash_value));
		bc->bc_tracked_hashes[index + 2] = hash_value;
	}
}

void cache_track_hash_clear(struct bittern_cache *bc, unsigned long sector)
{
	ASSERT(bc->bc_tracked_hashes != NULL);
	ASSERT(bc->bc_tracked_hashes_num > 0);
	__cache_track_hash_set(bc, NULL, sector, UINT128_ZERO);
	atomic_inc(&bc->bc_tracked_hashes_clear);
}

void cache_track_hash_set(struct bittern_cache *bc,
			  struct cache_block *cache_block,
			  uint128_t hash_value)
{
	ASSERT(bc->bc_tracked_hashes != NULL);
	ASSERT(bc->bc_tracked_hashes_num > 0);
	ASSERT(cache_block->bcb_sector >= 0);
	__cache_track_hash_set(bc,
			       cache_block,
			       cache_block->bcb_sector,
			       hash_value);
	atomic_inc(&bc->bc_tracked_hashes_set);
}

void cache_track_hash_check(struct bittern_cache *bc,
			    struct cache_block *cache_block,
			    uint128_t hash_value)
{
	unsigned long index;

	ASSERT(bc->bc_tracked_hashes != NULL);
	ASSERT(bc->bc_tracked_hashes_num > 0);
	ASSERT(cache_block->bcb_sector >= 0);
	ASSERT(uint128_eq(bc->bc_tracked_hashes[0], CACHE_TRACK_HASH_MAGIC0));
	ASSERT(uint128_eq(bc->bc_tracked_hashes[1], CACHE_TRACK_HASH_MAGIC1));
	ASSERT(uint128_eq(bc->bc_tracked_hashes[bc->bc_tracked_hashes_num + 2],
			  CACHE_TRACK_HASH_MAGICN));
	ASSERT(uint128_eq(bc->bc_tracked_hashes[bc->bc_tracked_hashes_num + 3],
			  CACHE_TRACK_HASH_MAGICN1));
	index = cache_block->bcb_sector / SECTORS_PER_CACHE_BLOCK;
	if (index >= bc->bc_tracked_hashes_num)
		return;
	if (uint128_z(bc->bc_tracked_hashes[index + 2])) {
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL,
			 NULL,
			 "check - sector=%lu, index=%lu, tracked_hash=zero, hash=" UINT128_FMT " [hash_null]",
			 cache_block->bcb_sector,
			 index,
			 UINT128_ARG(hash_value));
		atomic_inc(&bc->bc_tracked_hashes_null);
	} else if (uint128_eq(bc->bc_tracked_hashes[index + 2], hash_value)) {
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
			 "check - sector=%lu, index=%lu, tracked_hash=" UINT128_FMT ", hash=" UINT128_FMT " [hash_ok]",
			 cache_block->bcb_sector,
			 index,
			 UINT128_ARG(bc->bc_tracked_hashes[index + 2]),
			 UINT128_ARG(hash_value));
		atomic_inc(&bc->bc_tracked_hashes_ok);
	} else {
		BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, cache_block, NULL, NULL,
			 "check - sector=%lu, index=%lu, tracked_hash=" UINT128_FMT ", hash=" UINT128_FMT " [hash_bad]",
			 cache_block->bcb_sector,
			 index,
			 UINT128_ARG(bc->bc_tracked_hashes[index + 2]),
			 UINT128_ARG(hash_value));
		printk_err("check - sector=%lu, index=%lu, tracked_hashes=" UINT128_FMT ", hash=" UINT128_FMT " [hash_bad]\n",
			   cache_block->bcb_sector,
			   index,
			   UINT128_ARG(bc->bc_tracked_hashes[index + 2]),
			   UINT128_ARG(hash_value));
		M_ASSERT(uint128_eq(bc->bc_tracked_hashes[index + 2],
				    hash_value));
		atomic_inc(&bc->bc_tracked_hashes_bad);
	}
}

void cache_track_hash_check_buffer(struct bittern_cache *bc,
				   struct cache_block *cache_block,
				   void *buffer)
{
	cache_track_hash_check(bc,
			       cache_block,
			       murmurhash3_128(buffer, PAGE_SIZE));
}

#endif /* ENABLE_TRACK_CRC32C */
