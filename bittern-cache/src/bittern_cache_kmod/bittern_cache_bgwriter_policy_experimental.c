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
 * All these functions need a TON of comments/documentation: What is important to a maintainer or a policy writer is:
 * What are the inputs (implied through values in bc structure),
 * What are the outputs (again, implied in bc structure), and what are the valid values to set.
 * Also, if possible, Try and merge the similar functions and/or write it in a way that uses a table to set values.
 */

int cache_bgwriter_compute_policy_classic(struct
							       bittern_cache
							       *bc)
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
		    A_PERCENT_OF_B(50, bc->bc_bgwriter_curr_max_queue_depth);
	} else if (dirty_pct > 90) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(25, bc->bc_bgwriter_curr_max_queue_depth);
	} else if (dirty_pct > 80) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(10, bc->bc_bgwriter_curr_max_queue_depth);
	} else if (dirty_pct > 70) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(4, bc->bc_bgwriter_curr_max_queue_depth);
		bc->bc_bgwriter_curr_min_age_secs = 1;
	} else if (dirty_pct > 60) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(2, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 2)
			bc->bc_bgwriter_curr_queue_depth = 2;
		bc->bc_bgwriter_curr_min_age_secs = 1;
	} else if (dirty_pct > 50) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(2, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 2)
			bc->bc_bgwriter_curr_queue_depth = 2;
		bc->bc_bgwriter_curr_rate_per_sec = 400;
		bc->bc_bgwriter_curr_min_age_secs = 5;
	} else if (dirty_pct > 40) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(1, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 2)
			bc->bc_bgwriter_curr_queue_depth = 2;
		bc->bc_bgwriter_curr_rate_per_sec = 200;
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
		    A_PERCENT_OF_B
		    (CACHE_BGWRITER_DEFAULT_QUEUE_DEPTH_PCT,
		     bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth == 0)
			bc->bc_bgwriter_curr_queue_depth = 1;
		bc->bc_bgwriter_curr_rate_per_sec = 0;
		bc->bc_bgwriter_curr_min_age_secs = 0;
	}

	return 0;
}

int cache_bgwriter_compute_policy_aggressive(struct
								  bittern_cache
								  *bc)
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
		    A_PERCENT_OF_B(50, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 20)
			bc->bc_bgwriter_curr_queue_depth = 20;
	} else if (dirty_pct > 90) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(30, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 10)
			bc->bc_bgwriter_curr_queue_depth = 10;
	} else if (dirty_pct > 80) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(10, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 10)
			bc->bc_bgwriter_curr_queue_depth = 10;
		bc->bc_bgwriter_curr_min_age_secs = 1;
	} else if (dirty_pct > 70) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(5, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 5)
			bc->bc_bgwriter_curr_queue_depth = 5;
		bc->bc_bgwriter_curr_min_age_secs = 1;
	} else if (dirty_pct > 60) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(4, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 4)
			bc->bc_bgwriter_curr_queue_depth = 4;
		bc->bc_bgwriter_curr_min_age_secs = 5;
	} else if (dirty_pct > 50) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(3, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 3)
			bc->bc_bgwriter_curr_queue_depth = 3;
		bc->bc_bgwriter_curr_min_age_secs = 10;
	} else if (dirty_pct > 40) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(2, bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth < 2)
			bc->bc_bgwriter_curr_queue_depth = 2;
		bc->bc_bgwriter_curr_min_age_secs = 10;
	} else if (dirty_pct > 30) {
		bc->bc_bgwriter_curr_queue_depth = 2;
		bc->bc_bgwriter_curr_rate_per_sec = 200;
		bc->bc_bgwriter_curr_min_age_secs = 10;
	} else if (dirty_pct > 20) {
		bc->bc_bgwriter_curr_queue_depth = 1;
		bc->bc_bgwriter_curr_rate_per_sec = 100;
		bc->bc_bgwriter_curr_min_age_secs = 20;
	} else {
		bc->bc_bgwriter_curr_queue_depth = 1;
		bc->bc_bgwriter_curr_rate_per_sec = 50;
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
		    A_PERCENT_OF_B
		    (CACHE_BGWRITER_DEFAULT_QUEUE_DEPTH_PCT,
		     bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth == 0)
			bc->bc_bgwriter_curr_queue_depth = 1;
		bc->bc_bgwriter_curr_rate_per_sec = 0;
		bc->bc_bgwriter_curr_min_age_secs = 0;
	}

	return 0;
}

int cache_bgwriter_compute_policy_queue_depth(struct
								   bittern_cache
								   *bc)
{
	int dirty_pct, dirty_pct_f100;
	unsigned int valid_entries_dirty, total_entries;
	int queue_depth_non_writeback;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	valid_entries_dirty = atomic_read(&bc->bc_valid_entries_dirty);
	total_entries = atomic_read(&bc->bc_total_entries);
	ASSERT(valid_entries_dirty <= total_entries);
	dirty_pct = T_PCT(total_entries, valid_entries_dirty);
	dirty_pct_f100 = T_PCT_F100(total_entries, valid_entries_dirty);
	ASSERT(dirty_pct <= 100);

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
	    A_PERCENT_OF_B(bc->bc_bgwriter_conf_max_queue_depth_pct,
			   bc->bc_max_pending_requests);
	bc->bc_bgwriter_curr_rate_per_sec = 0;
	bc->bc_bgwriter_curr_min_age_secs = 0;

	if (dirty_pct > 95) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(70, bc->bc_bgwriter_curr_max_queue_depth);
	} else if (dirty_pct > 90) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(60, bc->bc_bgwriter_curr_max_queue_depth);
	} else if (dirty_pct > 85) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(40, bc->bc_bgwriter_curr_max_queue_depth);
	} else if (dirty_pct > 80) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(20, bc->bc_bgwriter_curr_max_queue_depth);
	} else if (dirty_pct > 75) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(10, bc->bc_bgwriter_curr_max_queue_depth);
		bc->bc_bgwriter_curr_min_age_secs = 1;
	} else if (dirty_pct > 50) {
		bc->bc_bgwriter_curr_queue_depth = 2;
		bc->bc_bgwriter_curr_min_age_secs = 1;
	} else if (dirty_pct > 40) {
		bc->bc_bgwriter_curr_queue_depth = 1;
		bc->bc_bgwriter_curr_min_age_secs = 5;
	} else if (dirty_pct > 30) {
		bc->bc_bgwriter_curr_queue_depth = 1;
		bc->bc_bgwriter_curr_min_age_secs = 10;
	} else if (dirty_pct > 20) {
		bc->bc_bgwriter_curr_queue_depth = 1;
		bc->bc_bgwriter_curr_min_age_secs = 20;
	} else {
		bc->bc_bgwriter_curr_queue_depth = 1;
		bc->bc_bgwriter_curr_min_age_secs = 30;
	}

	/*
	 * queue depth needs to at least be 1
	 */
	if (bc->bc_bgwriter_curr_queue_depth < 1)
		bc->bc_bgwriter_curr_queue_depth = 1;

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
		    A_PERCENT_OF_B
		    (CACHE_BGWRITER_DEFAULT_QUEUE_DEPTH_PCT,
		     bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth == 0)
			bc->bc_bgwriter_curr_queue_depth = 1;
		bc->bc_bgwriter_curr_rate_per_sec = 0;
		bc->bc_bgwriter_curr_min_age_secs = 0;
	}

	return 0;
}

int
cache_bgwriter_compute_policy_queue_depth_adaptive(struct
									bittern_cache
									*bc)
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
	    A_PERCENT_OF_B(bc->bc_bgwriter_conf_max_queue_depth_pct,
			   bc->bc_max_pending_requests);
	bc->bc_bgwriter_curr_rate_per_sec = 0;
	bc->bc_bgwriter_curr_min_age_secs = 0;

	if (dirty_pct >= 95) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(70, bc->bc_bgwriter_curr_max_queue_depth);
	} else if (dirty_pct >= 90) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(60, bc->bc_bgwriter_curr_max_queue_depth);
	} else if (dirty_pct >= 85) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(50, bc->bc_bgwriter_curr_max_queue_depth);
	} else if (dirty_pct >= 80) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(40, bc->bc_bgwriter_curr_max_queue_depth);
	} else if (dirty_pct >= 75) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(30, bc->bc_bgwriter_curr_max_queue_depth);
		bc->bc_bgwriter_curr_min_age_secs = 1;
	} else if (dirty_pct >= 70) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(20, bc->bc_bgwriter_curr_max_queue_depth);
		bc->bc_bgwriter_curr_min_age_secs = 1;
	} else if (dirty_pct >= 60) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(20, bc->bc_bgwriter_curr_max_queue_depth);
		bc->bc_bgwriter_curr_min_age_secs = 5;
	} else if (dirty_pct >= 50) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(20, bc->bc_bgwriter_curr_max_queue_depth);
		bc->bc_bgwriter_curr_min_age_secs = 10;
	} else if (dirty_pct >= 40) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(20, bc->bc_bgwriter_curr_max_queue_depth);
		bc->bc_bgwriter_curr_min_age_secs = 10;
	} else if (dirty_pct >= 30) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(20, bc->bc_bgwriter_curr_max_queue_depth);
		bc->bc_bgwriter_curr_min_age_secs = 10;
	} else if (dirty_pct >= 20) {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(20, bc->bc_bgwriter_curr_max_queue_depth);
		bc->bc_bgwriter_curr_min_age_secs = 20;
	} else {
		bc->bc_bgwriter_curr_queue_depth =
		    A_PERCENT_OF_B(20, bc->bc_bgwriter_curr_max_queue_depth);
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

	/*
	 * queue depth needs to at least be 1
	 */
	if (bc->bc_bgwriter_curr_queue_depth < 1)
		bc->bc_bgwriter_curr_queue_depth = 1;

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
		    A_PERCENT_OF_B
		    (CACHE_BGWRITER_DEFAULT_QUEUE_DEPTH_PCT,
		     bc->bc_bgwriter_curr_max_queue_depth);
		if (bc->bc_bgwriter_curr_queue_depth == 0)
			bc->bc_bgwriter_curr_queue_depth = 1;
		bc->bc_bgwriter_curr_rate_per_sec = 0;
		bc->bc_bgwriter_curr_min_age_secs = 0;
		return 0;
	}
#undef bc_bgwriter_curr_policy_count
#undef bc_bgwriter_curr_policy_quench_count
#undef bc_bgwriter_curr_policy_quench2_count
#undef bc_bgwriter_curr_policy_no_quench_count
#undef bc_bgwriter_curr_policy_queue_non_empty_count
#undef bc_bgwriter_curr_policy_queue_empty_count
#undef bc_bgwriter_curr_policy_no_sched_count
#undef bc_bgwriter_curr_policy_no_sched_until

	if (quench == 2)
		return -EBUSY;

	return 0;
}
