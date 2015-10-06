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
 * IMPORTANT: this code is unnecessarily messy and will undergo a major cleanup
 * once we are past bittern release 1.0.
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

void cache_bgwriter_compute_policy_old_default(struct bittern_cache *bc)
{
	int dirty_pct;
	unsigned int valid_entries_dirty, total_entries;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	valid_entries_dirty = atomic_read(&bc->bc_valid_entries_dirty);
	total_entries = atomic_read(&bc->bc_total_entries);
	ASSERT(valid_entries_dirty <= total_entries);
	dirty_pct = T_PCT(total_entries, valid_entries_dirty);
	ASSERT(dirty_pct <= 100);

	bc->bc_bgwriter_curr_max_queue_depth =
			PERCENT_OF(bc->bc_bgwriter_conf_max_queue_depth_pct,
				   bc->bc_max_pending_requests);
	bc->bc_bgwriter_curr_rate_per_sec = 0;
	bc->bc_bgwriter_curr_min_age_secs = 0;

	if (dirty_pct > 95) {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(80, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 5)
			bc->bc_bgwriter_curr_queue_depth = 5;
	} else if (dirty_pct > 90) {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(32, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 5)
			bc->bc_bgwriter_curr_queue_depth = 5;
	} else if (dirty_pct > 85) {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(16, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 5)
			bc->bc_bgwriter_curr_queue_depth = 5;
	} else if (dirty_pct > 80) {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(8, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 4)
			bc->bc_bgwriter_curr_queue_depth = 4;
	} else if (dirty_pct > 75) {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(2, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 2)
			bc->bc_bgwriter_curr_queue_depth = 2;
	} else if (dirty_pct > 70) {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(1, bc->bc_bgwriter_curr_max_queue_depth);
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
}

void cache_bgwriter_compute_policy_classic(struct bittern_cache *bc)
{
	int dirty_pct;
	unsigned int valid_entries_dirty, total_entries;

	/* writeback queue_depth indexed by percent dirty */
	static int queue_depth[] = {
		2,	/* 0% */
		2,	/* 5% */
		2,	/* 10% */
		2,	/* 15% */
		2,	/* 20% */
		5,	/* 25% */
		5,	/* 30% */
		5,	/* 35% */
		5,	/* 40% */
		10,	/* 45% */
		15,	/* 50% */
		20,	/* 55% */
		25,	/* 60% */
		30,	/* 65% */
		35,	/* 70% */
		40,	/* 75% */
		45,	/* 80% */
		50,	/* 85% */
		75,	/* 90% */
		100,	/* 95% */
		150,	/* 100% */
                250,    /* 105% */
	};

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	valid_entries_dirty = atomic_read(&bc->bc_valid_entries_dirty);
	total_entries = atomic_read(&bc->bc_total_entries);
	ASSERT(valid_entries_dirty <= total_entries);
	dirty_pct = T_PCT(total_entries, valid_entries_dirty);
	ASSERT(dirty_pct <= 100);

	bc->bc_bgwriter_curr_max_queue_depth = (bc->bc_max_pending_requests * bc->bc_bgwriter_conf_max_queue_depth_pct) / 100;
	bc->bc_bgwriter_curr_rate_per_sec = 0;
	bc->bc_bgwriter_curr_min_age_secs = 0;
	bc->bc_bgwriter_curr_queue_depth = (queue_depth[dirty_pct / 5] * bc->bc_bgwriter_curr_max_queue_depth) / 100;
	bc->bc_bgwriter_curr_min_age_secs = 4;
}

void cache_bgwriter_compute_policy_dirty_ratio(struct bittern_cache *bc)
{
	int dirty_pct;
	unsigned int valid_entries_dirty, total_entries;

	/* writeback queue_depth indexed by percent dirty */
	static int queue_depth[] = {
		1,	/* 0% */
		1,	/* 5% */
		1,	/* 10% */
		1,	/* 15% */
		1,	/* 20% */
		1,	/* 25% */
		1,	/* 30% */
		1,	/* 35% */
		1,	/* 40% */
		1,	/* 45% */
		1,	/* 50% */
		1,	/* 55% */
		1,	/* 60% */
		1,	/* 65% */
		5,	/* 70% */
		10,	/* 75% */
		25,	/* 80% */
		50,	/* 85% */
		100,	/* 90% */
		150,	/* 95% */
		200,	/* 100% */
                300,    /* 105% */
	};

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	valid_entries_dirty = atomic_read(&bc->bc_valid_entries_dirty);
	total_entries = atomic_read(&bc->bc_total_entries);
	ASSERT(valid_entries_dirty <= total_entries);
	dirty_pct = T_PCT(total_entries, valid_entries_dirty);
	ASSERT(dirty_pct <= 100);

	bc->bc_bgwriter_curr_max_queue_depth = (bc->bc_max_pending_requests * bc->bc_bgwriter_conf_max_queue_depth_pct) / 100;
	bc->bc_bgwriter_curr_rate_per_sec = 0;
	bc->bc_bgwriter_curr_min_age_secs = 0;
	bc->bc_bgwriter_curr_queue_depth = (queue_depth[dirty_pct / 5] * bc->bc_bgwriter_curr_max_queue_depth) / 100;
	bc->bc_bgwriter_curr_min_age_secs = 4;
}

#define BITTERN_CACHE_ALLOW_EXPERIMENTAL_POLICIES
#ifdef BITTERN_CACHE_ALLOW_EXPERIMENTAL_POLICIES

void
cache_bgwriter_compute_policy_queue_depth_adaptive(struct bittern_cache *bc)
{
	int dirty_pct, dirty_pct_f100;
	unsigned int valid_entries_dirty, total_entries;
	int queue_depth_non_writeback;
	int quench = 0;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	valid_entries_dirty = atomic_read(&bc->bc_valid_entries_dirty);
	total_entries = atomic_read(&bc->bc_total_entries);
	ASSERT(valid_entries_dirty <= total_entries);
	dirty_pct = T_PCT(total_entries, valid_entries_dirty);
	dirty_pct_f100 = T_PCT_F100(total_entries, valid_entries_dirty);
	ASSERT(dirty_pct <= 100);

/* bc_bgwriter_curr_policy[8]; */
#define bc_bgwriter_curr_policy_count bc_bgwriter_curr_policy[0]
#define bc_bgwriter_curr_policy_quench_count bc_bgwriter_curr_policy[1]
#define bc_bgwriter_curr_policy_quench2_count bc_bgwriter_curr_policy[2]
#define bc_bgwriter_curr_policy_no_quench_count bc_bgwriter_curr_policy[3]
#define bc_bgwriter_curr_policy_queue_non_empty_count bc_bgwriter_curr_policy[4]
#define bc_bgwriter_curr_policy_queue_empty_count bc_bgwriter_curr_policy[5]
#define bc_bgwriter_curr_policy_no_sched_count bc_bgwriter_curr_policy[6]
#define bc_bgwriter_curr_policy_no_sched_until bc_bgwriter_curr_policy[7]

	bc->bc_bgwriter_curr_policy_count++;

	/*
	 * because we compute this based on the value of two independently
	 * updated atomic variables, we expect that sometimes the value will
	 * drop below zero.
	 */
	queue_depth_non_writeback =
	    atomic_read(&bc->bc_pending_requests) -
	    atomic_read(&bc->bc_pending_writeback_requests);
	queue_depth_non_writeback += atomic_read(&bc->bc_deferred_requests);
	if (queue_depth_non_writeback < 0)
		queue_depth_non_writeback = 0;

	bc->bc_bgwriter_curr_max_queue_depth =
	    PERCENT_OF(bc->bc_bgwriter_conf_max_queue_depth_pct,
			   bc->bc_max_pending_requests);
	bc->bc_bgwriter_curr_rate_per_sec = 0;
	bc->bc_bgwriter_curr_min_age_secs = 0;

	if (dirty_pct >= 95) {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(70, bc->bc_bgwriter_curr_max_queue_depth);
	} else if (dirty_pct >= 90) {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(60, bc->bc_bgwriter_curr_max_queue_depth);
	} else if (dirty_pct >= 85) {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(50, bc->bc_bgwriter_curr_max_queue_depth);
	} else if (dirty_pct >= 80) {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(40, bc->bc_bgwriter_curr_max_queue_depth);
	} else if (dirty_pct >= 75) {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(30, bc->bc_bgwriter_curr_max_queue_depth);
		bc->bc_bgwriter_curr_min_age_secs = 1;
	} else if (dirty_pct >= 70) {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(20, bc->bc_bgwriter_curr_max_queue_depth);
		bc->bc_bgwriter_curr_min_age_secs = 1;
	} else if (dirty_pct >= 60) {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(20, bc->bc_bgwriter_curr_max_queue_depth);
		bc->bc_bgwriter_curr_min_age_secs = 5;
	} else if (dirty_pct >= 50) {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(20, bc->bc_bgwriter_curr_max_queue_depth);
		bc->bc_bgwriter_curr_min_age_secs = 10;
	} else if (dirty_pct >= 40) {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(20, bc->bc_bgwriter_curr_max_queue_depth);
		bc->bc_bgwriter_curr_min_age_secs = 10;
	} else if (dirty_pct >= 30) {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(20, bc->bc_bgwriter_curr_max_queue_depth);
		bc->bc_bgwriter_curr_min_age_secs = 10;
	} else if (dirty_pct >= 20) {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(20, bc->bc_bgwriter_curr_max_queue_depth);
		bc->bc_bgwriter_curr_min_age_secs = 20;
	} else {
		bc->bc_bgwriter_curr_queue_depth =
		    PERCENT_OF(20, bc->bc_bgwriter_curr_max_queue_depth);
		bc->bc_bgwriter_curr_min_age_secs = 30;
	}

	if (bc->bc_bgwriter_curr_policy_no_sched_until != 0) {
		if (jiffies_to_msecs(jiffies) >
		    bc->bc_bgwriter_curr_policy_no_sched_until)
			bc->bc_bgwriter_curr_policy_no_sched_until = 0;
	}
	if (bc->bc_bgwriter_curr_policy_no_sched_until != 0) {
		quench = 1;
		bc->bc_bgwriter_curr_policy_no_sched_count++;
	}
	if (queue_depth_non_writeback > 0) {
		quench = 1;
		bc->bc_bgwriter_curr_policy_queue_non_empty_count++;
		/*
		 * if queue is non empty, keep whatever decision we make for 5
		 * msecs.
		 */
		bc->bc_bgwriter_curr_policy_no_sched_until =
		    jiffies_to_msecs(jiffies) + 5;
	} else {
		bc->bc_bgwriter_curr_policy_queue_empty_count++;
	}
	if (atomic_read(&bc->bc_deferred_requests) > 0)
		quench++;
	switch (quench) {
	case 0:
	default:
		bc->bc_bgwriter_curr_policy_no_quench_count++;
		break;
	case 1:
		bc->bc_bgwriter_curr_policy_quench_count++;
		if (dirty_pct >= 80) {
			/*
			 * above 80 we no longer quench
			 */
			;
		} else if (dirty_pct >= 70) {
			bc->bc_bgwriter_curr_queue_depth -=
			    bc->bc_bgwriter_curr_queue_depth / 3;
			bc->bc_bgwriter_curr_policy_quench_count++;
		} else if (dirty_pct >= 60) {
			bc->bc_bgwriter_curr_queue_depth -=
			    bc->bc_bgwriter_curr_queue_depth / 2;
			bc->bc_bgwriter_curr_policy_quench_count++;
		} else if (dirty_pct >= 50) {
			bc->bc_bgwriter_curr_queue_depth -=
			    bc->bc_bgwriter_curr_queue_depth / 2;
			bc->bc_bgwriter_curr_policy_quench_count++;
		} else if (dirty_pct >= 20) {
			bc->bc_bgwriter_curr_queue_depth = 1;
			bc->bc_bgwriter_curr_policy_quench_count++;
		} else {
			bc->bc_bgwriter_curr_queue_depth = 1;
			bc->bc_bgwriter_curr_policy_quench_count++;
		}
		break;
	case 2:
		bc->bc_bgwriter_curr_policy_quench2_count++;
		if (dirty_pct >= 80) {
			bc->bc_bgwriter_curr_queue_depth -=
			    bc->bc_bgwriter_curr_queue_depth / 3;
			bc->bc_bgwriter_curr_policy_quench_count++;
		} else if (dirty_pct >= 70) {
			bc->bc_bgwriter_curr_queue_depth /= 2;
			bc->bc_bgwriter_curr_policy_quench_count++;
		} else if (dirty_pct >= 60) {
			bc->bc_bgwriter_curr_queue_depth /= 3;
			bc->bc_bgwriter_curr_policy_quench_count++;
		} else if (dirty_pct >= 50) {
			bc->bc_bgwriter_curr_queue_depth /= 3;
			bc->bc_bgwriter_curr_policy_quench_count++;
		} else if (dirty_pct >= 20) {
			bc->bc_bgwriter_curr_queue_depth = 1;
			bc->bc_bgwriter_curr_policy_quench_count++;
		} else {
			bc->bc_bgwriter_curr_queue_depth = 1;
			bc->bc_bgwriter_curr_policy_quench_count++;
		}
		break;
	}
#undef bc_bgwriter_curr_policy_count
#undef bc_bgwriter_curr_policy_quench_count
#undef bc_bgwriter_curr_policy_quench2_count
#undef bc_bgwriter_curr_policy_no_quench_count
#undef bc_bgwriter_curr_policy_queue_non_empty_count
#undef bc_bgwriter_curr_policy_queue_empty_count
#undef bc_bgwriter_curr_policy_no_sched_count
#undef bc_bgwriter_curr_policy_no_sched_until
}

#endif /*BITTERN_CACHE_ALLOW_EXPERIMENTAL_POLICIES*/

struct cache_bgwriter_policy {
	const char *bgw_policy_name;
	void (*bgw_policy_function_slow)(struct bittern_cache *bc);
	void (*bgw_policy_function_fast)(struct bittern_cache *bc);
} cache_bgwriter_policies[] = {
	/*! current default writeback policy */
	{
		"classic",
		cache_bgwriter_compute_policy_classic,
		NULL,
	},
	/*! old default writeback policy (before REQ_FUA|REQ_FLUSH) */
	{
		"old-default",
		cache_bgwriter_compute_policy_old_default,
		NULL,
	},
	/*! experimental, use at your own risk and peril */
	{
		"dirty-ratio",
		cache_bgwriter_compute_policy_dirty_ratio,
		cache_bgwriter_compute_policy_dirty_ratio,
	},
#ifdef BITTERN_CACHE_ALLOW_EXPERIMENTAL_POLICIES
	/*! experimental, use at your own risk and peril */
	{
		"exp/queue-depth-adaptive",
		cache_bgwriter_compute_policy_queue_depth_adaptive,
		cache_bgwriter_compute_policy_queue_depth_adaptive,
	},
#endif /*BITTERN_CACHE_ALLOW_EXPERIMENTAL_POLICIES*/
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
	DMEMIT("bgwriter_active_policy=%d ", bc->bc_bgwriter_active_policy);
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

void cache_bgwriter_compute_policy_common(struct bittern_cache *bc)
{

	/* account for greedyness */
	bc->bc_bgwriter_curr_queue_depth += bc->bc_bgwriter_conf_greedyness;

	/* make sure max queue depth is sane after policy calculations */
	if (bc->bc_bgwriter_curr_max_queue_depth < 1)
		bc->bc_bgwriter_curr_max_queue_depth = 1;
	if (bc->bc_bgwriter_curr_max_queue_depth >
	    bc->bc_bgwriter_curr_max_queue_depth)
		bc->bc_bgwriter_curr_max_queue_depth =
		    PERCENT_OF(bc->bc_bgwriter_conf_max_queue_depth_pct,
				   bc->bc_max_pending_requests);

	/* make sure queue depth is sane after policy calculations */
	if (bc->bc_bgwriter_curr_queue_depth < 1)
		bc->bc_bgwriter_curr_queue_depth = 1;
	if (bc->bc_bgwriter_curr_queue_depth >
	    bc->bc_bgwriter_curr_max_queue_depth)
		bc->bc_bgwriter_curr_queue_depth =
		bc->bc_bgwriter_curr_max_queue_depth;

	/* override parameters and flush out quickly if in write-thru */
	if (is_cache_mode_writethru(bc)) {
		bc->bc_bgwriter_conf_cluster_size =
			CACHE_BGWRITER_MAX_CLUSTER_SIZE;
		bc->bc_bgwriter_conf_greedyness = 0;
		bc->bc_bgwriter_conf_max_queue_depth_pct =
			CACHE_BGWRITER_MAX_QUEUE_DEPTH_PCT;
		bc->bc_bgwriter_curr_queue_depth =
			PERCENT_OF(bc->bc_bgwriter_conf_max_queue_depth_pct,
				   bc->bc_max_pending_requests);
		bc->bc_bgwriter_curr_max_queue_depth =
			bc->bc_max_pending_requests;
		bc->bc_bgwriter_curr_rate_per_sec = 0;
		bc->bc_bgwriter_curr_min_age_secs = 0;
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

	/* per policy calculations */
	(*bp->bgw_policy_function_slow)(bc);

	/* common calculations */
	cache_bgwriter_compute_policy_common(bc);
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

	/* per policy calculations */
	if (bp->bgw_policy_function_fast != NULL)
		(*bp->bgw_policy_function_fast)(bc);

	/* common calculations */
	cache_bgwriter_compute_policy_common(bc);
}
