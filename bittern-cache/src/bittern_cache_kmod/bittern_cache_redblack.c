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

/*
 * red-black tree operations
 *
 * these functions do not grab any spinlock, caller is responsible for that
 */

struct cache_block *cache_rb_lookup(struct bittern_cache *bc, sector_t sector)
{
	struct rb_node *n = bc->bc_rb_root.rb_node;
	int cc = 0;

	__ASSERT_BITTERN_CACHE(bc);

	while (n != NULL) {
		struct cache_block *cache_block;

		cache_block = rb_entry(n, struct cache_block, bcb_rb_node);
		__ASSERT_CACHE_BLOCK(cache_block, bc);
		ASSERT(RB_NON_EMPTY_NODE(&cache_block->bcb_rb_node));
		cc++;
		if (sector < cache_block->bcb_sector)
			n = n->rb_left;
		else if (sector > cache_block->bcb_sector)
			n = n->rb_right;
		else {
			bc->bc_rb_hit_loop_sum += cc;
			bc->bc_rb_hit_loop_count++;
			if (cc > bc->bc_rb_hit_loop_max)
				bc->bc_rb_hit_loop_max = cc++;
			return cache_block;
		}
	}

	bc->bc_rb_miss_loop_sum += cc;
	bc->bc_rb_miss_loop_count++;
	if (cc > bc->bc_rb_miss_loop_max)
		bc->bc_rb_miss_loop_max = cc++;
	return NULL;
}

void cache_rb_insert(struct bittern_cache *bc, struct cache_block *cache_block)
{
	struct rb_node **p = &bc->bc_rb_root.rb_node;
	struct rb_node *parent = NULL;

	__ASSERT_BITTERN_CACHE(bc);
	__ASSERT_CACHE_BLOCK(cache_block, bc);
	RB_CLEAR_NODE(&cache_block->bcb_rb_node);
	while ((*p) != NULL) {
		struct cache_block *tmp;

		parent = (*p);
		tmp = rb_entry(parent, struct cache_block, bcb_rb_node);
		__ASSERT_CACHE_BLOCK(cache_block, bc);
		__ASSERT_CACHE_BLOCK(tmp, bc);
		if (cache_block->bcb_sector < tmp->bcb_sector)
			p = &(*p)->rb_left;
		else if (cache_block->bcb_sector > tmp->bcb_sector)
			p = &(*p)->rb_right;
		else {
			/*
			 * Handle duplicate by arbitrarily choosing to go down
			 * the left subtree (we could do right tree either).
			 */
			p = &(*p)->rb_left;
		}
	}

	rb_link_node(&cache_block->bcb_rb_node, parent, p);
	rb_insert_color(&cache_block->bcb_rb_node, &bc->bc_rb_root);
	ASSERT(RB_NON_EMPTY_NODE(&cache_block->bcb_rb_node));
	ASSERT(RB_NON_EMPTY_ROOT(&bc->bc_rb_root));
}

void cache_rb_remove(struct bittern_cache *bc, struct cache_block *cache_block)
{
	__ASSERT_BITTERN_CACHE(bc);
	__ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(RB_NON_EMPTY_NODE(&cache_block->bcb_rb_node));
	ASSERT(RB_NON_EMPTY_ROOT(&bc->bc_rb_root));
	rb_erase(&cache_block->bcb_rb_node, &bc->bc_rb_root);
	RB_CLEAR_NODE(&cache_block->bcb_rb_node);
}

struct cache_block *cache_rb_first(struct bittern_cache *bc)
{
	struct rb_node *node;

	__ASSERT_BITTERN_CACHE(bc);
	node = rb_first(&bc->bc_rb_root);
	if (node != NULL) {
		struct cache_block *cache_block;

		cache_block = rb_entry(node, struct cache_block, bcb_rb_node);
		ASSERT(RB_NON_EMPTY_NODE(&cache_block->bcb_rb_node));
		ASSERT(RB_NON_EMPTY_ROOT(&bc->bc_rb_root));
		return cache_block;
	} else {
		ASSERT(RB_EMPTY_ROOT(&bc->bc_rb_root));
		return NULL;
	}
}

struct cache_block *cache_rb_next(struct bittern_cache *bc,
				  struct cache_block *cache_block)
{
	struct rb_node *node;

	__ASSERT_BITTERN_CACHE(bc);
	__ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(RB_NON_EMPTY_NODE(&cache_block->bcb_rb_node));
	node = rb_next(&cache_block->bcb_rb_node);
	if (node != NULL) {
		struct cache_block *next;

		next = rb_entry(node, struct cache_block, bcb_rb_node);
		ASSERT(RB_NON_EMPTY_NODE(&next->bcb_rb_node));
		ASSERT(RB_NON_EMPTY_ROOT(&bc->bc_rb_root));
		return next;
	} else {
		return NULL;
	}
}

struct cache_block *cache_rb_prev(struct bittern_cache *bc,
				  struct cache_block *cache_block)
{
	struct rb_node *node;

	__ASSERT_BITTERN_CACHE(bc);
	__ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(RB_NON_EMPTY_NODE(&cache_block->bcb_rb_node));
	node = rb_prev(&cache_block->bcb_rb_node);
	if (node != NULL) {
		struct cache_block *prev;

		prev = rb_entry(node, struct cache_block, bcb_rb_node);
		ASSERT(RB_NON_EMPTY_NODE(&prev->bcb_rb_node));
		ASSERT(RB_NON_EMPTY_ROOT(&bc->bc_rb_root));
		return prev;
	} else {
		return NULL;
	}
}

struct cache_block *cache_rb_last(struct bittern_cache *bc)
{
	struct rb_node *node;

	__ASSERT_BITTERN_CACHE(bc);
	node = rb_last(&bc->bc_rb_root);
	if (node != NULL) {
		struct cache_block *cache_block;

		cache_block = rb_entry(node, struct cache_block, bcb_rb_node);
		ASSERT(RB_NON_EMPTY_NODE(&cache_block->bcb_rb_node));
		ASSERT(RB_NON_EMPTY_ROOT(&bc->bc_rb_root));
		return cache_block;
	} else {
		ASSERT(RB_EMPTY_ROOT(&bc->bc_rb_root));
		return NULL;
	}
}
