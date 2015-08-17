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
#include "bittern_cache_module.h"

/*!
 * these rules are meant to allow to external scripts and commands
 * to easily parse most of the /sys/fs/ entries with the same code.
 *
 * the general format of all readable /sys/fs/entries is as follows:
 *
 * <cachename>: <sysfsentry>: <var1> <var2> <var3>
 * <cachename>: <sysfsentry>: <var1> <var2> <var3>
 * <cachename>: <sysfsentry>: <var1> <var2> <var3>
 *
 * the format is multiline and var is almost always further subdivided as:
 *
 * varname=varvalue
 *
 * both variable names and values cannot have any tab, blank or other
 * white spaces.
 * as a general rule, variables never get renamed. however, the order
 * in which they appear can change, along with the number of lines. so
 * never hardcode for a specific position or line number.
 *
 *
 * example:
 *
 * <stats entry>
 * bitcache0: stats: io_xid=339264 read_requests=97 write_requests=278159
 * bitcache0: stats: read+write_requests=278256 deferred_requests=0
 * bitcache0: stats: pending_writeback_requests=0 total_entries=252057
 *
 * <info entry>
 * bitcache0: info: version=0.20.2a.graysharbor
 *                  build_timestamp=2015-02-11-12:09:23 max_io_len_pages=1
 *                  memcpy_nt_type=intel-sse-nti-x64
 * bitcache0: info: cache_name=bitcache0 cache_device_name=/dev/pmem_ram0
 *                  cached_device_name=/dev/mapper/vg_hdd-lvol0
 * bitcache0: info: pmem_provider_version=0.9.1 cache_device_type=pmem_ram
 *                  cached_device_size_bytes=20401094656
 *                  cached_device_size_mbytes=19456 cache_entries=16127
 *                  mcb_size_bytes=64 cache_kaddr=ffffc90011f41000
 *                  cache_paddr=0x0 requested_cache_size=67108864
 *                  allocated_cache_size=67108864 in_use_cache_size=67100672
 * bitcache0: info: replacement_mode=repl-random cache_mode=writeback
 */

#define T_FMT_STRING(__name) \
	__name "=%llu.%03llu/%llu.%03llu(%llu%s)"
#define T_FMT_STRING_SUFFIX(__name) \
	__name "_%u=%llu.%03llu/%llu.%03llu(%llu%s)"
#define T_FMT_ARGS(__bc, __bc_timer) \
	((__bc)->__bc_timer.bct_avg_nsec / 1000),       \
	((__bc)->__bc_timer.bct_avg_nsec % 1000),       \
	((__bc)->__bc_timer.bct_max_nsec / 1000),       \
	((__bc)->__bc_timer.bct_max_nsec % 1000),       \
	(__bc)->__bc_timer.bct_count,                   \
	((__bc)->__bc_timer.bct_timewarp != 0 ? "*" : "")
#define T_FMT_ARGS_SUFFIX(__bc, __suffix, __bc_timer) \
	(__suffix),                                     \
	((__bc)->__bc_timer.bct_avg_nsec / 1000),       \
	((__bc)->__bc_timer.bct_avg_nsec % 1000),       \
	((__bc)->__bc_timer.bct_max_nsec / 1000),       \
	((__bc)->__bc_timer.bct_max_nsec % 1000),       \
	(__bc)->__bc_timer.bct_count,                   \
	((__bc)->__bc_timer.bct_timewarp != 0 ? "*" : "")
#define T_PCT_FMT_STRING(__x_name) \
	__x_name "=%u(%llu.%02llu%%) "
#define T_PCT_ARGS(__x_total, __x_part) \
	atomic_read(&__x_part),                         \
	T_PCT__ATOMIC_READ(__x_total, __x_part),        \
	T_PCT_F100__ATOMIC_READ(__x_total, __x_part)
#define S_PCT_FMT_STRING(__x_name) \
	__x_name "=%u(%llu.%02llu%%) "
#define S_PCT_ARGS(__x_a, __x_b) \
	atomic_read(&__x_b),                            \
	S_PCT__ATOMIC_READ(__x_a, __x_b),               \
	S_PCT_F100__ATOMIC_READ(__x_a, __x_b)
#define KT_FMT_STRING \
	"%s: 0x%llx"
#define KT_FMT_ARGS(__bc, __bc_nickname, __bc_task) \
	__bc_nickname,                                  \
	(uint64_t)(__bc)->__bc_task                     \

/*!
 * data type for @ref cache_conf_param_entry
 */
enum conf_param_type {
	CONF_TYPE_INT = 'L',
	CONF_TYPE_STR = 'S',
	UNDEF = -1,
};

/*!
 * This struct is used to keep a list of runtime configurable parameters.
 * The data type is described by @ref conf_param_type.
 * Note that we also use this list for "control" commands, that is runtime
 * commands which do not have a state associated to it. Because they don't
 * have a state, they only have the setter func without the corresponding
 * getter func.
 */
struct cache_conf_param_entry {
	/*! param name or control name */
	const char cache_conf_name[40];
	/*! param type */
	enum conf_param_type cache_conf_type;
	/*! for int param type, the allowed minimum value */
	int cache_conf_min;
	/*! for int param type, the allowed maximum value */
	int cache_conf_max;
	/*! setter function for int type */
	int (*cache_conf_setup_function)(struct bittern_cache *bc, int value);
	/*! getter function for int type - can be NULL for control ops */
	int (*cache_conf_show_function)(struct bittern_cache *bc);
	/*! setter function for string type */
	int (*cache_conf_setup_function_str)(struct bittern_cache *bc,
					     const char *value);
	/*! getter function for string type - can be NULL for control ops */
	const char *(*cache_conf_show_function_str)(struct bittern_cache *bc);
};

static int show_max_pending(struct bittern_cache *bc)
{
	return bc->bc_max_pending_requests;
}

static int set_bgwriter_conf_flush_on_exit(struct bittern_cache *bc, int value)
{
	bc->bc_bgwriter_conf_flush_on_exit = value;
	return 0;
}

static int show_bgwriter_conf_flush_on_exit(struct bittern_cache *bc)
{
	return bc->bc_bgwriter_conf_flush_on_exit;
}

static int set_bgwriter_conf_greedyness(struct bittern_cache *bc, int value)
{
	bc->bc_bgwriter_conf_greedyness = value;
	return 0;
}

static int show_bgwriter_conf_greedyness(struct bittern_cache *bc)
{
	return bc->bc_bgwriter_conf_greedyness;
}

static int set_bgwriter_max_queue_depth_pct(struct bittern_cache *bc, int value)
{
	bc->bc_bgwriter_conf_max_queue_depth_pct = value;
	return 0;
}

static int show_bgwriter_max_queue_depth_pct(struct bittern_cache *bc)
{
	return bc->bc_bgwriter_conf_max_queue_depth_pct;
}

static int set_bgwriter_conf_cluster_size(struct bittern_cache *bc, int value)
{
	bc->bc_bgwriter_conf_cluster_size = value;
	return 0;
}

static int show_bgwriter_conf_cluster_size(struct bittern_cache *bc)
{
	return bc->bc_bgwriter_conf_cluster_size;
}

static int cache_set_enable_extra_checksum(struct bittern_cache *bc, int value)
{
#if !defined(ENABLE_TRACK_CRC32C)
	bc->bc_enable_extra_checksum_check = value;
#endif /* !defined(ENABLE_TRACK_CRC32C) */
	return 0;

}

static int show_cache_enable_extra_checksum(struct bittern_cache *bc)
{
#if !defined(ENABLE_TRACK_CRC32C)
	return bc->bc_enable_extra_checksum_check;
#endif /* !defined(ENABLE_TRACK_CRC32C) */
	return 0;
}

static int show_cache_min_invalid(struct bittern_cache *bc)
{
	return bc->bc_invalidator_conf_min_invalid_count;
}

static int param_set_trace(struct bittern_cache *bc, int value)
{
#if !defined(DISABLE_BT_TRACE)
	printk_debug("curr value=0x%x, new value=0x%x, trace_level=0x%x, dev_trace_level=0x%x\n",
		     cache_trace_level,
		     value,
		     __CACHE_TRACE_LEVEL(),
		     __CACHE_DEV_TRACE_LEVEL());
	cache_trace_level = value & 0xffff;
#endif /*!defined(DISABLE_BT_TRACE) */
	return 0;
}

static int param_show_trace(struct bittern_cache *bc)
{
	return cache_trace_level;
}

static int param_set_cache_mode(struct bittern_cache *bc, const char *value)
{
	ASSERT(bc->bc_cache_mode_writeback == 0 ||
	       bc->bc_cache_mode_writeback == 1);
	if (strcmp(value, "writeback") == 0) {
		printk_debug("cache_mode set to writeback\n");
		bc->bc_cache_mode_writeback = 1;
		return 0;
	}
	if (strcmp(value, "writethrough") == 0) {
		printk_debug("cache_mode set to writethrough\n");
		bc->bc_cache_mode_writeback = 0;
		return 0;
	}
	printk_err("unknown cache_mode value '%s'\n", value);
	return -EINVAL;
}

static const char *param_show_cache_mode(struct bittern_cache *bc)
{
	return cache_mode_to_str(bc);
}

static int param_set_replacement_mode(struct bittern_cache *bc,
				      const char *value)
{
	if (strcmp(value, "lru") == 0) {
		bc->bc_replacement_mode = CACHE_REPLACEMENT_MODE_LRU;
		return 0;
	}
	if (strcmp(value, "fifo") == 0) {
		bc->bc_replacement_mode = CACHE_REPLACEMENT_MODE_FIFO;
		return 0;
	}
	if (strcmp(value, "random") == 0) {
		bc->bc_replacement_mode = CACHE_REPLACEMENT_MODE_RANDOM;
		return 0;
	}
	printk_err("unknown replacement_mode value '%s'\n", value);
	return -EINVAL;
}

static const char *param_show_replacement_mode(struct bittern_cache *bc)
{
	return cache_replacement_mode_to_str(bc->bc_replacement_mode);
}

static int param_set_dev_worker_delay(struct bittern_cache *bc, int value)
{
	if (value == 0)
		value = CACHED_DEV_WORKER_DELAY_DEFAULT;
	bc->devio.conf_worker_delay = value;
	return 0;
}

static int param_get_dev_worker_delay(struct bittern_cache *bc)
{
	return (int)bc->devio.conf_worker_delay;
}

static int param_set_dev_fua_insert(struct bittern_cache *bc, int value)
{
	if (value == 0)
		value = CACHED_DEV_FUA_INSERT_DEFAULT;
	bc->devio.conf_fua_insert = value;
	return 0;
}

static int param_get_dev_fua_insert(struct bittern_cache *bc)
{
	return (int)bc->devio.conf_fua_insert;
}

static int param_set_verifier_running(struct bittern_cache *bc, int value)
{
	ASSERT(value == 0 || value == 1);
	ASSERT(bc->bc_verifier_task != NULL);
	bc->bc_verifier_running = value;
	wake_up_interruptible(&bc->bc_verifier_wait);
	return 0;
}

static int param_get_verifier_running(struct bittern_cache *bc)
{
	return bc->bc_verifier_running;
}

static int param_set_verifier_one_shot(struct bittern_cache *bc, int value)
{
	ASSERT(value == 0 || value == 1);
	ASSERT(bc->bc_verifier_task != NULL);
	bc->bc_verifier_one_shot = value;
	wake_up_interruptible(&bc->bc_verifier_wait);
	return 0;
}

static int param_get_verifier_one_shot(struct bittern_cache *bc)
{
	return bc->bc_verifier_one_shot;
}

static int param_set_verifier_scan_delay_ms(struct bittern_cache *bc, int value)
{
	ASSERT(value >= CACHE_VERIFIER_BLOCK_SCAN_DELAY_MIN_MS);
	ASSERT(value <= CACHE_VERIFIER_BLOCK_SCAN_DELAY_MAX_MS);
	ASSERT(bc->bc_verifier_task != NULL);
	bc->bc_verifier_scan_delay_ms = value;
	wake_up_interruptible(&bc->bc_verifier_wait);
	return 0;
}

static int param_get_verifier_scan_delay_ms(struct bittern_cache *bc)
{
	return bc->bc_verifier_scan_delay_ms;
}

static int param_set_verifier_bugon_on_errors(struct bittern_cache *bc,
					      int value)
{
	ASSERT(value == 0 || value == 1);
	ASSERT(bc->bc_verifier_task != NULL);
	bc->bc_verifier_bug_on_verify_errors = value;
	wake_up_interruptible(&bc->bc_verifier_wait);
	return 0;
}

static int param_get_verifier_bugon_on_errors(struct bittern_cache *bc)
{
	return bc->bc_verifier_bug_on_verify_errors;
}

static int control_invalidate_cache(struct bittern_cache *bc, int value)
{
	cache_invalidate_blocks(bc);
	return 0;
}

static int control_zero_stats(struct bittern_cache *bc, int value)
{
	cache_zero_stats(bc);
	return 0;
}

static int control_tree_walk(struct bittern_cache *bc, int value)
{
	cache_walk(bc);
	return 0;
}

/*! only allow setting of error state. do not allow reset. */
static int param_set_error_state(struct bittern_cache *bc, int value)
{
	M_ASSERT(value == ES_ERROR_FAIL_ALL);
	bc->error_state = value;
	printk_info("%s: set error_state=%d\n",
		    bc->bc_name,
		    bc->error_state);
	return 0;
}

static int param_get_error_state(struct bittern_cache *bc)
{
	return (int)bc->error_state;
}

static int control_dump_blocks_clean(struct bittern_cache *bc, int value)
{
	return cache_dump_blocks(bc, "clean", value);
}

static int control_dump_blocks_dirty(struct bittern_cache *bc, int value)
{
	return cache_dump_blocks(bc, "dirty", value);
}

static int control_dump_blocks_busy(struct bittern_cache *bc, int value)
{
	return cache_dump_blocks(bc, "busy", value);
}

static int control_dump_blocks_pending(struct bittern_cache *bc, int value)
{
	return cache_dump_blocks(bc, "pending", value);
}

static int control_dump_blocks_deferred(struct bittern_cache *bc, int value)
{
	return cache_dump_blocks(bc, "deferred", value);
}

static int control_dump_blocks_deferred_busy(struct bittern_cache *bc,
					     int value)
{
	return cache_dump_blocks(bc, "deferred_wait_busy", value);
}

static int control_dump_blocks_deferred_page(struct bittern_cache *bc,
					     int value)
{
	return cache_dump_blocks(bc, "deferred_wait_page", value);
}

struct cache_conf_param_entry cache_conf_param_list[] = {
	/*
	 * max pending requests
	 */
	{
		.cache_conf_name = "max_pending_requests",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = CACHE_MAX_PENDING_REQUESTS_MIN,
		.cache_conf_max = CACHE_MAX_PENDING_REQUESTS_MAX,
		.cache_conf_setup_function = cache_calculate_max_pending,
		.cache_conf_show_function = show_max_pending,
	},
	/*
	 * bgwriter params
	 */
	{
		.cache_conf_name = "bgwriter_conf_flush_on_exit",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0,
		.cache_conf_max = 1,
		.cache_conf_setup_function = set_bgwriter_conf_flush_on_exit,
		.cache_conf_show_function = show_bgwriter_conf_flush_on_exit,
	},
	{
		.cache_conf_name = "bgwriter_conf_greedyness",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = -20,
		.cache_conf_max = 20,
		.cache_conf_setup_function = set_bgwriter_conf_greedyness,
		.cache_conf_show_function = show_bgwriter_conf_greedyness,
	},
	{
		.cache_conf_name = "bgwriter_conf_max_queue_depth_pct",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = CACHE_BGWRITER_MIN_QUEUE_DEPTH_PCT,
		.cache_conf_max = CACHE_BGWRITER_MAX_QUEUE_DEPTH_PCT,
		.cache_conf_setup_function = set_bgwriter_max_queue_depth_pct,
		.cache_conf_show_function = show_bgwriter_max_queue_depth_pct,
	},
	{
		.cache_conf_name = "bgwriter_conf_cluster_size",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = CACHE_BGWRITER_MIN_CLUSTER_SIZE,
		.cache_conf_max = CACHE_BGWRITER_MAX_CLUSTER_SIZE,
		.cache_conf_setup_function = set_bgwriter_conf_cluster_size,
		.cache_conf_show_function = show_bgwriter_conf_cluster_size,
	},
	{
		.cache_conf_name = "bgwriter_conf_policy",
		.cache_conf_type = CONF_TYPE_STR,
		.cache_conf_setup_function_str = cache_bgwriter_policy_set,
		.cache_conf_show_function_str = cache_bgwriter_policy,
	},
	/*
	 * invalidator min invalid count
	 */
	{
		.cache_conf_name = "invalidator_conf_min_invalid_count",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = INVALIDATOR_MIN_INVALID_COUNT,
		.cache_conf_max = INVALIDATOR_MAX_INVALID_COUNT,
		.cache_conf_setup_function = cache_calculate_min_invalid,
		.cache_conf_show_function = show_cache_min_invalid,
	},
	/*
	 * extra checksum check
	 */
	{
		.cache_conf_name = "enable_extra_checksum_check",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0,
		.cache_conf_max = 1,
		.cache_conf_setup_function = cache_set_enable_extra_checksum,
		.cache_conf_show_function = show_cache_enable_extra_checksum,
	},
	/*
	 * sequential read bypass parameters
	 */
	{
		.cache_conf_name = "read_bypass_enabled",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0,
		.cache_conf_max = 1,
		.cache_conf_setup_function = set_read_bypass_enabled,
		.cache_conf_show_function = read_bypass_enabled,
	},
	{
		.cache_conf_name = "read_bypass_threshold",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = SEQ_IO_THRESHOLD_COUNT_MIN,
		.cache_conf_max =  SEQ_IO_THRESHOLD_COUNT_MAX,
		.cache_conf_setup_function = set_read_bypass_threshold,
		.cache_conf_show_function = read_bypass_threshold,
	},
	{
		.cache_conf_name = "read_bypass_timeout",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = SEQ_IO_TIMEOUT_MIN_MS,
		.cache_conf_max = SEQ_IO_TIMEOUT_MAX_MS,
		.cache_conf_setup_function = set_read_bypass_timeout,
		.cache_conf_show_function = read_bypass_timeout,
	},
	/*
	 * sequential write bypass parameters
	 */
	{
		.cache_conf_name = "write_bypass_enabled",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0,
		.cache_conf_max = 1,
		.cache_conf_setup_function = set_write_bypass_enabled,
		.cache_conf_show_function = write_bypass_enabled,
	},
	{
		.cache_conf_name = "write_bypass_threshold",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = SEQ_IO_THRESHOLD_COUNT_MIN,
		.cache_conf_max =  SEQ_IO_THRESHOLD_COUNT_MAX,
		.cache_conf_setup_function = set_write_bypass_threshold,
		.cache_conf_show_function = write_bypass_threshold,
	},
	{
		.cache_conf_name = "write_bypass_timeout",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = SEQ_IO_TIMEOUT_MIN_MS,
		.cache_conf_max = SEQ_IO_TIMEOUT_MAX_MS,
		.cache_conf_setup_function = set_write_bypass_timeout,
		.cache_conf_show_function = write_bypass_timeout,
	},
	/*
	 * tracemask
	 */
	{
		.cache_conf_name = "trace",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0x0000,
		.cache_conf_max = 0x0f0f,
		.cache_conf_setup_function = param_set_trace,
		.cache_conf_show_function = param_show_trace,
	},
	/*
	 * cache_mode
	 */
	{
		.cache_conf_name = "cache_mode",
		.cache_conf_type = CONF_TYPE_STR,
		.cache_conf_setup_function_str = param_set_cache_mode,
		.cache_conf_show_function_str = param_show_cache_mode,
	},
	/*
	 * replacement
	 */
	{
		.cache_conf_name = "replacement",
		.cache_conf_type = CONF_TYPE_STR,
		.cache_conf_setup_function_str = param_set_replacement_mode,
		.cache_conf_show_function_str = param_show_replacement_mode,
	},
	/*
	 * devio_worker_delay
	 */
	{
		.cache_conf_name = "devio_worker_delay",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = CACHED_DEV_WORKER_DELAY_MIN,
		.cache_conf_max = CACHED_DEV_WORKER_DELAY_MAX,
		.cache_conf_setup_function = param_set_dev_worker_delay,
		.cache_conf_show_function = param_get_dev_worker_delay,
	},
	/*
	 * devio_fua_insert
	 */
	{
		.cache_conf_name = "devio_fua_insert",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = CACHED_DEV_FUA_INSERT_MIN,
		.cache_conf_max = CACHED_DEV_FUA_INSERT_MAX,
		.cache_conf_setup_function = param_set_dev_fua_insert,
		.cache_conf_show_function = param_get_dev_fua_insert,
	},
	/*
	 * verifier params
	 */
	{
		.cache_conf_name = "verifier_running",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0,
		.cache_conf_max = 1,
		.cache_conf_setup_function = param_set_verifier_running,
		.cache_conf_show_function = param_get_verifier_running,
	},
	{
		.cache_conf_name = "verifier_one_shot",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0,
		.cache_conf_max = 1,
		.cache_conf_setup_function = param_set_verifier_one_shot,
		.cache_conf_show_function = param_get_verifier_one_shot,
	},
	{
		.cache_conf_name = "verifier_scan_delay_ms",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = CACHE_VERIFIER_BLOCK_SCAN_DELAY_MIN_MS,
		.cache_conf_max = CACHE_VERIFIER_BLOCK_SCAN_DELAY_MAX_MS,
		.cache_conf_setup_function = param_set_verifier_scan_delay_ms,
		.cache_conf_show_function = param_get_verifier_scan_delay_ms,
	},
	{
		.cache_conf_name = "verifier_bugon_on_errors",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0,
		.cache_conf_max = 1,
		.cache_conf_setup_function = param_set_verifier_bugon_on_errors,
		.cache_conf_show_function = param_get_verifier_bugon_on_errors,
	},
	/*
	 * error state
	 */
	{
		.cache_conf_name = "error_state",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = ES_ERROR_FAIL_ALL,
		.cache_conf_max = ES_ERROR_FAIL_ALL,
		.cache_conf_setup_function = param_set_error_state,
		.cache_conf_show_function = param_get_error_state,
	},
	/*
	 * control function -- invalidate cache blocks.
	 */
	{
		.cache_conf_name = "invalidate_cache",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0,
		.cache_conf_max = 0,
		.cache_conf_setup_function = control_invalidate_cache,
	},
	/*
	 * control function -- zero stats (not really implemented).
	 */
	{
		.cache_conf_name = "zero_stats",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0,
		.cache_conf_max = 0,
		.cache_conf_setup_function = control_zero_stats,
	},
	/*
	 * control function -- tree walk.
	 */
	{
		.cache_conf_name = "tree_walk",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0,
		.cache_conf_max = 0,
		.cache_conf_setup_function = control_tree_walk,
	},
	/*
	 * control functions -- dump functions.
	 */
	{
		.cache_conf_name = "dump_blocks_clean",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0,
		.cache_conf_max = 0x7fffffff,
		.cache_conf_setup_function = control_dump_blocks_clean,
	},
	{
		.cache_conf_name = "dump_blocks_dirty",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0,
		.cache_conf_max = 0x7fffffff,
		.cache_conf_setup_function = control_dump_blocks_dirty,
	},
	{
		.cache_conf_name = "dump_blocks_busy",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0,
		.cache_conf_max = 0x7fffffff,
		.cache_conf_setup_function = control_dump_blocks_busy,
	},
	{
		.cache_conf_name = "dump_blocks_pending",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0,
		.cache_conf_max = 0x7fffffff,
		.cache_conf_setup_function = control_dump_blocks_pending,
	},
	{
		.cache_conf_name = "dump_blocks_deferred",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0,
		.cache_conf_max = 0x7fffffff,
		.cache_conf_setup_function = control_dump_blocks_deferred,
	},
	{
		.cache_conf_name = "dump_blocks_deferred_busy",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0,
		.cache_conf_max = 0x7fffffff,
		.cache_conf_setup_function = control_dump_blocks_deferred_busy,
	},
	{
		.cache_conf_name = "dump_blocks_deferred_page",
		.cache_conf_type = CONF_TYPE_INT,
		.cache_conf_min = 0,
		.cache_conf_max = 0x7fffffff,
		.cache_conf_setup_function = control_dump_blocks_deferred_page,
	},
};

struct cache_conf_param_entry *cache_get_conf(const char *param_name)
{
	int i;
	struct cache_conf_param_entry *e;

	for (i = 0; i < ARRAY_SIZE(cache_conf_param_list); i++) {
		e = &cache_conf_param_list[i];
		ASSERT(e->cache_conf_type == CONF_TYPE_STR ||
		       e->cache_conf_type == CONF_TYPE_INT);
		/*
		 * We don't care about strcasecmp(),
		 * but every other DM message handler uses strcasecmp(),
		 * so we keep with tradition.
		 */
		if (strcasecmp(param_name, e->cache_conf_name) == 0)
			return e;
	}

	return NULL;
}

ssize_t cache_op_show_conf(struct bittern_cache *bc, char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;
	int i;

	for (i = 0; i < ARRAY_SIZE(cache_conf_param_list); i++) {
		struct cache_conf_param_entry *e = &cache_conf_param_list[i];

		ASSERT(e->cache_conf_type == CONF_TYPE_STR ||
		       e->cache_conf_type == CONF_TYPE_INT);

		/*
		 * The param list is now too long to be shown in a single line.
		 */
		if (e->cache_conf_type == CONF_TYPE_INT &&
		    e->cache_conf_show_function != NULL)
			DMEMIT("%s: conf: %s=%d\n",
			       bc->bc_name,
			       e->cache_conf_name,
			       (*e->cache_conf_show_function)(bc));
		else if (e->cache_conf_type == CONF_TYPE_STR &&
			 e->cache_conf_show_function_str != NULL)
			DMEMIT("%s: conf: %s=%s\n",
			       bc->bc_name,
			       e->cache_conf_name,
			       (*e->cache_conf_show_function_str)(bc));
	}
	return sz;
}

ssize_t cache_op_show_stats_extra(struct bittern_cache *bc,
					  char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;

	DMEMIT("%s: stats_extra: "
	       "total_entries=%u "
	       "deferred_requests=%u "
	       "highest_deferred_requests=%u "
	       "total_deferred_requests=%u "
	       "pending_requests=%u "
	       "pending_read_requests=%u "
	       "pending_read_bypass_requests=%u "
	       "pending_write_requests=%u "
	       "pending_write_bypass_requests=%u "
	       "pending_writeback_requests=%u "
	       "pending_invalidate_requests=%u "
	       "highest_pending_requests=%u "
	       "highest_pending_invalidate_requests=%u "
	       "pending_cached_device_requests=%u "
	       "highest_pending_cached_device_requests=%u "
	       "\n",
	       bc->bc_name,
	       atomic_read(&bc->bc_total_entries),
	       atomic_read(&bc->bc_deferred_requests),
	       atomic_read(&bc->bc_highest_deferred_requests),
	       atomic_read(&bc->bc_total_deferred_requests),
	       atomic_read(&bc->bc_pending_requests),
	       atomic_read(&bc->bc_pending_read_requests),
	       atomic_read(&bc->bc_pending_read_bypass_requests),
	       atomic_read(&bc->bc_pending_write_requests),
	       atomic_read(&bc->bc_pending_write_bypass_requests),
	       atomic_read(&bc->bc_pending_writeback_requests),
	       atomic_read(&bc->bc_pending_invalidate_requests),
	       atomic_read(&bc->bc_highest_pending_requests),
	       atomic_read(&bc->bc_highest_pending_invalidate_requests),
	       atomic_read(&bc->bc_pending_cached_device_requests),
	       atomic_read(&bc->bc_highest_pending_cached_device_requests));
	DMEMIT("%s: stats_extra: completed_requests=%u completed_read_requests=%u completed_write_requests=%u completed_writebacks=%u completed_invalidations=%u\n",
	       bc->bc_name,
	       atomic_read(&bc->bc_completed_requests),
	       atomic_read(&bc->bc_completed_read_requests),
	       atomic_read(&bc->bc_completed_write_requests),
	       atomic_read(&bc->bc_completed_writebacks),
	       atomic_read(&bc->bc_completed_invalidations));
	DMEMIT("%s: stats_extra: total_read_misses=%u total_read_hits=%u total_write_misses=%u total_write_hits=%u read_hits_busy=%u write_hits_busy=%u read_misses_busy=%u write_misses_busy=%u\n",
	       bc->bc_name,
	       atomic_read(&bc->bc_total_read_misses),
	       atomic_read(&bc->bc_total_read_hits),
	       atomic_read(&bc->bc_total_write_misses),
	       atomic_read(&bc->bc_total_write_hits),
	       atomic_read(&bc->bc_read_hits_busy),
	       atomic_read(&bc->bc_write_hits_busy),
	       atomic_read(&bc->bc_read_misses_busy),
	       atomic_read(&bc->bc_write_misses_busy));
	DMEMIT("%s: stats_extra: "
	       "writebacks=%u writebacks_stalls=%u "
	       "writebacks_clean=%u writebacks_invalid=%u "
	       "\n",
	       bc->bc_name,
	       atomic_read(&bc->bc_writebacks),
	       atomic_read(&bc->bc_writebacks_stalls),
	       atomic_read(&bc->bc_writebacks_clean),
	       atomic_read(&bc->bc_writebacks_invalid));
	DMEMIT("%s: stats_extra: "
	       "invalidations=%u idle_invalidations=%u busy_invalidations=%u "
	       "invalidations_map=%u "
	       "invalidations_invalidator=%u "
	       "invalidations_writeback=%u "
	       "no_invalidations_all_blocks_busy=%u "
	       "invalid_blocks_busy=%u "
	       "\n",
	       bc->bc_name,
	       atomic_read(&bc->bc_invalidations),
	       atomic_read(&bc->bc_idle_invalidations),
	       atomic_read(&bc->bc_busy_invalidations),
	       atomic_read(&bc->bc_invalidations_map),
	       atomic_read(&bc->bc_invalidations_invalidator),
	       atomic_read(&bc->bc_invalidations_writeback),
	       atomic_read(&bc->bc_no_invalidations_all_blocks_busy),
	       atomic_read(&bc->bc_invalid_blocks_busy));
	DMEMIT("%s: stats_extra: "
	       "flush_requests=%u pure_flush_requests=%u "
	       "discard_requests=%u "
	       "\n",
	       bc->bc_name,
	       atomic_read(&bc->bc_flush_requests),
	       atomic_read(&bc->bc_pure_flush_requests),
	       atomic_read(&bc->bc_discard_requests));
	DMEMIT("%s: stats_extra: "
	       "read_sequential_bypass_count=%u "
	       "read_sequential_io_count=%u "
	       "read_non_sequential_io_count=%u "
	       "read_sequential_bypass_hit=%u "
	       "\n",
	       bc->bc_name,
	       atomic_read(&bc->bc_seq_read.bypass_count),
	       atomic_read(&bc->bc_seq_read.seq_io_count),
	       atomic_read(&bc->bc_seq_read.non_seq_io_count),
	       atomic_read(&bc->bc_seq_read.bypass_hit));
	DMEMIT("%s: stats_extra: "
	       "write_sequential_bypass_count=%u "
	       "write_sequential_io_count=%u "
	       "write_non_sequential_io_count=%u "
	       "write_sequential_bypass_hit=%u "
	       "\n",
	       bc->bc_name,
	       atomic_read(&bc->bc_seq_write.bypass_count),
	       atomic_read(&bc->bc_seq_write.seq_io_count),
	       atomic_read(&bc->bc_seq_write.non_seq_io_count),
	       atomic_read(&bc->bc_seq_write.bypass_hit));
	DMEMIT("%s: stats_extra: "
	       "dirty_write_clone_alloc_ok=%u dirty_write_clone_alloc_fail=%u "
	       "list_empty_pending=%u "
	       "\n",
	       bc->bc_name,
	       atomic_read(&bc->bc_dirty_write_clone_alloc_ok),
	       atomic_read(&bc->bc_dirty_write_clone_alloc_fail),
	       list_empty(&bc->bc_pending_requests_list));
	DMEMIT("%s: stats_extra: make_request_count=%u make_request_wq_count=%u\n",
	       bc->bc_name,
	       atomic_read(&bc->bc_make_request_count),
	       atomic_read(&bc->bc_make_request_wq_count));
	DMEMIT("%s: stats_extra: dev_pending_count=%d dev_flush_pending_count=%d dev_pure_flush_pending_count=%d\n",
	       bc->bc_name,
	       bc->devio.pending_count,
	       bc->devio.flush_pending_count,
	       bc->devio.pure_flush_pending_count);
	DMEMIT("%s: stats_extra: dev_pure_flush_total_count=%llu dev_flush_flush_total_count=%llu\n",
	       bc->bc_name,
	       bc->devio.pure_flush_total_count,
	       bc->devio.flush_total_count);
	DMEMIT("%s: stats_extra: dev_gennum=%llu dev_gennum_flush=%llu\n",
	       bc->bc_name,
	       bc->devio.gennum,
	       bc->devio.gennum_flush);
	DMEMIT("%s: stats_extra: defer_busy_curr_count=%u defer_busy_requeue_count=%u defer_busy_max_count=%u\n",
	       bc->bc_name,
	       bc->defer_busy.curr_count,
	       bc->defer_busy.requeue_count,
	       bc->defer_busy.max_count);
	DMEMIT("%s: stats_extra: defer_busy_work_count=%u defer_busy_no_work_count=%u " T_FMT_STRING("defer_busy_timer") "\n",
	       bc->bc_name,
	       bc->defer_busy.work_count,
	       bc->defer_busy.no_work_count,
	       T_FMT_ARGS(bc, defer_busy.timer));
	DMEMIT("%s: stats_extra: defer_page_curr_count=%u defer_page_requeue_count=%u defer_page_max_count=%u\n",
	       bc->bc_name,
	       bc->defer_page.curr_count,
	       bc->defer_page.requeue_count,
	       bc->defer_page.max_count);
	DMEMIT("%s: stats_extra: defer_page_work_count=%u defer_page_no_work_count=%u " T_FMT_STRING("defer_page_timer") "\n",
	       bc->bc_name,
	       bc->defer_page.work_count,
	       bc->defer_page.no_work_count,
	       T_FMT_ARGS(bc, defer_page.timer));
	return sz;
}

ssize_t cache_op_show_stats(struct bittern_cache *bc, char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;

	DMEMIT("%s: stats: "
	       "io_xid=%llu "
	       "read_requests=%u write_requests=%u read+write_requests=%u "
	       "deferred_requests=%u "
	       "pending_requests=%u "
	       "pending_writeback_requests=%u "
	       "total_entries=%u"
	       "\n",
	       bc->bc_name,
	       cache_xid_get(bc),
	       atomic_read(&bc->bc_read_requests),
	       atomic_read(&bc->bc_write_requests),
	       (atomic_read(&bc->bc_read_requests) +
		atomic_read(&bc->bc_write_requests)),
	       atomic_read(&bc->bc_deferred_requests),
	       atomic_read(&bc->bc_pending_requests),
	       atomic_read(&bc->bc_pending_writeback_requests),
	       atomic_read(&bc->bc_total_entries));
	DMEMIT("%s: stats: " T_PCT_FMT_STRING("valid_cache_entries")
	       T_PCT_FMT_STRING("valid_dirty_cache_entries")
	       T_PCT_FMT_STRING("valid_clean_cache_entries")
	       T_PCT_FMT_STRING("invalid_cache_entries")
	       T_PCT_FMT_STRING("clean_read_hits_pct")
	       T_PCT_FMT_STRING("clean_write_hits_pct")
	       "\n",
	       bc->bc_name,
	       T_PCT_ARGS(bc->bc_total_entries, bc->bc_valid_entries),
	       T_PCT_ARGS(bc->bc_total_entries, bc->bc_valid_entries_dirty),
	       T_PCT_ARGS(bc->bc_total_entries, bc->bc_valid_entries_clean),
	       T_PCT_ARGS(bc->bc_total_entries, bc->bc_invalid_entries),
	       T_PCT_ARGS(bc->bc_read_requests, bc->bc_clean_read_hits),
	       T_PCT_ARGS(bc->bc_write_requests, bc->bc_clean_write_hits));
	DMEMIT("%s: stats: "
	       "read_misses=%u "
	       "clean_write_misses=%u "
	       "clean_write_misses_rmw=%u "
	       "clean_write_hits_rmw=%u " T_PCT_FMT_STRING("dirty_read_hits")
	       T_PCT_FMT_STRING("dirty_write_hits")
	       "dirty_write_misses=%u "
	       "dirty_write_misses_rmw=%u "
	       "dirty_write_hits_rmw=%u "
	       "writebacks=%u "
	       "invalidations=%u "
	       "\n",
	       bc->bc_name,
	       atomic_read(&bc->bc_read_misses),
	       atomic_read(&bc->bc_clean_write_hits),
	       atomic_read(&bc->bc_clean_write_misses_rmw),
	       atomic_read(&bc->bc_clean_write_hits_rmw),
	       T_PCT_ARGS(bc->bc_read_requests, bc->bc_dirty_read_hits),
	       T_PCT_ARGS(bc->bc_write_requests, bc->bc_dirty_write_hits),
	       atomic_read(&bc->bc_dirty_write_misses),
	       atomic_read(&bc->bc_dirty_write_misses_rmw),
	       atomic_read(&bc->bc_dirty_write_hits_rmw),
	       atomic_read(&bc->bc_writebacks),
	       atomic_read(&bc->bc_invalidations));
	DMEMIT("%s: stats: "
	       "read_cached_device_requests=%u "
	       "write_cached_device_requests=%u "
	       "read+write_cached_device_requests=%u "
	       "pending_cached_device_requests=%u "
	       "\n",
	       bc->bc_name,
	       atomic_read(&bc->bc_read_cached_device_requests),
	       atomic_read(&bc->bc_write_cached_device_requests),
	       (atomic_read(&bc->bc_read_cached_device_requests) +
		atomic_read(&bc->bc_write_cached_device_requests)),
	       atomic_read(&bc->bc_pending_cached_device_requests));
	return sz;
}

ssize_t cache_op_show_pmem_stats(struct bittern_cache *bc,
					 char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;
	struct pmem_info *ps = &bc->bc_papi.papi_stats;

	DMEMIT("%s: pmem_stats: restore_header_valid=%u restore_header0_valid=%u restore_header1_valid=%u restore_corrupt_metadata_blocks=%u restore_valid_clean_metadata_blocks=%u restore_valid_dirty_metadata_blocks=%u restore_invalid_metadata_blocks=%u restore_pending_metadata_blocks=%u restore_invalid_data_blocks=%u restore_valid_clean_data_blocks=%u restore_valid_dirty_data_blocks=%u restore_hash_corrupt_metadata_blocks=%u restore_hash_corrupt_data_blocks=%u\n",
	       bc->bc_name,
	       ps->restore_header_valid,
	       ps->restore_header0_valid,
	       ps->restore_header1_valid,
	       ps->restore_corrupt_metadata_blocks,
	       ps->restore_valid_clean_metadata_blocks,
	       ps->restore_valid_dirty_metadata_blocks,
	       ps->restore_invalid_metadata_blocks,
	       ps->restore_pending_metadata_blocks,
	       ps->restore_invalid_data_blocks,
	       ps->restore_valid_clean_data_blocks,
	       ps->restore_valid_dirty_data_blocks,
	       ps->restore_hash_corrupt_metadata_blocks,
	       ps->restore_hash_corrupt_data_blocks);
	DMEMIT("%s: pmem_stats: "
	       "data_get_put_page_pending_count=%u "
	       "data_get_page_read_count=%u "
	       "data_put_page_read_count=%u "
	       "data_get_page_write_count=%u "
	       "data_put_page_write_count=%u "
	       "data_put_page_write_metadata_count=%u "
	       "data_convert_page_read_to_write_count=%u "
	       "data_clone_read_page_to_write_page_count=%u\n",
	       bc->bc_name,
	       atomic_read(&ps->data_get_put_page_pending_count),
	       atomic_read(&ps->data_get_page_read_count),
	       atomic_read(&ps->data_put_page_read_count),
	       atomic_read(&ps->data_get_page_write_count),
	       atomic_read(&ps->data_put_page_write_count),
	       atomic_read(&ps->data_put_page_write_metadata_count),
	       atomic_read(&ps->data_convert_page_read_to_write_count),
	       atomic_read(&ps->data_clone_read_page_to_write_page_count));
	DMEMIT("%s: pmem_stats: "
	       "pmem_read_not4k_count=%u "
	       "pmem_read_not4k_pending=%u "
	       "pmem_write_not4k_count=%u "
	       "pmem_write_not4k_pending=%u "
	       "pmem_read_4k_count=%u "
	       "pmem_read_4k_pending=%u "
	       "pmem_write_4k_count=%u "
	       "pmem_write_4k_pending=%u "
	       "\n",
	       bc->bc_name,
	       atomic_read(&ps->pmem_read_not4k_count),
	       atomic_read(&ps->pmem_read_not4k_pending),
	       atomic_read(&ps->pmem_write_not4k_count),
	       atomic_read(&ps->pmem_write_not4k_pending),
	       atomic_read(&ps->pmem_read_4k_count),
	       atomic_read(&ps->pmem_read_4k_pending),
	       atomic_read(&ps->pmem_write_4k_count),
	       atomic_read(&ps->pmem_write_4k_pending));
	DMEMIT("%s: pmem_stats: pmem_make_req_wq_count=%u\n",
	       bc->bc_name,
	       atomic_read(&ps->pmem_make_req_wq_count));

	return sz;
}

ssize_t cache_op_show_info(struct bittern_cache *bc, char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;
	uint64_t s;

	DMEMIT("%s: info: version=%s codename=%s build_timestamp=%s max_io_len_pages=%u memcpy_nt_type=%s\n",
	       bc->bc_name,
	       BITTERN_CACHE_VERSION,
	       BITTERN_CACHE_CODENAME,
	       cache_git_generated,
	       MAX_IO_LEN_PAGES,
	       memcpy_nt_type);
	DMEMIT("%s: info: cache_name=%s cache_device_name=%s cached_device_name=%s\n",
	       bc->bc_name,
	       bc->bc_name,
	       bc->bc_cache_device_name,
	       bc->bc_cached_device_name);
	DMEMIT("%s: info: cache_entries=%llu mcb_size_bytes=%llu in_use_cache_size=%llu in_use_cache_size_bytes=%llu in_use_cache_size_mbytes=%llu cache_actual_size_bytes=%lu cache_actual_size_mbytes=%lu cached_device_size_bytes=%llu cached_device_size_mbytes=%llu\n",
	       bc->bc_name,
	       bc->bc_papi.papi_hdr.lm_cache_blocks,
	       bc->bc_papi.papi_hdr.lm_mcb_size_bytes,
	       bc->bc_papi.papi_hdr.lm_cache_size_bytes,
	       bc->bc_papi.papi_hdr.lm_cache_size_bytes,
	       bc->bc_papi.papi_hdr.lm_cache_size_bytes / (1024ULL * 1024ULL),
	       bc->bc_papi.papi_bdev_actual_size_bytes,
	       bc->bc_papi.papi_bdev_actual_size_bytes / (1024UL * 1024UL),
	       bc->bc_cached_device_size_bytes,
	       bc->bc_cached_device_size_mbytes);
	DMEMIT("%s: info: replacement_mode=%s cache_mode=%s\n",
	       bc->bc_name,
	       cache_replacement_mode_to_str(bc->bc_replacement_mode),
	       cache_mode_to_str(bc));
	DMEMIT("%s: info: enable_req_fua=%d\n",
	       bc->bc_name,
	       bc->bc_enable_req_fua);
	DMEMIT("%s: info: handle=0x%llx handle_cache_blocks=0x%llx\n",
	       bc->bc_name,
	       (uint64_t)bc,
	       (uint64_t)(bc->bc_cache_blocks));
	s = bc->bc_papi.papi_hdr.lm_cache_blocks * sizeof(struct cache_block);
	DMEMIT("%s: info: cache_blocks_metadata_size_bytes=%llu, cache_blocks_metadata_size_mbytes=%llu\n",
	       bc->bc_name,
	       s,
	       s / (1024ULL * 1024ULL));
	DMEMIT("%s: info: kmem_map=0x%llx kmem_threads=0x%llx\n",
	       bc->bc_name,
	       (uint64_t)(bc->bc_kmem_map),
	       (uint64_t)(bc->bc_kmem_threads));
	DMEMIT("%s: info: error_state=%d error_count=%d\n",
	       bc->bc_name,
	       bc->error_state,
	       atomic_read(&bc->error_count));
	return sz;
}

ssize_t cache_op_show_git_info(struct bittern_cache *bc, char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;

	DMEMIT("%s: git_info: cache_git_generated=%s\n",
	       bc->bc_name,
	       cache_git_generated);
	DMEMIT("%s: git_info: cache_git_describe='%s'\n",
	       bc->bc_name,
	       cache_git_describe);
	DMEMIT("%s: git_info: cache_git_status='%s'\n",
	       bc->bc_name,
	       cache_git_status);
	DMEMIT("%s: git_info: cache_git_tag='%s'\n",
	       bc->bc_name,
	       cache_git_tag);
	DMEMIT("%s: git_info: cache_git_files=all %s\n",
	       bc->bc_name,
	       cache_git_files);
	return sz;
}

ssize_t cache_op_show_build_info(struct bittern_cache *bc,
					 char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;

	DMEMIT("%s: build_info: GCC_VER=%u.%u.%u LINUX_VER=0x%x "
#ifdef ENABLE_ASSERT
	       "defined(ENABLE_ASSERT) "
#else /*ENABLE_ASSERT */
	       "notdefined(ENABLE_ASSERT) "
#endif /*ENABLE_ASSERT */
#ifdef ENABLE_KMALLOC_DEBUG
	       "defined(ENABLE_KMALLOC_DEBUG) "
#else
	       "notdefined(ENABLE_KMALLOC_DEBUG) "
#endif
#ifdef ENABLE_TRACK_CRC32C
	       "defined(ENABLE_TRACK_CRC32C) "
#else
	       "notdefined(ENABLE_TRACK_CRC32C) "
#endif
#ifdef ENABLE_EXTRA_CHECKSUM_CHECK
	       "defined(ENABLE_EXTRA_CHECKSUM_CHECK) "
#else
	       "notdefined(ENABLE_EXTRA_CHECKSUM_CHECK) "
#endif
#ifdef DISABLE_BT_DEV_TRACE
	       "defined(DISABLE_BT_DEV_TRACE) "
#else
	       "notdefined(DISABLE_BT_DEV_TRACE) "
#endif
#ifdef DISABLE_BT_TRACE
	       "defined(DISABLE_BT_TRACE) "
#else
	       "notdefined(DISABLE_BT_TRACE) "
#endif
	       "BT_LEVEL_DEFAULT=0x%x "
	       "PRINTK_DEBUG_DEFAULT=%s"
	       "\n",
	       bc->bc_name,
	       __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__,
	       LINUX_VERSION_CODE,
	       BT_LEVEL_DEFAULT,
	       (strcmp(PRINTK_DEBUG_DEFAULT, KERN_DEBUG) ? "KERN_DEBUG" :
		(strcmp(PRINTK_DEBUG_DEFAULT, KERN_INFO) ? "KERN_INFO" :
		 PRINTK_DEBUG_DEFAULT)));

	return sz;
}

ssize_t cache_op_show_trace(struct bittern_cache *bc, char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;

	DMEMIT("%s: trace: "
	       "trace=0x%x bt_trace=%d bt_dev_trace=%d\n",
	       bc->bc_name,
	       cache_trace_level,
	       __CACHE_TRACE_LEVEL(),
	       __CACHE_DEV_TRACE_LEVEL());
	return sz;
}

ssize_t cache_op_show_verifier(struct bittern_cache *bc, char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;
	unsigned long s_started = 0, s_completed = 0, s_elapsed = 0;
	unsigned long s_last_block = 0;

	if (bc->bc_verifier_scan_started != 0 &&
	    bc->bc_verifier_scan_completed != 0) {
		s_started = jiffies - bc->bc_verifier_scan_started;
		s_last_block = jiffies - bc->bc_verifier_scan_last_block;
		s_completed = jiffies - bc->bc_verifier_scan_completed;
		if (s_started >= s_completed)
			s_elapsed = s_started - s_completed;
		else
			s_elapsed = 0;
	} else if (bc->bc_verifier_scan_started != 0) {
		s_started = jiffies - bc->bc_verifier_scan_started;
		s_last_block = jiffies - bc->bc_verifier_scan_last_block;
	}

	DMEMIT("%s: verifier: running=%d one_shot=%d task=%p delay_ms=%d bug_on_verify_errors=%d\n",
	       bc->bc_name,
	       bc->bc_verifier_running,
	       bc->bc_verifier_one_shot,
	       bc->bc_verifier_task,
	       bc->bc_verifier_scan_delay_ms,
	       bc->bc_verifier_bug_on_verify_errors);
	DMEMIT("%s: verifier: blocks_verified=%d blocks_not_verified_dirty=%d blocks_not_verified_busy=%d blocks_not_verified_invalid=%d\n",
	       bc->bc_name,
	       bc->bc_verifier_blocks_verified,
	       bc->bc_verifier_blocks_not_verified_dirty,
	       bc->bc_verifier_blocks_not_verified_busy,
	       bc->bc_verifier_blocks_not_verified_invalid);
	DMEMIT("%s: verifier: scans=%d verify_errors=%d verify_errors_cumulative=%d\n",
	       bc->bc_name,
	       bc->bc_verifier_scans,
	       bc->bc_verifier_verify_errors,
	       bc->bc_verifier_verify_errors_cumulative);
	DMEMIT("%s: verifier: scan_started=%ums scan_last_block=%ums scan_completed=%ums scan_elapsed=%ums\n",
	       bc->bc_name,
	       jiffies_to_msecs(s_started),
	       jiffies_to_msecs(s_last_block),
	       jiffies_to_msecs(s_completed),
	       jiffies_to_msecs(s_elapsed));
	return sz;
}

ssize_t cache_op_show_replacement(struct bittern_cache *bc, char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;

	DMEMIT("%s: replacement: replacement=%s ",
	       bc->bc_name,
	       cache_replacement_mode_to_str(bc->bc_replacement_mode));
	DMEMIT("replacement_%d=%s ",
	       CACHE_REPLACEMENT_MODE_FIFO,
	       cache_replacement_mode_to_str(CACHE_REPLACEMENT_MODE_FIFO));
	DMEMIT("replacement_%d=%s ",
	       CACHE_REPLACEMENT_MODE_LRU,
	       cache_replacement_mode_to_str(CACHE_REPLACEMENT_MODE_LRU));
	DMEMIT("replacement_%d=%s ",
	       CACHE_REPLACEMENT_MODE_RANDOM,
	       cache_replacement_mode_to_str(CACHE_REPLACEMENT_MODE_RANDOM));
	DMEMIT("\n");
	return sz;
}

ssize_t cache_op_show_cache_mode(struct bittern_cache *bc, char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;

	DMEMIT("%s: cache_mode: cache_mode=%s\n",
	       bc->bc_name,
	       cache_mode_to_str(bc));
	return sz;
}

ssize_t cache_op_show_redblack_info(struct bittern_cache *bc,
					    char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;

	DMEMIT("%s: redblack_info: "
	       "rb_hit_max=%llu rb_hit_avg=%llu "
	       "rb_miss_max=%llu rb_miss_avg=%llu "
	       "\n",
	       bc->bc_name,
	       bc->bc_rb_hit_loop_max,
	       (bc->bc_rb_hit_loop_count ?
		bc->bc_rb_hit_loop_sum / bc->bc_rb_hit_loop_count : 0),
	       bc->bc_rb_miss_loop_max,
	       (bc->bc_rb_miss_loop_count ?
		bc->bc_rb_miss_loop_sum / bc->bc_rb_miss_loop_count : 0));
	return sz;
}

ssize_t cache_op_show_kthreads(struct bittern_cache *bc, char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;

	DMEMIT("%s: kthreads: " KT_FMT_STRING "\n",
	       bc->bc_name,
	       KT_FMT_ARGS(bc, "verifier_task", bc_verifier_task));
	DMEMIT("%s: kthreads: " KT_FMT_STRING ": "
	       "bgwriter_no_work_count=%u "
	       "bgwriter_work_count=%u "
	       "\n",
	       bc->bc_name,
	       KT_FMT_ARGS(bc, "bgwriter_task", bc_bgwriter_task),
	       bc->bc_bgwriter_no_work_count, bc->bc_bgwriter_work_count);
	DMEMIT("%s: kthreads: "
	       KT_FMT_STRING
	       ": invalidator_no_work_count=%u invalidator_work_count=%u\n",
	       bc->bc_name,
	       KT_FMT_ARGS(bc, "invalidator_task", bc_invalidator_task),
	       bc->bc_invalidator_no_work_count, bc->bc_invalidator_work_count);
	return sz;
}

ssize_t cache_op_show_bgwriter(struct bittern_cache *bc, char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;
	unsigned int cc, avg_cluster_size, avg_cluster_size_sum;

	DMEMIT("%s: bgwriter: "
	       "conf_flush_on_exit=%u "
	       "conf_greedyness=%u "
	       "conf_max_queue_depth_pct=%u "
	       "conf_cluster_size=%u "
	       "conf_policy=%s "
	       "\n",
	       bc->bc_name,
	       bc->bc_bgwriter_conf_flush_on_exit,
	       bc->bc_bgwriter_conf_greedyness,
	       bc->bc_bgwriter_conf_max_queue_depth_pct,
	       bc->bc_bgwriter_conf_cluster_size,
	       cache_bgwriter_policy(bc));
	DMEMIT("%s: bgwriter: "
	       "curr_queue_depth=%u "
	       "curr_max_queue_depth=%u "
	       "curr_target_pct=%u "
	       "curr_rate_per_sec=%u "
	       "curr_min_age_secs=%u "
	       "\n",
	       bc->bc_name,
	       bc->bc_bgwriter_curr_queue_depth,
	       bc->bc_bgwriter_curr_max_queue_depth,
	       bc->bc_bgwriter_curr_target_pct,
	       bc->bc_bgwriter_curr_rate_per_sec,
	       bc->bc_bgwriter_curr_min_age_secs);
	DMEMIT("%s: bgwriter: "
	       "curr_block_count=%u "
	       "curr_block_count_sum=%u "
	       "\n",
	       bc->bc_name,
	       bc->bc_bgwriter_curr_block_count,
	       bc->bc_bgwriter_curr_block_count_sum);
	avg_cluster_size = 0;
	avg_cluster_size_sum = 0;
	cc = bc->bc_bgwriter_curr_cluster_count;
	if (cc > 0)
		avg_cluster_size =
		    (bc->bc_bgwriter_curr_block_count * 100) / cc;
	cc = bc->bc_bgwriter_curr_cluster_count_sum;
	if (cc > 0)
		avg_cluster_size_sum =
		    (bc->bc_bgwriter_curr_block_count_sum * 100) / cc;
	DMEMIT("%s: bgwriter: " "curr_cluster_count=%u "
	       "curr_avg_cluster_size=%u.%02u " "curr_cluster_count_sum=%u "
	       "curr_avg_cluster_size_sum=%u.%02u " "\n", bc->bc_name,
	       bc->bc_bgwriter_curr_cluster_count, avg_cluster_size / 100,
	       avg_cluster_size % 100, bc->bc_bgwriter_curr_cluster_count_sum,
	       avg_cluster_size_sum / 100, avg_cluster_size_sum % 100);
	DMEMIT("%s: bgwriter: stalls_count=%u cache_block_busy_count=%u queue_full_count=%u too_young_count=%u ready_count=%u\n",
	       bc->bc_name,
	       bc->bc_bgwriter_stalls_count,
	       bc->bc_bgwriter_cache_block_busy_count,
	       bc->bc_bgwriter_queue_full_count,
	       bc->bc_bgwriter_too_young_count,
	       bc->bc_bgwriter_ready_count);
	DMEMIT("%s: bgwriter: " "curr_policy_0=%lu " "curr_policy_1=%lu "
	       "curr_policy_2=%lu " "curr_policy_3=%lu " "\n", bc->bc_name,
	       bc->bc_bgwriter_curr_policy[0], bc->bc_bgwriter_curr_policy[1],
	       bc->bc_bgwriter_curr_policy[2], bc->bc_bgwriter_curr_policy[3]);
	DMEMIT("%s: bgwriter: " "curr_policy_4=%lu " "curr_policy_5=%lu "
	       "curr_policy_6=%lu " "curr_policy_7=%lu " "\n", bc->bc_name,
	       bc->bc_bgwriter_curr_policy[4], bc->bc_bgwriter_curr_policy[5],
	       bc->bc_bgwriter_curr_policy[6], bc->bc_bgwriter_curr_policy[7]);
	return sz;
}

ssize_t cache_op_show_timers(struct bittern_cache *bc, char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;
	struct pmem_info *ps = &bc->bc_papi.papi_stats;

	DMEMIT("%s: timers: "
	       T_FMT_STRING("reads") " "
	       T_FMT_STRING("writes") " "
	       T_FMT_STRING("read_hits") " "
	       T_FMT_STRING("write_hits") " "
	       T_FMT_STRING("read_misses") " "
	       T_FMT_STRING("write_misses") " "
	       "\n",
	       bc->bc_name,
	       T_FMT_ARGS(bc, bc_timer_reads),
	       T_FMT_ARGS(bc, bc_timer_writes),
	       T_FMT_ARGS(bc, bc_timer_read_hits),
	       T_FMT_ARGS(bc, bc_timer_write_hits),
	       T_FMT_ARGS(bc, bc_timer_read_misses),
	       T_FMT_ARGS(bc, bc_timer_write_misses));
	DMEMIT("%s: timers: "
	       T_FMT_STRING("write_dirty_misses") " "
	       T_FMT_STRING("write_clean_misses") " "
	       T_FMT_STRING("read_clean_hits") " "
	       T_FMT_STRING("write_clean_hits") " "
	       T_FMT_STRING("read_dirty_hits") " "
	       T_FMT_STRING("write_dirty_hits") " "
	       "\n",
	       bc->bc_name,
	       T_FMT_ARGS(bc, bc_timer_write_dirty_misses),
	       T_FMT_ARGS(bc, bc_timer_write_clean_misses),
	       T_FMT_ARGS(bc, bc_timer_read_clean_hits),
	       T_FMT_ARGS(bc, bc_timer_write_clean_hits),
	       T_FMT_ARGS(bc, bc_timer_read_dirty_hits),
	       T_FMT_ARGS(bc, bc_timer_write_dirty_hits));
	DMEMIT("%s: timers: "
	       T_FMT_STRING("writebacks") " "
	       T_FMT_STRING("invalidations") " "
	       T_FMT_STRING("pending_queue") " "
	       T_FMT_STRING("deferred_wait_busy") " "
	       T_FMT_STRING("deferred_wait_page") " "
	       "\n",
	       bc->bc_name,
	       T_FMT_ARGS(bc, bc_timer_writebacks),
	       T_FMT_ARGS(bc, bc_timer_invalidations),
	       T_FMT_ARGS(bc, bc_timer_pending_queue),
	       T_FMT_ARGS(bc, defer_busy.timer),
	       T_FMT_ARGS(bc, defer_page.timer));
	DMEMIT("%s: timers: "
	       T_FMT_STRING("cached_device_reads") " "
	       T_FMT_STRING("cached_device_writes") " "
	       T_FMT_STRING("cached_device_flushes") " "
	       "\n",
	       bc->bc_name,
	       T_FMT_ARGS(bc, bc_timer_cached_device_reads),
	       T_FMT_ARGS(bc, bc_timer_cached_device_writes),
	       T_FMT_ARGS(bc, bc_timer_cached_device_flushes));
	DMEMIT("%s: timers: " T_FMT_STRING("resource_alloc_reads") " " T_FMT_STRING("resource_alloc_writes") "\n",
	       bc->bc_name,
	       T_FMT_ARGS(bc, bc_timer_resource_alloc_reads),
	       T_FMT_ARGS(bc, bc_timer_resource_alloc_writes));
	DMEMIT("%s: timers: "
	       T_FMT_STRING("make_request_wq_timer") " "
	       "\n", bc->bc_name, T_FMT_ARGS(bc, bc_make_request_wq_timer));
	DMEMIT("%s: timers: "
	       T_FMT_STRING("metadata_read_async_timer") " "
	       T_FMT_STRING("metadata_write_async_timer") " "
	       "\n",
	       bc->bc_name,
	       T_FMT_ARGS(ps, metadata_read_async_timer),
	       T_FMT_ARGS(ps, metadata_write_async_timer));
	DMEMIT("%s: timers: "
	       T_FMT_STRING("data_get_page_read_timer") " "
	       T_FMT_STRING("data_get_page_read_async_timer") " "
	       T_FMT_STRING("data_put_page_read_timer") " "
	       T_FMT_STRING("data_get_page_write_timer") " "
	       T_FMT_STRING("data_put_page_write_async_timer") " "
	       T_FMT_STRING("data_put_page_write_async_metadata_timer") " "
	       T_FMT_STRING("data_put_page_write_timer") " "
	       "\n",
	       bc->bc_name,
	       T_FMT_ARGS(ps, data_get_page_read_timer),
	       T_FMT_ARGS(ps, data_get_page_read_async_timer),
	       T_FMT_ARGS(ps, data_put_page_read_timer),
	       T_FMT_ARGS(ps, data_get_page_write_timer),
	       T_FMT_ARGS(ps, data_put_page_write_async_timer),
	       T_FMT_ARGS(ps, data_put_page_write_async_metadata_timer),
	       T_FMT_ARGS(ps, data_put_page_write_timer));
	DMEMIT("%s: timers: "
	       T_FMT_STRING("pmem_read_not4k_timer") " "
	       T_FMT_STRING("pmem_write_not4k_timer") " "
	       T_FMT_STRING("pmem_read_4k_timer") " "
	       T_FMT_STRING("pmem_write_4k_timer") " "
	       "\n",
	       bc->bc_name,
	       T_FMT_ARGS(ps, pmem_read_not4k_timer),
	       T_FMT_ARGS(ps, pmem_write_not4k_timer),
	       T_FMT_ARGS(ps, pmem_read_4k_timer),
	       T_FMT_ARGS(ps, pmem_write_4k_timer));
	DMEMIT("%s: timers: " T_FMT_STRING("pmem_make_req_wq_timer") "\n",
	       bc->bc_name,
	       T_FMT_ARGS(ps, pmem_make_req_wq_timer));
	return sz;
}

ssize_t cache_op_show_tracked_hashes(struct bittern_cache *bc,
					     char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;

#ifdef ENABLE_TRACK_CRC32C
	DMEMIT("%s: tracked_hashes: tracked_hashes=%p tracked_hashes_num=%lu(%llumbytes) tracked_hashes_set=%u tracked_hashes_clear=%u tracked_hashes_null=%u tracked_hashes_ok=%u tracked_hashes_bad=%u\n",
	       bc->bc_name,
	       bc->bc_tracked_hashes,
	       bc->bc_tracked_hashes_num,
	       (((uint64_t)bc->bc_tracked_hashes_num * sizeof(uint32_t)) /
		(1024ULL * 1024ULL)),
	       atomic_read(&bc->bc_tracked_hashes_set),
	       atomic_read(&bc->bc_tracked_hashes_clear),
	       atomic_read(&bc->bc_tracked_hashes_null),
	       atomic_read(&bc->bc_tracked_hashes_ok),
	       atomic_read(&bc->bc_tracked_hashes_bad));
#else /*ENABLE_TRACK_CRC32C */
	DMEMIT("%s: tracked_hashes: tracked_hashes=0, tracked_hashes_checksums_to_track=0\n",
	       bc->bc_name);
#endif /*ENABLE_TRACK_CRC32C */
	return sz;
}

ssize_t cache_op_show_cache_states(struct bittern_cache *bc,
					   char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;
	int i;

	DMEMIT("%s: cache_states: ", bc->bc_name);
	for (i = 0; i < __TS_NUM; i++) {
		DMEMIT("p%d=%u ", i,
		       atomic_read(&bc->bc_cache_transitions_counters[i]));
	}
	DMEMIT("\n");

	DMEMIT("%s: cache_states: ", bc->bc_name);
	for (i = 0; i < __CACHE_STATES_NUM; i++) {
		DMEMIT("s%d=%u ",
		       i, atomic_read(&bc->bc_cache_states_counters[i]));
	}
	DMEMIT("\n");

	return sz;
}

ssize_t cache_op_show_pmem_api(struct bittern_cache *bc, char *result)
{
	size_t sz = 0, maxlen = PAGE_SIZE;

	DMEMIT("%s: pmem_api: interface=%p name=%s page_size_transfer_only=%d cache_layout=%c\n",
	       bc->bc_name,
	       bc->bc_papi.papi_interface,
	       pmem_api_name(bc),
	       pmem_page_size_transfer_only(bc),
	       pmem_cache_layout(bc));
	DMEMIT("%s: pmem_api: bdev=0x%llx make_request_wq=0x%llx bdev_size_bytes=%lu bdev_size_mbytes=%lu bdev_actual_size_bytes=%lu bdev_actual_size_mbytes=%lu\n",
	       bc->bc_name,
	       (uint64_t)bc->bc_papi.papi_bdev,
	       (uint64_t)bc->bc_papi.papi_make_request_wq,
	       bc->bc_papi.papi_bdev_size_bytes,
	       bc->bc_papi.papi_bdev_size_bytes / (1024UL * 1024UL),
	       bc->bc_papi.papi_bdev_actual_size_bytes,
	       bc->bc_papi.papi_bdev_actual_size_bytes / (1024UL * 1024UL));

	return sz;
}

ssize_t cache_op_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct bittern_cache *bc =
	    container_of(kobj, struct bittern_cache, bc_kobj);

	__ASSERT_BITTERN_CACHE(bc);

	if (strncmp(attr->name, "conf", 4) == 0)
		return cache_op_show_conf(bc, buf);

	if (strncmp(attr->name, "stats_extra", 11) == 0)
		return cache_op_show_stats_extra(bc, buf);

	if (strncmp(attr->name, "stats", 5) == 0)
		return cache_op_show_stats(bc, buf);

	if (strncmp(attr->name, "pmem_stats", 10) == 0)
		return cache_op_show_pmem_stats(bc, buf);

	if (strncmp(attr->name, "info", 4) == 0)
		return cache_op_show_info(bc, buf);

	if (strncmp(attr->name, "git_info", 8) == 0)
		return cache_op_show_git_info(bc, buf);

	if (strncmp(attr->name, "build_info", 8) == 0)
		return cache_op_show_build_info(bc, buf);

	if (strncmp(attr->name, "trace", 5) == 0)
		return cache_op_show_trace(bc, buf);

	if (strncmp(attr->name, "verifier", 8) == 0)
		return cache_op_show_verifier(bc, buf);

	if (strncmp(attr->name, "replacement", 11) == 0)
		return cache_op_show_replacement(bc, buf);

	if (strncmp(attr->name, "cache_mode", 10) == 0)
		return cache_op_show_cache_mode(bc, buf);

	if (strncmp(attr->name, "redblack_info", 9) == 0)
		return cache_op_show_redblack_info(bc, buf);

	if (strncmp(attr->name, "sequential", 10) == 0)
		return seq_bypass_stats(bc, buf, PAGE_SIZE);

	if (strncmp(attr->name, "kthreads", 8) == 0)
		return cache_op_show_kthreads(bc, buf);

	if (strncmp(attr->name, "bgwriter_policy", 15) == 0)
		return cache_bgwriter_op_show_policy(bc, buf);

	if (strncmp(attr->name, "bgwriter", 8) == 0)
		return cache_op_show_bgwriter(bc, buf);

	if (strncmp(attr->name, "timers", 6) == 0)
		return cache_op_show_timers(bc, buf);

	if (strncmp(attr->name, "tracked_hashes", 14) == 0)
		return cache_op_show_tracked_hashes(bc, buf);

	if (strncmp(attr->name, "cache_states", 12) == 0)
		return cache_op_show_cache_states(bc, buf);

	if (strncmp(attr->name, "pmem_api", 8) == 0)
		return cache_op_show_pmem_api(bc, buf);

	printk_warning("attribute not found\n");
	buf[0] = '\0';
	return 0;
}

int cache_message(struct bittern_cache *bc, int argc, char **argv)
{
	struct cache_conf_param_entry *e;
	char *param_name_s;
	char *param_value_s;
	int ret;

	ASSERT_BITTERN_CACHE(bc);

	if (argc != 2) {
		printk_err("cache_message: two arguments expected\n");
		return -EINVAL;
	}

	param_name_s = argv[0];
	param_value_s = argv[1];

	e = cache_get_conf(param_name_s);
	if (e == NULL) {
		printk_err("cache_message: no such param '%s'\n", param_name_s);
		return -EINVAL;
	}

	ASSERT(e->cache_conf_type == CONF_TYPE_STR ||
	       e->cache_conf_type == CONF_TYPE_INT);

	/* in case of a int type, do the extra processing */
	if (e->cache_conf_type == CONF_TYPE_INT) {
		int param_value;

		/*
		 * Allow the number base to be detected
		 * automagically, hence 0 as base.
		 */
		ret = kstrtos32(param_value_s, 0, &param_value);
		if (ret != 0) {
			/* conversion to int failed */
			printk_err("cache_message: param %s value '%s' not an integer\n",
				   param_name_s,
				   param_value_s);
			return ret;
		}
		if (param_value < e->cache_conf_min ||
		    param_value > e->cache_conf_max) {
			printk_err("cache_message: param %s value %d out of range [%d..%d]\n",
				   param_name_s,
				   param_value,
				   e->cache_conf_min,
				   e->cache_conf_max);
			return -EOVERFLOW;
		}
		ret = (*e->cache_conf_setup_function)(bc, param_value);

	} else {

		ret = (*e->cache_conf_setup_function_str)(bc, param_value_s);
	}

	if (ret != 0)
		printk_err("cache_message: param %s value %s set failed: ret=%d\n",
			   param_name_s,
			   param_value_s,
			   ret);

	return ret;
}

const struct sysfs_ops cache_stats_ops = {
	.show = cache_op_show,
};

struct attribute cache_sysfs_conf = {
	.name = "conf",
	.mode = 0444,
};

struct attribute cache_sysfs_stats = {
	.name = "stats",
	.mode = 0444,
};

struct attribute cache_sysfs_stats_extra = {
	.name = "stats_extra",
	.mode = 0444,
};

struct attribute cache_sysfs_pmem_stats = {
	.name = "pmem_stats",
	.mode = 0444,
};

struct attribute cache_sysfs_info = {
	.name = "info",
	.mode = 0444,
};

struct attribute cache_sysfs_git_info = {
	.name = "git_info",
	.mode = 0444,
};

struct attribute cache_sysfs_build_info = {
	.name = "build_info",
	.mode = 0444,
};

struct attribute cache_sysfs_trace = {
	.name = "trace",
	.mode = 0444,
};

struct attribute cache_sysfs_verifier = {
	.name = "verifier",
	.mode = 0444,
};

struct attribute cache_sysfs_replacement = {
	.name = "replacement",
	.mode = 0444,
};

struct attribute cache_sysfs_cache_mode = {
	.name = "cache_mode",
	.mode = 0444,
};

struct attribute cache_sysfs_redblack_info = {
	.name = "redblack_info",
	.mode = 0444,
};

struct attribute cache_sysfs_sequential = {
	.name = "sequential",
	.mode = 0444,
};

struct attribute cache_sysfs_kthreads = {
	.name = "kthreads",
	.mode = 0444,
};

struct attribute cache_sysfs_bgwriter = {
	.name = "bgwriter",
	.mode = 0444,
};

struct attribute cache_sysfs_bgwriter_policy = {
	.name = "bgwriter_policy",
	.mode = 0444,
};

struct attribute cache_sysfs_timers = {
	.name = "timers",
	.mode = 0444,
};

struct attribute cache_sysfs_tracked_hashes = {
	.name = "tracked_hashes",
	.mode = 0444,
};

struct attribute cache_sysfs_cache_states = {
	.name = "cache_states",
	.mode = 0444,
};

struct attribute cache_sysfs_pmem_api = {
	.name = "pmem_api",
	.mode = 0444,
};

struct attribute *cache_stats_files[] = {
	&cache_sysfs_conf,
	&cache_sysfs_stats,
	&cache_sysfs_stats_extra,
	&cache_sysfs_pmem_stats,
	&cache_sysfs_info,
	&cache_sysfs_git_info,
	&cache_sysfs_build_info,
	&cache_sysfs_trace,
	&cache_sysfs_verifier,
	&cache_sysfs_replacement,
	&cache_sysfs_cache_mode,
	&cache_sysfs_redblack_info,
	&cache_sysfs_sequential,
	&cache_sysfs_kthreads,
	&cache_sysfs_timers,
	&cache_sysfs_bgwriter,
	&cache_sysfs_bgwriter_policy,
	&cache_sysfs_tracked_hashes,
	&cache_sysfs_cache_states,
	&cache_sysfs_pmem_api,
	NULL,
};

void cache_stats_release(struct kobject *k)
{
	printk_info("(%p)\n", k);
}

struct kobj_type cache_stats_ktype = {
	.release = cache_stats_release,
	.sysfs_ops = &cache_stats_ops,
	.default_attrs = cache_stats_files,
};

struct kobject *cache_kobj = NULL;

void cache_sysfs_init(struct bittern_cache *bc)
{
	printk_info("kobject_init\n");
	kobject_init(&bc->bc_kobj, &cache_stats_ktype);
}

int cache_sysfs_add(struct bittern_cache *bc)
{
	int ret;

	ret = kobject_add(&bc->bc_kobj, cache_kobj, bc->bc_name);
	printk_info("kobject_add=%d\n", ret);
	return ret;
}

void cache_sysfs_deinit(struct bittern_cache *bc)
{
	printk_info("kobject_put\n");
	/* kobject_init() says we need to call kobject_put regardless */
	kobject_put(&bc->bc_kobj);
}

int bittern_cache_ctr_entry(struct dm_target *ti,
			    unsigned int argc,
			    char **argv)
{
	int ret;
	uint64_t tstamp;

	printk_info("ctr_func, ti=%p, argc=%u, argv=%p\n", ti, argc, argv);
	tstamp = current_kernel_time_nsec();
	ret = cache_ctr(ti, argc, argv);
	tstamp = current_kernel_time_nsec() - tstamp;
	printk_info("ctr_func done: ret=%d\n", ret);
	printk_info("ctr_func done: %llu usecs\n", tstamp / 1000ULL);
	return ret;
}

void bittern_cache_dtr_entry(struct dm_target *ti)
{
	struct bittern_cache *bc = ti->private;
	uint64_t t0;

	printk_info("dtr_func, ti=%p, bc=%p\n", ti, bc);
	printk_info("dtr_func, ti=%p, bc=%p: calling _dtr_pre()\n", ti, bc);
	cache_dtr_pre(ti);
	ASSERT_BITTERN_CACHE(bc);
	t0 = current_kernel_time_nsec();
	ASSERT_BITTERN_CACHE(bc);
	printk_info("dtr_func, ti=%p, bc=%p: calling _sysfs_deinit()\n", ti,
		    bc);
	cache_sysfs_deinit(bc);
	ASSERT_BITTERN_CACHE(bc);
	printk_info("dtr_func, ti=%p, bc=%p: calling _dtr()\n", ti, bc);
	cache_dtr(ti);
	t0 = current_kernel_time_nsec() - t0;
	printk_info("dtr_func done: %llu usecs\n", t0 / 1000ULL);
}

int bittern_cache_iterate_devices(struct dm_target *ti,
				  iterate_devices_callout_fn fn, void *data)
{
	struct bittern_cache *bc = ti->private;

	printk_info("bc=%p, ti=%p, fn=%p, data=%p\n", bc, ti, fn, data);
	ASSERT_BITTERN_CACHE(bc);

	return (*fn) (ti, bc->devio.dm_dev, 0, ti->len, data);
}

void bittern_cache_io_hints(struct dm_target *ti, struct queue_limits *lim)
{
	struct bittern_cache *bc = ti->private;

	printk_info("bc=%p, ti=%p, lim=%p\n", bc, ti, lim);
	ASSERT_BITTERN_CACHE(bc);

	printk_info("bc=%p: %s: lim->io_min=%u\n", bc, bc->bc_name,
		    lim->io_min);
	printk_info("bc=%p: %s: lim->io_opt=%u\n", bc, bc->bc_name,
		    lim->io_opt);
	printk_info("bc=%p: %s: lim->max_discard_sectors=%u\n", bc, bc->bc_name,
		    lim->max_discard_sectors);
	printk_info("bc=%p: %s: lim->discard_alignment=%u\n", bc, bc->bc_name,
		    lim->discard_alignment);
	printk_info("bc=%p: %s: lim->discard_misaligned=%u\n", bc, bc->bc_name,
		    lim->discard_misaligned);

	printk_info("bc=%p: %s: setting limits\n", bc, bc->bc_name);

	blk_limits_io_min(lim, 512);
	blk_limits_io_opt(lim, PAGE_SIZE);
	/* blk_limits_max_hw_sectors(lim, SECTORS_PER_CACHE_BLOCK); */
	lim->discard_alignment = PAGE_SIZE;
	lim->max_discard_sectors = SECTORS_PER_CACHE_BLOCK * 256;
	lim->discard_granularity = SECTORS_PER_CACHE_BLOCK;

	printk_info("bc=%p: %s: done setting limits\n", bc, bc->bc_name);

	printk_info("bc=%p: %s: lim->max_segments=%u\n", bc, bc->bc_name,
		    lim->max_segments);
	printk_info("bc=%p: %s: lim->max_integrity_segments=%u\n", bc,
		    bc->bc_name, lim->max_integrity_segments);
	printk_info("bc=%p: %s: lim->seg_boundary_mask=0x%lx\n", bc,
		    bc->bc_name, lim->seg_boundary_mask);
	printk_info("bc=%p: %s: lim->max_segment_size=%u\n", bc, bc->bc_name,
		    lim->max_segment_size);
	printk_info("bc=%p: %s: lim->max_sectors=%u\n", bc, bc->bc_name,
		    lim->max_sectors);
	printk_info("bc=%p: %s: lim->max_hw_sectors=%u\n", bc, bc->bc_name,
		    lim->max_hw_sectors);
	printk_info("bc=%p: %s: lim->max_write_same_sectors=%u\n", bc,
		    bc->bc_name, lim->max_write_same_sectors);
	printk_info("bc=%p: %s: lim->max_discard_sectors=%u\n", bc, bc->bc_name,
		    lim->max_discard_sectors);
	printk_info("bc=%p: %s: lim->discard_granularity=%u\n", bc, bc->bc_name,
		    lim->discard_granularity);
	printk_info("bc=%p: %s: lim->discard_alignment=%u\n", bc, bc->bc_name,
		    lim->discard_alignment);
	printk_info("bc=%p: %s: lim->discard_misaligned=%u\n", bc, bc->bc_name,
		    lim->discard_misaligned);
	printk_info("bc=%p: %s: lim->discard_zeroes_data=%u\n", bc, bc->bc_name,
		    lim->discard_zeroes_data);
	printk_info("bc=%p: %s: lim->logical_block_size=%u\n", bc, bc->bc_name,
		    lim->logical_block_size);
	printk_info("bc=%p: %s: lim->physical_block_size=%u\n", bc, bc->bc_name,
		    lim->physical_block_size);
	printk_info("bc=%p: %s: lim->io_min=%u\n", bc, bc->bc_name,
		    lim->io_min);
	printk_info("bc=%p: %s: lim->bounce_pfn=0x%lx\n", bc, bc->bc_name,
		    lim->bounce_pfn);
	printk_info("bc=%p: %s: lim->alignment_offset=%u\n", bc, bc->bc_name,
		    lim->alignment_offset);
	printk_info("bc=%p: %s: lim->io_opt=%u\n", bc, bc->bc_name,
		    lim->io_opt);
	printk_info("bc=%p: %s: lim->misaligned=%u\n", bc, bc->bc_name,
		    lim->misaligned);
	printk_info("bc=%p: %s: lim->cluster=%u\n", bc, bc->bc_name,
		    lim->cluster);
}

void bittern_cache_status(struct dm_target *ti, status_type_t type,
			  unsigned status_flags, char *result, unsigned maxlen)
{
	struct bittern_cache *bc = ti->private;
	int sz = 0;

	ASSERT_BITTERN_CACHE(bc);

	switch (type) {
	case STATUSTYPE_INFO:
		DMEMIT("%u %u %u %p", atomic_read(&bc->bc_pending_requests),
		       atomic_read(&bc->bc_read_requests),
		       atomic_read(&bc->bc_write_requests), bc->devio.dm_dev);
		break;

	case STATUSTYPE_TABLE:
		DMEMIT("%u %u %u %p", atomic_read(&bc->bc_pending_requests),
		       atomic_read(&bc->bc_read_requests),
		       atomic_read(&bc->bc_write_requests), bc->devio.dm_dev);
		break;
	}
}

int bittern_cache_message(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct bittern_cache *bc = ti->private;

	ASSERT_BITTERN_CACHE(bc);

	return cache_message(bc, argc, argv);
}

struct target_type cache_target = {
	.name = "bittern_cache",
	.version = {0, 0, 1},
	.module = THIS_MODULE,
	.ctr = bittern_cache_ctr_entry,
	.dtr = bittern_cache_dtr_entry,
	.status = bittern_cache_status,
	.message = bittern_cache_message,
	.map = bittern_cache_map,
	.iterate_devices = bittern_cache_iterate_devices,
	.io_hints = bittern_cache_io_hints,
};

int __init dm_cache_init(void)
{
	int ret;

	ret = dm_register_target(&cache_target);
	if (ret < 0) {
		printk_err("error: register failed %d\n", ret);
		return ret;
	}

	printk_info("register ok\n");

	cache_kobj = kobject_create_and_add("bittern", fs_kobj);
	printk_info("bcache_kobj=%p\n", cache_kobj);
	if (cache_kobj == NULL) {
		printk_err("failed to allocate bittern kobj\n");
		dm_unregister_target(&cache_target);
		return -ENOMEM;
	}

	printk_info("sizeof (struct bittern_cache) = %lu\n",
		    (unsigned long)sizeof(struct bittern_cache));
	printk_info("sizeof (struct cache_block) = %lu\n",
		    (unsigned long)sizeof(struct cache_block));
	printk_info("sizeof (struct work_item) = %lu\n",
		    (unsigned long)sizeof(struct work_item));
	printk_info("sizeof (struct pmem_header) = %lu\n",
		    (unsigned long)sizeof(struct pmem_header));
	printk_info("sizeof (struct pmem_block_metadata) = %lu\n",
		    (unsigned long)sizeof(struct pmem_block_metadata));

	/* we want this struct to be 64 bytes (hardware dependency) */
	M_ASSERT(sizeof(struct pmem_block_metadata) == 64);

	return 0;
}

void __exit dm_cache_exit(void)
{
	M_ASSERT(cache_kobj != NULL);
	printk_info("bcache_kobj=%p\n", cache_kobj);
	kobject_put(cache_kobj);

	dm_unregister_target(&cache_target);

	printk_info("kmem_buffers_in_use=%u\n", kmem_buffers_in_use());
	M_ASSERT(kmem_buffers_in_use() == 0);
}

/* Module hooks */
module_init(dm_cache_init);
module_exit(dm_cache_exit);

MODULE_ALIAS("dm-bittern_cache");

MODULE_DESCRIPTION(DM_NAME " bittern_cache target");
MODULE_AUTHOR("Fio Cattaneo <fio@twitter.com> <fio@cattaneo.us>");
MODULE_LICENSE("GPL");
