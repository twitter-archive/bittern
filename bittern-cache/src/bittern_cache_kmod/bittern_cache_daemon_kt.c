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

void cache_task_pmem_header_update_timeout(struct bittern_cache *bc)
{
	ASSERT_BITTERN_CACHE(bc);
	BT_TRACE(BT_LEVEL_TRACE3, bc, NULL, NULL, NULL, NULL, "bc=%p", bc);
	pmem_header_update(bc, 0);
}

void cache_task_sequential_timeout(struct bittern_cache *bc)
{
	ASSERT_BITTERN_CACHE(bc);
	BT_TRACE(BT_LEVEL_TRACE3, bc, NULL, NULL, NULL, NULL, "bc=%p", bc);
	seq_bypass_timeout(bc);
}

struct cache_tasks {
	void (*function)(struct bittern_cache *bc);
	unsigned long interval_ms;
} cache_tasks[] = {
	{ cache_task_sequential_timeout, 500, },
	{ cache_task_pmem_header_update_timeout, (30 * 1000), },
};

void cache_daemon_run_tasks(struct bittern_cache *bc, unsigned long *lastrun)
{
	int i = 0;
	int work_count = 0;

	ASSERT_BITTERN_CACHE(bc);
	BT_TRACE(BT_LEVEL_TRACE3, bc, NULL, NULL, NULL, NULL,
		 "bc=%p, lastrun=%p",
		 bc,
		 lastrun);
	for (i = 0; i < ARRAY_SIZE(cache_tasks); i++) {
		struct cache_tasks *bt = &cache_tasks[i];
		unsigned long jiffies_msecs = jiffies_to_msecs(jiffies);

		ASSERT(bt->function != NULL);
		ASSERT(bt->interval_ms > 0);
		BT_TRACE(BT_LEVEL_TRACE3, bc, NULL, NULL, NULL, NULL,
			 "bc=%p: #%d: function=%p, interval=%lums, lastrun=%lums, last run=%lums",
			 bc,
			 i,
			 bt->function,
			 bt->interval_ms,
			 lastrun[i],
			 (jiffies_msecs - lastrun[i]));
		if ((jiffies_msecs - lastrun[i]) >= bt->interval_ms) {
			BT_TRACE(BT_LEVEL_TRACE3, bc, NULL, NULL, NULL, NULL,
				 "bc=%p: running task %p", bc,
				 bt->function);
			(*bt->function)(bc);
			lastrun[i] = jiffies_msecs;
			work_count++;
		}
	}
	if (work_count > 0)
		bc->bc_daemon_work_count++;
	else
		bc->bc_daemon_no_work_count++;
}

int cache_daemon_kthread(void *__bc)
{
	struct bittern_cache *bc = (struct bittern_cache *)__bc;
	unsigned long *lastrun;

	set_user_nice(current, CACHE_DAEMON_THREAD_NICE);

	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL, "enter, nice=%d",
		 CACHE_DAEMON_THREAD_NICE);

	lastrun = kmem_zalloc(sizeof(unsigned long) * ARRAY_SIZE(cache_tasks),
			      GFP_NOIO);
	M_ASSERT_FIXME(lastrun != NULL);

	while (!kthread_should_stop()) {
		int ret;

		ASSERT(bc != NULL);
		ASSERT_BITTERN_CACHE(bc);
		/*
		 * we get woken up at each cache fill
		 */
		ret = wait_event_interruptible_timeout(bc->bc_daemon_wait,
						       kthread_should_stop(),
						       msecs_to_jiffies(500));
		if (signal_pending(current))
			flush_signals(current);

		if (kthread_should_stop())
			break;

		cache_daemon_run_tasks(bc, lastrun);

		schedule();

	}

	kmem_free(lastrun, sizeof(unsigned long) * ARRAY_SIZE(cache_tasks));

	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL, "exit");

	bc->bc_daemon_task = NULL;
	return 0;
}
