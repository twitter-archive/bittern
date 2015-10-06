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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <errno.h>
#include <strings.h>
#include <string.h>
#include <getopt.h>
#include <assert.h>
#include <stddef.h>

#include <math128.h>
#include <murmurhash3.h>
#include "../bittern_cache_kmod/bittern_cache_pmem_header.h"

/*
 * this is very poorly coded and should be rewritten
 */

#define SECTOR_SIZE     512     /*XXX*/
#define PAGE_SIZE       4096    /*XXX*/
#define ROUND_UP(__sz, __align) (((__sz) + (__align - 1)) & ~(__align - 1))

#define ULL_CAST(__x)			   ((unsigned long long)(__x))

int bc_print_verbose_flag = 0;
int bc_print_debug_flag = 0;
int bc_print_silent_flag = 0;

int bc_check_data_blocks = 0;

#define bc_print_debug(fmt, ...)				\
	((!bc_print_silent_flag && bc_print_debug_flag) ?	\
		printf("bc_tool: debug: " fmt, ##__VA_ARGS__) :	\
		(void)0)

#define bc_print_verbose(fmt, ...)				\
	((!bc_print_silent_flag && bc_print_verbose_flag) ?	\
		printf("bc_tool: debug: " fmt, ##__VA_ARGS__) : \
		(void)0)

#define bc_print_warning(fmt, ...)				\
	((!bc_print_silent_flag) ?				\
		printf("bc_tool: warning: " fmt, ##__VA_ARGS__) : \
		(void)0)

#define bc_print_info(fmt, ...)					\
	((!bc_print_silent_flag) ?				\
		printf("bc_tool: info: " fmt, ##__VA_ARGS__) :	\
		(void)0)

#define bc_print_err(fmt, ...)					\
	((!bc_print_silent_flag) ?				\
		printf("bc_tool: error: " fmt, ##__VA_ARGS__) : \
		(void)0)

int bc_stat_cb_valid_clean = 0;
int bc_stat_cb_valid_dirty = 0;
int bc_stat_cb_invalid = 0;
int bc_stat_cb_transient = 0;
int bc_stat_cb_corrupt = 0;

int bc_read_header(int fd,
		   unsigned long offset,
		   struct pmem_header *lm,
		   size_t device_size_bytes)
{
	ssize_t sz;
	uint128_t hash_computed;
	uint64_t m_first_offset;
	uint64_t d_first_offset;
	uint64_t cache_size;

	sz = pread(fd, lm, sizeof(struct pmem_header), offset);
	if (sz != sizeof(struct pmem_header)) {
		bc_print_err("bc_read_header(%lu): error reading header\n",
				offset);
		exit(6);
	}
	bc_print_debug("bc_read_header(%lu): magic=0x%x\n",
			offset,
			lm->lm_magic);
	bc_print_info("bc_read_header(%lu): version=%llu\n",
			offset,
			ULL_CAST(lm->lm_version));
	bc_print_info("bc_read_header(%lu): hash=" UINT128_FMT "\n",
			offset,
			UINT128_ARG(lm->lm_hash));
	bc_print_info("bc_read_header(%lu): lm_header_size_bytes=%llu\n",
			offset,
			ULL_CAST(lm->lm_header_size_bytes));
	bc_print_info("bc_read_header(%lu): lm_mcb_size_bytes=%llu\n",
			offset,
			ULL_CAST(lm->lm_mcb_size_bytes));
	bc_print_info("bc_read_header(%lu): lm_cache_size_bytes=%llu\n",
			offset,
			ULL_CAST(lm->lm_cache_size_bytes));
	bc_print_info("bc_read_header(%lu): lm_cache_blocks=%llu\n",
			offset,
			ULL_CAST(lm->lm_cache_blocks));
	bc_print_info("bc_read_header(%lu): lm_cache_layout=%c\n",
			offset,
			lm->lm_cache_layout);
	bc_print_info("bc_read_header(%lu): lm_first_offset_bytes=%llu\n",
			offset,
			ULL_CAST(lm->lm_first_offset_bytes));
	bc_print_info("bc_read_header(%lu): lm_first_data_block_offset_bytes=%llu\n",
			offset,
			ULL_CAST(lm->lm_first_data_block_offset_bytes));
	bc_print_info("bc_read_header(%lu): lm_name=%s\n",
			offset,
			lm->lm_name);
	bc_print_info("bc_read_header(%lu): lm_device_name=%s\n",
			offset,
			lm->lm_device_name);
	bc_print_info("bc_read_header(%lu): lm_xid_first=%llu\n",
			offset,
			ULL_CAST(lm->lm_xid_first));
	bc_print_info("bc_read_header(%lu): lm_xid_current=%llu\n",
			offset,
			ULL_CAST(lm->lm_xid_current));

	if (lm->lm_magic != LM_MAGIC) {
		bc_print_err("bc_read_header(%lu): magic numbers mismatch (0x%x/0x%x)\n",
			     offset,
			     lm->lm_magic,
			     LM_MAGIC);
		return -1;
	}
	if (lm->lm_first_offset_bytes != CACHE_MEM_FIRST_OFFSET_BYTES) {
		bc_print_err("bc_read_header(%lu): first_offset_bytes mismatch (%llu/%llu)\n",
				offset,
				ULL_CAST(lm->lm_first_offset_bytes),
				ULL_CAST(CACHE_MEM_FIRST_OFFSET_BYTES));
		return -1;
	}
	if (lm->lm_cache_layout != CACHE_LAYOUT_SEQUENTIAL &&
	    lm->lm_cache_layout != CACHE_LAYOUT_INTERLEAVED) {
		bc_print_info("bc_read_header(%lu): lm_cache_layout mismatch '%c'\n",
				offset,
				lm->lm_cache_layout);
		return -1;
	}

	hash_computed = murmurhash3_128(lm, PMEM_HEADER_HASHING_SIZE);
	if (uint128_ne(hash_computed, lm->lm_hash)) {
		bc_print_err("bc_read_header(%lu): computed hash=" UINT128_FMT
			     " does not match stored hash=" UINT128_FMT "\n",
			     offset,
			     UINT128_ARG(hash_computed),
			     UINT128_ARG(lm->lm_hash));
		return -1;
	}
	bc_print_info("bc_read_header(%lu): computed hash=" UINT128_FMT " matches stored hash=" UINT128_FMT "\n",
		      offset,
		      UINT128_ARG(hash_computed),
		      UINT128_ARG(lm->lm_hash));

	if (lm->lm_cache_layout == CACHE_LAYOUT_SEQUENTIAL) {
		if (lm->lm_mcb_size_bytes !=
		    sizeof(struct pmem_block_metadata)) {
			bc_print_err("bc_read_header(%lu): metadata size mismatch\n",
					offset);
			return -1;
		}
		m_first_offset = CACHE_MEM_FIRST_OFFSET_BYTES;
		d_first_offset = CACHE_MEM_FIRST_OFFSET_BYTES;
		d_first_offset += lm->lm_cache_blocks *
				  sizeof(struct pmem_block_metadata);
		d_first_offset = ROUND_UP(d_first_offset, PAGE_SIZE);
		cache_size = d_first_offset;
		cache_size += lm->lm_cache_blocks * PAGE_SIZE;
	} else {
		if (lm->lm_mcb_size_bytes != PAGE_SIZE) {
			bc_print_err("bc_read_header(%lu): metadata size mismatch\n",
					offset);
			return -1;
		}
		m_first_offset = CACHE_MEM_FIRST_OFFSET_BYTES;
		d_first_offset = CACHE_MEM_FIRST_OFFSET_BYTES;
		cache_size = d_first_offset;
		cache_size += lm->lm_cache_blocks * (PAGE_SIZE * 2);
	}
	if (m_first_offset != lm->lm_first_offset_bytes) {
		bc_print_err("bc_read_header(%lu): first_offset_bytes mismatch (%llu/%llu)\n",
				offset,
				ULL_CAST(m_first_offset),
				ULL_CAST(lm->lm_first_offset_bytes));
		return -1;
	}
	if (d_first_offset != lm->lm_first_data_block_offset_bytes) {
		bc_print_err("bc_read_header(%lu): first_data_block_offset_bytes mismatch (%llu/%llu)\n",
				offset,
				ULL_CAST(d_first_offset),
				ULL_CAST(lm->lm_first_data_block_offset_bytes));
		return -1;
	}
	if (cache_size > lm->lm_cache_size_bytes) {
		bc_print_err("bc_read_header(%lu): cache_size_bytes mismatch (%llu/%llu)\n",
				offset,
				ULL_CAST(cache_size),
				ULL_CAST(lm->lm_cache_size_bytes));
		return -1;
	}
	if (cache_size < device_size_bytes) {
		bc_print_info("bc_read_header(%lu): cache smaller than device\n",
				offset);
		bc_print_info("bc_read_header(%lu): cache_size=%llu mb\n",
				offset,
				cache_size / (1024ULL * 1024ULL));
		bc_print_info("bc_read_header(%lu): cache_device_size=%lu mb\n",
				offset,
				device_size_bytes / (1024 * 1024));
	}
	if (cache_size > device_size_bytes) {
		bc_print_err("bc_read_header(%lu): cache_size=%llu greater than cache_device_size=%lu\n",
				offset,
				ULL_CAST(cache_size),
				device_size_bytes);
		return -1;
	}

	return 0;
}

int bc_read_cache_block(int fd, unsigned int block_id,
			struct pmem_header *lm,
			uint64_t device_size_bytes)
{
	size_t m_offset;
	size_t data_m_offset;
	struct pmem_block_metadata mcbm;
	ssize_t sz;
	uint128_t hash_computed;
	uint128_t data_hash_computed;
	char databuf[PAGE_SIZE];

	if (lm->lm_cache_layout == CACHE_LAYOUT_SEQUENTIAL) {
		m_offset = lm->lm_first_offset_bytes +
			   (block_id - 1) * lm->lm_mcb_size_bytes;
		data_m_offset = lm->lm_first_data_block_offset_bytes +
				(block_id - 1) * PAGE_SIZE;
	} else {
		data_m_offset = lm->lm_first_offset_bytes +
				(block_id - 1) * (PAGE_SIZE * 2);
		m_offset = data_m_offset + PAGE_SIZE;
	}
	bc_print_debug("bc_read_cache_block(%u): m_offset=%lu, d_offset=%lu\n",
			block_id,
			m_offset,
			data_m_offset);
	sz = pread(fd, &mcbm, sizeof(struct pmem_block_metadata), m_offset);
	if (sz != sizeof(struct pmem_block_metadata)) {
		bc_print_err("bc_tool: bc_read: bc_read_cache_block(%u), m_offset=%lu: error reading metadata block\n",
				block_id,
				m_offset);
		exit(6);
	}
	bc_print_debug("bc_read_cache_block(%u): magic=0x%x\n",
			block_id,
			mcbm.pmbm_magic);
	bc_print_debug("bc_read_cache_block(%u): block_id=%u\n",
			block_id,
			mcbm.pmbm_block_id);
	bc_print_debug("bc_read_cache_block(%u): status=%u\n",
			block_id,
			mcbm.pmbm_status);
	bc_print_debug("bc_read_cache_block(%u): device_sector=%llu\n",
			block_id,
			ULL_CAST(mcbm.pmbm_device_sector));
	bc_print_debug("bc_read_cache_block(%u): hash_data=" UINT128_FMT "\n",
			block_id,
			UINT128_ARG(mcbm.pmbm_hash_data));
	bc_print_debug("bc_read_cache_block(%u): hash_metadata=" UINT128_FMT "\n",
			block_id,
			UINT128_ARG(mcbm.pmbm_hash_metadata));

#if 0
	bc_print_debug("           %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			((unsigned char *)&mcbm)[0],
			((unsigned char *)&mcbm)[1],
			((unsigned char *)&mcbm)[2],
			((unsigned char *)&mcbm)[3],
			((unsigned char *)&mcbm)[4],
			((unsigned char *)&mcbm)[5],
			((unsigned char *)&mcbm)[6],
			((unsigned char *)&mcbm)[7],
			((unsigned char *)&mcbm)[8],
			((unsigned char *)&mcbm)[9],
			((unsigned char *)&mcbm)[10],
			((unsigned char *)&mcbm)[11],
			((unsigned char *)&mcbm)[12],
			((unsigned char *)&mcbm)[13],
			((unsigned char *)&mcbm)[14],
			((unsigned char *)&mcbm)[15]);
	bc_print_debug("           %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			((unsigned char *)&mcbm)[16+0],
			((unsigned char *)&mcbm)[16+1],
			((unsigned char *)&mcbm)[16+2],
			((unsigned char *)&mcbm)[16+3],
			((unsigned char *)&mcbm)[16+4],
			((unsigned char *)&mcbm)[16+5],
			((unsigned char *)&mcbm)[16+6],
			((unsigned char *)&mcbm)[16+7],
			((unsigned char *)&mcbm)[16+8],
			((unsigned char *)&mcbm)[16+9],
			((unsigned char *)&mcbm)[16+10],
			((unsigned char *)&mcbm)[16+11],
			((unsigned char *)&mcbm)[16+12],
			((unsigned char *)&mcbm)[16+13],
			((unsigned char *)&mcbm)[16+14],
			((unsigned char *)&mcbm)[16+15]);
	bc_print_debug("           %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			((unsigned char *)&mcbm)[32+0],
			((unsigned char *)&mcbm)[32+1],
			((unsigned char *)&mcbm)[32+2],
			((unsigned char *)&mcbm)[32+3],
			((unsigned char *)&mcbm)[32+4],
			((unsigned char *)&mcbm)[32+5],
			((unsigned char *)&mcbm)[32+6],
			((unsigned char *)&mcbm)[32+7],
			((unsigned char *)&mcbm)[32+8],
			((unsigned char *)&mcbm)[32+9],
			((unsigned char *)&mcbm)[32+10],
			((unsigned char *)&mcbm)[32+11],
			((unsigned char *)&mcbm)[32+12],
			((unsigned char *)&mcbm)[32+13],
			((unsigned char *)&mcbm)[32+14],
			((unsigned char *)&mcbm)[32+15]);
	bc_print_debug("           %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			((unsigned char *)&mcbm)[48+0],
			((unsigned char *)&mcbm)[48+1],
			((unsigned char *)&mcbm)[48+2],
			((unsigned char *)&mcbm)[48+3],
			((unsigned char *)&mcbm)[48+4],
			((unsigned char *)&mcbm)[48+5],
			((unsigned char *)&mcbm)[48+6],
			((unsigned char *)&mcbm)[48+7],
			((unsigned char *)&mcbm)[48+8],
			((unsigned char *)&mcbm)[48+9],
			((unsigned char *)&mcbm)[48+10],
			((unsigned char *)&mcbm)[48+11],
			((unsigned char *)&mcbm)[48+12],
			((unsigned char *)&mcbm)[48+13],
			((unsigned char *)&mcbm)[48+14],
			((unsigned char *)&mcbm)[48+15]);
#endif

	if (mcbm.pmbm_magic != MCBM_MAGIC) {
		bc_print_err("bc_read_cache_block(%u): wrong magic 0x%x\n",
				block_id,
				mcbm.pmbm_magic);
		exit(12);
	}
	if (mcbm.pmbm_block_id != block_id) {
		bc_print_err("bc_read_cache_block(%u), wrong block_id (%u)\n",
			     block_id, mcbm.pmbm_block_id);
		exit(12);
	}

	hash_computed = murmurhash3_128((void *)&mcbm,
					PMEM_BLOCK_METADATA_HASHING_SIZE);
	if (uint128_ne(hash_computed, mcbm.pmbm_hash_metadata)) {
		bc_print_err("bc_read_cache_block(%u): computed metadata_hash=" UINT128_FMT " does not match stored metadata_hash=" UINT128_FMT "\n",
			     block_id,
			     UINT128_ARG(hash_computed),
			     UINT128_ARG(mcbm.pmbm_hash_metadata));
		bc_stat_cb_corrupt++;
		/* FIXME: abort if there are too many corrupt blocks */
	}
	bc_print_info("bc_read_cache_block(%u): computed metadata_hash=" UINT128_FMT " matches stored metadata_hash=" UINT128_FMT "\n",
		      block_id,
		      UINT128_ARG(hash_computed),
		      UINT128_ARG(mcbm.pmbm_hash_metadata));

	switch (mcbm.pmbm_status) {
	case P_S_INVALID:
		bc_print_verbose("bc_read_cache_block(%u,%llu), ",
				 block_id, ULL_CAST(mcbm.pmbm_device_sector));
		bc_print_verbose("data_m_offset=%lu: state=invalid\n",
				 data_m_offset);
		bc_stat_cb_invalid++;
		return 0;
	case P_S_CLEAN:
		bc_print_verbose("bc_read_cache_block(%u,%llu), ",
				 block_id, ULL_CAST(mcbm.pmbm_device_sector));
		bc_print_verbose("data_m_offset=%lu: state=clean\n",
				 data_m_offset);
		bc_stat_cb_valid_clean++;
		/* need to check data checksum */
		break;
	case P_S_DIRTY:
		bc_print_verbose("bc_read_cache_block(%u,%llu), ",
				 block_id, ULL_CAST(mcbm.pmbm_device_sector));
		bc_print_verbose("data_m_offset=%lu: state=dirty\n",
				 data_m_offset);
		bc_stat_cb_valid_dirty++;
		/* need to check data checksum */
		break;
	default:
		bc_stat_cb_transient++;
		bc_print_warning("bc_read_cache_block(%u,%llu), ",
				 block_id,
				 ULL_CAST(mcbm.pmbm_device_sector));
		bc_print_warning("data_m_offset=%lu, m_offset=%lu: ",
				 data_m_offset, m_offset);
		bc_print_warning("invalid / transient cache state %u (0x%x)\n",
				 mcbm.pmbm_status, mcbm.pmbm_status);
		return -1;
	}

	/* need to check data hash */
	bc_print_debug("bc_read_cache_block(%u), data_m_offset=%lu\n",
		       block_id, data_m_offset);

	sz = pread(fd, &databuf[0], PAGE_SIZE, data_m_offset);
	if (sz != PAGE_SIZE) {
		bc_print_err("bc_tool: bc_read: bc_read_cache_block(%u), m_offset=%lu: error reading data block\n",
			     block_id, m_offset);
		exit(6);
	}

	data_hash_computed = murmurhash3_128(databuf, PAGE_SIZE);
	if (uint128_ne(data_hash_computed, mcbm.pmbm_hash_data)) {
		bc_print_err("bc_read_cache_block(%u): computed data_hash=" UINT128_FMT " does not match stored data_hash=" UINT128_FMT "\n",
			     block_id,
			     UINT128_ARG(hash_computed),
			     UINT128_ARG(mcbm.pmbm_hash_data));
		/* FIXME: should count all errors */
		exit(12);
	}

	bc_print_info("bc_read_cache_block(%u): computed data_hash=" UINT128_FMT " matches stored data_hash=" UINT128_FMT "\n",
		      block_id,
		      UINT128_ARG(hash_computed),
		      UINT128_ARG(mcbm.pmbm_hash_metadata));

	return 0;
}

int read_sector(int fd, uint64_t sector, char buffer[512])
{
	ssize_t sz = pread(fd, &buffer[0], 512, sector * 512);

	return (sz == 512 ? 0 : -1);
}
int can_read_sector(int fd, uint64_t sector)
{
	char buffer[512];

	return read_sector(fd, sector, buffer) == 0;
}

/*!
 * \todo get rid of this hack once we migrate to DAX
 */
uint64_t find_device_last_sector(int fd, uint64_t sector,
				 uint64_t sector_start, uint64_t sector_end)
{
	assert(sector >= sector_start);
	assert(sector <= sector_end);
	bc_print_debug("bc_tool: find_device_size_last_sector(%d): %llu: %llu..%llu\n",
		       fd, (unsigned long long)sector,
		       (unsigned long long)sector_start,
		       (unsigned long long)sector_end);
	if (sector_start == sector_end) {
		bc_print_debug("bc_tool: sector_start == sector_end: %llu\n",
			       (unsigned long long)sector);
		assert(sector == sector_start);
		return sector;
	}
	if (sector == sector_start && sector + 1 == sector_end) {
		bc_print_debug("bc_tool: sector_start + 1 == sector_end\n");
		if (can_read_sector(fd, sector + 1)) {
			bc_print_debug("bc_tool: find_device_size_last_sector(%d): %llu: %llu..%llu: (sector+1)\n",
				       fd, (unsigned long long)sector,
				       (unsigned long long)sector_start,
				       (unsigned long long)sector_end);
			return sector + 1;
		}
		bc_print_debug("bc_tool: find_device_size_last_sector(%d): %llu: %llu..%llu: (sector)\n",
			       fd, (unsigned long long)sector,
			       (unsigned long long)sector_start,
			       (unsigned long long)sector_end);
		return sector;
	}
	if (can_read_sector(fd, sector)) {
		bc_print_debug("bc_tool: find_device_size_last_sector(%d): %llu: %llu..%llu: can read\n",
			       fd, (unsigned long long)sector,
			       (unsigned long long)sector_start,
			       (unsigned long long)sector_end);
		/* can read sector, so the range is [sector .. sector_end] */
		return find_device_last_sector(fd,
					sector + (sector_end - sector) / 2,
					sector,
					sector_end);
	} else {
		bc_print_debug("bc_tool: find_device_size_last_sector(%d): %llu: %llu..%llu: cannot read\n",
			       fd, (unsigned long long)sector,
			       (unsigned long long)sector_start,
			       (unsigned long long)sector_end);
		/*
		 * cannot read sector,
		 * so the range is [sector_start .. sector)
		 */
		return find_device_last_sector(fd,
					sector_start +
					((sector - 1) - sector_start) / 2,
					sector_start,
					sector - 1);
	}
}

/* FIXME: the pmem providers need to show the correct device size in stat() */
unsigned long long find_device_size(int fd)
{
	unsigned long long device_size;
	uint64_t sector_start = 1;
	uint64_t sector_end = ((uint64_t)-1) / 512LL;
	uint64_t sector = sector_start + (sector_end - sector_start) / 2;

	bc_print_debug("bc_tool: find_device_size(%d): %llu..%llu\n",
		       fd, (unsigned long long)sector_start,
		       (unsigned long long)sector_end);
	device_size = find_device_last_sector(fd, sector, sector_start,
					      sector_end) + 1;
	bc_print_debug("bc_tool: find_device_size(%d): %llu..%llu = %llu\n",
		       fd, (unsigned long long)sector_start,
		       (unsigned long long)sector_end, device_size);
	return device_size;
}

void bc_read(const char *cache_device)
{
	struct pmem_header pmem_header_0, pmem_header_1;
	int fd;
	unsigned int block_id;
	unsigned long long device_size;
	unsigned long long device_size_bytes;
	struct stat stbuf;
	int ret, ret0, ret1;
	int fatal;

	bc_print_debug("bc_tool: bc_read(%s)\n", cache_device);
	bc_print_debug("bc_tool: bc_read: sizeof(struct pmem_header) = %lu\n",
		       sizeof(struct pmem_header));

	ret = stat(cache_device, &stbuf);
	if (ret < 0) {
		printf("bc_tool: bc_read: cannot stat %s\n", cache_device);
		exit(5);
	}
	bc_print_debug("bc_tool: file_cache_device.st_size=%lu\n",
		       (unsigned long)stbuf.st_size);
	bc_print_debug("bc_tool: file_cache_device.st_blocks=%lu\n",
		       (unsigned long)stbuf.st_blocks);
	bc_print_debug("bc_tool: file_cache_device.st_mode=0x%lx\n",
		       (unsigned long)stbuf.st_mode);
	bc_print_debug("bc_tool: file_cache_device.st_mode.s_isblk()=%d\n",
		       S_ISBLK(stbuf.st_mode) ? 1 : 0);
	bc_print_debug("bc_tool: file_cache_device.st_mode.s_ischr()=%d\n",
		       S_ISCHR(stbuf.st_mode) ? 1 : 0);
	bc_print_debug("bc_tool: file_cache_device.st_dev=0x%lx\n",
		       (unsigned long)stbuf.st_dev);

	if (S_ISBLK(stbuf.st_mode) == 0 && S_ISCHR(stbuf.st_mode) == 0) {
		printf("%s is neither a block not a character device\n",
		       cache_device);
		exit(5);
	}

	fd = open(cache_device, O_RDONLY);
	if (fd < 0) {
		bc_print_err("bc_tool: cannot open %s for read\n",
			     cache_device);
		exit(5);
	}

	device_size = find_device_size(fd);
	device_size_bytes = device_size * 512;
	bc_print_info("cache-device=%s\n", cache_device);
	bc_print_info("device_size=%llu sectors (%llu bytes) (%llu mbytes)\n",
		      device_size, device_size_bytes,
		      device_size_bytes / (1024 * 1024));

	ret0 = bc_read_header(fd,
			      CACHE_MEM_HEADER_0_OFFSET_BYTES,
			      &pmem_header_0,
			      device_size_bytes);
	ret1 = bc_read_header(fd,
			      CACHE_MEM_HEADER_1_OFFSET_BYTES,
			      &pmem_header_1,
			      device_size_bytes);
	if (ret0 != 0 && ret1 != 0) {
		bc_print_err("bc_tool: both headers corrupt (or no headers)\n");
		exit(10);
	} else if (ret0 != 0) {
		bc_print_err("bc_tool: header_0 corrupt (or no header_0)\n");
		exit(10);
	} else if (ret1 != 0) {
		bc_print_err("bc_tool: header_1 corrupt (or no header_1)\n");
		exit(11);
	}
	if (pmem_header_0.lm_xid_current == pmem_header_1.lm_xid_current) {
		bc_print_info("highest xid %llu header_0 is the same as in header_1\n",
			      ULL_CAST(pmem_header_0.lm_xid_current));
	} else if (pmem_header_0.lm_xid_current >
		   pmem_header_1.lm_xid_current) {
		bc_print_warning("highest xid %llu is in header_0\n",
				 ULL_CAST(pmem_header_0.lm_xid_current));
	} else {
		bc_print_warning("highest xid %llu is in header_1\n",
				 ULL_CAST(pmem_header_1.lm_xid_current));
		/* we are only changing the in-core copy, not the pmem copy */
		pmem_header_0.lm_xid_current = pmem_header_1.lm_xid_current;
	}

	fatal = 0;

	if (bc_check_data_blocks) {
		for (block_id = 1; block_id <= pmem_header_0.lm_cache_blocks;
		     block_id++) {
			if (bc_read_cache_block(fd, block_id, &pmem_header_0,
						device_size_bytes) < 0)
				fatal++;
		}
		bc_print_info("cache_blocks: valid_clean=%d, valid_dirty=%d, invalid=%d, transient=%d, corrupt=%d\n",
			      bc_stat_cb_valid_clean, bc_stat_cb_valid_dirty,
			      bc_stat_cb_invalid, bc_stat_cb_transient,
			      bc_stat_cb_corrupt);
	}

	if (fatal)
		exit(12);
}

void usage(void)
{
	printf("bc_tool: usage: bc_tool [-r|--read] [-v|--verbose] ");
	printf("[-d|--debug] [-s|--silent] [-b|--check-data-blocks] ");
	printf("-c|--cache-device <cache-device>\n");
	exit(2);
}

/*
 * program exit codes
 *	  x == 0: everything is ok
 *  x > 0 && x < 5: usage error
 *	  x == 5: cannot open cache device
 *	  x == 6: i/o error on cache device
 *	 x == 10: header_0 corrupt (or no header_0)
 *	 x == 11: header_1 corrupt (or no header_1)
 *	 x == 12: cache block corrupt
 */
int main(int argc, char **argv)
{
	char *cache_device = NULL;
	char command = ' ';

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	while (1) {
		int c, option_index;
		static struct option long_options[] = {
			{ "read", no_argument, 0, 'r', },
			{ "cache-device", required_argument, 0, 'c', },
			{ "verbose", no_argument, 0, 'v', },
			{ "silent", no_argument, 0, 's', },
			{ "debug", no_argument, 0, 'd', },
			{ "check-data-blocks", no_argument, 0, 'b', },
			{ NULL, 0, 0, 0, },
		};
		c = getopt_long(argc, argv, "rc:vsdb", long_options,
				&option_index);
		switch (c) {
		case -1:
		case 0:
			goto done_getopt;
		case 'v':
			bc_print_verbose_flag = 1;
			break;
		case 'd':
			bc_print_debug_flag = 1;
			break;
		case 's':
			bc_print_silent_flag = 1;
			break;
		case 'r':
			if (command != ' ') {
				bc_print_err("bc_tool: error: command already specified\n");
				usage();
				/*NOTREACHED*/
			}
			command = 'R';
			break;
		case 'c':
			cache_device = optarg;
			break;
		case 'b':
			bc_check_data_blocks = 1;
			break;
		default:
			bc_print_err("bc_tool: error: unknown option '%c'\n",
				     c);
			usage();
			/*NOTREACHED*/
		}
	}
done_getopt:
	if (command == ' ') {
		bc_print_err("bc_tool: error: need to specify a command\n");
		usage();
		/*NOTREACHED*/
	}
	if (cache_device == 0) {
		bc_print_err("bc_tool: error: need to specify cache-device\n");
		usage();
		/*NOTREACHED*/
	}

	if (getuid() != 0) {
		bc_print_err("bc_tool: error: only root can execute this command\n");
		exit(2);
	}

	switch (command) {
	case 'R':
		bc_read(cache_device);
		break;
	default:
		break;
	}

	return 0;
}
