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

#ifndef BITTERN_CACHE_TODO_H
#define BITTERN_CACHE_TODO_H

/*! winterbird tags and branches */
struct TAGS_AND_BRANCHES_WINTERBIRD {
	/*! READ_ONLY for automated stress testing purposes.
	 * winterbird__3_pmem_api
	 * this branch has the pmem_api work */
	int winterbird__3_pmem_api;
	/*! READ_ONLY for automated stress testing purposes.
	 * winterbird__7_hw_inval_bg7
	 * this branch has the invalidate bug fixes,
	 * batched writes and bgwriter policies changes */
	int winterbird__7_hw_inval_bg7;
	/* bittern-next-next
	 * child branch of bittern-next as of 2015-02-03 around oon */
	int bittern_next_next;
	/*! READ_ONLY for automated stress testing purposes.
	 * bittern-nn_winterbird_3_pmem_api
	 * functionally identical to winterbird__3_pmem_api,
	 * but on bittern-next-next branch, slated for master merge. */
	int bittern_nn_winterbird_3_pmem_api;
	/*! READ_ONLY for automated stress testing purposes.
	 * bittern-nn_winterbird_7bg7_inval
	 * functionally identical to winterbird__7_hw_inval_bg7,
	 * but on bittern-next-next branch, slated for master merge. */
	int bittern_nn_winterbird_7bg7_inval;
	/*! bittern-nn_winterbird_8_ridgefield
	 * branch of bittern-next-next taken on 2015-02-03 around noon. */
	int bittern_nn_winterbird_8_ridgefield;
	/*! READ_WRITE for doxygen writing purposes.
	 * bittern-nn_winterbird_8a_ridgefield
	 * working branch for doxygen documentation.
	 * child branch of bittern-next-next. */
	int bittern_nn_winterbird_8a_ridgefield;
};

struct TODO_ONGOING {
	int x;
};

/*!
 * The American Bittern (Botaurus lentiginosus) is a stalking bird of the Heron
 * family. Unlike his well-known "cousin" the Great Blue Heron, the Bittern is
 * a very shy bird, very difficult to spot and photograph.
 * It can be found in wetlands through out through out North America, see
 * http://www.allaboutbirds.org/guide/American_Bittern/id for Habitat map.
 *
 * National Wildlife Refuges, "NWRs", (http://www.fws.gov/refuges/index.html)
 * are among the best places to find wildlife in the United States.
 * Bittern release codenames are all names of NWRs.
 *
 * This Bittern picture was taken in Ridgefield NWR:
 *
 * http://photo.cattaneo.us/Nature-Washington-US/Ridgefield-NWR/i-JktxFmG/A
 *
 */
struct ABOUT_BITTERN {
	int x;
};

/*! http://www.fws.gov/refuge/grays_harbor/ */
struct TODO_BITTERN_RELEASE_01_GRAYS_HARBOR {
	int done;
};

/*! http://www.fws.gov/refuge/Ridgefield/ */
struct TODO_BITTERN_RELEASE_02_RIDGEFIELD {
	int done;
};

/*! http://www.fws.gov/refuge/Julia_Butler_Hansen/ */
struct TODO_BITTERN_RELEASE_03_JBH__JULIA_BUTLER_HANSEN {
	/*! \todo TODO_RELEASE_03_JBH @mmullins DAX integration (possible after winterbird__3_pmem_api is checked in) */
	    int todo_os3186;
	/*! \todo TODO_RELEASE_03_JBH congestion control for io submission */
	    int todo2;
	/*! \todo TODO_RELEASE_03_JBH @fcattaneo winterbird__6_hw_inval BRANCH design docs and review */
	    int todo_winterbird__6_hw_inval;
	/*! \todo TODO_RELEASE_03_JBH @fcattaneo winterbird__7_hw_inval_bg7 BRANCH design docs and review */
	    int todo_winterbird__7_hw_inval_bg7;
	/*! \todo TODO_RELEASE_03_JBH speed up cache creation/restore/teardown */
	    int todo_os2947;
	/*! \todo TODO_RELEASE_03_JBH part 2 of os3184 (auto scan lvm devices and auto restore caches) */
	    int todo_os3190;
	/*! \todo TODO_RELEASE_02_RIDGEFIELD @mmullins some level of LVM integration */
	    int todo_os3187;
	/*! \todo TODO_RELEASE_02_RIDGEFIELD @msharbiani systemd integration */
	    int todo3;
};

/*! http://www.fws.gov/refuge/Bosque_del_Apache/ */
struct TODO_BITTERN_RELEASE_04_BOSQUE_DEL_APACHE {
	/*! \todo TODO_RELEASE_03_BOSQUE_DEL_APACHE @fcattaneo winterbird/hackweek project -- write data and metadata in one single operation */
	    int todo_os2944;
	/*! \todo TODO_RELEASE_03_BOSQUE_DEL_APACHE @msharbiani add multi volume support -- for instance add feature to save conf to a conf file) */
	    int todo_os3111;
	/*! \todo TODO_RELEASE_03_BOSQUE_DEL_APACHE @msharbiani add functional tests for all cache transitions */
	    int todo_os3077;
};

/*! http://www.fws.gov/klamathbasinrefuges/ http://www.fws.gov/refuge/Lower_Klamath/ */
struct TODO_BITTERN_RELEASE_05_KLAMATH_FALLS {
	/*! \todo TODO_RELEASE_05_KLAMATH_FALLS need to handle SEQ_FLUSH, SEQ_FUA in cache */
	    int todo10;
	/*! \todo TODO_RELEASE_05_KLAMATH_FALLS need to handle SEQ_DISCARD in cache */
	    int todo11;
	/*! \todo TODO_RELEASE_05_KLAMATH_FALLS need to properly set SEQ_FLUSH or SEQ_FUA when doing writebacks */
	    int todo12;
};

/*! http://www.fws.gov/refuge/sacramento/ */
struct TODO_BITTERN_RELEASE_06_SACRAMENTO_COMPLEX {
	/*! \todo TODO_RELEASE_06_SACRAMENTO_COMPLEX @ALL error handling and recovery - this is by far the biggest todo */
	    int todo14;
	/*! \todo TODO_RELEASE_06_SACRAMENTO_COMPLEX @ALL need to fail restore if too many entries are corrupt/transient */
	    int todo8;
};

/*! this is a fake struct for TODOs which don't fit yet in a release plan */
struct TODO_BITTERN_RELEASE_UNASSIGNED {
	/*! \todo UNASSIGNED design reviews */ int todo66;
	/*! \todo UNASSIGNED security review */ int todo77;
	/*! \todo UNASSIGNED understand why bonnies takes too long in some cases */
	    int todo_os3071;
	/*! \todo UNASSIGNED shrink memory usage of @ref cache_block array */
	    int todo_os2466;
	/*! \todo UNASSIGNED add pmem api to tell whether async calls are actually sync and use to optimize some paths (like submit bio path in writeback - see queue_work()) */
	    int todo1;
	/*! \todo UNASSIGNED use murmurhash 64 or 128 bits instead of crc32c */
	    int todo3;
	/*! \todo UNASSIGNED should we have macros to init/deinit dbi_data ? */
	    int todo4;
	/*! \todo UNASSIGNED do we really need to use a vmalloced buffer to read metadata? */
	    int todo5;
	/*! \todo UNASSIGNED for full buffer read/writes, no need have a vmalloced buffer */
	    int todo6;
	/*! \todo UNASSIGNED add sequential write bypass */ int todo9;
	/*! \todo UNASSIGNED sending a force flush for every write is very expensive on SSD, it's better to do a bunch of writes and then flush all of them together */
	    int todo13;
	/*! \todo UNASSIGNED need to control which elevator is used (noop for cache, deadline/noop for cached device) */
	    int todo15;
	/*! \todo UNASSIGNED need to implement dm callbacks for presuspend/postresume? */
	    int todo16;
	/*! \todo UNASSIGNED use dmsetup messages for all tunable params change and/or misc commands */
	    int todo17;
	/*! \todo UNASSIGNED do not crash on assert, fail every i/o --> allows for debugging ??? (or at least make it tunable whether to crash or not) */
	    int todo18;
	/*! \todo UNASSIGNED shrink down cache_block metadata to as small as practical size (keep readability) */
	    int todo19;
	/*! \todo UNASSIGNED should we use the dm-provided extra data embedded in the bio struct? */
	    int todo20;
	/*! \todo UNASSIGNED think about whether to use 8k, 16k, 32k cache block size vs. good multipage support */
	    int todo21;
	/*! \todo UNASSIGNED shoud get rid of cache_bio_request.c and just use the state machine ? if not, just simplify its code */
	    int todo22;
	/*! \todo UNASSIGNED replace list_first_entry with list_first_entry_or_null(ptr, type, member) -- much easier to use and read */
	    int todo23;
	/*! \todo UNASSIGNED replace global bc_spinlock with one spinlock per list? */
	    int todo24;
	/*! \todo UNASSIGNED handle xid rollover (use tcp sequence # technique) */
	    int todo25;
	/*! \todo UNASSIGNED allow to change back from writeback to writethrough */
	    int todo26;
	/*! \todo UNASSIGNED for most debug/trace stuff, should think of use printk rate limit instead of printk (or at least, for the very verbose one) -- FREEBSD has a really good implementation for this */
	    int todo27;
	/*! \todo UNASSIGNED most of atomic_t stats need to change to atomic64_t stats */
	    int todo28;
	/*! \todo UNASSIGNED in some cases (write miss / write hit) we don't really need to clone bio -- the code is cleaner however */
	    int todo30;
};

#endif /* BITTERN_CACHE_TODO_H */
