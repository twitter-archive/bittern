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

#ifdef ENABLE_KMALLOC_DEBUG

struct kmem_alloc_buffer {
	uint64_t ka_header;
	uint64_t ka_size;
	uint64_t ka_data[0];	/* real size will get allocated */
	uint64_t __ka_trailer;	/* this is not really here - declared so that we get the correct size for this struct */
};
#define KMEM_ALLOC_HEADER 0xf01cffffa5a54a2aLL
#define KMEM_ALLOC_TRAILER 0xf01cffff5a5a7a57LL

atomic_t kmem_buffers = ATOMIC_INIT(0);

unsigned int kmem_buffers_in_use(void)
{
	return atomic_read(&kmem_buffers);
}

void *kmem_allocate(size_t size, int flags, int zero)
{
	size_t alloc_size =
	    sizeof(struct kmem_alloc_buffer) + round_up(size, sizeof(uint64_t));
	struct kmem_alloc_buffer *alloc_buf =
	    (struct kmem_alloc_buffer *)kmalloc(alloc_size, flags);
	if (alloc_buf != NULL) {
		atomic_inc(&kmem_buffers);
		alloc_buf->ka_header = KMEM_ALLOC_HEADER;
		alloc_buf->ka_size = size;
		alloc_buf->ka_data[round_up(size, sizeof(uint64_t)) /
				   sizeof(uint64_t)] = KMEM_ALLOC_TRAILER;
		if (zero) {
			memset(&alloc_buf->ka_data[0], 0, size);
		} else {
			memset(&alloc_buf->ka_data[0], 0x71,
			       round_up(size, sizeof(uint64_t)));
		}
		ASSERT(alloc_buf->ka_header == KMEM_ALLOC_HEADER);
		ASSERT(alloc_buf->ka_size == size);
		ASSERT(alloc_buf->
		       ka_data[round_up(size, sizeof(uint64_t)) /
			       sizeof(uint64_t)] == KMEM_ALLOC_TRAILER);
		return &alloc_buf->ka_data[0];
	}
	return NULL;
}

void kmem_free(void *buf, size_t size)
{
	size_t alloc_size;
	struct kmem_alloc_buffer *alloc_buf;

	ASSERT(buf != NULL);
	atomic_dec(&kmem_buffers);
	alloc_size =
	    sizeof(struct kmem_alloc_buffer) + round_up(size, sizeof(uint64_t));
	alloc_buf = container_of(buf, struct kmem_alloc_buffer, ka_data);
	ASSERT(alloc_buf->ka_header == KMEM_ALLOC_HEADER);
	ASSERT(alloc_buf->ka_size == size);
	ASSERT(alloc_buf->
	       ka_data[round_up(size, sizeof(uint64_t)) / sizeof(uint64_t)] ==
	       KMEM_ALLOC_TRAILER);
	memset(alloc_buf, 0x7f, alloc_size);
	kfree(alloc_buf);
}

#endif /*ENABLE_KMALLOC_DEBUG */

/*
 * right now we just printk if requested. later on we'll store some parameters in a circular buffer
 */
int cache_trace_level = BT_LEVEL_DEFAULT;

DEFINE_SPINLOCK(cache_trace_spinlock);

/* #define BITTERN_TRACE_POINTERS */
#ifdef BITTERN_TRACE_POINTERS
#define BT_FMT_PTR "%p,"
#define BT_PTR(__p) (__p),
#else /*BITTERN_TRACE_POINTERS */
#define BT_FMT_PTR
#define BT_PTR(__p)
#endif /*BITTERN_TRACE_POINTERS */

void cache_trace(int level,
		 struct bittern_cache *bc,
		 struct work_item *wi,
		 struct cache_block *cache_block,
		 struct bio *original_bio,
		 struct bio *cloned_bio,
		 const char *func_name,
		 int line,
		 const char *fmt, ...)
{
	static char fmt_buf[512];
	static char bc_buf[128];
	static char wi_buf[128];
	static char cache_buf[128];
	static char o_bio_buf[128];
	static char c_bio_buf[128];
	va_list ap;
	unsigned long flags;

	/*
	 * this section of code is non-reentrant, so serialize with a spinlock
	 */

	spin_lock_irqsave(&cache_trace_spinlock, flags);

	va_start(ap, fmt);
	vsnprintf(fmt_buf, sizeof(fmt_buf), fmt, ap);
	va_end(ap);

	bc_buf[0] = '\0';
	wi_buf[0] = '\0';
	cache_buf[0] = '\0';
	o_bio_buf[0] = '\0';
	c_bio_buf[0] = '\0';

	if (wi != NULL) {
		snprintf(wi_buf, sizeof(wi_buf),
			 " wi(" BT_FMT_PTR "xid=%llu, flags=0x%x)",
			 BT_PTR(wi) wi->wi_io_xid, wi->wi_flags);
	}
	if (cache_block != NULL) {
		snprintf(cache_buf, sizeof(cache_buf),
			 " cache(" BT_FMT_PTR "%s,#%d,%lu,'%s',#%llu,#%d," UINT128_FMT ")",
			 BT_PTR(cache_block)
			 (is_cache_mode_writeback(bc) ? "WB" : "WT"),
			 cache_block->bcb_block_id, cache_block->bcb_sector,
			 cache_state_to_str(cache_block->bcb_state),
			 cache_block->bcb_xid,
			 atomic_read(&cache_block->bcb_refcount),
			 UINT128_ARG(cache_block->bcb_hash_data));
	}
	if (original_bio != NULL) {
		snprintf(o_bio_buf, sizeof(o_bio_buf),
			 " o_bio(" BT_FMT_PTR "%c,%lu:%d,%d/%d,%d:%d)",
			 BT_PTR(original_bio)
			 (bio_data_dir(original_bio) == WRITE ? 'W' : 'R'),
			 original_bio->bi_iter.bi_sector,
			 original_bio->bi_iter.bi_size,
			 original_bio->bi_iter.bi_idx, original_bio->bi_vcnt,
			 (original_bio->bi_iter.bi_idx <
			  original_bio->bi_vcnt ? bio_iovec(original_bio).
			  bv_offset : -1),
			 (original_bio->bi_iter.bi_idx <
			  original_bio->bi_vcnt ? bio_iovec(original_bio).
			  bv_len : -1));
	}
	if (cloned_bio != NULL) {
		snprintf(c_bio_buf, sizeof(c_bio_buf),
			 " c_bio(" BT_FMT_PTR "%c,%lu:%d,%d/%d,%d:%d)",
			 BT_PTR(cloned_bio)
			 (bio_data_dir(cloned_bio) == WRITE ? 'W' : 'R'),
			 cloned_bio->bi_iter.bi_sector,
			 cloned_bio->bi_iter.bi_size,
			 cloned_bio->bi_iter.bi_idx, cloned_bio->bi_vcnt,
			 (cloned_bio->bi_iter.bi_idx <
			  cloned_bio->bi_vcnt ? bio_iovec(cloned_bio).
			  bv_offset : -1),
			 (cloned_bio->bi_iter.bi_idx <
			  cloned_bio->bi_vcnt ? bio_iovec(cloned_bio).
			  bv_len : -1));
	}

	printk(PRINTK_DEBUG_DEFAULT "%s@%d: [%d] %s%s%s%s%s: %s\n",
	       func_name,
	       line,
	       current->pid,
	       bc_buf, wi_buf, cache_buf, o_bio_buf, c_bio_buf, fmt_buf);

	spin_unlock_irqrestore(&cache_trace_spinlock, flags);
}
