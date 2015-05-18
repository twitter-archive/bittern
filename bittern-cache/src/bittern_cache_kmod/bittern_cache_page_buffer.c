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

struct k_pagebuf {
	uint32_t magic;
	struct list_head list;
} __aligned(PAGE_SIZE);
#define K_PAGEBUF_MAGIC 0xf01c7a2c

/*
 * there is no need to make this constant tunable, so long as it's a reasonable
 * number. it should be only high enough to make sure that the _map() code
 * doesn't defer too many requests due to lack of buffers.
 * the absolute minimum required is the amount of buffer pages used
 * simultaneously during initialization (currently 2).
 */
#define CACHE_PAGE_BUFFER_LOW_BUFFER_PAGES 128

void pagebuf_initialize(struct bittern_cache *bc)
{
	int p;
	struct pagebuf *pg = &bc->bc_pagebuf;

	ASSERT(bc != NULL);
	printk_info("enter\n");

	spin_lock_init(&pg->freelist_spinlock);
	INIT_LIST_HEAD(&pg->freelist);
	atomic_set(&pg->pages, 0);
	atomic_set(&pg->max_pages, 0);
	atomic_set(&pg->free_pages, 0);
	atomic_set(&pg->stat_alloc_wait_count, 0);
	atomic_set(&pg->stat_alloc_nowait_count, 0);
	atomic_set(&pg->stat_free_count, 0);
	for (p = 0; p < PGPOOL_POOLS; p++) {
		struct pool *pool = &pg->pools[p];

		atomic_set(&pool->in_use_pages, 0);
		atomic_set(&pool->stat_alloc_wait_count, 0);
		atomic_set(&pool->stat_alloc_nowait_count, 0);
		atomic_set(&pool->stat_free_count, 0);
		atomic_set(&pool->stat_alloc_nowait_nopage, 0);
		atomic_set(&pool->stat_alloc_nowait_toomany, 0);
		atomic_set(&pool->stat_alloc_wait_vmalloc, 0);
		cache_timer_init(&pool->stat_wait_timer);
	}
	atomic_set(&pg->stat_vmalloc_count, 0);
	atomic_set(&pg->stat_vfree_count, 0);
	cache_timer_init(&pg->stat_vmalloc_timer);

	printk_info("done\n");
}

void pagebuf_vfree_one(struct bittern_cache *bc)
{
	struct pagebuf *pg = &bc->bc_pagebuf;
	unsigned long flags;
	struct k_pagebuf *k_buf = NULL;

	spin_lock_irqsave(&pg->freelist_spinlock, flags);
	if (list_non_empty(&pg->freelist)) {
		int pages;

		k_buf = list_first_entry(&pg->freelist, struct k_pagebuf, list);
		ASSERT(k_buf->magic == K_PAGEBUF_MAGIC);
		ASSERT(PAGE_ALIGNED(k_buf));
		k_buf->magic = -1;
		list_del_init(&k_buf->list);
		pages = atomic_dec_return(&pg->free_pages);
		ASSERT(pages >= 0);
		pages = atomic_dec_return(&pg->pages);
		ASSERT(pages >= 0);
		atomic_inc(&pg->stat_vfree_count);
	}
	spin_unlock_irqrestore(&pg->freelist_spinlock, flags);

	if (k_buf != NULL) {
		ASSERT(vmalloc_to_page(k_buf) != NULL);
		vfree(k_buf);
	}
}

void pagebuf_deinitialize(struct bittern_cache *bc)
{
	int p;
	struct pagebuf *pg = &bc->bc_pagebuf;

	printk_info("(before vfree) pagebuf_pages=%u pagebuf_max_pages=%u pagebuf_free_pages=%u\n",
		    atomic_read(&pg->pages),
		    atomic_read(&pg->max_pages),
		    atomic_read(&pg->free_pages));

	while (atomic_read(&pg->pages) > 0)
		pagebuf_vfree_one(bc);

	printk_info("(after vfree) pagebuf_pages=%u pagebuf_max_pages=%u pagebuf_free_pages=%u\n",
		    atomic_read(&pg->pages),
		    atomic_read(&pg->max_pages),
		    atomic_read(&pg->free_pages));

	M_ASSERT(atomic_read(&pg->pages) == 0);
	M_ASSERT(atomic_read(&pg->free_pages) == 0);
	for (p = 0; p < PGPOOL_POOLS; p++) {
		printk_info("pagebuf_in_use_pages[%d]=%u\n",
			    p,
			    atomic_read(&pg->pools[p].in_use_pages));
		M_ASSERT(atomic_read(&pg->pools[p].in_use_pages) == 0);
	}

	printk_info("pagebuf_stat_vmalloc_count=%u pagebuf_stat_vfree_count=%u\n",
		    atomic_read(&pg->stat_vmalloc_count),
		    atomic_read(&pg->stat_vfree_count));

	printk_info("done\n");
}

int pagebuf_in_use(struct bittern_cache *bc, int p)
{
	struct pagebuf *pg = &bc->bc_pagebuf;
	struct pool *pool = &pg->pools[p];

	ASSERT(p >= 0 && p < PGPOOL_POOLS);
	return atomic_read(&pool->in_use_pages);
}

void pagebuf_callout(struct bittern_cache *bc)
{
	struct pagebuf *pg = &bc->bc_pagebuf;

	if (atomic_read(&pg->pages) > CACHE_PAGE_BUFFER_LOW_BUFFER_PAGES)
		pagebuf_vfree_one(bc);
}

void *__pagebuf_allocate_nowait(struct bittern_cache *bc,
				int p,
				struct page **out_page)
{
	struct pagebuf *pg = &bc->bc_pagebuf;
	struct pool *pool = &pg->pools[p];
	struct k_pagebuf *k_buf = NULL;
	unsigned long flags;

	ASSERT(p >= 0 && p < PGPOOL_POOLS);
	ASSERT(out_page != NULL);

	*out_page = NULL;
	if (atomic_read(&pool->in_use_pages) >= pagebuf_max_bufs(bc)) {
		atomic_inc(&pool->stat_alloc_nowait_toomany);
		return NULL;
	}

	spin_lock_irqsave(&pg->freelist_spinlock, flags);
	if (list_non_empty(&pg->freelist)) {
		int free_pages;

		k_buf = list_first_entry(&pg->freelist, struct k_pagebuf, list);
		ASSERT(k_buf != NULL);
		list_del_init(&k_buf->list);

		spin_unlock_irqrestore(&pg->freelist_spinlock, flags);

		atomic_inc(&pool->in_use_pages);
		free_pages = atomic_dec_return(&pg->free_pages);
		ASSERT(free_pages >= 0);

		ASSERT(k_buf->magic == K_PAGEBUF_MAGIC);
		ASSERT(PAGE_ALIGNED(k_buf));
		*out_page = vmalloc_to_page(k_buf);
		ASSERT(*out_page != NULL);
		k_buf->magic = -1;
	} else {
		spin_unlock_irqrestore(&pg->freelist_spinlock, flags);
		atomic_inc(&pool->stat_alloc_nowait_nopage);
	}

	return (void *)k_buf;
}

void *pagebuf_allocate_nowait(struct bittern_cache *bc,
			      int p,
			      struct page **out_page)
{
	void *k_buf;
	struct pagebuf *pg = &bc->bc_pagebuf;
	struct pool *pool = &pg->pools[p];

	ASSERT(p >= 0 && p < PGPOOL_POOLS);
	ASSERT(out_page != NULL);

	atomic_inc(&pg->stat_alloc_nowait_count);
	atomic_inc(&pool->stat_alloc_nowait_count);
	k_buf = __pagebuf_allocate_nowait(bc, p, out_page);
	BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL,
		 "pool %d: k_buf=%p",
		 p,
		 k_buf);
	return k_buf;
}

void *pagebuf_allocate_wait(struct bittern_cache *bc,
			    int p,
			    struct page **out_page)
{
	struct pagebuf *pg = &bc->bc_pagebuf;
	struct pool *pool = &pg->pools[p];
	uint64_t start_timer;
	uint64_t start_timer_vmalloc;
	void *k_buf;
	int val;

	ASSERT(p >= 0 && p < PGPOOL_POOLS);
	ASSERT(out_page != NULL);

	atomic_inc(&pg->stat_alloc_wait_count);
	atomic_inc(&pool->stat_alloc_wait_count);
	start_timer = current_kernel_time_nsec();
	*out_page = NULL;

	/*
	 * First, try to allocate from freelist.
	 */
	k_buf = __pagebuf_allocate_nowait(bc, p, out_page);
	if (k_buf != NULL) {
		BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL,
			 "pool %d: k_buf=%p",
			 p,
			 k_buf);
		cache_timer_add(&pool->stat_wait_timer, start_timer);
		return k_buf;
	}

	/*
	 * Freelist is empty.
	 * We let the caller do flow control on the maximum number of buffers
	 * in this case, as it can better decide when to stop allocating and
	 * can keep better track of what is going on.
	 * With this said, we assert if the caller is going overboard by a
	 * little bit (the number used in the assertion below is arbitrary,
	 * callers really should never exceed the threshold by more than 1).
	 */

	/*
	 * DO NOT CHECK FOR EXCESS USE. WITH THE NEW CODE SOMETIMES WE END UP
	 * IN USING A BIT MORE PAGES IN THE WRITE CLONING PORTION.
	 * THIS IS COMPLETELT HARMLESS AND IN ANY CASE WE ARE GOING TO
	 * COMPLETELY REPLACE THIS CODE WITH kmem_cache SLABS.
	 */

	atomic_inc(&pool->stat_alloc_wait_vmalloc);

	start_timer_vmalloc = current_kernel_time_nsec();
	k_buf = vmalloc(PAGE_SIZE);
	M_ASSERT_FIXME(k_buf != NULL);
	ASSERT(PAGE_ALIGNED(k_buf));
	cache_timer_add(&pg->stat_vmalloc_timer, start_timer_vmalloc);

	*out_page = vmalloc_to_page(k_buf);
	ASSERT(*out_page != NULL);

	atomic_inc(&pool->in_use_pages);
	atomic_inc(&pg->stat_vmalloc_count);
	val = atomic_inc_return(&pg->pages);
	atomic_set_if_higher(&pg->max_pages, val);
	cache_timer_add(&pool->stat_wait_timer, start_timer);

	return k_buf;
}

/*! can be called in intr or softirq context */
void pagebuf_free(struct bittern_cache *bc, int p, void *__k_buf)
{
	struct pagebuf *pg = &bc->bc_pagebuf;
	struct pool *pool = &pg->pools[p];
	struct k_pagebuf *k_buf = (struct k_pagebuf *)__k_buf;
	unsigned long flags;
	int val;

	BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL,
		 "pool %d: k_buf=%p",
		 p,
		 k_buf);

	ASSERT(p >= 0 && p < PGPOOL_POOLS);
	ASSERT(k_buf != NULL);
	ASSERT(PAGE_ALIGNED(k_buf));
	ASSERT(vmalloc_to_page(k_buf) != NULL);

	atomic_inc(&pg->stat_free_count);
	atomic_inc(&pool->stat_free_count);

	INIT_LIST_HEAD(&k_buf->list);
	k_buf->magic = K_PAGEBUF_MAGIC;

	/*
	 * The free pages count is updated outside the freelist spinlock.
	 * Because of that there is a very brief time window during which
	 * the freelist is non empty and the count is zero. this does not
	 * affect integrity but prevents us from doing this assert
	 *      ASSERT(free_pages >= 0);
	 * in the allocate_nowait code.
	 * So to prevent this crash we move the atomic increment before
	 * releasing the block to the free list.
	 */
	/*!
	 * \todo should we just use a regular volatile int and change it
	 * within the spinlock critical section?
	 */
	atomic_inc(&pg->free_pages);

	spin_lock_irqsave(&pg->freelist_spinlock, flags);
	list_add(&k_buf->list, &pg->freelist);
	spin_unlock_irqrestore(&pg->freelist_spinlock, flags);

	val = atomic_dec_return(&pool->in_use_pages);
	ASSERT(val >= 0);

	/*
	 * wakeup possible waiters
	 */
	cache_wakeup_deferred(bc);
}
