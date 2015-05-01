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

#include <math128.h>
#include <murmurhash3.h>
#include "../bittern_cache_kmod/bittern_cache_pmem_header.h"

#define SECTOR_SIZE     512     /*XXX*/
#define PAGE_SIZE       4096    /*XXX*/
#define ROUND_UP(__sz, __align) (((__sz) + (__align - 1)) & ~(__align - 1))

#define ULL_CAST(__x)                           ((unsigned long long)(__x))

int bc_print_verbose_flag = 0;
int bc_print_debug_flag = 0;
int bc_print_silent_flag = 0;
uint64_t bc_start_sector_offset = 0;

#define bc_print_debug(fmt, ...) \
	((!bc_print_silent_flag && bc_print_debug_flag) ? \
	printf("bc_hash: debug: " fmt, ##__VA_ARGS__) : (void)0)

#define bc_print_verbose(fmt, ...) \
	((!bc_print_silent_flag && bc_print_verbose_flag) ? \
	printf("bc_hash: debug: " fmt, ##__VA_ARGS__) : (void)0)

#define bc_print_warning(fmt, ...) \
	((!bc_print_silent_flag) ? \
	printf("bc_hash: warning: " fmt, ##__VA_ARGS__) : (void)0)

#define bc_print_info(fmt, ...) \
	((!bc_print_silent_flag) ? \
	 printf("bc_hash: info: " fmt, ##__VA_ARGS__) : (void)0)

#define bc_print_err(fmt, ...) \
	((!bc_print_silent_flag) ? \
	printf("bc_hash: error: " fmt, ##__VA_ARGS__) : (void)0)

void bc_read(const char *cache_device)
{
	int fd;
	unsigned int cache_block;
	struct stat stbuf;
	int ret;

	bc_print_debug("bc_hash: bc_read(%s)\n", cache_device);
	bc_print_debug("bc_hash: bc_read: sizeof(struct pmem_header) = %lu\n",
		       sizeof(struct pmem_header));

	ret = stat(cache_device, &stbuf);
	if (ret < 0) {
		printf("bc_hash: bc_read: cannot stat %s\n", cache_device);
		exit(5);
	}
	bc_print_debug("bc_hash: file_cache_device.st_size=%lu\n",
		       (unsigned long)stbuf.st_size);
	bc_print_debug("bc_hash: file_cache_device.st_blocks=%lu\n",
		       (unsigned long)stbuf.st_blocks);
	bc_print_debug("bc_hash: file_cache_device.st_mode=0x%lx\n",
		       (unsigned long)stbuf.st_mode);
	bc_print_debug("bc_hash: file_cache_device.st_mode.s_isblk()=%d\n",
		       S_ISBLK(stbuf.st_mode) ? 1 : 0);
	bc_print_debug("bc_hash: file_cache_device.st_mode.s_ischr()=%d\n",
		       S_ISCHR(stbuf.st_mode) ? 1 : 0);
	bc_print_debug("bc_hash: file_cache_device.st_dev=0x%lx\n",
		       (unsigned long)stbuf.st_dev);

	if (S_ISBLK(stbuf.st_mode) == 0 && S_ISCHR(stbuf.st_mode) == 0) {
		printf("bc_hash: bc_read: ");
		printf("%s is neither a block not a character device\n",
		       cache_device);
		exit(5);
	}

	fd = open(cache_device, O_RDONLY);
	if (fd < 0) {
		bc_print_err("bc_hash: cannot open %s for read\n",
			     cache_device);
		exit(5);
	}

	bc_start_sector_offset -= (bc_start_sector_offset %
				   (PAGE_SIZE / SECTOR_SIZE));

	lseek(fd, bc_start_sector_offset * 512ULL, SEEK_SET);

	for (cache_block = bc_start_sector_offset;
	     ;
	     cache_block += (PAGE_SIZE / SECTOR_SIZE)) {
		uint128_t hash_computed;
		char pagebuf[PAGE_SIZE];
		int c = read(fd, pagebuf, PAGE_SIZE);

		if (c != PAGE_SIZE)
			break;
		hash_computed = murmurhash3_128(pagebuf, PAGE_SIZE);
		printf("%u " UINT128_FMT "\n",
			cache_block,
			UINT128_ARG(hash_computed));
	}
}

void usage(void)
{
	printf("bc_hash: usage: bc_hash ");
	printf("[-r|--read] [-v|--verbose] [-d|--debug] ");
	printf("[-s|--start sector_offset] ");
	printf("-c|--cache-device <cache-device>\n");
	exit(2);
}

/*
 * program exit codes
 *          x == 0: everything is ok
 *  x > 0 && x < 5: usage error
 *          x == 5: cannot open cache device
 *          x == 6: i/o error on cache device
 *         x == 10: header_0 corrupt (or no header_0)
 *         x == 11: header_1 corrupt (or no header_1)
 *         x == 12: cache block corrupt
 */
int main(int argc, char **argv)
{
	char *cache_device = NULL;
	char command = 'R'; /* default command 'r' */

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	while (1) {
		int c, option_index;
		static struct option long_options[] = {
			{ "read", no_argument, 0, 'r', },
			{ "cache-device", required_argument, 0, 'c', },
			{ "verbose", no_argument, 0, 'v', },
			{ "debug", no_argument, 0, 'd', },
			{ "start", required_argument, 0, 's', },
			{ NULL, 0, 0, 0, },
		};
		c = getopt_long(argc, argv, "rc:vs:d", long_options,
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
		case 'r':
			command = 'R';
			break;
		case 'c':
			cache_device = optarg;
			break;
		case 's':
			bc_start_sector_offset = strtoull(optarg, NULL, 0);
			break;
		default:
			bc_print_err("bc_hash: error: unknown option '%c'\n",
				     c);
			usage();
			/*NOTREACHED*/
		}
	}
done_getopt:
	if (command == ' ') {
		bc_print_err("bc_hash: error: need to specify a command\n");
		usage();
		/*NOTREACHED*/
	}
	if (cache_device == 0) {
		bc_print_err("bc_hash: error: need to specify cache-device\n");
		usage();
		/*NOTREACHED*/
	}

	if (getuid() != 0) {
		bc_print_err("bc_hash: error: only root can execute this command\n");
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
