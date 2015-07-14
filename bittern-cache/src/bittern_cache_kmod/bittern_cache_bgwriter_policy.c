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
 * PLEASE NOTE:
 *
 * only the standard policy is currently supported.
 * all other policies are experimental and they are not even used.
 *
 * experimental policies are in their own separate file.
 * this means that when they are moved here, there will be another review.
 * you do not need to review experimental policies at this point unless
 * you really want to.
 */

/*
 * from review feedback
 * --------------------
 *
 * All these functions need a TON of comments/documentation:
 * What is important to a maintainer or a policy writer is:
 * What are the inputs (implied through values in bc structure),
 * What are the outputs (again, implied in bc structure), and what are the
 *   valid values to set.
 * Also, if possible, Try and merge the similar functions and/or write it in a
 *   way that uses a table to set values.
 */

extern void cache_bgwriter_compute_policy_classic(struct bittern_cache *bc);
extern void cache_bgwriter_compute_policy_aggressive(struct bittern_cache *bc);
extern void cache_bgwriter_compute_policy_queue_depth(struct bittern_cache *bc);
extern void
cache_bgwriter_compute_policy_queue_depth_adaptive(struct bittern_cache *bc);

void cache_bgwriter_compute_policy_standard(struct bittern_cache *bc)
{
	int dirty_pct, dirty_pct_f100;
	unsigned int valid_entries_dirty, total_entries;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	valid_entries_dirty = atomic_read(&bc->bc_valid_entries_dirty);
	total_entries = atomic_read(&bc->bc_total_entries);
	ASSERT(valid_entries_dirty <= total_entries);
	dirty_pct = T_PCT(total_entries, valid_entries_dirty);
	dirty_pct_f100 = T_PCT_F100(total_entries, valid_entries_dirty);
	ASSERT(dirty_pct <= 100);

	bc->bc_bgwriter_curr_max_queue_depth =
	    A_PERCENT_OF_B(bc->bc_bgwriter_conf_max_queue_depth_pct,
			   bc->bc_max_pending_requests);
	bc->bc_bgwriter_curr_rate_per_sec = 0;
	bc->bc_bgwriter_curr_min_age_secs = 0;

	if (dirty_pct > 95) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(80, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 5)
			bc->bc_bgwriter_curr_queue_depth = 5;
	} else if (dirty_pct > 90) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(32, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 5)
			bc->bc_bgwriter_curr_queue_depth = 5;
	} else if (dirty_pct > 85) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(16, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 5)
			bc->bc_bgwriter_curr_queue_depth = 5;
	} else if (dirty_pct > 80) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(8, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 4)
			bc->bc_bgwriter_curr_queue_depth = 4;
	} else if (dirty_pct > 75) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(2, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 2)
			bc->bc_bgwriter_curr_queue_depth = 2;
	} else if (dirty_pct > 70) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(1, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 1)
			bc->bc_bgwriter_curr_queue_depth = 1;
	} else if (dirty_pct > 60) {
		bc->bc_bgwriter_curr_queue_depth = 2;
		bc->bc_bgwriter_curr_rate_per_sec = 300;
		bc->bc_bgwriter_curr_min_age_secs = 1;
	} else if (dirty_pct > 50) {
		bc->bc_bgwriter_curr_queue_depth = 2;
		bc->bc_bgwriter_curr_rate_per_sec = 200;
		bc->bc_bgwriter_curr_min_age_secs = 1;
	} else if (dirty_pct > 40) {
		bc->bc_bgwriter_curr_queue_depth = 2;
		bc->bc_bgwriter_curr_rate_per_sec = 100;
		bc->bc_bgwriter_curr_min_age_secs = 5;
	} else if (dirty_pct > 30) {
		bc->bc_bgwriter_curr_queue_depth = 2;
		bc->bc_bgwriter_curr_rate_per_sec = 50;
		bc->bc_bgwriter_curr_min_age_secs = 10;
	} else if (dirty_pct > 20) {
		bc->bc_bgwriter_curr_queue_depth = 1;
		bc->bc_bgwriter_curr_rate_per_sec = 50;
		bc->bc_bgwriter_curr_min_age_secs = 20;
	} else {
		bc->bc_bgwriter_curr_queue_depth = 1;
		bc->bc_bgwriter_curr_rate_per_sec = 30;
		bc->bc_bgwriter_curr_min_age_secs = 30;
	}

	/*
	 * account for greedyness
	 */
	bc->bc_bgwriter_curr_queue_depth += bc->bc_bgwriter_conf_greedyness;

	/*
	 * at least 1 request in queue
	 */
	if (bc->bc_bgwriter_curr_queue_depth < 1)
		bc->bc_bgwriter_curr_queue_depth = 1;
	if (bc->bc_bgwriter_curr_queue_depth >
	    bc->bc_bgwriter_curr_max_queue_depth)
		bc->bc_bgwriter_curr_queue_depth =
		    bc->bc_bgwriter_curr_max_queue_depth;

	/*
	 * if we switched back to writetrhu,
	 * override all parameters and flush out quickly
	 */
	if (is_cache_mode_writethru(bc)) {
		bc->bc_bgwriter_curr_max_queue_depth =
		    A_PERCENT_OF_B(bc->bc_bgwriter_conf_max_queue_depth_pct,
				   bc->bc_max_pending_requests);
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(CACHE_BGWRITER_DEFAULT_QUEUE_DEPTH_PCT,
				   bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth == 0)
			bc->bc_bgwriter_curr_queue_depth = 1;
		bc->bc_bgwriter_curr_rate_per_sec = 0;
		bc->bc_bgwriter_curr_min_age_secs = 0;
	}
}

struct cache_bgwriter_policy {
	const char *bgw_policy_name;
	void (*bgw_policy_function_slow)(struct bittern_cache *bc);
	void (*bgw_policy_function_fast)(struct bittern_cache *bc);
} cache_bgwriter_policies[] = {
	{
	"standard",
		    cache_bgwriter_compute_policy_standard, NULL,}, {
	"default", cache_bgwriter_compute_policy_standard, NULL,}, {
	"experimental/classic",
		    cache_bgwriter_compute_policy_classic,
		    NULL,}, {
	"experimental/aggressive",
		    cache_bgwriter_compute_policy_aggressive,
		    NULL,}, {
	"experimental/queue-depth-adaptive",
		    cache_bgwriter_compute_policy_queue_depth_adaptive,
		    cache_bgwriter_compute_policy_queue_depth_adaptive,}, {
	"experimental/queue-depth",
		    cache_bgwriter_compute_policy_queue_depth,
		    cache_bgwriter_compute_policy_queue_depth,},
};

#define CACHE_BGWRITER_POLICIES		\
		ARRAY_SIZE(cache_bgwriter_policies)

const char *cache_bgwriter_policy(struct bittern_cache *bc)
{
	int p = bc->bc_bgwriter_active_policy;
	struct cache_bgwriter_policy *bp;

	bp = &cache_bgwriter_policies[p];
	ASSERT(p >= 0 && p < CACHE_BGWRITER_POLICIES);
	ASSERT(bp->bgw_policy_name != NULL);
	ASSERT(bp->bgw_policy_function_slow != NULL);

	return bp->bgw_policy_name;
}

ssize_t cache_bgwriter_op_show_policy(struct bittern_cache *bc, char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;
	int p;

	DMEMIT("%s: bgwriter_policy: ", bc->bc_name);
	DMEMIT("bgwriter_policy=%s ", cache_bgwriter_policy(bc));
	for (p = 0; p < CACHE_BGWRITER_POLICIES; p++)
		DMEMIT("bgwriter_policy_%d=%s ",
		       p,
		       cache_bgwriter_policies[p].bgw_policy_name);
	DMEMIT("\n");

	return sz;
}

int cache_bgwriter_policy_set(struct bittern_cache *bc, const char *buf)
{
	int p;
	struct cache_bgwriter_policy *bp;

	for (p = 0; p < CACHE_BGWRITER_POLICIES; p++) {
		bp = &cache_bgwriter_policies[p];
		if (strncmp(bp->bgw_policy_name,
			    buf, strlen(bp->bgw_policy_name)) == 0) {
			printk_info("setting bgwriter policy to #%d:%s\n",
				    p, bp->bgw_policy_name);
			bc->bc_bgwriter_conf_policy = p;
			return 0;
		}
	}
	printk_err("unknown bgwriter policy\n");
	return -EINVAL;
}

void cache_bgwriter_policy_init(struct bittern_cache *bc)
{
	bc->bc_bgwriter_conf_policy = 0;
	bc->bc_bgwriter_active_policy = 0;
	cache_bgwriter_policy_set(bc, CACHE_BGWRITER_DEFAULT_POLICY);
}

void cache_bgwriter_update_policy(struct bittern_cache *bc)
{
	if (bc->bc_bgwriter_active_policy != bc->bc_bgwriter_conf_policy) {
		int i;

		printk_info("bgwriter: updating current policy %d to %d\n",
			    bc->bc_bgwriter_active_policy,
			    bc->bc_bgwriter_conf_policy);
		bc->bc_bgwriter_active_policy = bc->bc_bgwriter_conf_policy;
		for (i = 0; i < ARRAY_SIZE(bc->bc_bgwriter_curr_policy); i++)
			bc->bc_bgwriter_curr_policy[i] = 0UL;
	}
}

/*
 * this is called in the "slow" outer loop, so either after queueing
 * a certain number of requests or after a certain time is elapsed
 */
void cache_bgwriter_compute_policy_slow(struct bittern_cache *bc)
{
	int p;
	struct cache_bgwriter_policy *bp;

	cache_bgwriter_update_policy(bc);
	p = bc->bc_bgwriter_active_policy;
	bp = &cache_bgwriter_policies[p];
	ASSERT(p >= 0 && p < CACHE_BGWRITER_POLICIES);
	ASSERT(bp->bgw_policy_name != NULL);
	ASSERT(bp->bgw_policy_function_slow != NULL);

	(*bp->bgw_policy_function_slow)(bc);
}

/*
 * this is called in the "fast" inner loop after queueing each request
 * (or batch of requests).
 */
void cache_bgwriter_compute_policy_fast(struct bittern_cache *bc)
{
	int p;
	struct cache_bgwriter_policy *bp;

	cache_bgwriter_update_policy(bc);
	p = bc->bc_bgwriter_active_policy;
	bp = &cache_bgwriter_policies[p];
	ASSERT(p >= 0 && p < CACHE_BGWRITER_POLICIES);
	ASSERT(bp->bgw_policy_name != NULL);
	ASSERT(bp->bgw_policy_function_slow != NULL);

	if (bp->bgw_policy_function_fast != NULL)
		(*bp->bgw_policy_function_fast)(bc);
}
