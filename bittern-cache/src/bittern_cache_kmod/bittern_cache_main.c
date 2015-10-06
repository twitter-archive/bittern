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

/*!
 * handle completions which do not go thru the cache state machine
 * in this case we do not have a cache block
 */
void cached_dev_bypass_endio(struct bio *cloned_bio, int err)
{
	struct bittern_cache *bc;
	struct bio *original_bio;
	struct work_item *wi;

	M_ASSERT_FIXME(err == 0);
	ASSERT(cloned_bio != NULL);
	wi = cloned_bio->bi_private;
	ASSERT(wi != NULL);
	bc = wi->wi_cache;
	original_bio = wi->wi_original_bio;

	ASSERT(wi->wi_cache_block == NULL);

	ASSERT_WORK_ITEM(wi, bc);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT(cloned_bio == wi->wi_cloned_bio);
	ASSERT(original_bio == wi->wi_original_bio);

	ASSERT(bio_data_dir(cloned_bio) == READ
	       || bio_data_dir(cloned_bio) == WRITE);

	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, NULL, cloned_bio, NULL,
		 "bypass_complete");

	/* we no longer need the cloned bio */
	bio_put(cloned_bio);
	wi->wi_cloned_bio = NULL;

	/* right now the only use case is read bypass */
	ASSERT(wi->wi_bypass != 0);
	ASSERT(wi->wi_bypass == 1);
	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, NULL, original_bio, NULL,
		 "request_bypass complete");
	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT(wi->wi_cloned_bio == NULL);
	ASSERT(wi->wi_original_bio == original_bio);

	atomic_dec(&bc->bc_pending_requests);
	if (bio_data_dir(original_bio) == WRITE) {
		atomic_dec(&bc->bc_pending_write_bypass_requests);
		atomic_dec(&bc->bc_pending_write_requests);
		atomic_inc(&bc->bc_completed_write_requests);
	} else {
		atomic_dec(&bc->bc_pending_read_bypass_requests);
		atomic_dec(&bc->bc_pending_read_requests);
		atomic_inc(&bc->bc_completed_read_requests);
	}
	atomic_inc(&bc->bc_completed_requests);

	/* wakeup possible waiters */
	wakeup_deferred(bc);

	cache_timer_add(&bc->bc_timer_cached_device_reads,
				wi->wi_ts_physio);

	work_item_free(bc, wi);

	/* all done */
	bio_endio(original_bio, 0);
}

/*!
 * Copy bio from cache, aka userland reads.
 * Note that the returned hash is for the whole page,
 * regardless of the data amount being copied to userland.
 */
void __bio_copy_from_cache(struct work_item *wi,
			   struct bio *bio,
			   uint128_t *hash_data)
{
	unsigned int cache_block_copy_offset;
	unsigned int biovec_offset;
	struct bvec_iter bi_iterator;
	struct bio_vec bvec;
	struct bittern_cache *bc = wi->wi_cache;
	struct cache_block *cache_block = wi->wi_cache_block;
	char *cache_vaddr;

	ASSERT(bio != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));

	bc = bc; /* quiet compiler about unused variable (used in dev build) */
	cache_vaddr = pmem_context_data_vaddr(&wi->wi_pmem_ctx);

	/*
	 * for non-page aligned reads, we'll have to add this offset to memcpy.
	 * do not confuse with biovec bv_offset.
	 */
	ASSERT(bio->bi_iter.bi_sector >= cache_block->bcb_sector);
	cache_block_copy_offset =
	    (bio->bi_iter.bi_sector - cache_block->bcb_sector) * SECTOR_SIZE;
	ASSERT(cache_block_copy_offset < PAGE_SIZE);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "begin-loop-copy-from-cache");

	biovec_offset = 0;
	bio_for_each_segment(bvec, bio, bi_iterator) {
		char *bi_kaddr;

		bi_kaddr = kmap_atomic(bvec.bv_page);
		ASSERT(bi_kaddr != NULL);
		ASSERT(bvec.bv_len > 0);

		/*
		 * memcpy(bio_kaddr + bio_vec->bv_offset,
		 * wi->wi_cache_data_buffer + copy_offset, bio_vec->bv_len);
		 */

		BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio,
			 wi->wi_cloned_bio,
			 "loop-copy-from-cache: cache_block_copy_offset=%u, biovec_offset=%u, bvec.bv_page=%p, bi_kaddr=%p, bvec.bv_len=%u, bvec.bv_offset=%u",
			 cache_block_copy_offset, biovec_offset, bvec.bv_page,
			 bi_kaddr, bvec.bv_len, bvec.bv_offset);
		BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio,
			 wi->wi_cloned_bio,
			 "loop-copy-from-cache: to(bvec.bv_offset)=%u, from=(cache_block_copy_offset+biovec_offset)=%u, len=%u",
			 bvec.bv_offset,
			 (cache_block_copy_offset + biovec_offset),
			 bvec.bv_len);

		memcpy(bi_kaddr + bvec.bv_offset,
		       cache_vaddr + cache_block_copy_offset + biovec_offset,
		       bvec.bv_len);

		ASSERT(bvec.bv_offset <= PAGE_SIZE);
		ASSERT(bvec.bv_offset + bvec.bv_len <= PAGE_SIZE);
		ASSERT(cache_block_copy_offset + biovec_offset <= PAGE_SIZE);
		ASSERT(cache_block_copy_offset + biovec_offset + bvec.bv_len <=
		       PAGE_SIZE);
		if (bio->bi_iter.bi_size == PAGE_SIZE) {
			ASSERT(cache_block_copy_offset == 0);
		} else {
			ASSERT(cache_block_copy_offset >= 0);
			ASSERT(cache_block_copy_offset < PAGE_SIZE);
		}

		biovec_offset += bvec.bv_len;
		kunmap_atomic(bi_kaddr);
	}

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "end-loop-copy-from-cache: cache_block_copy_offset=%u, biovec_offset=%u, bio->bi_iter.bi_size=%u",
		 cache_block_copy_offset, biovec_offset, bio->bi_iter.bi_size);
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "done-copy-from-cache");

	M_ASSERT(biovec_offset == bio->bi_iter.bi_size);
	if (bio->bi_iter.bi_size == PAGE_SIZE) {
		ASSERT(biovec_offset == PAGE_SIZE);
		ASSERT(cache_block_copy_offset == 0);
	} else {
		ASSERT(biovec_offset < PAGE_SIZE);
		ASSERT(cache_block_copy_offset >= 0);
		ASSERT(cache_block_copy_offset < PAGE_SIZE);
		ASSERT(cache_block_copy_offset + biovec_offset <= PAGE_SIZE);
	}

	if (hash_data != NULL) {
		*hash_data = murmurhash3_128(cache_vaddr, PAGE_SIZE);
	}
}

/*!
 * Copy to cache from bio, aka userland writes.
 * Note that the returned hash is for the whole page,
 * regardless of the data amount being copied to userland.
 */
void bio_copy_to_cache(struct work_item *wi,
		       struct bio *bio,
		       uint128_t *hash_data)
{
	unsigned int cache_block_copy_offset;
	unsigned int biovec_offset;
	struct bvec_iter bi_iterator;
	struct bio_vec bvec;
	struct bittern_cache *bc = wi->wi_cache;
	struct cache_block *cache_block = wi->wi_cache_block;
	char *cache_vaddr;

	ASSERT(bio != NULL);
	ASSERT(hash_data != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));

	bc = bc; /* quiet compiler about unused variable (used in dev build) */
	cache_vaddr = pmem_context_data_vaddr(&wi->wi_pmem_ctx);

	/*
	 * for non-page aligned reads, we'll have to add this offset to memcpy.
	 * do not confuse with biovec bv_offset.
	 */
	ASSERT(bio->bi_iter.bi_sector >= cache_block->bcb_sector);
	cache_block_copy_offset =
	    (bio->bi_iter.bi_sector - cache_block->bcb_sector) * SECTOR_SIZE;
	ASSERT(cache_block_copy_offset < PAGE_SIZE);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "begin-loop-copy-to-cache");

	biovec_offset = 0;
	bio_for_each_segment(bvec, bio, bi_iterator) {
		char *bi_kaddr;

		ASSERT(bvec.bv_len > 0);
		bi_kaddr = kmap_atomic(bvec.bv_page);
		M_ASSERT(bi_kaddr != NULL);

		/*
		 * memcpy(bio_kaddr + bio_vec->bv_offset,
		 * wi->wi_cache_data_buffer + copy_offset, bio_vec->bv_len);
		 */

		BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio,
			 wi->wi_cloned_bio,
			 "loop-copy-to-cache: cache_block_copy_offset=%u, biovec_offset=%u, bvec.bv_page=%p, bi_kaddr=%p, bvec.bv_len=%u, bvec.bv_offset=%u",
			 cache_block_copy_offset, biovec_offset, bvec.bv_page,
			 bi_kaddr, bvec.bv_len, bvec.bv_offset);
		BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio,
			 wi->wi_cloned_bio,
			 "loop-copy-to-cache: to=(cache_block_copy_offset+biovec_offset)=%u, from(bvec.bv_offset)=%u, len=%u",
			 (cache_block_copy_offset + biovec_offset),
			 bvec.bv_offset, bvec.bv_len);

		/*
		 * use non-temporal writes - required for NVDIMM-type hardware.
		 */
		memcpy_nt(cache_vaddr + cache_block_copy_offset + biovec_offset,
			  bi_kaddr + bvec.bv_offset,
			  bvec.bv_len);

		ASSERT(bvec.bv_offset <= PAGE_SIZE);
		ASSERT(bvec.bv_offset + bvec.bv_len <= PAGE_SIZE);
		ASSERT(cache_block_copy_offset + biovec_offset <= PAGE_SIZE);
		ASSERT(cache_block_copy_offset + biovec_offset + bvec.bv_len <=
		       PAGE_SIZE);
		if (bio->bi_iter.bi_size == PAGE_SIZE) {
			ASSERT(cache_block_copy_offset == 0);
		} else {
			ASSERT(cache_block_copy_offset >= 0);
			ASSERT(cache_block_copy_offset < PAGE_SIZE);
		}

		biovec_offset += bvec.bv_len;
		kunmap_atomic(bi_kaddr);
	}

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "end-loop-copy-to-cache: cache_block_copy_offset=%u, biovec_offset=%u, bio->bi_iter.bi_size=%u",
		 cache_block_copy_offset, biovec_offset, bio->bi_iter.bi_size);
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "done-copy-to-cache");

	ASSERT(biovec_offset == bio->bi_iter.bi_size);
	if (bio->bi_iter.bi_size == PAGE_SIZE) {
		ASSERT(biovec_offset == PAGE_SIZE);
		ASSERT(cache_block_copy_offset == 0);
	} else {
		ASSERT(biovec_offset < PAGE_SIZE);
		ASSERT(cache_block_copy_offset >= 0);
		ASSERT(cache_block_copy_offset < PAGE_SIZE);
		ASSERT(cache_block_copy_offset + biovec_offset <= PAGE_SIZE);
	}

	*hash_data = murmurhash3_128(cache_vaddr, PAGE_SIZE);
}

void cache_get_page_read_callback(struct bittern_cache *bc,
				  struct cache_block *cache_block,
				  struct pmem_context *pmem_ctx,
				  void *callback_context,
				  int err)
{
	struct work_item *wi;
	struct bio *bio;

	ASSERT(bc != NULL);
	ASSERT(cache_block != NULL);
	ASSERT(callback_context != NULL);
	wi = (struct work_item *)callback_context;
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(pmem_ctx == &wi->wi_pmem_ctx);
	bio = wi->wi_original_bio;
	ASSERT(wi->wi_cache_block != NULL);
	ASSERT_CACHE_BLOCK(wi->wi_cache_block, bc);
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "err=%d, wi=%p, bc=%p, cache_block=%p, wi_original_cache_block=%p, wi_cache_block=%p, bio=%p",
		 err,
		 wi,
		 bc,
		 cache_block,
		 wi->wi_original_cache_block,
		 wi->wi_cache_block,
		 bio);

	/*
	 * cache_block arg could be wi_original_cache_block, but we always
	 * pass wi_cache_block to the state machine
	 */
	cache_state_machine(bc, wi, err);
}

void cache_put_page_write_callback(struct bittern_cache *bc,
				   struct cache_block *cache_block,
				   struct pmem_context *pmem_ctx,
				   void *callback_context,
				   int err)
{
	struct work_item *wi;
	struct bio *bio;

	ASSERT(bc != NULL);
	ASSERT(cache_block != NULL);
	ASSERT(callback_context != NULL);
	wi = (struct work_item *)callback_context;
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(pmem_ctx == &wi->wi_pmem_ctx);
	bio = wi->wi_original_bio;
	ASSERT(wi->wi_cache_block != NULL);
	ASSERT_CACHE_BLOCK(wi->wi_cache_block, bc);
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "err=%d, wi=%p, bc=%p, cache_block=%p, wi_original_cache_block=%p, wi_cache_block=%p, bio=%p",
		 err, wi, bc, cache_block, wi->wi_original_cache_block,
		 wi->wi_cache_block, bio);

	/*
	 * cache_block arg could be wi_original_cache_block, but we always
	 * pass wi_cache_block to the state machine
	 */
	cache_state_machine(bc, wi, err);
}

void cache_metadata_write_callback(struct bittern_cache *bc,
				   struct cache_block *cache_block,
				   struct pmem_context *pmem_ctx,
				   void *callback_context,
				   int err)
{
	struct work_item *wi;
	struct bio *bio;

	ASSERT(bc != NULL);
	ASSERT(cache_block != NULL);
	ASSERT(callback_context != NULL);
	wi = (struct work_item *)callback_context;
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(pmem_ctx == &wi->wi_pmem_ctx);
	bio = wi->wi_original_bio;
	ASSERT(wi->wi_cache_block != NULL);
	ASSERT_CACHE_BLOCK(wi->wi_cache_block, bc);
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "err=%d, wi=%p, bc=%p, cache_block=%p, wi_original_cache_block=%p, wi_cache_block=%p, bio=%p",
		 err, wi, bc, cache_block, wi->wi_original_cache_block,
		 wi->wi_cache_block, bio);

	/*
	 * cache_block arg could be wi_original_cache_block, but we always
	 * pass wi_cache_block to the state machine
	 */
	cache_state_machine(bc, wi, err);
}

/*!
 * Main state machine.
 * We can either be called in a process context or in a softirq.
 *
 * The only guarantee that is made here is that the first state for
 * each of the state transition paths is in a process context.
 * Only in these state transition the code is allowed to sleep. All
 * other code should assume a softirq context.
 *
 * Note "err" is always zero for the initial state.
 */
void cache_state_machine(struct bittern_cache *bc,
			 struct work_item *wi,
			 int err)
{
	struct cache_block *cache_block = wi->wi_cache_block;

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(wi->wi_cache == bc);
	cache_block = wi->wi_cache_block;
	ASSERT_CACHE_STATE(cache_block);
	ASSERT(cache_block->bcb_xid != 0);
	ASSERT(cache_block->bcb_xid == wi->wi_io_xid);
	ASSERT(is_sector_number_valid(cache_block->bcb_sector));

	BT_TRACE(BT_LEVEL_TRACE2,
		 bc, wi, cache_block, wi->wi_original_bio, wi->wi_cloned_bio,
		 "enter, err=%d",
		 err);
	M_ASSERT_FIXME(err == 0);

	switch (cache_block->bcb_state) {
		/*
		 * read hit (wt/wb-clean) :
		 *
		 * VALID_CLEAN -->
		 * VALID_READ_HIT_CPF_CACHE_START -->
		 * VALID_READ_HIT_CPF_CACHE_END -->
		 * VALID_CLEAN
		 */
		/*
		 * read hit (wb-dirty):
		 *
		 * VALID_DIRTY -->
		 * VALID_DIRTY_READ_HIT_CPF_CACHE_START -->
		 * VALID_DIRTY_READ_HIT_CPF_CACHE_END -->
		 * VALID_DIRTY
		 */
		/*
		 * clean/dirty read hit path
		 *
		 * VALID_DIRTY/CLEAN_READ_HIT_CPF_CACHE_START:
		 *      start async get_page_read to make data from cache
		 *      available.
		 *      sm_read_hit_copy_from_cache_start().
		 * VALID_DIRTY/CLEAN_READ_HIT_CPF_CACHE_END:
		 *      copy data to userland.
		 *      terminate async transaction (put_page_read).
		 *      sm_read_hit_copy_from_cache_end().
		 */
	case S_CLEAN_READ_HIT_CPF_CACHE_START:
	case S_DIRTY_READ_HIT_CPF_CACHE_START:
		M_ASSERT(err == 0); /* initial state, no err condition */
		sm_read_hit_copy_from_cache_start(bc, wi);
		break;

	case S_CLEAN_READ_HIT_CPF_CACHE_END:
	case S_DIRTY_READ_HIT_CPF_CACHE_END:
		sm_read_hit_copy_from_cache_end(bc, wi, err);
		break;

		/*
		 * read miss (wt/wb-clean):
		 *
		 * INVALID -->
		 * VALID_CLEAN_NO_DATA -->
		 * VALID_CLEAN_READ_MISS_CPF_DEVICE_START -->
		 * VALID_CLEAN_READ_MISS_CPF_DEVICE_END -->
		 * VALID_CLEAN_READ_MISS_CPT_CACHE_END -->
		 * VALID_CLEAN_READ_MISS_METADATA_UPDATE_END -->
		 * VALID_CLEAN
		 */
		/*
		 * read miss (wt/wb-clean):
		 *
		 * VALID_CLEAN_READ_MISS_CPF_DEVICE_START
		 *      get_page_write.
		 *      start async cached device disk read into cache.
		 *      sm_read_miss_copy_from_device_start().
		 * VALID_CLEAN_READ_MISS_CPF_DEVICE_END
		 *      completes disk io.
		 *      copy data to userland.
		 *      start async put_page_write to write data to cache and
		 *      update metadata as well.
		 *      sm_read_miss_copy_from_device_end().
		 * VALID_CLEAN_READ_MISS_CPT_CACHE_END
		 *      terminates async transaction (put_page_write).
		 *      start async transaction to update metadata.
		 *      sm_read_miss_copy_to_cache_end().
		 */
	case S_CLEAN_READ_MISS_CPF_DEVICE_START:
		M_ASSERT(err == 0); /* initial state, no err condition */
		sm_read_miss_copy_from_device_start(bc, wi);
		break;

	case S_CLEAN_READ_MISS_CPF_DEVICE_END:
		sm_read_miss_copy_from_device_end(bc, wi, err);
		break;

	case S_CLEAN_READ_MISS_CPT_CACHE_END:
		sm_read_miss_copy_to_cache_end(bc, wi, err);
		break;

		/*
		 * write miss (wb):
		 *
		 * INVALID
		 * VALID_DIRTY_NO_DATA
		 * VALID_DIRTY_WRITE_MISS_CPT_CACHE_START
		 *      get_page_write.
		 *      copy data from userland.
		 *      start async put_page_write to write data to cache and
		 *      update metadata as well.
		 *      sm_dirty_write_miss_copy_to_cache_start().
		 * VALID_DIRTY_WRITE_MISS_CPT_CACHE_END
		 *      terminates async transaction (put_page_write).
		 *      sm_dirty_write_miss_copy_to_cache_end()
		 * VALID_DIRTY
		 */
		/*
		 * [ write hit (wb-clean) ] uses the same states as
		 * [ dirty write miss (wb) ]
		 * write hit (wb-clean):
		 *
		 * VALID_CLEAN -->
		 * VALID_DIRTY_WRITE_HIT_CPT_CACHE_START -->
		 * VALID_DIRTY_WRITE_HIT_CPT_CACHE_END -->
		 * VALID_DIRTY
		 */
	case S_DIRTY_WRITE_MISS_CPT_CACHE_START:
		M_ASSERT(err == 0); /* initial state, no err condition */
		sm_dirty_write_miss_copy_to_cache_start(bc, wi);
		break;

	case S_DIRTY_WRITE_MISS_CPT_CACHE_END:
		sm_dirty_write_miss_copy_to_cache_end(bc, wi, err);
		break;

		/*
		 * write miss (wt):
		 *
		 * INVALID
		 * VALID_CLEAN_NO_DATA
		 * VALID_CLEAN_WRITE_MISS_CPT_DEVICE_START
		 *      get_page_write.
		 *      copy data from userland.
		 *      start async write to cached device.
		 *      sm_clean_write_miss_copy_to_device_start().
		 * VALID_CLEAN_WRITE_MISS_CPT_DEVICE_END
		 *      completes i/o to cached device.
		 *      start async put_page_write to write data to cache and
		 *      update metadata as well.
		 *      sm_clean_write_miss_copy_to_device_end().
		 * VALID_CLEAN_WRITE_MISS_CPT_CACHE_END
		 *      terminates async transaction (put_page_write).
		 *      sm_clean_write_miss_copy_to_cache_end().
		 * VALID_CLEAN
		 */
		/*
		 * [ write hit (wt) ] uses the same states as
		 * [ write miss (wb) ]
		 * write hit (wt):
		 *
		 * VALID_CLEAN -->
		 * VALID_CLEAN_WRITE_HIT_CPT_DEVICE_START  -->
		 * VALID_CLEAN_WRITE_HIT_CPT_DEVICE_END  -->
		 * VALID_CLEAN_WRITE_HIT_CPT_CACHE_END -->
		 * VALID_CLEAN
		 */
	case S_CLEAN_WRITE_MISS_CPT_DEVICE_START:
	case S_CLEAN_WRITE_HIT_CPT_DEVICE_START:
		M_ASSERT(err == 0); /* initial state, no err condition */
		sm_clean_write_miss_copy_to_device_start(bc, wi);
		break;

	case S_CLEAN_WRITE_MISS_CPT_DEVICE_END:
	case S_CLEAN_WRITE_HIT_CPT_DEVICE_END:
		sm_clean_write_miss_copy_to_device_end(bc, wi, err);
		break;

	case S_CLEAN_WRITE_MISS_CPT_CACHE_END:
	case S_CLEAN_WRITE_HIT_CPT_CACHE_END:
		sm_clean_write_miss_copy_to_cache_end(bc, wi, err);
		break;

		/*
		 * [ partial write hit (wt) ] uses the same states as
		 * [ write miss (wb) ] plus the initial copy-from-cache phase
		 * write hit (wt):
		 *
		 * S_CLEAN -->
		 * S_CLEAN_P_WRITE_HIT_CPF_O_CACHE_START -->
		 * S_CLEAN_P_WRITE_HIT_CPT_DEVICE_START  -->
		 * S_CLEAN_P_WRITE_HIT_CPT_DEVICE_END  -->
		 * S_CLEAN_P_WRITE_HIT_CPT_CACHE_END -->
		 * S_CLEAN
		 *
		 * the initial step will make the cache page available for
		 * read/write, all the next steps are the same as in
		 * [ write miss (wb) ].
		 */
	case S_CLEAN_P_WRITE_HIT_CPF_O_CACHE_START:
		M_ASSERT(err == 0); /* initial state, no err condition */
		sm_clean_pwrite_hit_copy_from_cache_start(bc, wi);
		break;

	case S_CLEAN_P_WRITE_HIT_CPT_DEVICE_START:
		M_ASSERT(err == 0); /* initial state, no err condition */
		sm_clean_write_miss_copy_to_device_start(bc, wi);
		break;

	case S_CLEAN_P_WRITE_HIT_CPT_DEVICE_END:
		sm_clean_write_miss_copy_to_device_end(bc, wi, err);
		break;

	case S_CLEAN_P_WRITE_HIT_CPT_CACHE_END:
		sm_clean_write_miss_copy_to_cache_end(bc, wi, err);
		break;

		/*
		 * dirty to dirty write hit:
		 *
		 * S_DIRTY_NO_DATA -->
		 * S_DIRTY_WRITE_HIT_CPT_CACHE_START -->
		 *      get_page_write.
		 *      copy data from userland.
		 *      start async write to cached device.
		 *      sm_dirty_write_hit_copy_to_cache_start().
		 * S_DIRTY_WRITE_HIT_CPT_CACHE_END -->
		 *      completes i/o to cached device.
		 *      start async put_page_write to write data to cache and
		 *      update metadata as well.
		 *      start async invalidate of original cache block.
		 *      sm_dirty_write_hit_copy_to_cache_end().
		 * S_DIRTY
		 */
		/*
		 * clean to dirty write hit:
		 * (same as dirty to dirty write hit)
		 *
		 * S_DIRTY_NO_DATA -->
		 * S_C2_DIRTY_WRITE_HIT_CPT_CACHE_START -->
		 * S_C2_DIRTY_WRITE_HIT_CPT_CACHE_END -->
		 * S_DIRTY
		 */
		/*
		 * dirty to dirty partial write hit:
		 *
		 * S_DIRTY_NO_DATA -->
		 * S_DIRTY_P_WRITE_HIT_CPF_O_CACHE_START -->
		 *      get_page_read.
		 *      start async read from cached device.
		 *      sm_dirty_pwrite_hit_copy_from_cache_start().
		 * S_DIRTY_P_WRITE_HIT_CPT_CACHE_START -->
		 *      clone read page into write page.
		 * S_DIRTY_P_WRITE_HIT_CPT_CACHE_END -->
		 * S_DIRTY
		 */
		/*
		 * clean to dirty partial write hit:
		 * (same as dirty to dirty partial write hit)
		 *
		 * S_DIRTY_NO_DATA -->
		 * S_C2_DIRTY_P_WRITE_HIT_CPF_O_CACHE_START -->
		 * S_C2_DIRTY_P_WRITE_HIT_CPT_CACHE_START -->
		 * S_C2_DIRTY_P_WRITE_HIT_CPT_CACHE_END -->
		 * S_DIRTY
		 */
	case S_C2_DIRTY_P_WRITE_HIT_CPF_O_CACHE_START:
	case S_DIRTY_P_WRITE_HIT_CPF_O_CACHE_START:
		M_ASSERT(err == 0); /* initial state, no err condition */
		sm_dirty_pwrite_hit_copy_from_cache_start(bc, wi);
		break;

	case S_DIRTY_WRITE_HIT_CPT_CACHE_START:
	case S_C2_DIRTY_WRITE_HIT_CPT_CACHE_START:
		M_ASSERT(err == 0); /* initial state, no err condition */
		sm_dirty_write_hit_copy_to_cache_start(bc, wi);
		break;

	case S_DIRTY_P_WRITE_HIT_CPT_CACHE_START: /* not an initial state */
	case S_C2_DIRTY_P_WRITE_HIT_CPT_CACHE_START: /* not an initial state */
		sm_dirty_pwrite_hit_copy_to_cache_start(bc, wi, err);
		break;

	case S_DIRTY_WRITE_HIT_CPT_CACHE_END:
	case S_C2_DIRTY_WRITE_HIT_CPT_CACHE_END:
	case S_DIRTY_P_WRITE_HIT_CPT_CACHE_END:
	case S_C2_DIRTY_P_WRITE_HIT_CPT_CACHE_END:
		sm_dirty_write_hit_copy_to_cache_end(bc, wi, err);
		break;

		/*
		 * partial write miss (wb):
		 *
		 * the state transition logic here is the same as in
		 * [ partial write miss (wt) ],
		 * except it skips the step of writing the data back to the
		 * cached device.
		 *
		 * INVALID -->
		 * VALID_DIRTY_NO_DATA -->
		 * VALID_DIRTY_P_WRITE_MISS_CPF_DEVICE_START -->
		 * VALID_DIRTY_P_WRITE_MISS_CPF_DEVICE_END -->
		 * VALID_DIRTY_P_WRITE_MISS_CPT_CACHE_END -->
		 */
		/*
		 * partial write miss (wt):
		 *
		 * INVALID -->
		 * VALID_CLEAN_NO_DATA -->
		 * VALID_CLEAN_P_WRITE_MISS_CPF_DEVICE_START -->
		 *      get_page_read.
		 *      start async read from cached device.
		 *      sm_pwrite_miss_copy_from_device_start().
		 * VALID_CLEAN_P_WRITE_MISS_CPF_DEVICE_END -->
		 *      copy data from userland to cache.
		 *      start async write to cached device.
		 *      sm_pwrite_miss_copy_from_device_start().
		 * VALID_CLEAN_P_WRITE_MISS_CPT_DEVICE_END -->
		 *      completes async write to cached device.
		 *      starts async write of data and metadata to cache.
		 *      sm_pwrite_miss_copy_to_device_end().
		 * VALID_CLEAN_P_WRITE_MISS_CPT_CACHE_END -->
		 *      completes async write of data and metadata to cache.
		 *      sm_pwrite_miss_copy_to_cache_end().
		 * VALID_CLEAN
		 */
	case S_CLEAN_P_WRITE_MISS_CPF_DEVICE_START:
	case S_DIRTY_P_WRITE_MISS_CPF_DEVICE_START:
		M_ASSERT(err == 0); /* initial state, no err condition */
		sm_pwrite_miss_copy_from_device_start(bc, wi);
		break;

	case S_CLEAN_P_WRITE_MISS_CPF_DEVICE_END:
	case S_DIRTY_P_WRITE_MISS_CPF_DEVICE_END:
		sm_pwrite_miss_copy_from_device_end(bc, wi, err);
		break;

	case S_CLEAN_P_WRITE_MISS_CPT_DEVICE_END:
		sm_pwrite_miss_copy_to_device_end(bc, wi, err);
		break;

	case S_CLEAN_P_WRITE_MISS_CPT_CACHE_END:
	case S_DIRTY_P_WRITE_MISS_CPT_CACHE_END:
		sm_pwrite_miss_copy_to_cache_end(bc, wi, err);
		break;

		/*
		 * writeback_flush (wb):
		 *
		 * VALID_DIRTY -->
		 * VALID_DIRTY_WRITEBACK_CPF_CACHE_START -->
		 * VALID_DIRTY_WRITEBACK_CPF_CACHE_END -->
		 * VALID_DIRTY_WRITEBACK_CPT_DEVICE_END -->
		 * VALID_DIRTY_WRITEBACK_UPD_METADATA_END -->
		 * VALID_CLEAN
		 *
		 * bgwriter-initiated state path to write out dirty data.
		 *
		 * VALID_DIRTY
		 * VALID_DIRTY_WRITEBACK_CPF_CACHE_START
		 *      setup cache buffer for read.
		 *      sm_writeback_copy_from_cache_start().
		 * VALID_DIRTY_WRITEBACK_CPF_CACHE_END
		 *      done setup cache buffer for read.
		 *      start io write to cached device.
		 *      sm_writeback_copy_from_cache_end().
		 * VALID_DIRTY_WRITEBACK_CPT_DEVICE_END
		 *      done io write to cached device.
		 *      start async metadata update.
		 *      sm_writeback_copy_to_device_end().
		 * VALID_DIRTY_WRITEBACK_UPD_METADATA_END
		 *      complete async metadata update.
		 *      sm_writeback_update_metadata_end().
		 * VALID_CLEAN
		 *
		 */
		/*
		 * writeback_flush (wb):
		 *
		 * VALID_DIRTY -->
		 * VALID_DIRTY_WRITEBACK_INV_CPF_CACHE_START -->
		 * VALID_DIRTY_WRITEBACK_INV_CPF_CACHE_END -->
		 * VALID_DIRTY_WRITEBACK_INV_CPT_DEVICE_END -->
		 * VALID_DIRTY_WRITEBACK_INV_UPD_METADATA_END -->
		 * VALID_CLEAN
		 *
		 * writeback_invalidate is the same as writeback_flush,
		 * except that the final state is INVALID.
		 */
	case S_DIRTY_WRITEBACK_CPF_CACHE_START:
	case S_DIRTY_WRITEBACK_INV_CPF_CACHE_START:
		M_ASSERT(err == 0); /* initial state, no err condition */
		sm_writeback_copy_from_cache_start(bc, wi);
		break;

	case S_DIRTY_WRITEBACK_CPF_CACHE_END:
	case S_DIRTY_WRITEBACK_INV_CPF_CACHE_END:
		sm_writeback_copy_from_cache_end(bc, wi, err);
		break;

	case S_DIRTY_WRITEBACK_CPT_DEVICE_END:
	case S_DIRTY_WRITEBACK_INV_CPT_DEVICE_END:
		sm_writeback_copy_to_device_end(bc, wi, err);
		break;

	case S_DIRTY_WRITEBACK_UPD_METADATA_END:
	case S_DIRTY_WRITEBACK_INV_UPD_METADATA_END:
		sm_writeback_update_metadata_end(bc, wi, err);
		break;

		/*
		 * clean invalidation (wt/wb):  VALID_CLEAN -->
		 *			      VALID_CLEAN_INVALIDATE_START,
		 *			      VALID_CLEAN_INVALIDATE_END,
		 *			      INVALID
		 *
		 * dirty invalidation (wb):     VALID_DIRTY -->
		 *			      VALID_DIRTY_INVALIDATE_START,
		 *			      VALID_DIRTY_INVALIDATE_END,
		 *			      INVALID
		 *
		 * VALID_CLEAN -->
		 * VALID_CLEAN_INVALIDATE_START
		 *      start async metadata update.
		 * VALID_CLEAN_INVALIDATE_END
		 *      complete async metadata update.
		 * INVALID
		 *
		 * the dirty block invalidation follows the same schema and uses
		 * the same functions.
		 */
	case S_CLEAN_INVALIDATE_START:
	case S_DIRTY_INVALIDATE_START:
		M_ASSERT(err == 0); /* initial state, no err condition */
		sm_invalidate_start(bc, wi);
		break;

	case S_CLEAN_INVALIDATE_END:
	case S_DIRTY_INVALIDATE_END:
		sm_invalidate_end(bc, wi, err);
		break;

	case S_INVALID:
	case S_CLEAN_NO_DATA:
	case S_DIRTY_NO_DATA:
	case S_CLEAN:
	case S_DIRTY:
	case S_CLEAN_VERIFY:
	default:
		printk_err("unknown_cache_state: bc=%p, wi=%p, bio=0x%llx, cache_block=%p\n",
			   bc, wi, (long long)wi->wi_original_bio, cache_block);
		if (wi->wi_original_bio != NULL)
			printk_err("unknown_cache_state: bio_data_dir=0x%lx, bio_bi_sector=%lu\n",
				   bio_data_dir(wi->wi_original_bio),
				   wi->wi_original_bio->bi_iter.bi_sector);
		printk_err("unknown_cache_state: wi_op_type=%s, wi_op_sector=%lu, wi_op_rw=0x%lx\n",
			   wi->wi_op_type, wi->wi_op_sector, wi->wi_op_rw);
		printk_err("unknown_cache_state: bcb_sector=%lu, cb_state=%d(%s)\n",
			   cache_block->bcb_sector,
			   cache_block->bcb_state,
			   cache_state_to_str(cache_block->bcb_state));
		M_ASSERT("unknown cache state" == NULL);
		break;
	}
}

void cache_handle_read_hit(struct bittern_cache *bc,
			   struct work_item *wi,
			   struct cache_block *cache_block,
			   struct bio *bio)
{
	unsigned long flags, cache_flags;

	/*
	 * here we are either in a process or kernel thread context,
	 * i.e., we can sleep during resource allocation if needed.
	 */
	M_ASSERT(!in_softirq());
	M_ASSERT(!in_irq());

	ASSERT(bc != NULL);
	ASSERT(cache_block != NULL);
	ASSERT(bio != NULL);
	ASSERT(wi != NULL);
	BT_TRACE(BT_LEVEL_TRACE3, bc, wi, cache_block, bio, NULL, "enter");
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(wi->wi_original_bio == bio);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(wi->wi_cache_block == cache_block);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(cache_block->bcb_state == S_CLEAN ||
	       cache_block->bcb_state == S_DIRTY);
	ASSERT(cache_block->bcb_cache_transition == TS_NONE);
	ASSERT(atomic_read(&cache_block->bcb_refcount) > 0);
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));

	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);

	/* set transaction xid */
	cache_block->bcb_xid = wi->wi_io_xid;

	ASSERT(bio_data_dir(bio) == READ);

	atomic_inc(&bc->bc_total_read_hits);
	ASSERT(wi->wi_original_cache_block == NULL);
	if (wi->wi_bypass)
		atomic_inc(&bc->bc_seq_read.bypass_hit);
	if (cache_block->bcb_state == S_DIRTY) {
		atomic_inc(&bc->bc_dirty_read_hits);
		/*
		 * read hit (wb-dirty):
		 *
		 * VALID_DIRTY -->
		 * VALID_DIRTY_READ_HIT_CPF_CACHE_START -->
		 * VALID_DIRTY_READ_HIT_CPF_CACHE_END -->
		 * VALID_DIRTY
		 */
		cache_state_transition_initial(bc,
					cache_block,
					TS_READ_HIT_WB_DIRTY,
					S_DIRTY_READ_HIT_CPF_CACHE_START);
		/* add/move to the tail of the dirty list */
		list_del_init(&cache_block->bcb_entry_cleandirty);
		list_add_tail(&cache_block->bcb_entry_cleandirty,
			      &bc->bc_valid_entries_dirty_list);
	} else {
		atomic_inc(&bc->bc_clean_read_hits);
		/*
		 * read hit (wt/wb-clean) :
		 *
		 * VALID_CLEAN -->
		 * VALID_CLEAN_READ_HIT_CPF_CACHE_START -->
		 * VALID_CLEAN_READ_HIT_CPF_CACHE_END -->
		 * VALID_CLEAN
		 */
		cache_state_transition_initial(bc,
					cache_block,
					TS_READ_HIT_WTWB_CLEAN,
					S_CLEAN_READ_HIT_CPF_CACHE_START);
		/* add/move to the tail of the clean list */
		list_del_init(&cache_block->bcb_entry_cleandirty);
		list_add_tail(&cache_block->bcb_entry_cleandirty,
			      &bc->bc_valid_entries_clean_list);
	}

	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "handle-cache-hit");
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	cache_state_machine(bc, wi, 0);
}

void cache_handle_write_hit_wt(struct bittern_cache *bc,
			       struct work_item *wi,
			       struct cache_block *original_cache_block,
			       struct bio *bio,
			       struct cache_block *cloned_cache_block)
{
	unsigned long flags, cache_flags;
	int partial_page;

	/*
	 * here we are either in a process or kernel thread context,
	 * i.e., we can sleep during resource allocation if needed.
	 */
	M_ASSERT(!in_softirq());
	M_ASSERT(!in_irq());

	ASSERT(bc != NULL);
	ASSERT(original_cache_block != NULL);
	ASSERT(cloned_cache_block != NULL);
	ASSERT(bio != NULL);
	ASSERT(wi != NULL);
	if (bio_is_request_cache_block(bio))
		partial_page = 0;
	else
		partial_page = 1;
	BT_TRACE(BT_LEVEL_TRACE3, bc, wi, original_cache_block, bio, NULL,
		 "enter, partial_page=%d",
		 partial_page);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(original_cache_block, bc);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(wi->wi_original_bio == bio);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(original_cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(wi->wi_cache_block == original_cache_block);
	ASSERT_CACHE_BLOCK(original_cache_block, bc);
	ASSERT(original_cache_block->bcb_state == S_CLEAN);
	ASSERT(original_cache_block->bcb_cache_transition == TS_NONE);
	ASSERT(atomic_read(&original_cache_block->bcb_refcount) > 0);
	ASSERT(original_cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(bio_data_dir(bio) == WRITE);
	ASSERT(!is_work_item_mode_writeback(wi));

	atomic_inc(&bc->bc_total_write_hits);
	atomic_inc(&bc->bc_clean_write_hits);

	/* set transaction xid */
	cloned_cache_block->bcb_xid = wi->wi_io_xid;

	wi->wi_original_cache_block = original_cache_block;
	wi->wi_cache_block = cloned_cache_block;

	ASSERT(original_cache_block->bcb_state == S_CLEAN);

	/*
	 * set state for original cache block,
	 * either clean invalidation or dirty invalidation.
	 */
	/*
	 * clean invalidation (wtwb):
	 *      S_CLEAN -->
	 *      S_CLEAN_INVALIDATE_START,
	 *      S_CLEAN_INVALIDATE_END,
	 *      S_INVALID
	 */
	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	spin_lock_irqsave(&original_cache_block->bcb_spinlock, cache_flags);
	cache_state_transition_initial(bc,
				       original_cache_block,
				       TS_CLEAN_INVALIDATION_WTWB,
				       S_CLEAN_INVALIDATE_START);
	/* move to the tail of the clean list */
	list_del_init(&original_cache_block->bcb_entry_cleandirty);
	list_add_tail(&original_cache_block->bcb_entry_cleandirty,
		      &bc->bc_valid_entries_clean_list);
	spin_unlock_irqrestore(&original_cache_block->bcb_spinlock,
			       cache_flags);
	/*
	 * Note the global lock is not released, as it is still needed
	 * for the next operation. This is just an optimization, the two cache
	 * block operations can be manipulated independently, and in we could
	 * just as well release and acquire the global lock again, as it was
	 * with the original code.
	 */

	spin_lock_irqsave(&cloned_cache_block->bcb_spinlock, cache_flags);

	cloned_cache_block->bcb_xid = wi->wi_io_xid;
	if (bio->bi_iter.bi_size == PAGE_SIZE) {
		/* full page write */
		/*
		 * [ write hit (wt) ] uses the same states as
		 * [ write miss (wb) ]
		 *
		 * write hit (wt):
		 * S_CLEAN -->
		 * S_CLEAN_WRITE_HIT_CPT_DEVICE_START  -->
		 * S_CLEAN_WRITE_HIT_CPT_DEVICE_END  -->
		 * S_CLEAN_WRITE_HIT_CPT_CACHE_END -->
		 * S_CLEAN
		 */
		cache_state_transition_initial(bc,
					cloned_cache_block,
					TS_WRITE_HIT_WT,
					S_CLEAN_WRITE_HIT_CPT_DEVICE_START);
	} else {
		/* partial page write */
		atomic_inc(&bc->bc_clean_write_hits_rmw);
		/*
		 * [ partial write hit (wt) ] uses the same
		 * states as [ write miss (wb) ] plus the
		 * initial copy-from-cache phase
		 *
		 * write hit (wt):
		 *
		 * S_CLEAN -->
		 * S_CLEAN_P_WRITE_HIT_CPF_CACHE_START -->
		 * S_CLEAN_P_WRITE_HIT_CPT_DEVICE_START-->
		 * S_CLEAN_P_WRITE_HIT_CPT_DEVICE_END  -->
		 * S_CLEAN_P_WRITE_HIT_CPT_CACHE_END -->
		 * S_CLEAN
		 */
		cache_state_transition_initial(bc,
					cloned_cache_block,
					TS_P_WRITE_HIT_WT,
					S_CLEAN_P_WRITE_HIT_CPF_O_CACHE_START);
	}

	/* add/move to the tail of the clean list */
	list_del_init(&cloned_cache_block->bcb_entry_cleandirty);
	list_add_tail(&cloned_cache_block->bcb_entry_cleandirty,
		      &bc->bc_valid_entries_clean_list);

	spin_unlock_irqrestore(&cloned_cache_block->bcb_spinlock, cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, original_cache_block, bio, NULL,
		 "handle-cache-write-hit-wt-original");
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cloned_cache_block, bio, NULL,
		 "handle-cache-write-hit-wt-cloned");
	ASSERT(cloned_cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	cache_state_machine(bc, wi, 0);
}

void cache_handle_write_hit_wb(struct bittern_cache *bc,
			       struct work_item *wi,
			       struct cache_block *original_cache_block,
			       struct bio *bio,
			       struct cache_block *cloned_cache_block)
{
	unsigned long flags, cache_flags;
	enum cache_state original_cache_block_state;

	/*
	 * here we are either in a process or kernel thread context,
	 * i.e., we can sleep during resource allocation if needed.
	 */
	M_ASSERT(!in_softirq());
	M_ASSERT(!in_irq());

	ASSERT(bc != NULL);
	ASSERT(original_cache_block != NULL);
	ASSERT(bio != NULL);
	ASSERT(wi != NULL);
	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, original_cache_block, bio, NULL,
		 "enter");
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(original_cache_block, bc);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(wi->wi_original_bio == bio);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(original_cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(wi->wi_cache_block == original_cache_block);
	ASSERT_CACHE_BLOCK(original_cache_block, bc);
	ASSERT(wi->wi_cache_mode_writeback == 1);
	ASSERT(original_cache_block->bcb_state == S_DIRTY ||
	       original_cache_block->bcb_state == S_CLEAN);

	ASSERT(cloned_cache_block != NULL);
	ASSERT(cloned_cache_block->bcb_sector ==
	       original_cache_block->bcb_sector);
	ASSERT(cloned_cache_block->bcb_state ==
	       S_DIRTY_NO_DATA);
	ASSERT(atomic_read(&cloned_cache_block->bcb_refcount) > 0);

	/* set transaction xid */
	cloned_cache_block->bcb_xid = wi->wi_io_xid;

	wi->wi_original_cache_block = original_cache_block;
	wi->wi_cache_block = cloned_cache_block;

	atomic_inc(&bc->bc_total_write_hits);
	if (original_cache_block->bcb_state == S_CLEAN) {
		atomic_inc(&bc->bc_clean_write_hits);
		original_cache_block_state = S_CLEAN;
	} else {
		ASSERT(original_cache_block->bcb_state == S_DIRTY);
		atomic_inc(&bc->bc_dirty_write_hits);
		original_cache_block_state = S_DIRTY;
	}

	/*
	 * set state for original cache block,
	 * either clean invalidation or dirty invalidation.
	 */
	/*
	 * clean invalidation (wtwb):
	 *      S_CLEAN -->
	 *      S_CLEAN_INVALIDATE_START,
	 *      S_CLEAN_INVALIDATE_END,
	 *      S_INVALID
	 */
	/*
	 * dirty invalidation (wb):
	 *      S_DIRTY -->
	 *      S_DIRTY_INVALIDATE_START,
	 *      S_DIRTY_INVALIDATE_END,
	 *      S_INVALID
	 */
	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	spin_lock_irqsave(&original_cache_block->bcb_spinlock, cache_flags);
	if (original_cache_block_state == S_CLEAN) {
		cache_state_transition_initial(bc,
					       original_cache_block,
					       TS_CLEAN_INVALIDATION_WTWB,
					       S_CLEAN_INVALIDATE_START);
		/* move to the tail of the clean list */
		list_del_init(&original_cache_block->bcb_entry_cleandirty);
		list_add_tail(&original_cache_block->bcb_entry_cleandirty,
			      &bc->bc_valid_entries_clean_list);
	} else {
		cache_state_transition_initial(bc,
					       original_cache_block,
					       TS_DIRTY_INVALIDATION_WB,
					       S_DIRTY_INVALIDATE_START);
		/* move to the tail of the dirty list */
		list_del_init(&original_cache_block->bcb_entry_cleandirty);
		list_add_tail(&original_cache_block->bcb_entry_cleandirty,
			      &bc->bc_valid_entries_dirty_list);
	}
	spin_unlock_irqrestore(&original_cache_block->bcb_spinlock,
			       cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	/*
	 * set state for cloned cache block
	 */
	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	spin_lock_irqsave(&cloned_cache_block->bcb_spinlock, cache_flags);

	cloned_cache_block->bcb_xid = wi->wi_io_xid;
	if (bio->bi_iter.bi_size == PAGE_SIZE) {
		/*
		 * handle full page write
		 */
		if (original_cache_block_state == S_CLEAN) {
			/*
			 * clean to dirty write hit:
			 *
			 * S_DIRTY_NO_DATA -->
			 * S_C2_DIRTY_WRITE_HIT_CPT_CACHE_START -->
			 * S_C2_DIRTY_WRITE_HIT_CPT_CACHE_END -->
			 * S_DIRTY
			 */
			cache_state_transition_initial(bc,
					cloned_cache_block,
					TS_WRITE_HIT_WB_C2_DIRTY,
					S_C2_DIRTY_WRITE_HIT_CPT_CACHE_START);
		} else {
			/*
			 * dirty to dirty write hit:
			 *
			 * S_DIRTY_NO_DATA -->
			 * S_DIRTY_WRITE_HIT_CPT_CACHE_START -->
			 * S_DIRTY_WRITE_HIT_CPT_CACHE_END -->
			 * S_DIRTY
			 */
			cache_state_transition_initial(bc,
					cloned_cache_block,
					TS_WRITE_HIT_WB_DIRTY,
					S_DIRTY_WRITE_HIT_CPT_CACHE_START);
		}
	} else {
		/*
		 * handle partial page write
		 */
		atomic_inc(&bc->bc_dirty_write_hits_rmw);
		if (original_cache_block_state == S_CLEAN) {
			/*
			 * partial clean to dirty write hit:
			 * S_DIRTY_NO_DATA -->
			 * S_C2_DIRTY_P_WRITE_HIT_CPF_O_CACHE_START -->
			 * S_C2_DIRTY_P_WRITE_HIT_CPT_CACHE_START -->
			 * S_C2_DIRTY_P_WRITE_HIT_CPT_CACHE_END -->
			 * S_DIRTY
			 */
			cache_state_transition_initial(bc,
				cloned_cache_block,
				TS_P_WRITE_HIT_WB_C2_DIRTY,
				S_C2_DIRTY_P_WRITE_HIT_CPF_O_CACHE_START);
		} else {
			/*
			 * partial dirty to dirty write hit:
			 * S_DIRTY_NO_DATA -->
			 * S_DIRTY_P_WRITE_HIT_CPF_O_CACHE_START -->
			 * S_DIRTY_P_WRITE_HIT_CPT_CACHE_START -->
			 * S_DIRTY_P_WRITE_HIT_CPT_CACHE_END -->
			 * S_DIRTY
			 */
			cache_state_transition_initial(bc,
					cloned_cache_block,
					TS_P_WRITE_HIT_WB_DIRTY,
					S_DIRTY_P_WRITE_HIT_CPF_O_CACHE_START);
		}
	}
	/* add/move to the tail of the dirty list */
	list_del_init(&cloned_cache_block->bcb_entry_cleandirty);
	list_add_tail(&cloned_cache_block->bcb_entry_cleandirty,
		      &bc->bc_valid_entries_dirty_list);

	spin_unlock_irqrestore(&cloned_cache_block->bcb_spinlock, cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, original_cache_block, bio, NULL,
		 "handle-cache-hit-original-cache-block original_state=%d(%s)",
		 original_cache_block_state,
		 cache_state_to_str(original_cache_block_state));
	ASSERT(original_cache_block_state == S_CLEAN ||
	       original_cache_block_state == S_DIRTY);

	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, cloned_cache_block, bio, NULL,
		 "handle-cache-hit-cloned-cache-block");
	ASSERT(cloned_cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));

	ASSERT(wi->wi_cache_block == cloned_cache_block);
	cache_state_machine(bc, wi, 0);
}

void cache_handle_read_miss(struct bittern_cache *bc,
			    struct work_item *wi,
			    struct bio *bio,
			    struct cache_block *cache_block)
{
	unsigned long flags, cache_flags;

	/*
	 * here we are either in a process or kernel thread context,
	 * i.e., we can sleep during resource allocation if needed.
	 */
	M_ASSERT(!in_softirq());
	M_ASSERT(!in_irq());

	BT_TRACE(BT_LEVEL_TRACE3, bc, wi, cache_block, bio, NULL, "enter");

	ASSERT(bc != NULL);
	ASSERT(bio != NULL);
	ASSERT(wi != NULL);
	ASSERT(cache_block != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(wi->wi_original_bio == bio);

	ASSERT(bio_is_request_single_cache_block(bio));

	/* read bypass requests should never miss */
	ASSERT(wi->wi_bypass == 0);
	ASSERT(atomic_read(&cache_block->bcb_refcount) > 0);
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(cache_block->bcb_state == S_CLEAN_NO_DATA ||
	       cache_block->bcb_state == S_DIRTY_NO_DATA);
	ASSERT(cache_block->bcb_cache_transition == TS_NONE);

	ASSERT(wi->wi_original_cache_block == NULL);
	ASSERT(bio_data_dir(bio) == READ);

	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);

	/* set transaction xid */
	cache_block->bcb_xid = wi->wi_io_xid;
	wi->wi_cache_block = cache_block;

	ASSERT(cache_block->bcb_state == S_CLEAN_NO_DATA);
	atomic_inc(&bc->bc_total_read_misses);
	atomic_inc(&bc->bc_read_misses);
	/*
	 * read miss (wt/wb-clean):
	 *
	 * INVALID -->
	 * VALID_CLEAN_NO_DATA -->
	 * VALID_CLEAN_READ_MISS_CPF_DEVICE_START -->
	 * VALID_CLEAN_READ_MISS_CPF_DEVICE_END -->
	 * VALID_CLEAN_READ_MISS_CPT_CACHE_END -->
	 * VALID_CLEAN_READ_MISS_METADATA_UPDATE_END -->
	 * VALID_CLEAN
	 */
	cache_state_transition_initial(bc,
				       cache_block,
				       TS_READ_MISS_WTWB_CLEAN,
				       S_CLEAN_READ_MISS_CPF_DEVICE_START);
	/* add/move to the tail of the clean list */
	list_del_init(&cache_block->bcb_entry_cleandirty);
	list_add_tail(&cache_block->bcb_entry_cleandirty,
		      &bc->bc_valid_entries_clean_list);

	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "handling-read-miss");
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(wi->wi_cache_block == cache_block);
	cache_state_machine(bc, wi, 0);
}

void cache_handle_write_miss_wb(struct bittern_cache *bc,
				struct work_item *wi,
				struct bio *bio,
				struct cache_block *cache_block)
{
	int partial_page = (bio_is_request_cache_block(bio) == 0);
	unsigned long flags, cache_flags;

	/*
	 * here we are either in a process or kernel thread context,
	 * i.e., we can sleep during resource allocation if needed.
	 */
	M_ASSERT(!in_softirq());
	M_ASSERT(!in_irq());

	BT_TRACE(BT_LEVEL_TRACE3, bc, wi, cache_block, bio, NULL,
		 "enter: partial_page=%d",
		 partial_page);

	ASSERT(bc != NULL);
	ASSERT(bio != NULL);
	ASSERT(wi != NULL);
	ASSERT(cache_block != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(wi->wi_original_bio == bio);

	ASSERT(bio_is_request_single_cache_block(bio));

	/* read bypass requests should never miss */
	ASSERT(wi->wi_bypass == 0);
	ASSERT(atomic_read(&cache_block->bcb_refcount) > 0);
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(cache_block->bcb_state == S_DIRTY_NO_DATA);
	ASSERT(cache_block->bcb_cache_transition == TS_NONE);
	ASSERT(is_work_item_mode_writeback(wi));

	ASSERT(wi->wi_original_cache_block == NULL);
	ASSERT(bio_data_dir(bio) == WRITE);

	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);

	/* set transaction xid */
	cache_block->bcb_xid = wi->wi_io_xid;
	wi->wi_cache_block = cache_block;

	atomic_inc(&bc->bc_total_write_misses);
	atomic_inc(&bc->bc_dirty_write_misses);

	if (partial_page) {
		atomic_inc(&bc->bc_dirty_write_misses_rmw);
		/*
		 * partial write miss (wb):
		 *
		 * INVALID -->
		 * VALID_DIRTY_NO_DATA -->
		 * VALID_DIRTY_P_WRITE_MISS_CPF_DEVICE_START -->
		 * VALID_DIRTY_P_WRITE_MISS_CPF_DEVICE_END -->
		 * VALID_DIRTY_P_WRITE_MISS_CPT_CACHE_END -->
		 */
		cache_state_transition_initial(bc,
				cache_block,
				TS_P_WRITE_MISS_WB,
				S_DIRTY_P_WRITE_MISS_CPF_DEVICE_START);
		BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
			 "partial-write-rmw");
		/* add/move to the tail of the dirty list */
		list_del_init(&cache_block->
			      bcb_entry_cleandirty);
		list_add_tail(&cache_block->
			      bcb_entry_cleandirty,
			      &bc->bc_valid_entries_dirty_list);
	} else {
		/*
		 * write miss (wb):
		 *
		 * INVALID -->
		 * VALID_DIRTY_NO_DATA -->
		 * VALID_DIRTY_WRITE_MISS_CPT_CACHE_START -->
		 * VALID_DIRTY_WRITE_MISS_CPT_CACHE_END -->
		 * VALID_DIRTY
		 */
		cache_state_transition_initial(bc,
					cache_block,
					TS_WRITE_MISS_WB,
					S_DIRTY_WRITE_MISS_CPT_CACHE_START);
		/* add/move to the tail of the dirty list */
		list_del_init(&cache_block->bcb_entry_cleandirty);
		list_add_tail(&cache_block->bcb_entry_cleandirty,
			      &bc->bc_valid_entries_dirty_list);
	}

	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "handling-write-miss-wb");
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(wi->wi_cache_block == cache_block);
	cache_state_machine(bc, wi, 0);
}

void cache_handle_write_miss_wt(struct bittern_cache *bc,
				struct work_item *wi,
				struct bio *bio,
				struct cache_block *cache_block)
{
	int partial_page = (bio_is_request_cache_block(bio) == 0);
	unsigned long flags, cache_flags;

	/*
	 * here we are either in a process or kernel thread context,
	 * i.e., we can sleep during resource allocation if needed.
	 */
	M_ASSERT(!in_softirq());
	M_ASSERT(!in_irq());

	BT_TRACE(BT_LEVEL_TRACE3, bc, wi, cache_block, bio, NULL,
		 "enter: partial_page=%d",
		 partial_page);

	ASSERT(bc != NULL);
	ASSERT(bio != NULL);
	ASSERT(wi != NULL);
	ASSERT(cache_block != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(wi->wi_original_bio == bio);

	ASSERT(bio_is_request_single_cache_block(bio));

	/* read bypass requests should never miss */
	ASSERT(wi->wi_bypass == 0);
	ASSERT(atomic_read(&cache_block->bcb_refcount) > 0);
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(cache_block->bcb_state == S_CLEAN_NO_DATA);
	ASSERT(cache_block->bcb_cache_transition == TS_NONE);
	ASSERT(!is_work_item_mode_writeback(wi));

	ASSERT(wi->wi_original_cache_block == NULL);
	ASSERT(bio_data_dir(bio) == WRITE);

	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);

	/* set transaction xid */
	cache_block->bcb_xid = wi->wi_io_xid;
	wi->wi_cache_block = cache_block;

	atomic_inc(&bc->bc_total_write_misses);
	atomic_inc(&bc->bc_clean_write_misses);

	if (partial_page) {
		atomic_inc(&bc->bc_clean_write_misses_rmw);
		/*
		 * partial write miss (wt):
		 *
		 * INVALID -->
		 * VALID_CLEAN_NO_DATA -->
		 * VALID_CLEAN_P_WRITE_MISS_CPF_DEVICE_START -->
		 * VALID_CLEAN_P_WRITE_MISS_CPF_DEVICE_END -->
		 * VALID_CLEAN_P_WRITE_MISS_CPT_DEVICE_END -->
		 * VALID_CLEAN_P_WRITE_MISS_CPT_CACHE_END -->
		 * VALID_CLEAN
		 */
		cache_state_transition_initial(bc,
				cache_block,
				TS_P_WRITE_MISS_WT,
				S_CLEAN_P_WRITE_MISS_CPF_DEVICE_START);
		/* add/move to the tail of the clean list */
		list_del_init(&cache_block->
			      bcb_entry_cleandirty);
		list_add_tail(&cache_block->
			      bcb_entry_cleandirty,
			      &bc->bc_valid_entries_clean_list);
		BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block,
			 bio, NULL, "partial-write-rmw");
	} else {
		/*
		 * write miss (wt):
		 *
		 * INVALID -->
		 * VALID_CLEAN_NO_DATA -->
		 * VALID_CLEAN_WRITE_MISS_CPT_DEVICE_START -->
		 * VALID_CLEAN_WRITE_MISS_CPT_DEVICE_END -->
		 * VALID_CLEAN_WRITE_MISS_CPT_CACHE_END -->
		 * VALID_CLEAN
		 */
		cache_state_transition_initial(bc,
					cache_block,
					TS_WRITE_MISS_WT,
					S_CLEAN_WRITE_MISS_CPT_DEVICE_START);
		/* add/move to the tail of the clean list */
		list_del_init(&cache_block->
			      bcb_entry_cleandirty);
		list_add_tail(&cache_block->
			      bcb_entry_cleandirty,
			      &bc->bc_valid_entries_clean_list);
	}

	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "handling-write-miss-wt");
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(wi->wi_cache_block == cache_block);
	cache_state_machine(bc, wi, 0);
}

static inline void cache_update_pending(struct bittern_cache *bc,
					struct bio *bio,
					bool is_sequential)
{
	struct seq_io_bypass *bsi;
	int val;

	val = atomic_inc_return(&bc->bc_pending_requests);
	atomic_set_if_higher(&bc->bc_highest_pending_requests, val);
	if (bio_data_dir(bio) == WRITE) {
		atomic_inc(&bc->bc_pending_write_requests);
		bsi = &bc->bc_seq_write;
	} else {
		atomic_inc(&bc->bc_pending_read_requests);
		bsi = &bc->bc_seq_write;
	}
	if (is_sequential)
		atomic_inc(&bsi->seq_io_count);
	else
		atomic_inc(&bsi->non_seq_io_count);
}

/*!
 * Handle bypass requests. We still allocate a work_item because it makes it
 * much easier to track what's going on. Later we'll need it anyway because
 * we'll have to allocate a fake cache_block entry to avoid the only semantical
 * difference between Bittern+RAID5 and hardware RAID (namely, the consistency
 * between bypassed IO and in progress cached IO).
 */
void cache_map_workfunc_handle_bypass(struct bittern_cache *bc, struct bio *bio)
{
	struct bio *cloned_bio;
	struct work_item *wi;
	uint64_t tstamp = current_kernel_time_nsec();

	ASSERT(bc != NULL);
	ASSERT(bio != NULL);
	ASSERT_BITTERN_CACHE(bc);

	/*
	 * here we are either in a process or kernel thread context,
	 * i.e., we can sleep during resource allocation if needed.
	 */
	M_ASSERT(!in_softirq());
	M_ASSERT(!in_irq());

	wi = work_item_allocate(bc,
				NULL,
				bio,
				(WI_FLAG_BIO_CLONED |
				 WI_FLAG_XID_NEW));
	M_ASSERT_FIXME(wi != NULL);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(wi->wi_io_xid != 0);
	ASSERT(wi->wi_original_bio == bio);
	ASSERT(wi->wi_cache == bc);
	ASSERT(wi->wi_cloned_bio == NULL);
	ASSERT(wi->wi_original_bio == bio);
	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	wi->wi_bypass = 1;
	wi->wi_ts_started = tstamp;

	/* inc pending counters, add to pending io list, and start bio  */
	cache_update_pending(bc, bio, true);
	work_item_add_pending_io(bc,
				 wi,
				 "bypass",
				 bio->bi_iter.bi_sector,
				 bio->bi_rw);

	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, NULL, bio, NULL, "handle_bypass");

	/*
	 * clone bio
	 */
	cloned_bio = bio_clone(bio, GFP_NOIO);
	M_ASSERT_FIXME(cloned_bio != NULL);
	cloned_bio->bi_bdev = bc->devio.dm_dev->bdev;
	cloned_bio->bi_end_io = cached_dev_bypass_endio;
	cloned_bio->bi_private = wi;
	wi->wi_cloned_bio = cloned_bio;
	if (bio_data_dir(bio) == READ) {
		atomic_inc(&bc->bc_seq_read.bypass_count);
		atomic_inc(&bc->bc_pending_read_bypass_requests);
		cache_timer_add(&bc->bc_timer_resource_alloc_reads, tstamp);
	} else {
#if 0
		/*
		 * Turn off issuing of REQ_FUA until hang problem is fixed.
		 */
		/*
		 * Always set REQ_FUA unless disabled.
		 *
		 * Bittern gives the same guarantees that HW RAID does, every
		 * committed write is on stable storage. Sequential access
		 * bypass is transparent to the caller, so REQ_FUA must be
		 * set here as well.
		 */
		M_ASSERT(bc->bc_enable_req_fua == false ||
			 bc->bc_enable_req_fua == true);
		if (bc->bc_enable_req_fua)
			cloned_bio->bi_rw |= REQ_FUA;
#endif
		atomic_inc(&bc->bc_seq_write.bypass_count);
		atomic_inc(&bc->bc_pending_write_bypass_requests);
		cache_timer_add(&bc->bc_timer_resource_alloc_writes, tstamp);
	}


	cache_track_hash_clear(bc, bio_sector_to_cache_block_sector(bio));
	/*
	 * schedule request
	 */
	wi->wi_ts_physio = current_kernel_time_nsec();
	generic_make_request(cloned_bio);
}

/*!
 * Read/write hit.
 * We found a cache block and it's idle. We can now start IO on it.
 */
int cache_map_workfunc_hit(struct bittern_cache *bc,
			   struct cache_block *cache_block,
			   struct bio *bio,
			   bool do_writeback,
			   struct deferred_queue *old_queue)
{
	struct work_item *wi;
	struct cache_block *cloned_cache_block = NULL;
	uint64_t tstamp = current_kernel_time_nsec();
	int ret;

	M_ASSERT(atomic_read(&bc->bc_valid_entries_clean) >= 0);
	M_ASSERT(atomic_read(&bc->bc_valid_entries_dirty) <=
		 atomic_read(&bc->bc_total_entries));
	M_ASSERT(atomic_read(&bc->bc_valid_entries_clean) <=
		 atomic_read(&bc->bc_total_entries));
	M_ASSERT(atomic_read(&bc->bc_valid_entries) <=
		 atomic_read(&bc->bc_total_entries));
	M_ASSERT(atomic_read(&bc->bc_invalid_entries) <=
		 atomic_read(&bc->bc_total_entries));

	/*
	 * here we are either in a process or kernel thread context,
	 * i.e., we can sleep during resource allocation if needed.
	 */
	M_ASSERT(!in_softirq());
	M_ASSERT(!in_irq());

	ASSERT(bc != NULL);
	ASSERT(bio != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(do_writeback == false || do_writeback == true);

	/*
	 * Allocate cache block clone for write operations.
	 */
	if (bio_data_dir(bio) == WRITE) {
		int r;

		r = cache_get_clone(bc,
				    cache_block,
				    &cloned_cache_block,
				    do_writeback);
		if (r == CACHE_GET_RET_MISS) {
			ASSERT(cloned_cache_block == NULL);
			/*
			 * no cloned block, so we need to release
			 * original block and defer the request.
			 */
			cache_put_update_age(bc, cache_block, 1);
			atomic_inc(&bc->bc_write_hits_busy);
			/*
			 * defer request
			 */
			queue_to_deferred(bc,
						&bc->defer_page,
						bio,
						old_queue);
			BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, bio, NULL,
				 "write-hit-no-clone-deferring");
			return 0;
		}
		BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, cache_block, bio, NULL,
			 "dirty-write-hit-cache-block");
		BT_TRACE(BT_LEVEL_TRACE2, bc, NULL,
			 cloned_cache_block, bio, NULL,
			 "dirty-write-hit-cloned-cache-block");
		ASSERT(r == CACHE_GET_RET_MISS_INVALID_IDLE);
		ASSERT(cloned_cache_block != NULL);
	} else {
		BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, cache_block, bio, NULL,
			 "read-hit");
	}

	wi = work_item_allocate(bc,
				cache_block,
				bio,
				(WI_FLAG_BIO_CLONED |
				 WI_FLAG_XID_NEW));
	M_ASSERT_FIXME(wi != NULL);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(wi->wi_io_xid != 0);
	ASSERT(wi->wi_original_bio == bio);
	ASSERT(wi->wi_cache == bc);
	ASSERT(wi->wi_cloned_bio == NULL);
	ASSERT(wi->wi_original_bio == bio);
	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT(wi->wi_cache_block == cache_block);
	ASSERT(wi->wi_bypass == 0);
	wi->wi_ts_started = tstamp;
	wi->wi_cache_mode_writeback = do_writeback;

	ret = pmem_context_setup(bc,
				 bc->bc_kmem_map,
				 cache_block,
				 cloned_cache_block,
				 &wi->wi_pmem_ctx);
	M_ASSERT_FIXME(ret == 0);

	if (bio_data_dir(bio) == READ)
		cache_timer_add(&bc->bc_timer_resource_alloc_reads, tstamp);
	else
		cache_timer_add(&bc->bc_timer_resource_alloc_writes, tstamp);

	/*
	 * inc pending counters
	 */
	cache_update_pending(bc, bio, false);

	if (bio_data_dir(bio) == READ) {
		/*
		 * wt or wb read hit
		 */
		/*
		 * add to pending list and start state machine
		 */
		work_item_add_pending_io(bc,
					 wi,
					 "read-hit",
					 cache_block->bcb_sector,
					 bio->bi_rw);
		cache_handle_read_hit(bc, wi, cache_block, bio);
	} else if (do_writeback != 0) {
		ASSERT(bio_data_dir(bio) == WRITE);
		/*
		 * wb write hit
		 */
		ASSERT(cloned_cache_block != NULL);
		ASSERT(do_writeback == 1);
		ASSERT(is_work_item_mode_writeback(wi));
		/*
		 * add to pending list and start state machine
		 */
		work_item_add_pending_io(bc,
					 wi,
					 "write-hit-wb",
					 cache_block->bcb_sector,
					 bio->bi_rw);
		cache_handle_write_hit_wb(bc,
					  wi,
					  cache_block,
					  bio,
					  cloned_cache_block);
	} else {
		ASSERT(bio_data_dir(bio) == WRITE);
		/*
		 * wt write hit
		 */
		ASSERT(cloned_cache_block != NULL);
		ASSERT(do_writeback == 0);
		ASSERT(!is_work_item_mode_writeback(wi));
		/*
		 * add to pending list and start state machine
		 */
		work_item_add_pending_io(bc,
					 wi,
					 "write-hit-wt",
					 cache_block->bcb_sector,
					 bio->bi_rw);
		cache_handle_write_hit_wt(bc,
					  wi,
					  cache_block,
					  bio,
					  cloned_cache_block);
	}

	return 1;
}

/*!
 * Handle resource busy case (we have a cache hit, but the cache block
 * is in use). All we can do is queue to deferred for later execution.
 */
void cache_map_workfunc_resource_busy(struct bittern_cache *bc,
				      struct bio *bio,
				      struct deferred_queue *old_queue)
{
	/*
	 * here we are either in a process or kernel thread context,
	 * i.e., we can sleep during resource allocation if needed.
	 */
	M_ASSERT(!in_softirq());
	M_ASSERT(!in_irq());

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, bio, NULL, "cache-hit-busy");
	if (bio_data_dir(bio) == WRITE)
		atomic_inc(&bc->bc_write_hits_busy);
	else
		atomic_inc(&bc->bc_read_hits_busy);
	queue_to_deferred(bc, &bc->defer_busy, bio, old_queue);
}

/*!
 * Handle miss case (in this case we missed, but an idle invalid block
 * has been reallocated, so we can handle cache miss, i.e. do a cache fill).
 */
void cache_map_workfunc_miss(struct bittern_cache *bc,
			     struct cache_block *cache_block,
			     struct bio *bio,
			     int do_writeback)
{
	struct work_item *wi;
	uint64_t tstamp = current_kernel_time_nsec();
	int ret;

	ASSERT(bc != NULL);
	ASSERT(bio != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);

	M_ASSERT(atomic_read(&bc->bc_valid_entries_clean) >= 0);
	M_ASSERT(atomic_read(&bc->bc_valid_entries_dirty) <=
		 atomic_read(&bc->bc_total_entries));
	M_ASSERT(atomic_read(&bc->bc_valid_entries_clean) <=
		 atomic_read(&bc->bc_total_entries));
	M_ASSERT(atomic_read(&bc->bc_valid_entries) <=
		 atomic_read(&bc->bc_total_entries));
	M_ASSERT(atomic_read(&bc->bc_invalid_entries) <=
		 atomic_read(&bc->bc_total_entries));

	/*
	 * here we are either in a process or kernel thread context,
	 * i.e., we can sleep during resource allocation if needed.
	 */
	M_ASSERT(!in_softirq());
	M_ASSERT(!in_irq());

	wi = work_item_allocate(bc,
				cache_block,
				bio,
				(WI_FLAG_BIO_CLONED |
				 WI_FLAG_XID_NEW));
	M_ASSERT_FIXME(wi != NULL);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(wi->wi_io_xid != 0);
	ASSERT(wi->wi_original_bio == bio);
	ASSERT(wi->wi_cache == bc);
	ASSERT(wi->wi_cloned_bio == NULL);
	ASSERT(wi->wi_original_bio == bio);
	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	ASSERT(wi->wi_cache_block == cache_block);
	ASSERT(wi->wi_bypass == 0);
	wi->wi_ts_started = tstamp;
	wi->wi_cache_mode_writeback = do_writeback;

	ret = pmem_context_setup(bc,
				 bc->bc_kmem_map,
				 cache_block,
				 NULL,
				 &wi->wi_pmem_ctx);
	M_ASSERT_FIXME(ret == 0);

	if (bio_data_dir(bio) == READ)
		cache_timer_add(&bc->bc_timer_resource_alloc_reads, tstamp);
	else
		cache_timer_add(&bc->bc_timer_resource_alloc_writes, tstamp);

	/*
	 * if we got a cache block back,
	 * we'd better be not doing a bypass
	 * */
	ASSERT(cache_block->bcb_cache_transition == TS_NONE);
	if (is_work_item_mode_writeback(wi) && bio_data_dir(bio) == WRITE)
		ASSERT(cache_block->bcb_state == S_DIRTY_NO_DATA);
	else
		ASSERT(cache_block->bcb_state == S_CLEAN_NO_DATA);
	/*
	 * inc pending counters
	 */
	cache_update_pending(bc, bio, false);

	if (bio_data_dir(bio) == WRITE) {
		/*
		 * add to pending list and start state machine
		 */
		work_item_add_pending_io(bc,
					 wi,
					 "write-miss",
					 cache_block->bcb_sector,
					 bio->bi_rw);
		if (is_work_item_mode_writeback(wi))
			cache_handle_write_miss_wb(bc, wi, bio, cache_block);
		else
			cache_handle_write_miss_wt(bc, wi, bio, cache_block);
	} else {
		/*
		 * add to pending list and start state machine
		 */
		work_item_add_pending_io(bc,
					 wi,
					 "read-miss",
					 cache_block->bcb_sector,
					 bio->bi_rw);
		cache_handle_read_miss(bc, wi, bio, cache_block);
	}
}

/*!
 * Handle complete miss case (not only we have a cache miss, we also
 * do not have a cache block). No choice but defer the IO request.
 */
void cache_map_workfunc_no_resources(struct bittern_cache *bc,
				     struct bio *bio,
				     struct deferred_queue *old_queue)
{

	/*
	 * here we are either in a process or kernel thread context,
	 * i.e., we can sleep during resource allocation if needed.
	 */
	M_ASSERT(!in_softirq());
	M_ASSERT(!in_irq());

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, bio, NULL,
		 "blocks-busy-defer (bc_invalid_entries=%u)",
		 atomic_read(&bc->bc_invalid_entries));
	if (bio_data_dir(bio) == WRITE)
		atomic_inc(&bc->bc_write_misses_busy);
	else
		atomic_inc(&bc->bc_read_misses_busy);
	queue_to_deferred(bc, &bc->defer_page, bio, old_queue);
}

/*!
 * Mark io request as pending and start processing it thru the main state
 * machine. Returns 1 if item has been processed, or 0 it if hasn't. In
 * the latter case the item was queued into one of the deferred queues for
 * later processing.
 * \todo add to documentation the undefined case in which we break hardware
 * RAID compatibility. It should not matter, but we need to document it and
 * also document how to fix it if it turns out it matters.
 */
int cache_map_workfunc(struct bittern_cache *bc,
		       struct bio *bio,
		       struct deferred_queue *old_queue)
{
	struct cache_block *cache_block;
	int ret;
	int cache_get_flags;
	int do_bypass;
	bool do_writeback;

	BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, bio, NULL, "enter");
	ASSERT_BITTERN_CACHE(bc);

	/*
	 * here we are either in a process or kernel thread context,
	 * i.e., we can sleep during resource allocation if needed.
	 */
	M_ASSERT(!in_softirq());
	M_ASSERT(!in_irq());

	if (bio_is_pureflush_request(bio)) {
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, bio, NULL,
			 "req-pureflush");
		bio_endio(bio, 0);
		return 1;
	}

	if (bio_is_discard_request(bio)) {
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, bio, NULL,
			 "req-discard");
		/*
		 * wakeup possible waiters
		 */
		wakeup_deferred(bc);
		bio_endio(bio, 0);
		return 1;
	}

	M_ASSERT(bio_is_data_request(bio));

	/*
	 * This is a fundamental design feature of Bittern.
	 * We depend on DM to split the request such that there
	 * no requests which span multiple blocks.
	 */
	M_ASSERT(bio_is_request_single_cache_block(bio));

	/*
	 * Detect sequential access and see if we want to bypass the cache.
	 * for this latter case we rely on the fact that the upper layer will
	 * never send a duplicate request Y for block N [ request(Y,N) ] while
	 * the current request X for the same block is pending [ request(X,N) ].
	 * This is standard block io cache layer behavior, and it should never
	 * be broken.
	 *
	 * There are however possible cases in which this could occur if raw io
	 * is made at the same time. Because unix/posix gives no guarantees in
	 * such case, we do not give them either (too much of a hassle to
	 * track non-cache requests in flight).
	 * Although inconsequential because the result is undefined regardless,
	 * we are breaking compatibility with Hardware RAID in this case.
	 * If we wanted to avoid it, we'd have to create transient cache entries
	 * for the in-flight bypass requests and delete them at the end.
	 * Not sure if this is worth doing it.
	 */
	/*
	 * We need to carry the read_bypass status, because we still want to
	 * return a cache hit if we find the element in cache.
	 *
	 * Sequential reads will not be detected correctly if we have to
	 * defer the request. This is not a big problem because it's an
	 * optimization anyway and not required for correctness.
	 * Furthermore, deferrals very rarely happens.
	 */
	do_bypass = seq_bypass_is_sequential(bc, bio);
	ASSERT(do_bypass == 0 || do_bypass == 1);

	/*
	 * Cache operating mode can change mid-flight, so copy its value.
	 * It's important for the operating mode to be stable within the
	 * context of any single request.
	 */
	do_writeback = is_cache_mode_writeback(bc);
	ASSERT(do_writeback == false || do_writeback == true);

	/*
	 * By default we do want to get a "valid_no_valid" block if we
	 * miss, exception when sequential read/write bypass is set, in
	 * which case we want to bypass the cache and do the IO directly.
	 */
	cache_get_flags = CACHE_FL_HIT;
	if (do_bypass == 0) {
		cache_get_flags |= CACHE_FL_MISS;
		if (do_writeback != 0 && bio_data_dir(bio) == WRITE)
			cache_get_flags |= CACHE_FL_DIRTY;
		else
			cache_get_flags |= CACHE_FL_CLEAN;
	}
	/*
	 * lookup cache block
	 */
	ret = cache_get(bc,
			bio->bi_iter.bi_sector,
			cache_get_flags,
			&cache_block);
	ASSERT_CACHE_GET_RET(ret);

	switch (ret) {
	case CACHE_GET_RET_HIT_IDLE:
		/*
		 * Read/write hit.
		 * We found a cache block and it's idle.
		 * We can handle a read or write hit.
		 */
		ASSERT(cache_block != NULL);
		ASSERT_CACHE_BLOCK(cache_block, bc);
		ASSERT(cache_block->bcb_state == S_CLEAN ||
		       cache_block->bcb_state == S_DIRTY);
		ASSERT(cache_block->bcb_cache_transition ==
		       TS_NONE);

		return cache_map_workfunc_hit(bc,
					      cache_block,
					      bio,
					      do_writeback,
					      old_queue);

	case CACHE_GET_RET_HIT_BUSY:
		/*
		 * Read/write hit.
		 * We found a cache block but it's busy, need to defer.
		 */
		ASSERT(cache_block == NULL);
		cache_map_workfunc_resource_busy(bc, bio, old_queue);
		return 0;

	case CACHE_GET_RET_MISS_INVALID_IDLE:
		/*
		 * Read/write miss.
		 * There is no valid cache block, but got an invalid cache
		 * block on which we can start IO.
		 */
		ASSERT(cache_block != NULL);
		ASSERT_CACHE_BLOCK(cache_block, bc);
		cache_map_workfunc_miss(bc, cache_block, bio, do_writeback);
		return 1;

	case CACHE_GET_RET_MISS:
		/*
		 * Read/write miss. There is no valid cache block, and there
		 * are no free cache blocks, so either handle the bypass case
		 * immediately or defer for execution when resources are
		 * available.
		 */
		ASSERT(cache_block == NULL);
		if (do_bypass) {
			cache_map_workfunc_handle_bypass(bc, bio);
			return 1;
		}
		cache_map_workfunc_no_resources(bc, bio, old_queue);;
		return 0;

	case CACHE_GET_RET_INVALID:
		M_ASSERT("unexpected value of cache_get()" == NULL);
		return 0;

	default:
		M_ASSERT("unexpected value of cache_get()" == NULL);
		return 0;
	}

	/*NOTREACHED*/
}

/*!
 * This function is the main entry point for bittern cache.
 * All user-initiated requests go thru here.
 *
 * In order to avoid excessive resource allocation, the work_item structure
 * for any given request is only allocated right before the request is about
 * to go thru the state machine.
 * In particular, no work_item structure is allocated for deferred requests.
 * We lose the ability of calculating how much time a request spends in the
 * deferred queue, but we save a significant amount of memory.
 */
int bittern_cache_map(struct dm_target *ti, struct bio *bio)
{
	struct bittern_cache *bc = ti->private;

	ASSERT_BITTERN_CACHE(bc);

	ASSERT((bio->bi_rw & REQ_WRITE_SAME) == 0);

	if (bc->error_state != ES_NOERROR) {
		/* error state, bailout with error */
		bio_endio(bio, -EIO);
		return DM_MAPIO_SUBMITTED;
	}

	/* do this here (not workfunc) so to increment these counters once */
	if (bio_is_discard_request(bio)) {
		atomic_inc(&bc->bc_discard_requests);
	} else if (bio_is_pureflush_request(bio)) {
		atomic_inc(&bc->bc_flush_requests);
		atomic_inc(&bc->bc_pure_flush_requests);
	} else {
		ASSERT(bio_is_data_request(bio));
		if ((bio->bi_rw & REQ_FLUSH) != 0)
			atomic_inc(&bc->bc_flush_requests);
		if (bio_data_dir(bio) == WRITE)
			atomic_inc(&bc->bc_write_requests);
		else
			atomic_inc(&bc->bc_read_requests);
	}

	/*
	 * defer to queued queue if pending queue is too high,
	 * or if any of the deferred queues are non-empty (so to avoid request
	 * starvation).
	 */
	if (!can_schedule_map_request(bc) || atomic_read(&bc->bc_deferred_requests) > 0) {
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, bio, NULL,
			 "queue-to-deferred (can_schedule=%u, pending=%u, deferred=%u)",
			 can_schedule_map_request(bc),
			 atomic_read(&bc->bc_pending_requests),
			 atomic_read(&bc->bc_deferred_requests));
		/*
		 * queue to wait_pending deferred list.
		 * note this is the only case in which we do not bump up the
		 * pending request count.
		 */
		queue_to_deferred(bc,
					&bc->defer_page,
					bio,
					NULL);

		return DM_MAPIO_SUBMITTED;
	}

	/*
	 * submit the request as normal.
	 * it's possible that cache_map_workfunc may have to defer it,
	 * but here we don't care, we are return DM_MAPIO_SUBMITTED anyway.
	 */
	cache_map_workfunc(bc, bio, NULL);

	return DM_MAPIO_SUBMITTED;
}

/*!
 * waiter thread for busy deferred queues relies on bc_pending_requests
 * being incremented before we call this.
 */
void wakeup_deferred(struct bittern_cache *bc)
{
	unsigned long flags;

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		 "deferred_requests=%d, defer_busy.curr_count=%d, defer_page.curr_count=%d",
		 atomic_read(&bc->bc_deferred_requests),
		 bc->defer_busy.curr_count,
		 bc->defer_page.curr_count);

	spin_lock_irqsave(&bc->defer_lock, flags);

	if (bc->defer_busy.curr_count != 0 ||
	    bc->defer_page.curr_count != 0)
		queue_work(bc->defer_wq, &bc->defer_work);

	spin_unlock_irqrestore(&bc->defer_lock, flags);
}

/*!
 * Queue a request for deferred execution.
 * This function is called by the map() function in order to queue
 * a request which cannot be satisfied immediately. The deferred worker
 * @ref cache_deferred_worker can also call this function if it finds
 * that the block is busy. In this latter case it needs to avoid waking
 * up itself again to avoid a infinite loop, which is why the old_queue
 * needs to be passed as parameter.
 */
void queue_to_deferred(struct bittern_cache *bc,
		       struct deferred_queue *queue,
		       struct bio *bio,
		       struct deferred_queue *old_queue)
{
	unsigned long flags;
	int val;

	ASSERT(queue == &bc->defer_busy || queue == &bc->defer_page);
	ASSERT(old_queue == &bc->defer_busy ||
	       old_queue == &bc->defer_page ||
	       old_queue == NULL);

	spin_lock_irqsave(&bc->defer_lock, flags);

	queue->tstamp = current_kernel_time_nsec();
	/* bio_list_add adds to tail */
	bio_list_add(&queue->list, bio);
	atomic_inc(&bc->bc_total_deferred_requests);
	val = atomic_inc_return(&bc->bc_deferred_requests);
	atomic_set_if_higher(&bc->bc_highest_deferred_requests, val);
	queue->curr_count++;
	if (queue->curr_count > queue->max_count)
		queue->max_count = queue->curr_count;

	spin_unlock_irqrestore(&bc->defer_lock, flags);

	if (queue != old_queue)
		queue_work(bc->defer_wq, &bc->defer_work);

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		 "%s",
		 (queue == &bc->defer_busy ? "busy" : "page"));
}

struct bio *dequeue_from_deferred(struct bittern_cache *bc,
				  struct deferred_queue *queue)
{
	unsigned long flags;
	struct bio *bio = NULL;

	ASSERT(queue == &bc->defer_busy ||
	       queue == &bc->defer_page);

	spin_lock_irqsave(&bc->defer_lock, flags);

	if (bio_list_non_empty(&queue->list)) {
		bio = bio_list_pop(&queue->list);
		ASSERT(bio != NULL);
		ASSERT(atomic_read(&bc->bc_deferred_requests) > 0);
		queue->curr_count--;
		atomic_dec(&bc->bc_deferred_requests);
		if (queue->curr_count == 0)
			ASSERT(bio_list_empty(&queue->list));
	} else {
		ASSERT(queue->curr_count == 0);
	}

	spin_unlock_irqrestore(&bc->defer_lock, flags);

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, bio, NULL,
		 "%s",
		 (queue == &bc->defer_busy ? "busy" : "page"));
	return bio;
}

/*!
 * returns true if there is work to do and if there enough resources
 * to queue work from this queue.
 */
bool deferred_has_work(struct bittern_cache *bc,
		       struct deferred_queue *queue)
{
	bool has_requests = queue->curr_count != 0;
	return has_requests && can_schedule_map_request(bc);
}

/*! handle one or more deferred requests on a given queue */
int __handle_deferred(struct bittern_cache *bc, struct deferred_queue *queue)
{
	int ret;
	struct bio *bio;

	ASSERT(queue == &bc->defer_busy || queue == &bc->defer_page);

	bio = dequeue_from_deferred(bc, queue);

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, bio, NULL,
		 "wait_%s: curr_c=%u, max_c=%u",
		 (queue == &bc->defer_busy ? "busy" : "page"),
		 queue->curr_count,
		 queue->max_count);

	if (bio == NULL) {
		queue->no_work_count++;
		return 0;
	}

	/*
	 * Now try resubmit the request. It's possible that it will be requeued
	 * again. In the latter case, make sure we don't wake up ourselves
	 * again.
	 */
	ret = cache_map_workfunc(bc, bio, queue);

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		 "wait_%s: curr_c=%u, max_c=%u, ret=%d",
		 (queue == &bc->defer_busy ? "busy" : "pp"),
		 queue->curr_count,
		 queue->max_count,
		 ret);

	if (ret == 0) {
		queue->requeue_count++;
		return 0;
	} else {
		queue->work_count++;
		return 1;
	}
}

/*! deferred io handler */
int handle_deferred(struct bittern_cache *bc, struct deferred_queue *queue)
{
	unsigned long flags;
	int cc, count = 0;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);

	spin_lock_irqsave(&bc->defer_lock, flags);
	if (queue->tstamp != 0) {
		cache_timer_add(&queue->timer, queue->tstamp);
		queue->tstamp = 0;
	}
	spin_unlock_irqrestore(&bc->defer_lock, flags);

	/*
	 * for as long as we have queued deferred requests and
	 * we can requeue, then we need to do this
	 */
	while (deferred_has_work(bc, queue)) {

		ASSERT(bc != NULL);
		ASSERT_BITTERN_CACHE(bc);

		cc = __handle_deferred(bc, queue);
		count += cc;
		if (cc == 0)
			break;
	}

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		 "%d: completed=%u, pending=%u, deferred=%u",
		 count,
		 atomic_read(&bc->bc_completed_requests),
		 atomic_read(&bc->bc_pending_requests),
		 atomic_read(&bc->bc_deferred_requests));
	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		 "%d: io_curr_count=%u",
		 count, queue->curr_count);

	return count;
}

void cache_deferred_worker(struct work_struct *work)
{
	struct bittern_cache *bc;

	ASSERT(!in_irq());
	ASSERT(!in_softirq());

	bc = container_of(work, struct bittern_cache, defer_work);
	ASSERT_BITTERN_CACHE(bc);

	/*
	 * Handle both deferred queues. Busy blocks have higher priority
	 * as they are blocked by an existing request and not by a specific
	 * resource.
	 */

	handle_deferred(bc, &bc->defer_busy);

	handle_deferred(bc, &bc->defer_page);
}
