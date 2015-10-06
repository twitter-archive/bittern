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

#ifndef BITTERN_CACHE_H
#define BITTERN_CACHE_H

#include <linux/bio.h>
#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/delay.h>
#include <linux/device-mapper.h>
#include <linux/dm-io.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stringify.h>
#include <linux/uuid.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>

/* this must be first */
#include "bittern_cache_config.h"

#include "math128.h"
#include "murmurhash3.h"

#include "bittern_cache_list_debug.h"

#include "bittern_cache_linux.h"

#define BITTERN_CACHE_VERSION "1.0.5"
#define BITTERN_CACHE_CODENAME "plumbers"

#include "bittern_cache_todo.h"

/*! sectors per cache block -- right now PAGE_SIZE is the cache block size */
#define SECTORS_PER_CACHE_BLOCK ((sector_t)(PAGE_SIZE / SECTOR_SIZE))

/*!
 * Cache block size is PAGE_SIZE by definition, so this constant cannot be
 * changed until we allow multiples of PAGE_SIZE cache blocks.
 */
#define MAX_IO_LEN_PAGES	1
#define MAX_IO_LEN_SECTORS	((PAGE_SIZE * MAX_IO_LEN_PAGES) / SECTOR_SIZE)

/*! invalid sector number */
#define SECTOR_NUMBER_INVALID ((sector_t)-1)
/*! any non-negative number is valid */
#define is_sector_number_valid(__sector) ((int64_t)(__sector) >= 0)
/*! any negative number is invalid */
#define is_sector_number_invalid(__sector) (!is_sector_number_valid(__sector))

/*
 * all the tunables are in the tunables file.
 */
#include "bittern_cache_tunables.h"

#include "bittern_cache_timer.h"

#include "bittern_cache_pmem_api.h"

#include "bittern_cache_states.h"

/*
 * intel x86-sse memcpy_nt
 */
#include "memcpy_nt.h"

#define CACHE_REPLACEMENT_MODE_FIFO 1
#define CACHE_REPLACEMENT_MODE_LRU 2
#define CACHE_REPLACEMENT_MODE_RANDOM 3
#define CACHE_REPLACEMENT_MODE_DEFAULT CACHE_REPLACEMENT_MODE_RANDOM
/* made it tunable --- #define CACHE_REPLACEMENT_MODE_RANDOM_MAX_SCANS 10 */

#define ASSERT_CACHE_REPLACEMENT_MODE(__mode) \
	ASSERT((__mode) == CACHE_REPLACEMENT_MODE_FIFO || \
		(__mode) == CACHE_REPLACEMENT_MODE_LRU || \
		(__mode) == CACHE_REPLACEMENT_MODE_RANDOM)

/* \todo move out from include and into a .c file (_subr.c ?) */
static inline const char *cache_replacement_mode_to_str(int mode)
{
	ASSERT_CACHE_REPLACEMENT_MODE(mode);
	if (mode == CACHE_REPLACEMENT_MODE_FIFO)
		return "fifo";
	else if (mode == CACHE_REPLACEMENT_MODE_LRU)
		return "lru";
	else
		return "random";
}

/*! returns true if sector is cache aligned */
static inline int is_sector_cache_aligned(sector_t s)
{
	/* is the request cache aligned? */
	return (s % SECTORS_PER_CACHE_BLOCK) == 0;
}

/*!
 * returns true if the request a perfect cache block,
 * that is cache-aligned and cache-multiple.
 */
static inline int is_request_cache_block(sector_t s, unsigned int len)
{
	return is_sector_cache_aligned(s) && (len == PAGE_SIZE);
}

/*!
 * returns true if the request completely fits within a cache block even
 * though is not aligned or a full cache block size
 */
static inline int is_request_single_cache_block(sector_t s, unsigned int len)
{
	sector_t s_end = s + (len / SECTOR_SIZE) - 1;
	sector_t cache_block = s / SECTORS_PER_CACHE_BLOCK;
	sector_t cache_block_end = s_end / SECTORS_PER_CACHE_BLOCK;
	return cache_block == cache_block_end;
}

/*! convert sector number to cache block sector number */
static inline sector_t sector_to_cache_block_sector(sector_t s)
{
	return s & ~(SECTORS_PER_CACHE_BLOCK - 1);
}

/*! bio equivalent of @ref is_sector_cache_aligned */
#define bio_is_sector_cache_aligned(__bio) \
	is_sector_cache_aligned((__bio)->bi_iter.bi_sector)
/*! bio equivalent of @ref is_request_cache_block */
#define bio_is_request_cache_block(__bio) \
	is_request_cache_block((__bio)->bi_iter.bi_sector, (__bio)->bi_iter.bi_size)
/*! bio equivalent of @ref is_request_single_cache_block */
#define bio_is_request_single_cache_block(__bio) \
	is_request_single_cache_block((__bio)->bi_iter.bi_sector, (__bio)->bi_iter.bi_size)
/*! bio equivalent of @ref sector_to_cache_block_sector */
#define bio_sector_to_cache_block_sector(__bio) \
	sector_to_cache_block_sector((__bio)->bi_iter.bi_sector)

#define WI_MAGIC1 0xf10ca593
#define WI_MAGIC2 0xf10ca595

/*! @defgroup wi_flags_bitvalues work_item wi_flags bitmask values
 * @{
 */
/*! @ref wi_flags : indicates request bio has been cloned */
#define WI_FLAG_BIO_CLONED 0x0020
/*!
 * @ref wi_flags : indicates request bio has not been cloned.
 * \todo should probably just use (WI_FLAG_BIO_CLONED == 0) instead of this.
 */
#define WI_FLAG_BIO_NOT_CLONED 0x0040
/*! @ref wi_flags : if set, use a new XID */
#define WI_FLAG_XID_NEW 0x0100
/*! @ref wi_flags : if set, use the XID from the cache_block */
#define WI_FLAG_XID_USE_CACHE_BLOCK 0x0200
/*! @ref wi_flags : mask of all possible legal values for wi_flag */
#define WI_FLAG_MASK (WI_FLAG_BIO_CLONED |    \
		WI_FLAG_BIO_NOT_CLONED |      \
		WI_FLAG_XID_NEW |             \
		WI_FLAG_XID_USE_CACHE_BLOCK)
/*! @} */

/*!
 * work_item structure.
 * This contains all the information and resources which are needed to perform
 * any given state transition, either user-initiated (via map callback) or
 * bittern initiated (invalidation and writeback).
 */
struct work_item {
	int wi_magic1;
	/*! @ref wi_flags_bitvalues */
	int wi_flags;
	/*! access to this member is serialized with global spinlock */
	struct list_head wi_pending_io_list;
	/*! workstruct used when a thread context is required */
	struct work_struct wi_work;
	/* pointer to original bio (if any) */
	struct bio *wi_original_bio;
	/* pointer to cloned bio (if any) */
	struct bio *wi_cloned_bio;
	/* 1 if cache mode writeback, 0 if cache mode write-through */
	int wi_cache_mode_writeback;
	/* pointer to bittern_cache */
	struct bittern_cache *wi_cache;
	/* pointer to bittern cache block being worked on */
	struct cache_block *wi_cache_block;
	/*
	 * if we are handling a write-hit, we need to clone the cache block and update the clone.
	 * after update is complete, then we can delete the original cache block.
	 */
	struct cache_block *wi_original_cache_block;
	/*!
	 * The struct @ref pmem_context contains pmem specific information used
	 * by pmem.
	 * In particular data buffer info holds virtual address and page struct
	 * pointer to cache data being transferred. If the pmem hardware does
	 * not support direct dma access and/or direct memory access,
	 * then we allocate a page buffer to do double buffering during memory
	 * copies.
	 * All of this is hidden by the pmem layer. The higher level code only
	 * sees the accessor functions @ref pmem_context_data_vaddr and
	 * @ref pmem_context_data_page.
	 */
	struct pmem_context wi_pmem_ctx;
	/* transaction id */
	uint64_t wi_io_xid;
	/* bypass cache for this workitem */
	int wi_bypass;
	/*! keep track here of the request type and block information */
	const char *wi_op_type;
	sector_t wi_op_sector;
	/* bio rw flags */
	unsigned long wi_op_rw;
	/* time in workqueue */
	uint64_t wi_ts_workqueue;
	/* io start time */
	uint64_t wi_ts_started;
	/*! keeps track of physical io */
	uint64_t wi_ts_physio;
	/*!
	 * Keeps track of flush latency. Note that this latency is
	 * already accounted for in @ref wi_ts_physio.
	 */
	uint64_t wi_ts_physio_flush;
	/*! pmem async context for cache operations */
	struct async_context wi_async_context;
	int wi_magic2;
	/*! bi_data_dir used for deferred worker */
	int bi_datadir;
	/*! bi_set_original_bio used for deferred worker */
	bool bi_set_original_bio;
	/*! access serialized with @ref devio spinlock */
	struct list_head devio_pending_list;
	/*! access serialized with @ref devio spinlock */
	int64_t devio_gennum;
	/*!
	 * Copy of current bio's flags. Looking at the bio flags
	 * is not possible, as bio strips them before acking the request.
	 */
	int devio_flags;
};
#define __ASSERT_WORK_ITEM(__wi) ({					\
	/* make sure it's l-value, compiler will optimize this away */	\
	__wi = (__wi);							\
	ASSERT((__wi) != NULL);						\
	ASSERT((__wi)->wi_magic1 == WI_MAGIC1);				\
	ASSERT((__wi)->wi_magic2 == WI_MAGIC2);				\
	ASSERT(((__wi)->wi_flags & ~WI_FLAG_MASK) == 0);		\
	ASSERT((__wi)->wi_cache_mode_writeback == 1 ||			\
	       (__wi)->wi_cache_mode_writeback == 0);			\
})
#define ASSERT_WORK_ITEM(__wi, __bc) ({					\
	/* make sure it's l-value, compiler will optimize this away */	\
	__wi = (__wi);							\
	__ASSERT_WORK_ITEM(__wi);					\
	ASSERT((__wi)->wi_cache == (__bc));				\
})
#define is_work_item_mode_writeback(__wi) ((__wi)->wi_cache_mode_writeback)

extern const char *cache_transition_to_str(enum
							cache_transition
							path);
extern const char *cache_state_to_str(enum cache_state state);

/*
 * there are quite a few optimizations possible here, including compiling out magic numbers
 * for production version, changing some fields to bitfields and so on ....
 * we can also use one linked list field -- some assembly required.
 */
#define BCB_MAGIC1        0xf10c8f2b
#define BCB_MAGIC3        0xf10c8f37
struct cache_block {
	int bcb_magic1;
	int bcb_block_id;
	spinlock_t bcb_spinlock;
	/*!
	 * a cache block with bcb_refcount == 0 is idle and un-owned
	 * a cache block with bcb_refcount == 1 is busy and owned by the caller who got it
	 * a cache block with bcb_refcount > 1 is busy and not owned by the caller who got it
	 */
	atomic_t bcb_refcount;
	sector_t bcb_sector;
	/*! the last xid for this cache block */
	uint64_t bcb_xid;
	/*!
	 * last modify time, in seconds since boot. this will roll-over in
	 * about 137 years, so you need to make sure to reboot your machine
	 * before then.
	 */
	unsigned int bcb_last_modify;
	/*!
	 * for the most part this is only valid when state == VALID and refcount == 0
	 * it's also valid during read hit handling
	 */
	uint128_t bcb_hash_data;
	/* linked list for either valid (clean+dirty) or invalid blocks */
	struct list_head bcb_entry;
	/*! linked list for valid or dirty blocks */
	struct list_head bcb_entry_cleandirty;
	/*! red-black tree node */
	struct rb_node bcb_rb_node;
	enum cache_state bcb_state:8;
	enum cache_transition bcb_cache_transition:8;
	uint32_t bcb_magic3;
};

#define BC_MAGIC1 0xf10c7a93
#define BC_MAGIC2 0xf10c754a
#define BC_MAGIC3 0xf10ca793
#define BC_MAGIC4 0xf10c85a7

#define BC_NAMELEN 128

#define BCSIO_MAGIC 0xf10c1234

/*
 * sequential i/o bypass
 */

struct seq_io_stream {
	struct list_head list_entry;
	int magic;
	sector_t last_sector;
	unsigned int sector_count;
	pid_t stream_pid;
	unsigned long timestamp_ms;
};
/*!
 * This structure keeps track of a certain number of IO streams
 * and it used to detect if any given stream is doing sequential access.
 */
struct seq_io_bypass {
	/* counters */
	atomic_t seq_io_count;
	atomic_t non_seq_io_count;
	atomic_t bypass_count;
	atomic_t bypass_hit;
	/* superuser tunables */
	unsigned int bypass_threshold;
	unsigned int bypass_timeout;
	bool bypass_enabled;
	/* internal stuff */
	spinlock_t seq_lock;
	unsigned int streams_count;
	unsigned int streams_count_max;
	/* sum and average (sum/count) of sequential streams length */
	uint64_t s_streams_len_sum;
	uint64_t s_streams_len_count;
	unsigned int s_streams_len_max;
	/* sum and average (sum/count) of non-sequential streams length */
	uint64_t ns_streams_len_sum;
	uint64_t ns_streams_len_count;
	unsigned int ns_streams_len_max;
	struct list_head streams_lru;
	struct seq_io_stream streams_array[SEQ_IO_TRACK_DEPTH];
	/*
	 * Measure list traversal length for a hit. It tells us how good
	 * (or bad) this lru is. We don't count misses because by definition
	 * they need to traverse the full list.
	 */
	uint64_t lru_hit_depth_sum;
	uint64_t lru_hit_depth_count;
};

/*! holds queue of deferred requests */
struct deferred_queue {
	struct bio_list list;
	volatile unsigned int curr_count;
	unsigned int requeue_count;
	unsigned int max_count;
	unsigned int no_work_count;
	unsigned int work_count;
	struct cache_timer timer;
	uint64_t tstamp;
};

struct pmem_api {
	/*
	 * per instance state
	 * FIXME: should move to per instance
	 */
	struct block_device *papi_bdev;
	struct workqueue_struct *papi_make_request_wq;
	/* used size */
	size_t papi_bdev_size_bytes;
	size_t papi_bdev_actual_size_bytes;
	/* pmem stats */
	struct pmem_info papi_stats;
	/* in memory copy of pmem header, always up-to-date */
	struct pmem_header papi_hdr;
	/* tells which copy (0 or 1) we updated last */
	int papi_hdr_updated_last;
	/* pmem_api context */
	const struct cache_papi_interface *papi_interface;
};

/*! error state */
enum error_state {
	/*! all is good */
	ES_NOERROR = 0,
	/*! error state - fail all requests */
	ES_ERROR_FAIL_ALL,
	/*! error state - fail all read requests */
	ES_ERROR_FAIL_READS,
};

/*!
 * The mother of all structures.
 * All of Bittern state is declared directly here or pointed by here.
 */
struct bittern_cache {
	int bc_magic1;

	/*! /sys/fs/ kobject */
	struct kobject bc_kobj;

	/*! cache name, e.g. bitcache0 */
	char bc_name[BC_NAMELEN];
	/*! cached device path, e.g. /dev/mapper/hdd0 */
	char bc_cached_device_name[BC_NAMELEN];
	/*! cache device path, e.g. /dev/mapper/nvme0 */
	char bc_cache_device_name[BC_NAMELEN];
	/*! cache device type, e.g. "mem" or "block" */
	char bc_cache_device_type[BC_NAMELEN];
	/*! size of cached device in bytes */
	uint64_t bc_cached_device_size_bytes;
	/*! size of cached device in mbytes */
	uint64_t bc_cached_device_size_mbytes;

	/*! error state */
	enum error_state error_state;
	/*! error count */
	atomic_t error_count;

	/*! cache replacement mode (RANDOM, FIFO, LRU) */
	int bc_replacement_mode;

	/*! cache mode: writeback == 1, writethru == 0 */
	volatile int bc_cache_mode_writeback;

	/*!
	 * Enable issuing of req_flush to the cached device.
	 * This option can be only be disable if the device writeback cache
	 * is disabled. Incorrectly setting this option without doing so
	 * will lead to data corruption.
	 */
	bool bc_enable_req_fua;

	/* total deferred requests - sum of queue lens in deferred thread */
	atomic_t bc_total_deferred_requests;
	/* current # of deferred requests - sum of queue lens in deferred thread */
	atomic_t bc_deferred_requests;
	/* highest deferred requests */
	atomic_t bc_highest_deferred_requests;
	/* total read requests */
	atomic_t bc_read_requests;
	/* total write requests */
	atomic_t bc_write_requests;
	/*
	 * FIXME: "pending" is so overloaded here.
	 */
	/*!
	 * current number of pending requests (read+write+readbypass+writeback)
	 * note that we do not count invalidations in pending requests.
	 */
	atomic_t bc_pending_requests;
	/* current # of pending read requests */
	atomic_t bc_pending_read_requests;
	/* current # of pending read bypass requests */
	atomic_t bc_pending_read_bypass_requests;
	/* current # of pending write requests */
	atomic_t bc_pending_write_requests;
	/* current # of pending write bypass requests */
	atomic_t bc_pending_write_bypass_requests;
	/* current # of pending writebacks */
	atomic_t bc_pending_writeback_requests;
	/* current # of pending writebacks */
	atomic_t bc_pending_invalidate_requests;
	/* highest # of pending requests */
	atomic_t bc_highest_pending_requests;
	/* total # of completed requests */
	atomic_t bc_completed_requests;
	/* total # of completed read requests */
	atomic_t bc_completed_read_requests;
	/* total # of completed write requests */
	atomic_t bc_completed_write_requests;
	/* total # of completed writebacks */
	atomic_t bc_completed_writebacks;
	/* total # of completed invalidations */
	atomic_t bc_completed_invalidations;
	/* total # of cached device read requests */
	atomic_t bc_read_cached_device_requests;
	/* total # of cached device write requests */
	atomic_t bc_write_cached_device_requests;
	/* current # of pending cached device requests */
	atomic_t bc_pending_cached_device_requests;
	/* highest # of pending cached device requests */
	atomic_t bc_highest_pending_cached_device_requests;
	/* highest # of pending cached device requests */
	atomic_t bc_highest_pending_invalidate_requests;
	/* total read misses */
	atomic_t bc_total_read_misses;
	/* total read hits */
	atomic_t bc_total_read_hits;
	/* total write misses */
	atomic_t bc_total_write_misses;
	/* total write hits */
	atomic_t bc_total_write_hits;
	/* clean read hits */
	atomic_t bc_clean_read_hits;
	/* read misses */
	atomic_t bc_read_misses;
	/*!
	 * \todo having the distinction between clean hits and dirty hits
	 * is now almost completely irrelevant given write cloning is used
	 * anyway.
	 * keeping clean hits vs dirty hits is general is probably just very
	 * confusing given that cache operating mode will almost always be
	 * writeback. all in all, this distinction should be just removed,
	 * which would also simplify code.
	 *
	 * clean write hits
	 */
	atomic_t bc_clean_write_hits;
	/* clean write hits - partial page */
	atomic_t bc_clean_write_hits_rmw;
	/* clean write misses */
	atomic_t bc_clean_write_misses;
	/* clean write misses - partial page */
	atomic_t bc_clean_write_misses_rmw;
	/* dirty read hits */
	atomic_t bc_dirty_read_hits;
	/* dirty write hits */
	atomic_t bc_dirty_write_hits;
	/* dirty write hits - partial page (need to do full clone copy) */
	atomic_t bc_dirty_write_hits_rmw;
	/* dirty write misses */
	atomic_t bc_dirty_write_misses;
	/* dirty write misses - partial page */
	atomic_t bc_dirty_write_misses_rmw;
	/* read hits on busy block */
	atomic_t bc_read_hits_busy;
	/* write hits on busy block */
	atomic_t bc_write_hits_busy;
	/* read misses - all blocks busy */
	atomic_t bc_read_misses_busy;
	/* write misses - all blocks busy */
	atomic_t bc_write_misses_busy;
	/* total # of writebacks */
	atomic_t bc_writebacks;
	/* total # of writebacks to clean */
	atomic_t bc_writebacks_clean;
	/* total # of writebacks to invalid */
	atomic_t bc_writebacks_invalid;
	/* writeback stalls (dirty block busy) */
	atomic_t bc_writebacks_stalls;
	/* invalidations */
	atomic_t bc_invalidations;
	/* idle invalidations */
	atomic_t bc_idle_invalidations;
	/* busy invalidations */
	atomic_t bc_busy_invalidations;
	/* could not invalidate, all blocks busy */
	atomic_t bc_no_invalidations_all_blocks_busy;
	atomic_t bc_invalidations_map;
	atomic_t bc_invalidations_invalidator;
	atomic_t bc_invalidations_writeback;
	/* could not grab invalid block (busy) */
	atomic_t bc_invalid_blocks_busy;
	/* count of flush requests */
	atomic_t bc_flush_requests;
	/* count of pure flush requests */
	atomic_t bc_pure_flush_requests;
	/* count of discard requests */
	atomic_t bc_discard_requests;
	/* write clone allocation ok */
	/* WRITE_CLONE_FIXME_LATER */
	/* \todo rename to "bc_write_clone_alloc_ok once done w/ cloning */
	atomic_t bc_dirty_write_clone_alloc_ok;
	/* write clone allocation fail */
	/* WRITE_CLONE_FIXME_LATER */
	/* \todo rename to "bc_write_clone_alloc_ok once done w/ cloning */
	atomic_t bc_dirty_write_clone_alloc_fail;

	/* reads timer (pending i/o time only) */
	struct cache_timer bc_timer_reads;
	/* writes timer (pending i/o time only) */
	struct cache_timer bc_timer_writes;
	struct cache_timer bc_timer_read_hits;
	struct cache_timer bc_timer_write_hits;
	struct cache_timer bc_timer_read_misses;
	struct cache_timer bc_timer_write_misses;
	struct cache_timer bc_timer_write_dirty_misses;
	struct cache_timer bc_timer_write_clean_misses;
	struct cache_timer bc_timer_read_clean_hits;
	struct cache_timer bc_timer_write_clean_hits;
	struct cache_timer bc_timer_read_dirty_hits;
	struct cache_timer bc_timer_write_dirty_hits;
	struct cache_timer bc_timer_cached_device_reads;
	struct cache_timer bc_timer_cached_device_writes;
	struct cache_timer bc_timer_cached_device_flushes;
	struct cache_timer bc_timer_writebacks;
	struct cache_timer bc_timer_invalidations;
	struct cache_timer bc_timer_pending_queue;

	/*! resource allocation read timer */
	struct cache_timer bc_timer_resource_alloc_reads;
	/*! resource allocation write timer */
	struct cache_timer bc_timer_resource_alloc_writes;

	/*! sequential access tracker for reads */
	struct seq_io_bypass bc_seq_read;
	/*! sequential access tracker for writes */
	struct seq_io_bypass bc_seq_write;
	/*! bypass timeout handler workqueue */
	struct workqueue_struct *bc_seq_workqueue;
	/*! delayed work struct for seq_io bypass */
	struct delayed_work bc_seq_work;

	int bc_magic2;

	/*! PMEM state */
	struct pmem_api bc_papi;

	/*! PMEM header update workqueue */
	struct workqueue_struct *bc_pmem_update_workqueue;
	/*! delayed work struct for pmem header update workqueue */
	struct delayed_work bc_pmem_update_work;

	/*! runtime configurable option (enables extra hash checking) */
	int bc_enable_extra_checksum_check;

	int bc_magic3;

	/*!
	 * holds the transaction identifier.
	 * we rely on this to be unique. at some point we need to handle
	 * transaction id rollover (we can use the same scheme that tcp
	 * uses for sequeuence rollover).
	 */
	atomic64_t bc_xid;

	/*!
	 * absolute maximum total requests that can be in flight at any time.
	 * this includes the extra cache block needed for write cloning. in order
	 * to avoid severe performance degradation due to resource starvation,
	 * there needs to be a minimum amount of blocks that must be invalid
	 * in order to guarantee speedy forward progress.
	 *
	 * the function @ref can_schedule_map_request needs to used to flow control
	 * all explicit requests coming via DM map() which can potentially require the
	 * allocation of an extra cache block.
	 *
	 * if the above happens and the number of invalid free blocks is already below
	 * @ref bc_max_pending requests, then such requests will be starved because the new
	 * block can only be allocated if there is an available free block.
	 */
	volatile unsigned int bc_max_pending_requests;

	/*!
	 * each count of invalid, valid_clean and valid_dirty entries can never
	 * be negative. the only relationship that holds true is that in a
	 * qiuesced state the following holds true:
	 *
	 * bc_invalid_entries + bc_valid_entries_clean + bc_valid_entries_dirty
	 *                          ==
	 *                  bc_total_entries
	 *
	 * note however that because they are updated separately, one should
	 * never test the above equality until removal time when everything is
	 * quiesced
	 * (in practice the above counts are accurate within 1 or 2 units).
	 */
	atomic_t bc_invalid_entries;
	struct list_head bc_invalid_entries_list;
	/*!
	 * clean+dirty list contains all the valid and busy elements, and it's
	 * used for LRU/FIFO replacement
	 *
	 * invariant:   state != INVALID
	 */
	atomic_t bc_valid_entries;
	atomic_t bc_valid_entries_clean;
	atomic_t bc_valid_entries_dirty;
	/* clean + dirty */
	struct list_head bc_valid_entries_list;
	/* clean */
	struct list_head bc_valid_entries_clean_list;
	/* dirty */
	struct list_head bc_valid_entries_dirty_list;
	/*
	 * the spinlock serializes access to invalid, valid/busy lists
	 * and pending list. it also serializes access to the redblack tree.
	 * FIXME: should split the gloabl lock into finer grain locks.
	 * thus far we have not seen perf degradation because of this.
	 */
	/* total # of cache entries */
	atomic_t bc_total_entries;
	spinlock_t bc_entries_lock;
	/*!
	 * pending_requests list. All pending requests, read and writes,
	 * are in
	 */
	struct list_head bc_pending_requests_list;

	struct workqueue_struct *bc_make_request_wq;
	struct cache_timer bc_make_request_wq_timer;
	atomic_t bc_make_request_wq_count;
	atomic_t bc_make_request_count;

#ifdef ENABLE_TRACK_CRC32C
#define CACHE_TRACK_HASH_MAGIC0       UINT128_FROM_UINT(0xf10c6a4a)
#define CACHE_TRACK_HASH_MAGIC1       UINT128_FROM_UINT(0xf10c78cb)
#define CACHE_TRACK_HASH_MAGICN       UINT128_FROM_UINT(0xf10c5a4c)
#define CACHE_TRACK_HASH_MAGICN1      UINT128_FROM_UINT(0xf10c873d)
	/*
	 * array format
	 * array[0] = MAGIC0
	 * array[1] = MAGIC2
	 * array[N] = MAGICN
	 * array[N+1] = MAGICN1
	 */
	uint128_t *bc_tracked_hashes;
	/* cannot be larger than CACHE_MAX_TRACK_HASH_CHECKSUMS */
	size_t bc_tracked_hashes_num;
	/* goes from 0 to N-2 to make comparison easy */
	atomic_t bc_tracked_hashes_set;
	atomic_t bc_tracked_hashes_clear;
	atomic_t bc_tracked_hashes_null;
	atomic_t bc_tracked_hashes_ok;
	atomic_t bc_tracked_hashes_bad;
#endif /*ENABLE_TRACK_CRC32C */

	atomic_t bc_cache_transitions_counters[__TS_NUM];
	atomic_t bc_cache_states_counters[__CACHE_STATES_NUM];

	/*! synchronizes access to both deferred queues */
	spinlock_t defer_lock;
	/*! deferred queue, cases 1 and 2. see @ref (doxy_deferredqueues.md) */
	struct deferred_queue defer_busy;
	/*! deferred queue, cases 3 and 4. see @ref (doxy_deferredqueues.md) */
	struct deferred_queue defer_page;
	/*! deferred queue workqueue */
	struct workqueue_struct *defer_wq;
	struct work_struct defer_work;

	/*
	 * background writer kernel thread to writeback dirty blocks
	 */
	struct task_struct *bc_bgwriter_task;
	wait_queue_head_t bc_bgwriter_wait;
	unsigned int bc_bgwriter_no_work_count;
	unsigned int bc_bgwriter_work_count;
	/*
	 * bgwriter specific stats
	 */
	unsigned int bc_bgwriter_stalls_count;
	unsigned int bc_bgwriter_stalls_nowait_count;
	unsigned int bc_bgwriter_cache_block_busy_count;
	unsigned int bc_bgwriter_queue_full_count;
	unsigned int bc_bgwriter_too_young_count;
	unsigned int bc_bgwriter_ready_count;
	unsigned int bc_bgwriter_hint_block_clean_count;
	unsigned int bc_bgwriter_hint_no_block_count;
	/*
	 * internal bgwriter state, related to bgwriter writeback machine.
	 * and writeback parameters based on writeback policy.
	 *
	 * the policy plugin uses the general state of bittern
	 * (more specifically, the cache blocks dirty/valid/invalid counts,
	 * pending_requests and max_pending_pending requests)
	 * and the configuration parameters to determine the writeback
	 * parameters.
	 *
	 * the following are the writeback parameters which are calculated by
	 * the writeback policies.
	 */
	unsigned int bc_bgwriter_curr_queue_depth;
	unsigned int bc_bgwriter_curr_max_queue_depth;
	unsigned int bc_bgwriter_curr_target_pct;
	unsigned int bc_bgwriter_curr_rate_per_sec;
	unsigned int bc_bgwriter_curr_min_age_secs;
	/*
	 * writeback block count for a specific writeback cycle, and sum
	 * of said writeback block count.
	 * also the cluster counts and sums.
	 */
	unsigned int bc_bgwriter_curr_block_count;
	unsigned int bc_bgwriter_curr_block_count_sum;
	unsigned int bc_bgwriter_curr_cluster_count;
	unsigned int bc_bgwriter_curr_cluster_count_sum;
	unsigned int bc_bgwriter_curr_msecs_elapsed_start_io;
	unsigned int bc_bgwriter_curr_msecs_slept_start_io;
	/*
	 * policy specific internal state.
	 * each policy engine uses these as it sees git.
	 * this state is reset to all zeros when policy is changed.
	 */
	unsigned long bc_bgwriter_curr_policy[8];
	/*
	 * bgwriter policy.
	 * if the default is not modified, the writeback behaviour is
	 * exactly the same as the previous fixed behavior
	 * (standard / default).
	 * conf_policy is the policy configured via bc_control,
	 * active_policy is the currently applied policy. this
	 * mechanism allows for a lock-free policy update by bgwriter
	 * when it is safe to do so.
	 */
	volatile unsigned int bc_bgwriter_conf_policy;
	volatile unsigned int bc_bgwriter_active_policy;
	/*
	 * flush dirty blocks on exit.
	 * used by writeback policy
	 */
	volatile unsigned int bc_bgwriter_conf_flush_on_exit;
	/*
	 * cluster size
	 */
	volatile unsigned int bc_bgwriter_conf_cluster_size;
	/*
	 * how greedy bgwriter is in flushing, default 0.
	 * minimum -10 (least greedy)
	 * maximum +20 (very very greedy)
	 * used by writeback policy
	 */
	volatile int bc_bgwriter_conf_greedyness;
	/*
	 * maximum queue depth percentage which we'll dedicate to writebacks
	 * used by writeback policy
	 */
	volatile unsigned int bc_bgwriter_conf_max_queue_depth_pct;

	unsigned long bc_bgwriter_loop_count;

	/*
	 * invalidator kernel thread
	 */
	struct task_struct *bc_invalidator_task;
	wait_queue_head_t bc_invalidator_wait;
	unsigned int bc_invalidator_no_work_count;
	unsigned int bc_invalidator_work_count;
	/*
	 * config variable.
	 * tells how many invalid blocks the invalidator thread needs to
	 * maintain.
	 */
	volatile unsigned int bc_invalidator_conf_min_invalid_count;

	/*! array of in-memory cache block metadata */
	struct cache_block *bc_cache_blocks;

	/*! red-black tree index for metadata */
	struct rb_root bc_rb_root;
	uint64_t bc_rb_hit_loop_sum;
	uint64_t bc_rb_miss_loop_sum;
	uint64_t bc_rb_hit_loop_count;
	uint64_t bc_rb_miss_loop_count;
	uint64_t bc_rb_hit_loop_max;
	uint64_t bc_rb_miss_loop_max;

	/*! target info */
	struct dm_target *bc_ti;

	/*!
	 *
	 * This struct implements the devio layer which sits between Bittern
	 * and the cached device itself @ref bc_dev
	 *
	 * It implements the abstraction of stable writes, that is writes which
	 * are guaranteed to be stable on the storage media, that is, written
	 * from the device's hardware cache (if any) to the storage media, the
	 * spinning platter in the case of HDD.
	 * This abstraction is needed by the rest of Bittern, as Bittern
	 * disk model is one of in which disk writes are always guaranteed to
	 * be on stable storage (that is, it assumes a good ol' disk without
	 * the write buffer).
	 *
	 * Basically, write requests are kept pending until a later
	 * REQ_FUA|REQ_FLUSH completes.
	 *
	 * Read requests are passed through directly to the cached device.
	 * Write request are kept in two different (mutually exclusive) queues,
	 * @ref pending_queue and @ref flush_pending_queue. Pending writes are
	 * first held in pending_queue. Upon write completion the request is
	 * moved to @ref flush_pending_queue, and kept there until a later flush
	 * request is acknowledged.
	 *
	 * That is to say, the devio layer buffers write operations and only
	 * acknowledge them to Bittern once they are guaranteed stable.
	 *
	 * Say we have these requests issued:
	 *
	 * W1         gen=1
	 * W2         gen=2
	 * F3         gen=3
	 * W4         gen=4
	 *
	 * For the sake of example, consider this possible timeline:
	 *
	 * time  event               pend_queue  flush_pend_queue  acked
	 * -------------------------------------------------------------
	 * 0     write W1 issued     W1
	 * 1     write W2 issued     W1 W2
	 * 2     write W1 completes  W2          W1
	 * 3     flush F3 issued     W2          W1
	 * 4     write W3 issued     W2 W3       W1
	 * 5     write W3 completes  W2          W1 W3
	 * 6     write W2 completes  W1 W2 W3
	 * 7     flush F3 completes  W3                            W1 W2
	 *
	 * When F3 completes, all previously completed write requests which are
	 * waiting for a flush will be acknowledged. So it follows that in the
	 * above case W1 and W3 will be acknowledged, but W3 cannot because it
	 * was issued after F3.
	 *
	 */
	struct devio {
		/*! device being cached */
		struct dm_dev *dm_dev;

		/*!  serializes access to bio and flush pending lists */
		spinlock_t spinlock;
		/*!
		 * List of all pending requests issed to @ref bc_dev.
		 * When a read or write request is issued, the work_item is
		 * inserted into this list.
		 * When IO completes, the request is either acked immediately
		 * or moved to the @ref flush_pending_list.
		 * A request can only be in one of these two queues.
		 */
		struct list_head pending_list;
		/*! counts elements in @ref bio_pending */
		int pending_count;
		/*! list of all pending FLUSH requests issed to @ref bc_dev */
		struct list_head flush_pending_list;
		/*! counts elements in @ref flush_pending */
		int flush_pending_count;
		/*! counts number of pending pure flushes */
		int pure_flush_pending_count;
		/*! count of flushes */
		uint64_t flush_total_count;
		/*! count of pure flushes */
		uint64_t pure_flush_total_count;
		/*!
		 * Generation number used to associate requests which
		 * have been issued before a given generation number.
		 * \todo handle rollover
		 */
		uint64_t gennum;
		/*!
		 * Gennum of last flush which was issued.
		 * (@ref gennum_flush - ref @gennum) tells
		 * how many write requests were issued after the last flush
		 * was issued.
		 * \todo handle rollover
		 */
		uint64_t gennum_flush;
		/*! workqueue used to issue explicit flushes */
		struct workqueue_struct *flush_wq;
		/*! work struct for explicit flushes */
		struct delayed_work flush_delayed_work;
		/*! conf param - how often the delayed worker runs */
		int conf_worker_delay;
		/*! conf param - how often FUA is inserted in the write stream */
		int conf_fua_insert;
	} devio;

	/*! device acting as the cache */
	struct dm_dev *bc_cache_dev;

	int bc_verifier_running;
	struct task_struct *bc_verifier_task;
	wait_queue_head_t bc_verifier_wait;
	int bc_verifier_blocks_verified;
	int bc_verifier_blocks_not_verified_dirty;
	int bc_verifier_blocks_not_verified_busy;
	int bc_verifier_blocks_not_verified_invalid;
	int bc_verifier_one_shot;
	int bc_verifier_scans;
	unsigned long bc_verifier_scan_started;
	unsigned long bc_verifier_scan_last_block;
	unsigned long bc_verifier_scan_completed;
	int bc_verifier_scan_delay_ms;
	int bc_verifier_verify_errors;
	int bc_verifier_verify_errors_cumulative;
	int bc_verifier_bug_on_verify_errors;

	/*! slab used at init time and DM map entry point */
	struct kmem_cache *bc_kmem_map;
	/*! slab used by bgwriter and invalidator threads */
	struct kmem_cache *bc_kmem_threads;

	int bc_magic4;
};

/*!
 * returns true if a dm map() request can be queued into the state machine.
 * needs a minimum number of free blocks, which is the sum of
 * @ref bc_max_pending_requests and @ref INVALIDATOR_MIN_INVALID_COUNT.
 * it's possible to get away with the maximum of the two quantities, but it's better
 * to be a bit conservative.
 * please also see @ref bittern_cache::bc_max_pending_requests.
 */
static inline bool can_schedule_map_request(struct bittern_cache *bc)
{
	bool avail, can_queue;

#if 0
	unsigned int avail_reserved;
	avail_reserved = bc->bc_max_pending_requests;
	avail = atomic_read(&bc->bc_invalid_entries) > avail_reserved;
#else
	avail = atomic_read(&bc->bc_invalid_entries) > 0;
#endif
	can_queue = atomic_read(&bc->bc_pending_requests) < bc->bc_max_pending_requests;
	return avail && can_queue;
}

static inline bool is_cache_mode_writeback(struct bittern_cache *bc)
{
	ASSERT(bc->bc_cache_mode_writeback == 0 ||
	       bc->bc_cache_mode_writeback == 1);
	return bc->bc_cache_mode_writeback != 0;
}
static inline bool is_cache_mode_writethru(struct bittern_cache *bc)
{
	return !is_cache_mode_writeback(bc);
}
/*! \todo move out from include and into a .c file (_subr.c ?) */
static inline const char *cache_mode_to_str(struct bittern_cache *bc)
{
	if (is_cache_mode_writeback(bc))
		return "writeback";
	else
		return "writethrough";
}

#define ASSERT_CACHE_STATE(__bcb) ({                                                  \
	/* make sure it's an l-value. compiler will optimize this away */                     \
	__bcb = (__bcb);                                                                      \
	ASSERT((__bcb) != NULL);                                                              \
	ASSERT(CACHE_STATE_VALID((__bcb)->bcb_state));                                \
})

#define ASSERT_CACHE_TRANSITION_VALID(__bcb) ({				\
	/* make sure it's l-value. compiler will optimize this away */	\
	__bcb = (__bcb);						\
	ASSERT((__bcb) != NULL);					\
	ASSERT(CACHE_TRANSITION_VALID((__bcb)->bcb_cache_transition));	\
})

#define __ASSERT_CACHE_BLOCK(__bcb, __bc) ({				\
	/* make sure it's l-value. compiler will optimize this away */	\
	__bcb = (__bcb);						\
	/* make sure it's l-value. compiler will optimize this away */	\
	__bc = (__bc);                                                  \
	ASSERT((__bc) != NULL);                                         \
	ASSERT((__bcb) != NULL);                                        \
	ASSERT((__bcb)->bcb_magic1 == BCB_MAGIC1);                      \
	ASSERT((__bcb)->bcb_magic3 == BCB_MAGIC3);                      \
	ASSERT(atomic_read(&(__bcb)->bcb_refcount) >= 0);               \
	ASSERT_CACHE_STATE(__bcb);                                      \
	ASSERT_CACHE_TRANSITION_VALID(__bcb);                           \
	/* block_id starts from 1, array starts from 0 */               \
	ASSERT(&(__bc)->bc_cache_blocks[(__bcb)->bcb_block_id - 1] ==	\
	       (__bcb));						\
})

#define ASSERT_CACHE_BLOCK(__bcb, __bc) ({                                            \
	/* this makes sure __bcb is an l-value -- compiler will optimize this out */          \
	__bcb = (__bcb);                                                                      \
	/* this makes sure __bc is an l-value -- compiler will optimize this out */           \
	__bc = (__bc);                                                                        \
	ASSERT((__bc) != NULL);                                                               \
	ASSERT((__bcb) != NULL);                                                              \
	ASSERT((__bcb) >= &(__bc)->bc_cache_blocks[0] &&                                      \
		(__bcb) < &(__bc)->bc_cache_blocks[atomic_read(&(__bc)->bc_total_entries)]);  \
	ASSERT((__bcb)->bcb_block_id >= 1 &&                                                  \
		(__bcb)->bcb_block_id <= atomic_read(&(__bc)->bc_total_entries));             \
	__ASSERT_CACHE_BLOCK(__bcb, __bc);                                            \
	ASSERT((__bcb) != NULL);                                                              \
})

#define __ASSERT_BITTERN_CACHE(__bc) ({					\
	__bc = (__bc);							\
	ASSERT((__bc) != NULL);						\
	ASSERT((__bc)->bc_magic1 == BC_MAGIC1);				\
	ASSERT((__bc)->bc_magic2 == BC_MAGIC2);				\
	ASSERT((__bc)->bc_magic3 == BC_MAGIC3);				\
	ASSERT((__bc)->bc_magic4 == BC_MAGIC4);				\
	ASSERT((__bc)->bc_ti != NULL);					\
	ASSERT((__bc)->devio.dm_dev != NULL);				\
	ASSERT_CACHE_REPLACEMENT_MODE((__bc)->bc_replacement_mode);	\
	ASSERT((__bc)->bc_cache_blocks != NULL);			\
	ASSERT((__bc)->bc_cache_mode_writeback == 0 ||			\
	       (__bc)->bc_cache_mode_writeback == 1);			\
	ASSERT((__bc)->bc_enable_req_fua == false ||			\
	       (__bc)->bc_enable_req_fua == true);			\
	ASSERT((__bc)->error_state == ES_NOERROR ||			\
	       (__bc)->error_state == ES_ERROR_FAIL_ALL ||		\
	       (__bc)->error_state == ES_ERROR_FAIL_READS);		\
})

#define ASSERT_BITTERN_CACHE(__bc) ({                                                               \
	__ASSERT_BITTERN_CACHE(__bc);                                                               \
	ASSERT(atomic_read(&(__bc)->bc_total_entries) == (__bc)->bc_papi.papi_hdr.lm_cache_blocks); \
})

extern int seq_bypass_initialize(struct bittern_cache *bc);
extern void seq_bypass_deinitialize(struct bittern_cache *bc);
extern void seq_bypass_start_workqueue(struct bittern_cache *bc);
extern void seq_bypass_stop_workqueue(struct bittern_cache *bc);
extern int seq_bypass_is_sequential(struct bittern_cache *bc, struct bio *bio);
extern int seq_bypass_stats(struct bittern_cache *bc,
			    char *result,
			    size_t maxlen);

int set_read_bypass_enabled(struct bittern_cache *bc, int value);
int read_bypass_enabled(struct bittern_cache *bc);
int set_read_bypass_threshold(struct bittern_cache *bc, int value);
int read_bypass_threshold(struct bittern_cache *bc);
int set_read_bypass_timeout(struct bittern_cache *bc, int value);
int read_bypass_timeout(struct bittern_cache *bc);
int set_write_bypass_enabled(struct bittern_cache *bc, int value);
int write_bypass_enabled(struct bittern_cache *bc);
int set_write_bypass_threshold(struct bittern_cache *bc, int value);
int write_bypass_threshold(struct bittern_cache *bc);
int set_write_bypass_timeout(struct bittern_cache *bc, int value);
int write_bypass_timeout(struct bittern_cache *bc);

static inline void cache_xid_set(struct bittern_cache *bc,
				 uint64_t new_xid)
{
	atomic64_set(&bc->bc_xid, new_xid);
}

static inline uint64_t cache_xid_inc(struct bittern_cache *bc)
{
	return atomic64_inc_return(&bc->bc_xid);
}

static inline uint64_t cache_xid_get(struct bittern_cache *bc)
{
	return atomic64_read(&bc->bc_xid);
}

/*
 * red-black tree operations
 *
 * these functions do not grab any spinlock, caller is responsible for that
 */
extern struct cache_block *cache_rb_lookup(struct bittern_cache *bc,
					   sector_t sector);
extern void cache_rb_insert(struct bittern_cache *bc,
			    struct cache_block *cache_block);
extern void cache_rb_remove(struct bittern_cache *bc,
			    struct cache_block *cache_block);
extern struct cache_block *cache_rb_first(struct bittern_cache *bc);
extern struct cache_block *cache_rb_next(struct bittern_cache *bc,
					 struct cache_block *cache_block);
extern struct cache_block *cache_rb_prev(struct bittern_cache *bc,
					 struct cache_block *cache_block);
extern struct cache_block *cache_rb_last(struct bittern_cache *bc);

#include "bittern_cache_main.h"

extern int cache_bgwriter_kthread(void *__bc);
extern int cache_invalidator_kthread(void *__bc);
extern int cache_invalidator_has_work_schmitt(struct bittern_cache *bc);

/*! worker used to issue explicit flushes */
extern void cache_deferred_worker(struct work_struct *work);

/*! return bgwriter current policy name */
extern const char *cache_bgwriter_policy(struct bittern_cache *bc);
extern ssize_t cache_bgwriter_op_show_policy(struct bittern_cache *bc,
					     char *result);
extern int cache_bgwriter_policy_set(struct bittern_cache *bc,
				     const char *buf);
extern void cache_bgwriter_policy_init(struct bittern_cache *bc);

extern void cache_bgwriter_flush_dirty_blocks(struct bittern_cache *bc);
extern void cache_bgwriter_compute_policy_slow(struct bittern_cache *bc);
extern void cache_bgwriter_compute_policy_fast(struct bittern_cache *bc);

/*! the main DM entry point for bittern */
extern int bittern_cache_map(struct dm_target *ti, struct bio *bio);

extern int cache_block_verifier_kthread(void *bc);
extern void cache_invalidate_clean_block(struct bittern_cache *bc,
					 struct cache_block *cache_block);
extern void cache_invalidate_blocks(struct bittern_cache *bc);
/*! \todo should this be in cache_getput? */
extern void cache_invalidate_block_io_end(struct bittern_cache *bc,
					  struct work_item *wi,
					  struct cache_block *cache_block);
/*! \todo should this be in cache_getput? */
extern void cache_invalidate_block_io_start(struct bittern_cache *bc,
					    struct cache_block *cache_block);

/*! worker used to issue explicit flushes */
extern void cached_devio_flush_delayed_worker(struct work_struct *work);
/*! queue request to devio layer */
extern void cached_devio_make_request(struct bittern_cache *bc,
				      struct work_item *wi,
				      struct bio *bio);

extern void cache_zero_stats(struct bittern_cache *bc);
extern int cache_dump_blocks(struct bittern_cache *bc,
			     const char *dump_op,
			     unsigned int dump_offset);
extern void cache_walk(struct bittern_cache *bc);

#ifdef ENABLE_KMALLOC_DEBUG
extern void *kmem_allocate(size_t size, int flags, int zero);
#define kmem_alloc(__size, __flags) kmem_allocate((__size), (__flags), 0)
#define kmem_zalloc(__size, __flags) kmem_allocate((__size), (__flags), 1)
extern void kmem_free(void *buf, size_t size);
extern unsigned int kmem_buffers_in_use(void);
#else /*ENABLE_KMALLOC_DEBUG */
#define kmem_alloc(__size, __flags) kmalloc((__size), (__flags))
#define kmem_zalloc(__size, __flags) kzalloc((__size), (__flags))
#define kmem_free(__buf, __size) kfree((__buf))
#define kmem_buffers_in_use() (0)
#endif /*ENABLE_KMALLOC_DEBUG */

#define BT_LEVEL_ERROR 0
#define BT_LEVEL_WARN 1
#define BT_LEVEL_INFO 2
#define BT_LEVEL_TRACE0         3
#define BT_LEVEL_TRACE1         4
#define BT_LEVEL_TRACE2         5
#define BT_LEVEL_TRACE3         6
#define BT_LEVEL_TRACE4         7
#define BT_LEVEL_TRACE5         8

#define BT_LEVEL_MIN BT_LEVEL_TRACE0
#define BT_LEVEL_MAX BT_LEVEL_TRACE5

extern int cache_trace_level;
#define __CACHE_TRACE_LEVEL() ((cache_trace_level) & 0x0f)
#define __CACHE_DEV_TRACE_LEVEL() ((cache_trace_level >> 8) & 0x0f)

extern void
__printf(9, 10) cache_trace(int level,
			    struct bittern_cache *bc,
			    struct work_item *wi,
			    struct cache_block *cache_block,
			    struct bio *original_bio,
			    struct bio *cloned_bio,
			    const char *file_name, int line,
			    const char *fmt, ...);

#ifdef DISABLE_BT_TRACE
#define BT_TRACE(__level, __bc, __wi, __cache_block, __original_bio, __cloned_bio, __printf_args...) (void)0
#else /*DISABLE_BT_TRACE */
#define BT_TRACE(__level, __bc, __wi, __cache_block, __original_bio, __cloned_bio, __printf_args...) \
({                                                                                              \
	if ((__level) <= __CACHE_TRACE_LEVEL()) {                                       \
		cache_trace((__level),                                                  \
		(__bc), (__wi), (__cache_block), (__original_bio), (__cloned_bio),              \
		__func__,                                                                       \
		__LINE__,                                                                       \
		__printf_args);                                                                 \
	}                                                                                       \
})
#endif /*DISABLE_BT_TRACE */

#ifdef DISABLE_BT_DEV_TRACE
#define BT_DEV_TRACE(__level, __bc, __wi, __cache_block, __original_bio, __cloned_bio, __printf_args...) (void)0
#else /*DISABLE_BT_DEV_TRACE */
#define BT_DEV_TRACE(__level, __bc, __wi, __cache_block, __original_bio, __cloned_bio, __printf_args...) \
({                                                                                              \
	if ((__level) <= __CACHE_DEV_TRACE_LEVEL()) {                                   \
		cache_trace((__level),                                                  \
		(__bc), (__wi), (__cache_block), (__original_bio), (__cloned_bio),              \
		__func__,                                                                       \
		__LINE__,                                                                       \
		__printf_args);                                                                 \
	}                                                                                       \
})
#endif /*DISABLE_BT_DEV_TRACE */

/* __xop accessors */
#define __xop_null(__x_value) (__x_value)
#define __xop_atomic_read(__x_value) atomic_read(&(__x_value))
/* gives integer percentage of __part over __total */
#define __T_PCT__XOP(__total, __part, __xop) ({		\
		uint64_t total = __xop(__total);	\
		uint64_t part = __xop(__part);		\
		uint64_t r = 0;				\
		if (total) {				\
			r = (part * 100) / (total);	\
		}					\
		r;					\
})
/* gives first two digits after decimal of percentage of __part over __total */
#define __T_PCT_F100__XOP(__total, __part, __xop) ({\
		uint64_t total = __xop(__total);		\
		uint64_t part = __xop(__part);			\
		uint64_t r = 0;					\
		if (total) {					\
			r = (part * 100 * 100) / (total);	\
			r = r % 100;                            \
		}						\
		r;						\
})
/* gives integer percentage of __b over (__a + __b) */
#define __S_PCT__XOP(__a, __b, __xop) ({                \
		uint64_t a = __xop(__a);                \
		uint64_t b = __xop(__b);                \
		uint64_t r = 0;                         \
		if (a + b) {                            \
			r = (b * 100) / (a + b);        \
		}                                       \
		r;                                      \
})
/* gives first two digits after decimal of percentage of __b over (__a + __b) */
#define __S_PCT_F100__XOP(__a, __b, __xop) ({           \
		uint64_t a = __xop(__a);                \
		uint64_t b = __xop(__b);                \
		uint64_t r = 0;                         \
		if (a + b) {                            \
			r = (b * 100 * 100) / (a + b);  \
			r = r % 100;                    \
		}                                       \
		r;                                      \
})

/* gives integer percentage of __part over __total */
#define T_PCT__ATOMIC_READ(__total, __part) __T_PCT__XOP(__total, __part, __xop_atomic_read)
/* gives first two digits after decimal of percentage of __part over __total */
#define T_PCT_F100__ATOMIC_READ(__total, __part) __T_PCT_F100__XOP(__total, __part, __xop_atomic_read)
/* gives integer percentage of __b over (__a + __b) */
#define S_PCT__ATOMIC_READ(__a, __b) __S_PCT__XOP(__a, __b, __xop_atomic_read)
/* gives first two digits after decimal of percentage of __b over (__a + __b) */
#define S_PCT_F100__ATOMIC_READ(__a, __b)        __S_PCT__XOP(__a, __b, __xop_atomic_read)

/* gives integer percentage of __part over __total */
#define T_PCT(__total, __part) __T_PCT__XOP(__total, __part, __xop_null)
/* gives first two digits after decimal of percentage of __part over __total */
#define T_PCT_F100(__total, __part)              __T_PCT_F100__XOP(__total, __part, __xop_null)
/* gives integer percentage of __b over (__a + __b) */
#define S_PCT(__a, __b) __S_PCT__XOP(__a, __b, __xop_null)
/* gives first two digits after decimal of percentage of __b over (__a + __b) */
#define S_PCT_F100(__a, __b)                     __S_PCT__XOP(__a, __b, __xop_null)

#ifdef ENABLE_TRACK_CRC32C

extern void cache_track_hash_set(struct bittern_cache *bc,
				 struct cache_block *cache_block,
				 uint128_t hash_value);
extern void cache_track_hash_check(struct bittern_cache *bc,
				   struct cache_block *cache_block,
				   uint128_t hash_value);
extern void cache_track_hash_check_buffer(struct bittern_cache *bc,
					  struct cache_block *cache_block,
					  void *buffer);
extern void cache_track_hash_clear(struct bittern_cache *bc,
				   unsigned long sector);

#else /*ENABLE_TRACK_CRC32C */

#define cache_track_hash_set(__bc, __cache_block, __value) (void)0
#define cache_track_hash_check(__bc, __cache_block, __value) (void)0
#define cache_track_hash_check_buffer(__bc, __cache_block, __buffer) (void)0
#define cache_track_hash_clear(__bc, __sector) (void)0

#endif /*ENABLE_TRACK_CRC32C */

#endif /* BITTERN_CACHE_H */
