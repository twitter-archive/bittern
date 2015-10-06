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

#ifndef BITTERN_CACHE_MODULE_H
#define BITTERN_CACHE_MODULE_H

extern const char cache_git_generated[];
extern const char cache_git_branch[];
extern const char cache_git_describe[];
extern const char cache_git_status[];
extern const char cache_git_tag[];
extern const char cache_git_files[];

/*! init sysfs */
extern void cache_sysfs_init(struct bittern_cache *bc);
/*! add bittern entry to sysfs */
extern int cache_sysfs_add(struct bittern_cache *bc);
/*! deinit sysfs */
extern void cache_sysfs_deinit(struct bittern_cache *bc);

extern int cache_ctr(struct dm_target *ti, unsigned int argc, char **argv);
extern void cache_dtr_pre(struct dm_target *ti);
extern void cache_dtr(struct dm_target *ti);

extern int cache_calculate_max_pending(struct bittern_cache *bc,
				       int max_requests);
extern int cache_calculate_min_invalid(struct bittern_cache *bc,
				       int min_invalid_count);

extern int cache_message(struct bittern_cache *bc, int argc, char **argv);

#endif /* BITTERN_CACHE_MODULE_H */
