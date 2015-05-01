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

#ifndef BITTERN_CACHE_STATES_H
#define BITTERN_CACHE_STATES_H

/*
 * cache states
 *
 * INVALID means the cache entry has not valid data
 * VALID means the cache entry has valid data and is available for
 * reading/writing
 * READ_MISS states are for read misses
 * READ_HIT state is for read hit
 * WRITE_MISS and WRITE_HIT states are for write hits and misses to cache
 *
 * partial writes are writes which are less than a page. in such case we need to
 * a read-modify-write cycle for the miss case.
 * the hit case is the same as the full page write, except we only copy part of
 * the data into the cache.
 *
 * we keep WT and WB state transition modes completely separate. in this way we
 * have completely unambiguous states.
 * the only exception to "unambiguous" states is "VALID_NO_DATA". this state is
 * returned by a read or write miss with a cache block
 * already prefilled with the block id.
 * we need this to insure serialization between releasing the lock in read/write
 * miss and starting the next state transition. this is just
 * an artifact of the SMP locking and is not a "real" state.
 *
 * the states ending with "_ENDIO" denote the endio state transtion of a block
 * i/o request.
 *
 * possible cache state transitions are:
 *
 * read miss (wt/wb-clean):
 *      INVALID -->
 *      VALID_CLEAN_NO_DATA -->
 *      VALID_CLEAN_READ_MISS_COPY_FROM_DEVICE_STARTIO -->
 *      VALID_CLEAN_READ_MISS_COPY_FROM_DEVICE_ENDIO -->
 *      VALID_CLEAN_READ_MISS_COPY_TO_CACHE_END -->
 *      VALID_CLEAN
 *
 * read hit (wt/wb-clean):
 *      VALID_CLEAN -->
 *      VALID_CLEAN_READ_HIT_COPY_FROM_CACHE_START -->
 *      VALID_CLEAN_READ_HIT_COPY_FROM_CACHE_END -->
 *      VALID_CLEAN
 *
 * read hit (wb-dirty):
 *      VALID_DIRTY -->
 *      VALID_DIRTY_READ_HIT_COPY_FROM_CACHE_START -->
 *      VALID_DIRTY_READ_HIT_COPY_FROM_CACHE_END -->
 *      VALID_DIRTY
 *
 * write miss (wt):
 *      INVALID -->
 *      VALID_CLEAN_NO_DATA -->
 *      VALID_CLEAN_WRITE_MISS_COPY_TO_DEVICE_STARTIO -->
 *      VALID_CLEAN_WRITE_MISS_COPY_TO_DEVICE_ENDIO -->
 *      VALID_CLEAN_WRITE_MISS_COPY_TO_CACHE_END -->
 *      VALID_CLEAN
 *
 * write miss (wb):
 *      INVALID -->
 *      VALID_DIRTY_NO_DATA -->
 *      VALID_DIRTY_WRITE_MISS_COPY_TO_CACHE_START -->
 *      VALID_DIRTY_WRITE_MISS_COPY_TO_CACHE_END -->
 *      VALID_DIRTY
 *
 * [ write hit (wt) ] uses the same states as [ write miss (wb) ]
 * write hit (wt):
 *      VALID_CLEAN -->
 *      VALID_CLEAN_WRITE_HIT_COPY_TO_DEVICE_STARTIO  -->
 *      VALID_CLEAN_WRITE_HIT_COPY_TO_DEVICE_ENDIO  -->
 *      VALID_CLEAN_WRITE_HIT_COPY_TO_CACHE_END -->
 *      VALID_CLEAN
 *
 * [ partial write hit (wt) ] uses the same states as [ write miss (wb) ] plus
 * the initial copy-from-cache phase write hit (wt):
 *      VALID_CLEAN -->
 *      VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_FROM_CACHE_START -->
 *      VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_TO_DEVICE_STARTIO  -->
 *      VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_TO_DEVICE_ENDIO  -->
 *      VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_TO_CACHE_END -->
 *      VALID_CLEAN
 *
 * [ partial write hit (wb-clean) ] uses the same states as [ write miss (wb) ]
 * plus the initial copy-from-cache phase write hit (wb-clean):
 *      VALID_CLEAN -->
 *      VALID_DIRTY_PARTIAL_WRITE_HIT_COPY_FROM_CACHE_START -->
 *      VALID_DIRTY_PARTIAL_WRITE_HIT_COPY_TO_CACHE_START -->
 *      VALID_DIRTY_PARTIAL_WRITE_HIT_COPY_TO_CACHE_END -->
 *      VALID_DIRTY
 *
 * on a dirty write hit, we first need to clone the cache block.
 * we then:
 * 1. for partial writes, copy the original block into the clone (partial clone)
 * otherwise just do #2 (full clone)
 * 2. copy data into the clone
 * 3. update the clone to VALID_DIRTY
 * 4. update the original to INVALID
 *
 * dirty write hit (dirty write cloning - clone):
 *      VALID_DIRTY_NO_DATA -->
 *      VALID_DIRTY_WRITE_HIT_DWC_COPY_TO_CACHE_START -->
 *      VALID_DIRTY_WRITE_HIT_DWC_COPY_TO_CACHE_END -->
 *      VALID_DIRTY
 *
 * partial dirty write hit (dirty write cloning - clone):
 *      VALID_DIRTY_NO_DATA -->
 *      VALID_DIRTY_PARTIAL_WRITE_HIT_DWC_COPY_FROM_ORIGINAL_CACHE_START -->
 *      VALID_DIRTY_PARTIAL_WRITE_HIT_DWC_COPY_TO_CLONED_CACHE_START -->
 *      VALID_DIRTY_PARTIAL_WRITE_HIT_DWC_COPY_TO_CLONED_CACHE_END -->
 *      VALID_DIRTY
 *
 * partial write miss (wt):
 *      INVALID -->
 *      VALID_CLEAN_NO_DATA -->
 *      VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO -->
 *      VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO -->
 *      VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_TO_DEVICE_ENDIO -->
 *      VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END -->
 *      VALID_CLEAN
 *
 * partial write miss (wb):
 *      INVALID -->
 *      VALID_DIRTY_NO_DATA -->
 *      VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO -->
 *      VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO -->
 *      VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END -->
 *
 * verify (clean-wt/wb):
 *      VALID_CLEAN -->
 *      CACHE_VALID_CLEAN_VERIFY -->
 *      VALID_CLEAN
 *
 * writeback_flush (wb):
 *      VALID_DIRTY -->
 *      VALID_DIRTY_WRITEBACK_COPY_FROM_CACHE_START -->
 *      VALID_DIRTY_WRITEBACK_COPY_FROM_CACHE_END -->
 *      VALID_DIRTY_WRITEBACK_COPY_TO_DEVICE_ENDIO -->
 *      VALID_DIRTY_WRITEBACK_UPDATE_METADATA_END -->
 *      VALID_CLEAN
 *
 * writeback_invalidate (wb):
 *      VALID_DIRTY -->
 *      VALID_DIRTY_WRITEBACK_INV_COPY_FROM_CACHE_START -->
 *      VALID_DIRTY_WRITEBACK_INV_COPY_FROM_CACHE_END -->
 *      VALID_DIRTY_WRITEBACK_INV_COPY_TO_DEVICE_ENDIO -->
 *      VALID_DIRTY_WRITEBACK_INV_UPDATE_METADATA_END -->
 *      INVALID
 *
 * clean invalidation (wt/wb):
 *      VALID_CLEAN -->
 *      VALID_CLEAN_INVALIDATE_START,
 *      VALID_CLEAN_INVALIDATE_END,
 *      INVALID
 *
 * dirty invalidation (wb):
 * [ this is also used to invalidate the original block on write cloning ]
 *      VALID_DIRTY -->
 *      VALID_DIRTY_INVALIDATE_START,
 *      VALID_DIRTY_INVALIDATE_END,
 *      INVALID
 *
 * if a hit in cache is made, and the block is in VALID_CLEAN or VALID_DIRTY
 * state, the requester owns the block, otherwise it does not and it has to
 * postpone the request.
 *
 * in the write hit (wb-dirty) case we need to clone the cache block being
 * written in order to maintain write atomicity.
 *
 */

enum transition_path {
	CACHE_TRANSITION_PATH_NONE = 0,	/* keep this entry first */
	CACHE_TRANSITION_PATH_READ_MISS_WTWB_CLEAN,
	CACHE_TRANSITION_PATH_READ_HIT_WTWB_CLEAN,
	CACHE_TRANSITION_PATH_READ_HIT_WB_DIRTY,
	CACHE_TRANSITION_PATH_WRITE_MISS_WT,
	CACHE_TRANSITION_PATH_WRITE_MISS_WB,
	CACHE_TRANSITION_PATH_WRITE_HIT_WT,
	CACHE_TRANSITION_PATH_PARTIAL_WRITE_HIT_WT,
	CACHE_TRANSITION_PATH_WRITE_HIT_WB_CLEAN,
	CACHE_TRANSITION_PATH_PARTIAL_WRITE_HIT_WB_CLEAN,
	CACHE_TRANSITION_PATH_PARTIAL_WRITE_HIT_WB_DIRTY_DWC_CLONE,
	CACHE_TRANSITION_PATH_WRITE_HIT_WB_DIRTY_DWC_CLONE,
	CACHE_TRANSITION_PATH_PARTIAL_WRITE_MISS_WT,
	CACHE_TRANSITION_PATH_PARTIAL_WRITE_MISS_WB,
	CACHE_TRANSITION_PATH_WRITEBACK_WB,
	CACHE_TRANSITION_PATH_WRITEBACK_INV_WB,
	CACHE_TRANSITION_PATH_CLEAN_INVALIDATION_WTWB,
	CACHE_TRANSITION_PATH_DIRTY_INVALIDATION_WB,
	CACHE_TRANSITION_PATH_VERIFY_CLEAN_WTWB,
	__CACHE_TRANSITION_PATHS_NUM,	/* keep this entry last */
};
#define CACHE_TRANSITION_PATH_VALID(_p) \
	((_p) >= CACHE_TRANSITION_PATH_NONE && (_p) < \
	__CACHE_TRANSITION_PATHS_NUM)

/*!
 * Allowable cache states. For the three "persistent" states
 * which are in persistent memory (pmem cache) we need to
 * have the binary values constant in order to keep compatibility
 * across versions.
 * Transient states need not be compatible as they all get rolled back
 * on recovery.
 */
enum cache_state {
	CACHE_INVALID = P_CACHE_INVALID,
	CACHE_VALID_CLEAN = P_CACHE_VALID_CLEAN,
	CACHE_VALID_DIRTY = P_CACHE_VALID_DIRTY,
	CACHE_VALID_CLEAN_NO_DATA,
	CACHE_VALID_CLEAN_READ_HIT_COPY_FROM_CACHE_START,
	CACHE_VALID_CLEAN_READ_HIT_COPY_FROM_CACHE_END,
	CACHE_VALID_CLEAN_READ_MISS_COPY_FROM_DEVICE_STARTIO,
	CACHE_VALID_CLEAN_READ_MISS_COPY_FROM_DEVICE_ENDIO,
	CACHE_VALID_CLEAN_READ_MISS_COPY_TO_CACHE_END,
	CACHE_VALID_CLEAN_WRITE_MISS_COPY_TO_DEVICE_STARTIO,
	CACHE_VALID_CLEAN_WRITE_MISS_COPY_TO_DEVICE_ENDIO,
	CACHE_VALID_CLEAN_WRITE_MISS_COPY_TO_CACHE_END,
	CACHE_VALID_CLEAN_WRITE_HIT_COPY_TO_DEVICE_STARTIO,
	CACHE_VALID_CLEAN_WRITE_HIT_COPY_TO_DEVICE_ENDIO,
	CACHE_VALID_CLEAN_WRITE_HIT_COPY_TO_CACHE_END,
	CACHE_VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_FROM_CACHE_START,
	CACHE_VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_TO_DEVICE_STARTIO,
	CACHE_VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_TO_DEVICE_ENDIO,
	CACHE_VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_TO_CACHE_END,
	CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO,
	CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO,
	CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END,
	CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_TO_DEVICE_ENDIO,
	CACHE_VALID_CLEAN_INVALIDATE_START,
	CACHE_VALID_CLEAN_INVALIDATE_END,
	CACHE_VALID_CLEAN_VERIFY,
	CACHE_VALID_DIRTY_NO_DATA,
	CACHE_VALID_DIRTY_READ_HIT_COPY_FROM_CACHE_START,
	CACHE_VALID_DIRTY_READ_HIT_COPY_FROM_CACHE_END,
	CACHE_VALID_DIRTY_WRITE_MISS_COPY_TO_CACHE_START,
	CACHE_VALID_DIRTY_WRITE_MISS_COPY_TO_CACHE_END,
	CACHE_VALID_DIRTY_WRITE_HIT_COPY_TO_CACHE_START,
	CACHE_VALID_DIRTY_WRITE_HIT_COPY_TO_CACHE_END,
	CACHE_VALID_DIRTY_PARTIAL_WRITE_HIT_COPY_FROM_CACHE_START,
	CACHE_VALID_DIRTY_PARTIAL_WRITE_HIT_COPY_TO_CACHE_START,
	CACHE_VALID_DIRTY_PARTIAL_WRITE_HIT_COPY_TO_CACHE_END,
	/*
	 * dirty write cloning -- cloned block -- full clone simply writes a
	 * new block
	 */
	CACHE_VALID_DIRTY_WRITE_HIT_DWC_COPY_TO_CACHE_START,
	CACHE_VALID_DIRTY_WRITE_HIT_DWC_COPY_TO_CACHE_END,
	/*
	 * dirty write cloning -- cloned block -- partial write, so duplicate
	 * the original
	 */
	CACHE_VALID_DIRTY_PARTIAL_WRITE_HIT_DWC_COPY_FROM_ORIGINAL_CACHE_START,
	CACHE_VALID_DIRTY_PARTIAL_WRITE_HIT_DWC_COPY_TO_CLONED_CACHE_START,
	CACHE_VALID_DIRTY_PARTIAL_WRITE_HIT_DWC_COPY_TO_CLONED_CACHE_END,
	CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO,
	CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO,
	CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END,
	CACHE_VALID_DIRTY_WRITEBACK_COPY_FROM_CACHE_START,
	CACHE_VALID_DIRTY_WRITEBACK_COPY_FROM_CACHE_END,
	CACHE_VALID_DIRTY_WRITEBACK_COPY_TO_DEVICE_ENDIO,
	CACHE_VALID_DIRTY_WRITEBACK_UPDATE_METADATA_END,
	CACHE_VALID_DIRTY_WRITEBACK_INV_COPY_FROM_CACHE_START,
	CACHE_VALID_DIRTY_WRITEBACK_INV_COPY_FROM_CACHE_END,
	CACHE_VALID_DIRTY_WRITEBACK_INV_COPY_TO_DEVICE_ENDIO,
	CACHE_VALID_DIRTY_WRITEBACK_INV_UPDATE_METADATA_END,
	CACHE_VALID_DIRTY_INVALIDATE_START,
	CACHE_VALID_DIRTY_INVALIDATE_END,
	__CACHE_STATES_NUM,	/* keep this entry last */
};
#define CACHE_STATE_VALID(_s) \
	((_s) >= CACHE_INVALID && (_s) < __CACHE_STATES_NUM)

#endif /* BITTERN_CACHE_STATES_H */
