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

/*! \todo replace switch with a static array of strings */
const char *cache_state_to_str(enum cache_state cache_state)
{
	switch (cache_state) {
	case S_INVALID:
		return "S_INVALID";
	case S_CLEAN:
		return "S_CLEAN";
	case S_CLEAN_NO_DATA:
		return "S_CLEAN_NO_DATA";
	case S_CLEAN_READ_MISS_CPF_DEVICE_STARTIO:
		return "S_CLEAN_READ_MISS_CPF_DEVICE_STARTIO";
	case S_CLEAN_READ_MISS_CPF_DEVICE_ENDIO:
		return "S_CLEAN_READ_MISS_CPF_DEVICE_ENDIO";
	case S_CLEAN_READ_MISS_CPT_CACHE_END:
		return "S_CLEAN_READ_MISS_CPT_CACHE_END";
	case S_CLEAN_READ_HIT_CPF_CACHE_START:
		return "S_CLEAN_READ_HIT_CPF_CACHE_START";
	case S_CLEAN_READ_HIT_CPF_CACHE_END:
		return "S_CLEAN_READ_HIT_CPF_CACHE_END";
	case S_CLEAN_WRITE_MISS_CPT_DEVICE_STARTIO:
		return "S_CLEAN_WRITE_MISS_CPT_DEVICE_STARTIO";
	case S_CLEAN_WRITE_MISS_CPT_DEVICE_ENDIO:
		return "S_CLEAN_WRITE_MISS_CPT_DEVICE_ENDIO";
	case S_CLEAN_WRITE_MISS_CPT_CACHE_END:
		return "S_CLEAN_WRITE_MISS_CPT_CACHE_END";
	case S_CLEAN_WRITE_HIT_CPT_DEVICE_STARTIO:
		return "S_CLEAN_WRITE_HIT_CPT_DEVICE_STARTIO";
	case S_CLEAN_WRITE_HIT_CPT_DEVICE_ENDIO:
		return "S_CLEAN_WRITE_HIT_CPT_DEVICE_ENDIO";
	case S_CLEAN_WRITE_HIT_CPT_CACHE_END:
		return "S_CLEAN_WRITE_HIT_CPT_CACHE_END";
	case S_CLEAN_P_WRITE_HIT_CPF_CACHE_START:
		return
		    "S_CLEAN_P_WRITE_HIT_CPF_CACHE_START";
	case S_CLEAN_P_WRITE_HIT_CPT_DEVICE_STARTIO:
		return
		    "S_CLEAN_P_WRITE_HIT_CPT_DEVICE_STARTIO";
	case S_CLEAN_P_WRITE_HIT_CPT_DEVICE_ENDIO:
		return
		    "S_CLEAN_P_WRITE_HIT_CPT_DEVICE_ENDIO";
	case S_CLEAN_P_WRITE_HIT_CPT_CACHE_END:
		return "S_CLEAN_P_WRITE_HIT_CPT_CACHE_END";
	case S_CLEAN_P_WRITE_MISS_CPF_DEVICE_STARTIO:
		return
		    "S_CLEAN_P_WRITE_MISS_CPF_DEVICE_STARTIO";
	case S_CLEAN_P_WRITE_MISS_CPF_DEVICE_ENDIO:
		return
		    "S_CLEAN_P_WRITE_MISS_CPF_DEVICE_ENDIO";
	case S_CLEAN_P_WRITE_MISS_CPT_CACHE_END:
		return "S_CLEAN_P_WRITE_MISS_CPT_CACHE_END";
	case S_CLEAN_P_WRITE_MISS_CPT_DEVICE_ENDIO:
		return
		    "S_CLEAN_P_WRITE_MISS_CPT_DEVICE_ENDIO";
	case S_CLEAN_INVALIDATE_START:
		return "S_CLEAN_INVALIDATE_START";
	case S_CLEAN_INVALIDATE_END:
		return "S_CLEAN_INVALIDATE_END";
	case S_CLEAN_VERIFY:
		return "S_CLEAN_VERIFY";
	case S_DIRTY:
		return "S_DIRTY";
	case S_DIRTY_NO_DATA:
		return "S_DIRTY_NO_DATA";
	case S_DIRTY_READ_HIT_CPF_CACHE_START:
		return "S_DIRTY_READ_HIT_CPF_CACHE_START";
	case S_DIRTY_READ_HIT_CPF_CACHE_END:
		return "S_DIRTY_READ_HIT_CPF_CACHE_END";
	case S_DIRTY_WRITE_MISS_CPT_CACHE_START:
		return "S_DIRTY_WRITE_MISS_CPT_CACHE_START";
	case S_DIRTY_WRITE_MISS_CPT_CACHE_END:
		return "S_DIRTY_WRITE_MISS_CPT_CACHE_END";
	case S_CLEAN_2_DIRTY_P_WRITE_HIT_DWC_CPF_ORIGINAL_CACHE_START:
		return
		    "S_CLEAN_2_DIRTY_P_WRITE_HIT_DWC_CPF_ORIGINAL_CACHE_START";
	case S_CLEAN_2_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_START:
		return "S_CLEAN_2_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_START";
	case S_CLEAN_2_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_END:
		return "S_CLEAN_2_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_END";
	case S_DIRTY_WRITE_HIT_DWC_CPT_CACHE_START:
		return "S_DIRTY_WRITE_HIT_DWC_CPT_CACHE_START";
	case S_DIRTY_WRITE_HIT_DWC_CPT_CACHE_END:
		return "S_DIRTY_WRITE_HIT_DWC_CPT_CACHE_END";
	case S_CLEAN_2_DIRTY_WRITE_HIT_DWC_CPT_CACHE_START:
		return "S_CLEAN_2_DIRTY_WRITE_HIT_DWC_CPT_CACHE_START";
	case S_CLEAN_2_DIRTY_WRITE_HIT_DWC_CPT_CACHE_END:
		return "S_CLEAN_2_DIRTY_WRITE_HIT_DWC_CPT_CACHE_END";
	case S_DIRTY_P_WRITE_HIT_DWC_CPF_ORIGINAL_CACHE_START:
		return "S_DIRTY_P_WRITE_HIT_DWC_CPF_ORIGINAL_CACHE_START";
	case S_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_START:
		return "S_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_START";
	case S_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_END:
		return "S_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_END";
	case S_DIRTY_P_WRITE_MISS_CPF_DEVICE_STARTIO:
		return
		    "S_DIRTY_P_WRITE_MISS_CPF_DEVICE_STARTIO";
	case S_DIRTY_P_WRITE_MISS_CPF_DEVICE_ENDIO:
		return
		    "S_DIRTY_P_WRITE_MISS_CPF_DEVICE_ENDIO";
	case S_DIRTY_P_WRITE_MISS_CPT_CACHE_END:
		return "S_DIRTY_P_WRITE_MISS_CPT_CACHE_END";
	case S_DIRTY_WRITEBACK_CPF_CACHE_START:
		return "S_DIRTY_WRITEBACK_CPF_CACHE_START";
	case S_DIRTY_WRITEBACK_CPF_CACHE_END:
		return "S_DIRTY_WRITEBACK_CPF_CACHE_END";
	case S_DIRTY_WRITEBACK_CPT_DEVICE_ENDIO:
		return "S_DIRTY_WRITEBACK_CPT_DEVICE_ENDIO";
	case S_DIRTY_WRITEBACK_UPDATE_METADATA_END:
		return "S_DIRTY_WRITEBACK_UPDATE_METADATA_END";
	case S_DIRTY_WRITEBACK_INV_CPF_CACHE_START:
		return "S_DIRTY_WRITEBACK_INV_CPF_CACHE_START";
	case S_DIRTY_WRITEBACK_INV_CPF_CACHE_END:
		return "S_DIRTY_WRITEBACK_INV_CPF_CACHE_END";
	case S_DIRTY_WRITEBACK_INV_CPT_DEVICE_ENDIO:
		return "S_DIRTY_WRITEBACK_INV_CPT_DEVICE_ENDIO";
	case S_DIRTY_WRITEBACK_INV_UPDATE_METADATA_END:
		return "S_DIRTY_WRITEBACK_INV_UPDATE_METADATA_END";
	case S_DIRTY_INVALIDATE_START:
		return "S_DIRTY_INVALIDATE_START";
	case S_DIRTY_INVALIDATE_END:
		return "S_DIRTY_INVALIDATE_END";
	case __CACHE_STATES_NUM:
		break;
	}
	return "CACHE_UNKNOWN";
}

/*! \todo replace switch with a static array of strings */
const char *cache_transition_to_str(enum cache_transition ts)
{
	switch (ts) {
	case TS_NONE:
		return "TS_NONE";
	case TS_READ_MISS_WTWB_CLEAN:
		return "TS_READ_MISS_WTWB_CLEAN";
	case TS_READ_HIT_WTWB_CLEAN:
		return "TS_READ_HIT_WTWB_CLEAN";
	case TS_READ_HIT_WB_DIRTY:
		return "TS_READ_HIT_WB_DIRTY";
	case TS_WRITE_MISS_WT:
		return "TS_WRITE_MISS_WT";
	case TS_WRITE_MISS_WB:
		return "TS_WRITE_MISS_WB";
	case TS_WRITE_HIT_WT:
		return "TS_WRITE_HIT_WT";
	case TS_P_WRITE_HIT_WT:
		return "TS_P_WRITE_HIT_WT";
	case TS_WRITE_HIT_WB_CLEAN_DWC_CLONE:
		return "TS_WRITE_HIT_WB_CLEAN_DWC_CLONE";
	case TS_P_WRITE_HIT_WB_CLEAN_DWC_CLONE:
		return "TS_P_WRITE_HIT_WB_CLEAN_DWC_CLONE";
	case TS_P_WRITE_HIT_WB_DIRTY_DWC_CLONE:
		return "TS_P_WRITE_HIT_WB_DIRTY_DWC_CLONE";
	case TS_WRITE_HIT_WB_DIRTY_DWC_CLONE:
		return "TS_WRITE_HIT_WB_DIRTY_DWC_CLONE";
	case TS_P_WRITE_MISS_WT:
		return "TS_P_WRITE_MISS_WT";
	case TS_P_WRITE_MISS_WB:
		return "TS_P_WRITE_MISS_WB";
	case TS_WRITEBACK_WB:
		return "TS_WRITEBACK_WB";
	case TS_WRITEBACK_INV_WB:
		return "TS_WRITEBACK_INV_WB";
	case TS_CLEAN_INVALIDATION_WTWB:
		return "TS_CLEAN_INVALIDATION_WTWB";
	case TS_DIRTY_INVALIDATION_WB:
		return "TS_DIRTY_INVALIDATION_WB";
	case TS_VERIFY_CLEAN_WTWB:
		return "TS_VERIFY_CLEAN_WTWB";
	case __TS_NUM:
		break;
	}
	return "TS_UNKNOWN";
}

#ifdef ENABLE_ASSERT

struct cache_state_transitions {
	enum cache_transition path_from;
	enum cache_state state_from;
	enum cache_transition path_to;
	enum cache_state state_to;
};

/*!
 * \todo right now this code is only used to validate transitions
 * which are hard coded in the state machine code. at some point this
 * code ought to be cleaned up and ought to be used to drive state transitions
 * directly, so there will be no need for hard coding.
 */
const struct cache_state_transitions cache_valid_state_transitions[] = {
/*
 * read miss (wt/wb-clean):
 * INVALID -->
 * VALID_CLEAN_NO_DATA -->
 * VALID_CLEAN_READ_MISS_CPF_DEVICE_STARTIO -->
 * VALID_CLEAN_READ_MISS_CPF_DEVICE_ENDIO -->
 * VALID_CLEAN_READ_MISS_CPT_CACHE_END -->
 * VALID_CLEAN
 */
	{
	 TS_NONE,
	 S_CLEAN_NO_DATA,
	 TS_READ_MISS_WTWB_CLEAN,
	 S_CLEAN_READ_MISS_CPF_DEVICE_STARTIO,
	 },
	{
	 TS_READ_MISS_WTWB_CLEAN,
	 S_CLEAN_READ_MISS_CPF_DEVICE_STARTIO,
	 TS_READ_MISS_WTWB_CLEAN,
	 S_CLEAN_READ_MISS_CPF_DEVICE_ENDIO,
	 },
	{
	 TS_READ_MISS_WTWB_CLEAN,
	 S_CLEAN_READ_MISS_CPF_DEVICE_ENDIO,
	 TS_READ_MISS_WTWB_CLEAN,
	 S_CLEAN_READ_MISS_CPT_CACHE_END,
	 },
	{
	 TS_READ_MISS_WTWB_CLEAN,
	 S_CLEAN_READ_MISS_CPT_CACHE_END,
	 TS_NONE,
	 S_CLEAN,
	 },

/*
 * read hit (wt/wb-clean):      VALID_CLEAN -->
 *                              VALID_CLEAN_READ_HIT_CPF_CACHE_START -->
 *                              VALID_CLEAN_READ_HIT_CPF_CACHE_END -->
 *                              VALID_CLEAN
 */
	{
	 TS_NONE,
	 S_CLEAN,
	 TS_READ_HIT_WTWB_CLEAN,
	 S_CLEAN_READ_HIT_CPF_CACHE_START,
	 },
	{
	 TS_READ_HIT_WTWB_CLEAN,
	 S_CLEAN_READ_HIT_CPF_CACHE_START,
	 TS_READ_HIT_WTWB_CLEAN,
	 S_CLEAN_READ_HIT_CPF_CACHE_END,
	 },
	{
	 TS_READ_HIT_WTWB_CLEAN,
	 S_CLEAN_READ_HIT_CPF_CACHE_END,
	 TS_NONE,
	 S_CLEAN,
	 },
/*
 * read hit (wb-dirty):         VALID_DIRTY -->
 *                              VALID_DIRTY_READ_HIT_CPF_CACHE_START -->
 *                              VALID_DIRTY_READ_HIT_CPF_CACHE_END -->
 *                              VALID_DIRTY
 */
	{
	 TS_NONE,
	 S_DIRTY,
	 TS_READ_HIT_WB_DIRTY,
	 S_DIRTY_READ_HIT_CPF_CACHE_START,
	 },
	{
	 TS_READ_HIT_WB_DIRTY,
	 S_DIRTY_READ_HIT_CPF_CACHE_START,
	 TS_READ_HIT_WB_DIRTY,
	 S_DIRTY_READ_HIT_CPF_CACHE_END,
	 },
	{
	 TS_READ_HIT_WB_DIRTY,
	 S_DIRTY_READ_HIT_CPF_CACHE_END,
	 TS_NONE,
	 S_DIRTY,
	 },
/*
 * write miss (wt):
 * INVALID -->
 * VALID_CLEAN_NO_DATA -->
 * VALID_CLEAN_WRITE_MISS_CPT_DEVICE_STARTIO -->
 * VALID_CLEAN_WRITE_MISS_CPT_DEVICE_ENDIO -->
 * VALID_CLEAN_WRITE_MISS_CPT_CACHE_END -->
 * VALID_CLEAN
 */
	{
	 TS_NONE,
	 S_CLEAN_NO_DATA,
	 TS_WRITE_MISS_WT,
	 S_CLEAN_WRITE_MISS_CPT_DEVICE_STARTIO,
	 },
	{
	 TS_WRITE_MISS_WT,
	 S_CLEAN_WRITE_MISS_CPT_DEVICE_STARTIO,
	 TS_WRITE_MISS_WT,
	 S_CLEAN_WRITE_MISS_CPT_DEVICE_ENDIO,
	 },
	{
	 TS_WRITE_MISS_WT,
	 S_CLEAN_WRITE_MISS_CPT_DEVICE_ENDIO,
	 TS_WRITE_MISS_WT,
	 S_CLEAN_WRITE_MISS_CPT_CACHE_END,
	 },
	{
	 TS_WRITE_MISS_WT,
	 S_CLEAN_WRITE_MISS_CPT_CACHE_END,
	 TS_NONE,
	 S_CLEAN,
	 },
/*
 * write miss (wb):             INVALID -->
 *                              VALID_DIRTY_NO_DATA -->
 *                              VALID_DIRTY_WRITE_MISS_CPT_CACHE_START -->
 *                              VALID_DIRTY_WRITE_MISS_CPT_CACHE_END -->
 *                              VALID_DIRTY
 */
	{
	 TS_NONE,
	 S_DIRTY_NO_DATA,
	 TS_WRITE_MISS_WB,
	 S_DIRTY_WRITE_MISS_CPT_CACHE_START,
	 },
	{
	 TS_WRITE_MISS_WB,
	 S_DIRTY_WRITE_MISS_CPT_CACHE_START,
	 TS_WRITE_MISS_WB,
	 S_DIRTY_WRITE_MISS_CPT_CACHE_END,
	 },
	{
	 TS_WRITE_MISS_WB,
	 S_DIRTY_WRITE_MISS_CPT_CACHE_END,
	 TS_NONE,
	 S_DIRTY,
	 },
/*
 * [ write hit (wt) ] uses the same states as [ write miss (wb) ]
 * write hit (wt):
 * VALID_CLEAN -->
 * VALID_CLEAN_WRITE_HIT_CPT_DEVICE_STARTIO  -->
 * VALID_CLEAN_WRITE_HIT_CPT_DEVICE_ENDIO  -->
 * VALID_CLEAN_WRITE_HIT_CPT_CACHE_END -->
 * VALID_CLEAN
 */
	{
	 TS_NONE,
	 S_CLEAN,
	 TS_WRITE_HIT_WT,
	 S_CLEAN_WRITE_HIT_CPT_DEVICE_STARTIO,
	 },
	{
	 TS_WRITE_HIT_WT,
	 S_CLEAN_WRITE_HIT_CPT_DEVICE_STARTIO,
	 TS_WRITE_HIT_WT,
	 S_CLEAN_WRITE_HIT_CPT_DEVICE_ENDIO,
	 },
	{
	 TS_WRITE_HIT_WT,
	 S_CLEAN_WRITE_HIT_CPT_DEVICE_ENDIO,
	 TS_WRITE_HIT_WT,
	 S_CLEAN_WRITE_HIT_CPT_CACHE_END,
	 },
	{
	 TS_WRITE_HIT_WT,
	 S_CLEAN_WRITE_HIT_CPT_CACHE_END,
	 TS_NONE,
	 S_CLEAN,
	 },
/*
 * [ partial write hit (wt) ] uses the same states as [ write miss (wb) ] plus
 * the initial copy-from-cache phase
 * write hit (wt):
 * VALID_CLEAN -->
 * VALID_CLEAN_P_WRITE_HIT_CPF_CACHE_START -->
 * VALID_CLEAN_P_WRITE_HIT_CPT_DEVICE_STARTIO  -->
 * VALID_CLEAN_P_WRITE_HIT_CPT_DEVICE_ENDIO  -->
 * VALID_CLEAN_P_WRITE_HIT_CPT_CACHE_END -->
 * VALID_CLEAN
 */
	{
	 TS_NONE,
	 S_CLEAN,
	 TS_P_WRITE_HIT_WT,
	 S_CLEAN_P_WRITE_HIT_CPF_CACHE_START,
	 },
	{
	 TS_P_WRITE_HIT_WT,
	 S_CLEAN_P_WRITE_HIT_CPF_CACHE_START,
	 TS_P_WRITE_HIT_WT,
	 S_CLEAN_P_WRITE_HIT_CPT_DEVICE_STARTIO,
	 },
	{
	 TS_P_WRITE_HIT_WT,
	 S_CLEAN_P_WRITE_HIT_CPT_DEVICE_STARTIO,
	 TS_P_WRITE_HIT_WT,
	 S_CLEAN_P_WRITE_HIT_CPT_DEVICE_ENDIO,
	 },
	{
	 TS_P_WRITE_HIT_WT,
	 S_CLEAN_P_WRITE_HIT_CPT_DEVICE_ENDIO,
	 TS_P_WRITE_HIT_WT,
	 S_CLEAN_P_WRITE_HIT_CPT_CACHE_END,
	 },
	{
	 TS_P_WRITE_HIT_WT,
	 S_CLEAN_P_WRITE_HIT_CPT_CACHE_END,
	 TS_NONE,
	 S_CLEAN,
	 },
/*
 * partial clean to dirty write hit:
 * VALID_DIRTY_NO_DATA -->
 * VALID_CLEAN_2_DIRTY_P_WRITE_HIT_DWC_CPF_ORIGINAL_CACHE_START -->
 * VALID_CLEAN_2_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_START -->
 * VALID_CLEAN_2_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_END -->
 * VALID_DIRTY
 */
	{
	 TS_NONE,
	 S_DIRTY_NO_DATA,
	 TS_P_WRITE_HIT_WB_CLEAN_DWC_CLONE,
	 S_CLEAN_2_DIRTY_P_WRITE_HIT_DWC_CPF_ORIGINAL_CACHE_START,
	 },
	{
	 TS_P_WRITE_HIT_WB_CLEAN_DWC_CLONE,
	 S_CLEAN_2_DIRTY_P_WRITE_HIT_DWC_CPF_ORIGINAL_CACHE_START,
	 TS_P_WRITE_HIT_WB_CLEAN_DWC_CLONE,
	 S_CLEAN_2_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_START,
	 },
	{
	 TS_P_WRITE_HIT_WB_CLEAN_DWC_CLONE,
	 S_CLEAN_2_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_START,
	 TS_P_WRITE_HIT_WB_CLEAN_DWC_CLONE,
	 S_CLEAN_2_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_END,
	 },
	{
	 TS_P_WRITE_HIT_WB_CLEAN_DWC_CLONE,
	 S_CLEAN_2_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_END,
	 TS_NONE,
	 S_DIRTY,
	 },
/*
 * partial dirty to dirty write hit:
 * VALID_DIRTY_NO_DATA -->
 * VALID_DIRTY_P_WRITE_HIT_DWC_CPF_ORIGINAL_CACHE_START -->
 * VALID_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_START -->
 * VALID_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_END -->
 * VALID_DIRTY
 */
	{
	 TS_NONE,
	 S_DIRTY_NO_DATA,
	 TS_P_WRITE_HIT_WB_DIRTY_DWC_CLONE,
	 S_DIRTY_P_WRITE_HIT_DWC_CPF_ORIGINAL_CACHE_START,
	 },
	{
	 TS_P_WRITE_HIT_WB_DIRTY_DWC_CLONE,
	 S_DIRTY_P_WRITE_HIT_DWC_CPF_ORIGINAL_CACHE_START,
	 TS_P_WRITE_HIT_WB_DIRTY_DWC_CLONE,
	 S_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_START,
	 },
	{
	 TS_P_WRITE_HIT_WB_DIRTY_DWC_CLONE,
	 S_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_START,
	 TS_P_WRITE_HIT_WB_DIRTY_DWC_CLONE,
	 S_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_END,
	 },
	{
	 TS_P_WRITE_HIT_WB_DIRTY_DWC_CLONE,
	 S_DIRTY_P_WRITE_HIT_DWC_CPT_CLONED_CACHE_END,
	 TS_NONE,
	 S_DIRTY,
	 },
/*
 * clean to dirty write hit (dirty write cloning - clone):
 * VALID_DIRTY_NO_DATA -->
 * VALID_DIRTY_WRITE_HIT_DWC_CPT_CACHE_START -->
 * VALID_DIRTY_WRITE_HIT_DWC_CPT_CACHE_END -->
 * VALID_DIRTY
 */
	{
	 TS_NONE,
	 S_DIRTY_NO_DATA,
	 TS_WRITE_HIT_WB_CLEAN_DWC_CLONE,
	 S_CLEAN_2_DIRTY_WRITE_HIT_DWC_CPT_CACHE_START,
	 },
	{
	 TS_WRITE_HIT_WB_CLEAN_DWC_CLONE,
	 S_CLEAN_2_DIRTY_WRITE_HIT_DWC_CPT_CACHE_START,
	 TS_WRITE_HIT_WB_CLEAN_DWC_CLONE,
	 S_CLEAN_2_DIRTY_WRITE_HIT_DWC_CPT_CACHE_END,
	 },
	{
	 TS_WRITE_HIT_WB_CLEAN_DWC_CLONE,
	 S_CLEAN_2_DIRTY_WRITE_HIT_DWC_CPT_CACHE_END,
	 TS_NONE,
	 S_DIRTY,
	 },
/*
 * dirty to dirty write hit (dirty write cloning - clone):
 * VALID_DIRTY_NO_DATA -->
 * VALID_DIRTY_WRITE_HIT_DWC_CPT_CACHE_START -->
 * VALID_DIRTY_WRITE_HIT_DWC_CPT_CACHE_END -->
 * VALID_DIRTY
 */
	{
	 TS_NONE,
	 S_DIRTY_NO_DATA,
	 TS_WRITE_HIT_WB_DIRTY_DWC_CLONE,
	 S_DIRTY_WRITE_HIT_DWC_CPT_CACHE_START,
	 },
	{
	 TS_WRITE_HIT_WB_DIRTY_DWC_CLONE,
	 S_DIRTY_WRITE_HIT_DWC_CPT_CACHE_START,
	 TS_WRITE_HIT_WB_DIRTY_DWC_CLONE,
	 S_DIRTY_WRITE_HIT_DWC_CPT_CACHE_END,
	 },
	{
	 TS_WRITE_HIT_WB_DIRTY_DWC_CLONE,
	 S_DIRTY_WRITE_HIT_DWC_CPT_CACHE_END,
	 TS_NONE,
	 S_DIRTY,
	 },
/*
 * partial write miss (wt):
 * INVALID -->
 * VALID_CLEAN_NO_DATA -->
 * VALID_CLEAN_P_WRITE_MISS_CPF_DEVICE_STARTIO -->
 * VALID_CLEAN_P_WRITE_MISS_CPF_DEVICE_ENDIO -->
 * VALID_CLEAN_P_WRITE_MISS_CPT_DEVICE_ENDIO -->
 * VALID_CLEAN_P_WRITE_MISS_CPT_CACHE_END -->
 * VALID_CLEAN
 */
	{
	 TS_NONE,
	 S_CLEAN_NO_DATA,
	 TS_P_WRITE_MISS_WT,
	 S_CLEAN_P_WRITE_MISS_CPF_DEVICE_STARTIO,
	 },
	{
	 TS_P_WRITE_MISS_WT,
	 S_CLEAN_P_WRITE_MISS_CPF_DEVICE_STARTIO,
	 TS_P_WRITE_MISS_WT,
	 S_CLEAN_P_WRITE_MISS_CPF_DEVICE_ENDIO,
	 },
	{
	 TS_P_WRITE_MISS_WT,
	 S_CLEAN_P_WRITE_MISS_CPF_DEVICE_ENDIO,
	 TS_P_WRITE_MISS_WT,
	 S_CLEAN_P_WRITE_MISS_CPT_DEVICE_ENDIO,
	 },
	{
	 TS_P_WRITE_MISS_WT,
	 S_CLEAN_P_WRITE_MISS_CPT_DEVICE_ENDIO,
	 TS_P_WRITE_MISS_WT,
	 S_CLEAN_P_WRITE_MISS_CPT_CACHE_END,
	 },
	{
	 TS_P_WRITE_MISS_WT,
	 S_CLEAN_P_WRITE_MISS_CPT_CACHE_END,
	 TS_NONE,
	 S_CLEAN,
	 },
/*
 * partial write miss (wb):
 * INVALID -->
 * VALID_DIRTY_NO_DATA -->
 * VALID_DIRTY_P_WRITE_MISS_CPF_DEVICE_STARTIO -->
 * VALID_DIRTY_P_WRITE_MISS_CPF_DEVICE_ENDIO -->
 * VALID_DIRTY_P_WRITE_MISS_CPT_CACHE_END -->
 */
	{
	 TS_NONE,
	 S_DIRTY_NO_DATA,
	 TS_P_WRITE_MISS_WB,
	 S_DIRTY_P_WRITE_MISS_CPF_DEVICE_STARTIO,
	 },
	{
	 TS_P_WRITE_MISS_WB,
	 S_DIRTY_P_WRITE_MISS_CPF_DEVICE_STARTIO,
	 TS_P_WRITE_MISS_WB,
	 S_DIRTY_P_WRITE_MISS_CPF_DEVICE_ENDIO,
	 },
	{
	 TS_P_WRITE_MISS_WB,
	 S_DIRTY_P_WRITE_MISS_CPF_DEVICE_ENDIO,
	 TS_P_WRITE_MISS_WB,
	 S_DIRTY_P_WRITE_MISS_CPT_CACHE_END,
	 },
	{
	 TS_P_WRITE_MISS_WB,
	 S_DIRTY_P_WRITE_MISS_CPT_CACHE_END,
	 TS_NONE,
	 S_DIRTY,
	 },
/*
 * verify (clean-wt/wb):        VALID_CLEAN -->
 *                              S_CLEAN_VERIFY -->
 *                              VALID_CLEAN
 */
	{
	 TS_NONE,
	 S_CLEAN,
	 TS_VERIFY_CLEAN_WTWB,
	 S_CLEAN_VERIFY,
	 },
	{
	 TS_VERIFY_CLEAN_WTWB,
	 S_CLEAN_VERIFY,
	 TS_NONE,
	 S_CLEAN,
	 },
/*
 * writeback_flush (wb):        VALID_DIRTY -->
 *                              VALID_DIRTY_WRITEBACK_CPF_CACHE_START -->
 *                              VALID_DIRTY_WRITEBACK_CPF_CACHE_END -->
 *                              VALID_DIRTY_WRITEBACK_CPT_DEVICE_ENDIO -->
 *                              VALID_DIRTY_WRITEBACK_UPDATE_METADATA_END -->
 *                              VALID_CLEAN
 */
	{
	 TS_NONE,
	 S_DIRTY,
	 TS_WRITEBACK_WB,
	 S_DIRTY_WRITEBACK_CPF_CACHE_START,
	 },
	{
	 TS_WRITEBACK_WB,
	 S_DIRTY_WRITEBACK_CPF_CACHE_START,
	 TS_WRITEBACK_WB,
	 S_DIRTY_WRITEBACK_CPF_CACHE_END,
	 },
	{
	 TS_WRITEBACK_WB,
	 S_DIRTY_WRITEBACK_CPF_CACHE_END,
	 TS_WRITEBACK_WB,
	 S_DIRTY_WRITEBACK_CPT_DEVICE_ENDIO,
	 },
	{
	 TS_WRITEBACK_WB,
	 S_DIRTY_WRITEBACK_CPT_DEVICE_ENDIO,
	 TS_WRITEBACK_WB,
	 S_DIRTY_WRITEBACK_UPDATE_METADATA_END,
	 },
	{
	 TS_WRITEBACK_WB,
	 S_DIRTY_WRITEBACK_UPDATE_METADATA_END,
	 TS_NONE,
	 S_CLEAN,
	 },
/*
 * writeback_invalidate (wb):
 * VALID_DIRTY -->
 * VALID_DIRTY_WRITEBACK_INV_CPF_CACHE_START -->
 * VALID_DIRTY_WRITEBACK_INV_CPF_CACHE_END -->
 * VALID_DIRTY_WRITEBACK_INV_CPT_DEVICE_ENDIO -->
 * VALID_DIRTY_WRITEBACK_INV_UPDATE_METADATA_END -->
 * VALID_CLEAN
 */
	{
	 TS_NONE,
	 S_DIRTY,
	 TS_WRITEBACK_INV_WB,
	 S_DIRTY_WRITEBACK_INV_CPF_CACHE_START,
	 },
	{
	 TS_WRITEBACK_INV_WB,
	 S_DIRTY_WRITEBACK_INV_CPF_CACHE_START,
	 TS_WRITEBACK_INV_WB,
	 S_DIRTY_WRITEBACK_INV_CPF_CACHE_END,
	 },
	{
	 TS_WRITEBACK_INV_WB,
	 S_DIRTY_WRITEBACK_INV_CPF_CACHE_END,
	 TS_WRITEBACK_INV_WB,
	 S_DIRTY_WRITEBACK_INV_CPT_DEVICE_ENDIO,
	 },
	{
	 TS_WRITEBACK_INV_WB,
	 S_DIRTY_WRITEBACK_INV_CPT_DEVICE_ENDIO,
	 TS_WRITEBACK_INV_WB,
	 S_DIRTY_WRITEBACK_INV_UPDATE_METADATA_END,
	 },
	{
	 TS_WRITEBACK_INV_WB,
	 S_DIRTY_WRITEBACK_INV_UPDATE_METADATA_END,
	 TS_NONE,
	 S_INVALID,
	 },
/*
 * clean invalidation (wt/wb):  VALID_CLEAN -->
 *                              VALID_CLEAN_INVALIDATE_START -->
 *                              VALID_CLEAN_INVALIDATE_END -->
 *                              INVALID
 */
	{
	 TS_NONE,
	 S_CLEAN,
	 TS_CLEAN_INVALIDATION_WTWB,
	 S_CLEAN_INVALIDATE_START,
	 },
	{
	 TS_CLEAN_INVALIDATION_WTWB,
	 S_CLEAN_INVALIDATE_START,
	 TS_CLEAN_INVALIDATION_WTWB,
	 S_CLEAN_INVALIDATE_END,
	 },
	{
	 TS_CLEAN_INVALIDATION_WTWB,
	 S_CLEAN_INVALIDATE_END,
	 TS_NONE,
	 S_INVALID,
	 },
/*
 * dirty invalidation (wb):     VALID_DIRTY -->
 *                              VALID_DIRTY_INVALIDATE_START -->
 *                              VALID_DIRTY_INVALIDATE_END -->
 *                              INVALID
 */
	{
	 TS_NONE,
	 S_DIRTY,
	 TS_DIRTY_INVALIDATION_WB,
	 S_DIRTY_INVALIDATE_START,
	 },
	{
	 TS_DIRTY_INVALIDATION_WB,
	 S_DIRTY_INVALIDATE_START,
	 TS_DIRTY_INVALIDATION_WB,
	 S_DIRTY_INVALIDATE_END,
	 },
	{
	 TS_DIRTY_INVALIDATION_WB,
	 S_DIRTY_INVALIDATE_END,
	 TS_NONE,
	 S_INVALID,
	 },
};

#define CACHE_VALID_STATE_TRANSITIONS \
	(int)(sizeof(cache_valid_state_transitions) / \
	sizeof(struct cache_state_transitions))

/*!
 * \todo right now this code is only used to validate transitions
 * which are hard coded in the state machine code. at some point this
 * code ought to be cleaned up and ought to be used to drive state transitions
 * directly, so there will be no need for hard coding.
 */
int __cache_validate_state_transition(struct bittern_cache *bc,
					      struct cache_block
					      *cache_block,
					      const char *___function,
					      int ___line,
					      enum cache_transition
					      p_from,
					      enum cache_state s_from,
					      enum cache_transition
					      p_to,
					      enum cache_state s_to)
{
	int i;

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(CACHE_TRANSITION_VALID(p_from));
	ASSERT(CACHE_TRANSITION_VALID(p_to));
	ASSERT(CACHE_STATE_VALID(s_from));
	ASSERT(CACHE_STATE_VALID(s_to));

	for (i = 0; i < CACHE_VALID_STATE_TRANSITIONS; i++) {
		if (p_from == cache_valid_state_transitions[i].path_from
		    && s_from ==
		    cache_valid_state_transitions[i].state_from
		    && p_to == cache_valid_state_transitions[i].path_to
		    && s_to ==
		    cache_valid_state_transitions[i].state_to) {
			BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL,
				 NULL,
				 "%s@%d(): valid-state-transition[%d/%d]: (%s,%s) -> (%s,%s)",
				 ___function, ___line, i,
				 CACHE_VALID_STATE_TRANSITIONS,
				 cache_transition_to_str(p_from),
				 cache_state_to_str(s_from),
				 cache_transition_to_str(p_to),
				 cache_state_to_str(s_to));
			ASSERT(p_from >= 0
			       && p_from <
			       __TS_NUM);
			ASSERT(s_from >= 0
			       && s_from < __CACHE_STATES_NUM);
			ASSERT(p_to >= 0
			       && p_to < __TS_NUM);
			ASSERT(s_to >= 0 && s_to < __CACHE_STATES_NUM);
			return 0;
		}
	}
	BT_TRACE(BT_LEVEL_ERROR, bc, NULL, cache_block, NULL, NULL,
		 "%s@%d(): invalid/unknown-state-transition: (%s,%s) -> (%s,%s)",
		 ___function, ___line,
		 cache_transition_to_str(p_from),
		 cache_state_to_str(s_from),
		 cache_transition_to_str(p_to),
		 cache_state_to_str(s_to));
	__printk_err(___function, ___line,
		     "invalid/unknown-state-transition: (%s,%s) -> (%s,%s)",
		     cache_transition_to_str(p_from),
		     cache_state_to_str(s_from),
		     cache_transition_to_str(p_to),
		     cache_state_to_str(s_to));
	return -1;
}

#endif /*ENABLE_ASSERT */

struct work_item *work_item_allocate(struct bittern_cache *bc,
				     struct cache_block *cache_block,
				     struct bio *bio,
				     int wi_flags,
				     wi_io_endio_f wi_io_endio)
{
	struct work_item *wi;

	ASSERT_BITTERN_CACHE(bc);
	if (cache_block != NULL)
		ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT((wi_flags & ~WI_FLAG_MASK) == 0);
	ASSERT((wi_flags & WI_FLAG_BIO_CLONED) != 0
	       || (wi_flags & WI_FLAG_BIO_NOT_CLONED) != 0);
	ASSERT((wi_flags & (WI_FLAG_BIO_CLONED | WI_FLAG_BIO_NOT_CLONED)) !=
	       (WI_FLAG_BIO_CLONED | WI_FLAG_BIO_NOT_CLONED));
	ASSERT((wi_flags & WI_FLAG_MAP_IO) != 0
	       || (wi_flags & WI_FLAG_WRITEBACK_IO) != 0
	       || (wi_flags & WI_FLAG_INVALIDATE_IO) != 0);
	ASSERT((wi_flags &
		(WI_FLAG_MAP_IO | WI_FLAG_WRITEBACK_IO | WI_FLAG_INVALIDATE_IO))
	       !=
	       (WI_FLAG_MAP_IO | WI_FLAG_WRITEBACK_IO | WI_FLAG_INVALIDATE_IO));
	ASSERT((wi_flags & WI_FLAG_XID_NEW) != 0
	       || (wi_flags & WI_FLAG_XID_USE_CACHE_BLOCK) != 0);
	ASSERT((wi_flags & (WI_FLAG_XID_NEW | WI_FLAG_XID_USE_CACHE_BLOCK)) !=
	       (WI_FLAG_XID_NEW | WI_FLAG_XID_USE_CACHE_BLOCK));
	ASSERT((wi_flags & WI_FLAG_WRITE_CLONING) == 0);

	wi = kmem_zalloc(sizeof(struct work_item), GFP_NOIO);
	M_ASSERT_FIXME(wi != NULL);

	wi->wi_magic1 = WI_MAGIC1;
	wi->wi_magic2 = WI_MAGIC2;
	wi->wi_magic3 = WI_MAGIC3;
	wi->wi_cache_block = cache_block;
	wi->wi_original_cache_block = NULL;
	wi->wi_original_bio = bio;
	wi->wi_flags = wi_flags;
	if (wi_flags & WI_FLAG_BIO_CLONED) {
		ASSERT(bio != NULL);
	} else {
		ASSERT((wi_flags & WI_FLAG_BIO_NOT_CLONED) != 0);
		ASSERT(bio == NULL);
		wi->wi_original_bio = NULL;
	}
	if ((wi_flags & WI_FLAG_XID_NEW) != 0)
		wi->wi_io_xid = cache_xid_inc(bc);
	if ((wi_flags & WI_FLAG_XID_USE_CACHE_BLOCK) != 0) {
		ASSERT(cache_block != NULL);
		wi->wi_io_xid = cache_block->bcb_xid;
	}
	ASSERT(wi->wi_io_xid != 0);
	if ((wi_flags & WI_FLAG_HAS_ENDIO) != 0) {
		ASSERT(wi_io_endio != NULL);
		wi->wi_io_endio = wi_io_endio;
	} else {
		ASSERT(wi_io_endio == NULL);
		wi->wi_io_endio = NULL;
	}
	wi->wi_cache = bc;
	INIT_LIST_HEAD(&wi->wi_pending_io_list);

	pmem_context_initialize(&wi->wi_pmem_ctx);

	ASSERT_WORK_ITEM(wi, bc);

	return wi;
}

/*!
 * \todo should pass the new string for the pending io list
 */
void work_item_reallocate(struct bittern_cache *bc,
			  struct cache_block *cache_block,
			  struct work_item *wi,
			  struct bio *bio,
			  int wi_flags,
			  wi_io_endio_f wi_io_endio)
{
	ASSERT_BITTERN_CACHE(bc);
	if (cache_block != NULL)
		ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT((wi_flags & ~WI_FLAG_MASK) == 0);
	ASSERT((wi_flags & WI_FLAG_BIO_CLONED) != 0
	       || (wi_flags & WI_FLAG_BIO_NOT_CLONED) != 0);
	ASSERT((wi_flags & (WI_FLAG_BIO_CLONED | WI_FLAG_BIO_NOT_CLONED)) !=
	       (WI_FLAG_BIO_CLONED | WI_FLAG_BIO_NOT_CLONED));
	ASSERT((wi_flags & WI_FLAG_MAP_IO) != 0
	       || (wi_flags & WI_FLAG_WRITEBACK_IO) != 0
	       || (wi_flags & WI_FLAG_INVALIDATE_IO) != 0);
	ASSERT((wi_flags &
		(WI_FLAG_MAP_IO | WI_FLAG_WRITEBACK_IO | WI_FLAG_INVALIDATE_IO))
	       !=
	       (WI_FLAG_MAP_IO | WI_FLAG_WRITEBACK_IO | WI_FLAG_INVALIDATE_IO));
	ASSERT((wi_flags & WI_FLAG_XID_NEW) != 0
	       || (wi_flags & WI_FLAG_XID_USE_CACHE_BLOCK) != 0);
	ASSERT((wi_flags & (WI_FLAG_XID_NEW | WI_FLAG_XID_USE_CACHE_BLOCK)) !=
	       (WI_FLAG_XID_NEW | WI_FLAG_XID_USE_CACHE_BLOCK));
	ASSERT((wi_flags & WI_FLAG_WRITE_CLONING) == 0);

	ASSERT(wi != NULL);
	ASSERT_WORK_ITEM(wi, bc);

	wi->wi_cache_block = cache_block;
	wi->wi_original_cache_block = NULL;
	wi->wi_original_bio = bio;
	wi->wi_flags = wi_flags;
	if (wi_flags & WI_FLAG_BIO_CLONED) {
		ASSERT(bio != NULL);
	} else {
		ASSERT((wi_flags & WI_FLAG_BIO_NOT_CLONED) != 0);
		ASSERT(bio == NULL);
		wi->wi_original_bio = NULL;
	}
	if ((wi_flags & WI_FLAG_XID_NEW) != 0) {
		ASSERT(cache_block == NULL);
		wi->wi_io_xid = cache_xid_inc(bc);
		wi->wi_cache_block = NULL;
	}
	if ((wi_flags & WI_FLAG_XID_USE_CACHE_BLOCK) != 0) {
		ASSERT(cache_block != NULL);
		wi->wi_io_xid = cache_block->bcb_xid;
	}
	ASSERT(wi->wi_io_xid != 0);
	if ((wi_flags & WI_FLAG_HAS_ENDIO) != 0) {
		ASSERT(wi_io_endio != NULL);
		wi->wi_io_endio = wi_io_endio;
	} else {
		ASSERT(wi_io_endio == NULL);
		wi->wi_io_endio = NULL;
	}
	wi->wi_cache = bc;
	ASSERT(list_empty(&wi->wi_pending_io_list));
	ASSERT_WORK_ITEM(wi, bc);
}

void work_item_free(struct bittern_cache *bc, struct work_item *wi)
{
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_WORK_ITEM(wi, bc);

	work_item_del_pending_io(bc, wi);

	pmem_context_destroy(bc, &wi->wi_pmem_ctx);

	kmem_free(wi, sizeof(struct work_item));
}

/*!
 * \todo the caller uses the return value as "numbers of errors", which
 * is weird. we should think of a better way to do this.
 * \todo the name of this function is for sure poorly chosen.
 */
int __cache_verify_hash_data_ret(struct bittern_cache *bc,
				 struct cache_block *cache_block,
				 uint128_t hash_data,
				 const char *func,
				 int line)
{
	if (uint128_ne(hash_data, cache_block->bcb_hash_data)) {
		/*
		 * corrupt cache entry
		 */
		BT_TRACE(BT_LEVEL_TRACE0,
			 bc, NULL, cache_block, NULL, NULL,
			 "corrupt cache entry, hash=" UINT128_FMT ", computed_hash=" UINT128_FMT,
			 UINT128_ARG(cache_block->bcb_hash_data),
			 UINT128_ARG(hash_data));
		__printk_err(func,
			     line,
			     "corrupt hash id=#%d, cache_block=%lu, state=%d(%s), refcount=%d, hash=" UINT128_FMT ", computed_hash=" UINT128_FMT "\n",
			     cache_block->bcb_block_id,
			     cache_block->bcb_sector,
			     cache_block->bcb_state,
			     cache_state_to_str(cache_block->bcb_state),
			     atomic_read(&cache_block->bcb_refcount),
			     UINT128_ARG(cache_block->bcb_hash_data),
			     UINT128_ARG(hash_data));
		return 1;
	}
	return 0;
}

/*!
 * \todo the name of this function is for sure poorly chosen.
 */
int __cache_verify_hash_data_buffer_ret(struct bittern_cache *bc,
					struct cache_block *cache_block,
					void *buffer,
					const char *func,
					int line)
{
	uint128_t hash_data;

	hash_data = murmurhash3_128(buffer, PAGE_SIZE);
	return __cache_verify_hash_data_ret(bc,
					    cache_block,
					    hash_data,
					    func,
					    line);
}

/*!
 * caller uses this to "verify" the data hash, and expects
 * this function to assert if the data hash mismatches
 * \todo the name of this function is for sure poorly chosen.
 */
void __cache_verify_hash_data(struct bittern_cache *bc,
			      struct cache_block *cache_block,
			      uint128_t hash_data,
			      const char *func,
			      int line)
{
	__cache_verify_hash_data_ret(bc, cache_block, hash_data, func, line);
	M_ASSERT(uint128_eq(hash_data, cache_block->bcb_hash_data));
}

/*!
 * caller uses this to "verify" the data hash, and expects
 * this function to assert if the data hash mismatches
 * \todo the name of this function is for sure poorly chosen.
 */
void __cache_verify_hash_data_buffer(struct bittern_cache *bc,
				     struct cache_block *cache_block,
				     void *buffer,
				     const char *func,
				     int line)
{
	uint128_t hash_data;

	hash_data = murmurhash3_128(buffer, PAGE_SIZE);
	__cache_verify_hash_data_ret(bc, cache_block, hash_data, func, line);
	M_ASSERT(uint128_eq(hash_data, cache_block->bcb_hash_data));
}

/*! allocate bio and make the request */
void cache_do_make_request(struct bittern_cache *bc, struct work_item *wi)
{
	struct bio *bio;
	struct cache_block *cache_block;

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_WORK_ITEM(wi, bc);

	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		     "in_irq=%lu, in_softirq=%lu, bc=%p, wi=%p",
		     in_irq(),
		     in_softirq(),
		     bc,
		     wi);

	M_ASSERT(!in_irq());
	M_ASSERT(!in_softirq());

	cache_block = wi->wi_cache_block;
	ASSERT_CACHE_BLOCK(cache_block, bc);

	bio = bio_alloc(GFP_NOIO, 1);
	M_ASSERT_FIXME(bio != NULL);

	if (wi->bi_datadir == WRITE)
		bio_set_data_dir_write(bio);
	else
		bio_set_data_dir_read(bio);
	M_ASSERT(cache_block->bcb_sector == wi->bi_sector);
	bio->bi_iter.bi_sector = cache_block->bcb_sector;
	bio->bi_iter.bi_size = PAGE_SIZE;
	bio->bi_bdev = bc->bc_dev->bdev;
	bio->bi_end_io = wi->bi_endio;
	bio->bi_private = wi;
	bio->bi_vcnt = 1;
	bio->bi_io_vec[0].bv_page = wi->bi_page;
	M_ASSERT(pmem_context_data_page(&wi->wi_pmem_ctx) == wi->bi_page);
	bio->bi_io_vec[0].bv_len = PAGE_SIZE;
	bio->bi_io_vec[0].bv_offset = 0;
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	if (wi->bi_set_original_bio)
		wi->wi_original_bio = bio;
	if (wi->bi_set_cloned_bio)
		wi->wi_cloned_bio = bio;
	ASSERT(wi->wi_cache == bc);
	ASSERT(wi->wi_cache_block == cache_block);

	cache_timer_add(&bc->bc_make_request_wq_timer,
			wi->wi_ts_workqueue);

	atomic_inc(&bc->bc_make_request_count);

	wi->wi_ts_physio = current_kernel_time_nsec();
	generic_make_request(bio);
}

void cache_make_request_worker(struct work_struct *work)
{
	struct work_item *wi;
	struct bittern_cache *bc;

	ASSERT(!in_irq());
	ASSERT(!in_softirq());

	wi = container_of(work, struct work_item, wi_work);
	__ASSERT_WORK_ITEM(wi);
	bc = wi->wi_cache;
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_WORK_ITEM(wi, bc);

	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		     "in_irq=%lu, in_softirq=%lu, bc=%p, wi=%p, work=%p",
		     in_irq(),
		     in_softirq(),
		     bc,
		     wi,
		     &wi->wi_work);

	cache_timer_add(&bc->bc_make_request_wq_timer, wi->wi_ts_workqueue);

	cache_do_make_request(bc, wi);
}

/*!
 * This function indirectly calls generic_make_request. Because call to
 * generic_make_request() cannot be done in softirq, we defer it to a
 * work_queue in such case.
 */
void cache_make_request_defer(struct bittern_cache *bc, struct work_item *wi)
{
	int ret;

	ASSERT_BITTERN_CACHE(bc);

	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		     "in_irq=%lu, in_softirq=%lu, bc=%p, wi=%p, work=%p",
		     in_irq(),
		     in_softirq(),
		     bc,
		     wi,
		     &wi->wi_work);

	atomic_inc(&bc->bc_make_request_wq_count);
	wi->wi_ts_workqueue = current_kernel_time_nsec();

	/* defer to worker thread, which will start io */
	INIT_WORK(&wi->wi_work, cache_make_request_worker);
	ret = queue_work(bc->bc_make_request_wq, &wi->wi_work);
	ASSERT(ret == 1);
}
