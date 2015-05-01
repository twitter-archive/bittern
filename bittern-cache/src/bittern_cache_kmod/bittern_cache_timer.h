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

#ifndef BITTERN_CACHE_TIMER_H
#define BITTERN_CACHE_TIMER_H

#include <linux/time.h>

struct cache_timer {
	spinlock_t bct_spinlock;
	uint64_t bct_sum_nsec;
	uint64_t bct_avg_nsec;
	uint64_t bct_max_nsec;
	uint64_t bct_count;
	uint64_t bct_timewarp;
};

static inline uint64_t current_kernel_time_nsec(void)
{
	struct timespec ts = CURRENT_TIME;
	return timespec_to_ns(&ts);
}

static inline void cache_timer_init(struct cache_timer *bc_timer)
{
	memset(bc_timer, 0, sizeof(struct cache_timer));
	spin_lock_init(&bc_timer->bct_spinlock);
}

static inline void __cache_timer_add(struct cache_timer *bc_timer,
				     uint64_t ts_start, uint64_t ts_end)
{
	unsigned long flags;

	spin_lock_irqsave(&bc_timer->bct_spinlock, flags);
	if (ts_end >= ts_start) {
		ts_end -= ts_start;
		bc_timer->bct_sum_nsec += ts_end;
		if (ts_end > bc_timer->bct_max_nsec)
			bc_timer->bct_max_nsec = ts_end;
		bc_timer->bct_count++;
		bc_timer->bct_avg_nsec =
		    bc_timer->bct_sum_nsec / bc_timer->bct_count;
	} else {
		bc_timer->bct_timewarp++;
	}
	spin_unlock_irqrestore(&bc_timer->bct_spinlock, flags);
}

#define cache_timer_add(__bc_timer, __ts_start)       \
	__cache_timer_add((__bc_timer), (__ts_start), \
			  current_kernel_time_nsec())

#endif /* BITTERN_CACHE_TIMER_H */
