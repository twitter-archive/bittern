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

#ifndef BITTERN_CACHE_TUNABLES_H
#define BITTERN_CACHE_TUNABLES_H

/* creating a cache smaller thanm this will make all hell break loose */
#define __CACHE_SIZE_ABSOLUTE_MIN (32 * 1024ULL * 1024ULL)
#define __CACHE_SIZE_ABSOLUTE_MAX (200 * 1024ULL * 1024ULL * 1024ULL)
#define __CACHE_SIZE_DEFAULT (4 * 1024ULL * 1024ULL * 1024ULL)

/* #define BT_LEVEL_DEFAULT        BT_LEVEL_TRACE0 */
/* #define BT_LEVEL_DEFAULT        BT_LEVEL_TRACE1 */
/* #define BT_LEVEL_DEFAULT        BT_LEVEL_TRACE2 */
/* #define BT_LEVEL_DEFAULT        BT_LEVEL_TRACE3 */
/* #define BT_LEVEL_DEFAULT        BT_LEVEL_TRACE4 */
/* #define BT_LEVEL_DEFAULT        (BT_LEVEL_TRACE0 | (BT_LEVEL_TRACE0 << 8)) */
/* #define BT_LEVEL_DEFAULT        (BT_LEVEL_TRACE1 | (BT_LEVEL_TRACE0 << 8)) */
/* #define BT_LEVEL_DEFAULT        (BT_LEVEL_TRACE2 | (BT_LEVEL_TRACE0 << 8)) */
/* #define BT_LEVEL_DEFAULT        (BT_LEVEL_TRACE3 | (BT_LEVEL_TRACE0 << 8)) */
/* #define BT_LEVEL_DEFAULT        (BT_LEVEL_TRACE4 | (BT_LEVEL_TRACE0 << 8)) */

#define BT_LEVEL_DEFAULT_0            (BT_LEVEL_TRACE0 | (BT_LEVEL_TRACE0 << 8))
#define BT_LEVEL_DEFAULT_1            (BT_LEVEL_TRACE1 | (BT_LEVEL_TRACE0 << 8))
#define BT_LEVEL_DEFAULT_2            (BT_LEVEL_TRACE2 | (BT_LEVEL_TRACE0 << 8))
#define BT_LEVEL_DEFAULT_2_1          (BT_LEVEL_TRACE2 | (BT_LEVEL_TRACE1 << 8))
#define BT_LEVEL_DEFAULT_2_2          (BT_LEVEL_TRACE2 | (BT_LEVEL_TRACE2 << 8))

#if !defined(BT_LEVEL_DEFAULT)
#define BT_LEVEL_DEFAULT BT_LEVEL_DEFAULT_0
#endif /* !defined(BT_LEVEL_DEFAULT) */

#if !defined(PRINTK_DEBUG_DEFAULT)
#define PRINTK_DEBUG_DEFAULT KERN_DEBUG
#endif /* !defined(PRINTK_DEBUG_DEFAULT) */

/* millisecond delay between one full scan and the next */
#define CACHE_VERIFIER_PAUSE_DELAY_MS 10000

/* millisecond delay between one cache block scan and the next (min) */
#define CACHE_VERIFIER_BLOCK_SCAN_DELAY_MIN_MS 0

/* millisecond delay between one cache block scan and the next (max) */
#define CACHE_VERIFIER_BLOCK_SCAN_DELAY_MAX_MS 100

/* millisecond delay between one cache block scan and the next (default) */
#define CACHE_VERIFIER_BLOCK_SCAN_DELAY_DEFAULT_MS 10

/*! How many sequential IO streams we keep track */
#define SEQ_IO_TRACK_DEPTH		32
/*!
 * Minimum disk sectors threshold after which we consider the IO stream
 * to be sequential.
 */
#define SEQ_IO_THRESHOLD_COUNT_MIN		KBYTES_TO_SECTORS(64)
/*!
 * Default disk sectors threshold after which we consider the IO stream
 * to be sequential read.
 * The page cache already filters out most reads, and so Bittern gets
 * the 1st level cache misses. Because of this, and also because
 * Bittern is really tuned towards caching writes, tuning of this parameter
 * is probably not very critical.
 */
#define SEQ_IO_THRESHOLD_COUNT_READ_DEFAULT	KBYTES_TO_SECTORS(128)
/*!
 * Default disk sectors threshold after which we consider the IO stream
 * to be sequential write.
 * Unlike sequential reads, tuning of this parameter is critical because
 * the writeback cache absorbs write bursts for later asynchronous writeback.
 * It's quite likely that this is heavily dependent on the workload.
 *
 * For the sake of example, consider this scenario: we populate a file system
 * with a large SQL database (thus, very large sequential writes), followed
 * by a sysbench benchmark.
 *
 * During the sequential write phase, the average streams length are as follows:
 *
 * bitcache0: sequential: write_s_streams_len_avg=105315
 *                        write_ns_streams_len_avg=1
 *
 * During the sysbench phase, the average streams length are as follows:
 *
 * bitcache0: sequential: write_s_streams_len_avg=3573
 *                        write_ns_streams_len_avg=28
 *
 * Note how the average sequential stream length changes from about 50 Mbytes
 * down to about 1.5 Mbytes. The average non-sequential stream length stays
 * fairly low in both scenarios.
 *
 * This mixed scenario is a fairly extreme case, but if we want to optimize
 * for this, we would want the sysbench phase to avoid doing too many write
 * bypasses, so we set the sequential write threshold somewhere
 * between 1.5 Mbytes and 50 Mbytes, and hope that it will work correctly
 * for most workloads.
 */
#define SEQ_IO_THRESHOLD_COUNT_WRITE_DEFAULT	KBYTES_TO_SECTORS(8000)
/*!
 * Maximum disk sectors threshold after which we consider the IO stream
 * to be sequential.
 */
#define SEQ_IO_THRESHOLD_COUNT_MAX		KBYTES_TO_SECTORS(128000)

/*! how long before we timeout an idle sequential stream (min) */
#define SEQ_IO_TIMEOUT_MIN_MS		100
/*! how long before we timeout an idle sequential stream (default) */
#define SEQ_IO_TIMEOUT_DEFAULT_MS	5000
/*! how long before we timeout an idle sequential stream (max) */
#define SEQ_IO_TIMEOUT_MAX_MS		180000
/*!
 * Sequential IO bypass enabled default
 */
#define SEQ_IO_BYPASS_ENABLED_DEFAULT	1

/*
 * random replacement
 */
#define CACHE_REPLACEMENT_MODE_RANDOM_MAX_SCANS 100

/*
 * maximum pending requests - we will never have more than max pending requests
 * queued the max is the lowest of the two numbers below
 */
#define CACHE_MAX_PENDING_REQUESTS_MIN 10
#define CACHE_MAX_PENDING_REQUESTS_DEFAULT 500
#define CACHE_MAX_PENDING_REQUESTS_MAX 2000

/* expressed as percentage of max pending requests */
#define CACHE_BGWRITER_MIN_QUEUE_DEPTH_PCT 5

/* expressed as percentage of max pending requests */
#define CACHE_BGWRITER_MAX_QUEUE_DEPTH_PCT 1000

/* expressed as percentage of max pending requests */
#define CACHE_BGWRITER_DEFAULT_QUEUE_DEPTH_PCT 200

/*!
 * default cluster size amount, that is, the number of blocks
 * which the bgwriter will try to write out in a sequential batch.
 * the exact number is not hugely important right now, but eventually
 * we should have the starting block to cluster-size aligned.
 * once we do that we'll be optimized to write out to RAID5 arrays
 * with the same stripe size (just make the raid array stripe size the
 * same as (CACHE_BGWRITER_CLUSTER_SIZE * PAGE_SIZE)
 *
 * cluster size of 1 essentially turns off sequential batching.
 */
#define CACHE_BGWRITER_MIN_CLUSTER_SIZE 1 /* no clustering */
#define CACHE_BGWRITER_DEFAULT_CLUSTER_SIZE 64 /* 256 kbytes */
#define CACHE_BGWRITER_MAX_CLUSTER_SIZE 512 /* 2048 mbytes */

/*! bgwriter policy */
#define CACHE_BGWRITER_DEFAULT_POLICY	"dirty-ratio"
/* #define CACHE_BGWRITER_DEFAULT_POLICY	"classic" */

/*!
 * Lowest minimum number of invalid blocks allowed.
 * Note that the invalidator thread also adds @ref bc_max_pending_requests to this value
 * to avoid invalid block starvation due to setting @ref bc_max_pending_requests too high.
 */
#define INVALIDATOR_MIN_INVALID_COUNT 10
#define INVALIDATOR_DEFAULT_INVALID_COUNT 1000
#define INVALIDATOR_MAX_INVALID_COUNT 2000

/*
 * we need a minimal amount of allocatable page pool buffers.
 * this is because page pools are used during initialization
 * when the max buffer pages may not be calculated yet (that is
 * because it's calculated over max_pending_requests which is calculated
 * later.
 * so the minimum number needs to be plenty for initialization. right now
 * the initialization code uses about 2 buffers, but later we'll have to account
 * for multi-threaded code to restore cache content in parallel.
 */
#define CACHE_PGPOOL_MIN_BUFFERS 256

/*
 * background threads priorities
 */
#define S_INVALIDATOR_THREAD_NICE -20
#define CACHE_BACKGROUND_WRITER_THREAD_NICE -19
#define CACHE_DEFERRED_IO_THREAD_NICE -18
#define CACHE_DAEMON_THREAD_NICE -10
#define CACHE_VERIFIER_THREAD_NICE -10

/*
 * max number of cached device crc32c checksums we want to track
 * - with 1 megabyte of this array we can track 1 gigabyte of cached disk device
 *   crc32c checksums so with 100 megabytes we track 100 gbytes
 */
#define CACHE_MAX_TRACK_HASH_CHECKSUMS                (1000UL * 1024UL * 1024UL)

/*! how often dev worker runs */
#define CACHED_DEV_WORKER_DELAY_MIN 1
#define CACHED_DEV_WORKER_DELAY_DEFAULT 1
#define CACHED_DEV_WORKER_DELAY_MAX 100

/*! how often FUA is inserted in the write stream */
#define CACHED_DEV_FUA_INSERT_MIN 10
#define CACHED_DEV_FUA_INSERT_DEFAULT 500
#define CACHED_DEV_FUA_INSERT_MAX 5000

#endif /* BITTERN_CACHE_TUNABLES_H */
