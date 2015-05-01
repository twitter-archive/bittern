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

/*! queue requests to workqueue */
void cache_make_request_worker(struct work_struct *work)
{
	struct work_item *wi = container_of(work, struct work_item, wi_work);
	struct bittern_cache *bc = wi->wi_cache;
	struct cache_block *cache_block = wi->wi_cache_block;
	struct bio *cloned_bio = wi->wi_cloned_bio;

	ASSERT_WORK_ITEM(wi, bc);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	cache_timer_add(&bc->bc_make_request_wq_timer,
			wi->wi_ts_workqueue);
	/*
	 * printk_info("wi=%p, bc=%p, cache_block=%p, original_bio=%p,
	 * cloned_bio=%p\n", wi, bc, cache_block, original_bio, cloned_bio);
	 */
	wi->wi_ts_physio = current_kernel_time_nsec();
	generic_make_request(cloned_bio);
}

/*! endio function used by state machine */
void cache_state_machine_endio(struct bio *cloned_bio, int err)
{
	struct bittern_cache *bc;
	struct cache_block *cache_block;
	struct bio *original_bio;
	struct work_item *wi;
	int cloned_bio_dir;

	M_ASSERT_FIXME(err == 0);
	ASSERT(cloned_bio != NULL);
	wi = cloned_bio->bi_private;

	ASSERT(wi != NULL);

	bc = wi->wi_cache;
	cache_block = wi->wi_cache_block;
	original_bio = wi->wi_original_bio;

	ASSERT_WORK_ITEM(wi, bc);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(cache_block->bcb_xid != 0);
	ASSERT(cache_block->bcb_xid == wi->wi_io_xid);
	ASSERT(is_sector_number_valid(cache_block->bcb_sector));
	ASSERT(cloned_bio == wi->wi_cloned_bio);
	ASSERT(original_bio == wi->wi_original_bio);

	ASSERT(bio_data_dir(cloned_bio) == READ
	       || bio_data_dir(cloned_bio) == WRITE);
	if (wi->wi_original_bio == wi->wi_cloned_bio) {
		ASSERT((wi->wi_flags & WI_FLAG_BIO_NOT_CLONED) != 0);
		/*
		 * bio has not been cloned, we are using the original one
		 * (this happens on bittern-initiated io requests like dirty
		 * writebacks)
		 */
		BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, original_bio,
			 cloned_bio, "endio-not-cloned");
		ASSERT(bio_data_dir(original_bio) == READ
		       || bio_data_dir(original_bio) == WRITE);
	} else {
		ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
		ASSERT(bio_is_request_single_cache_block(original_bio));
		ASSERT(bio_data_dir(cloned_bio) == READ
		       || bio_data_dir(cloned_bio) == WRITE);
		ASSERT(cache_block->bcb_sector ==
		       bio_sector_to_cache_block_sector(original_bio));
		BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, original_bio,
			 cloned_bio, "endio-cloned");
	}
	if (bio_data_dir(cloned_bio) == READ) {
		cloned_bio_dir = READ;
		cache_timer_add(&bc->bc_timer_cached_device_reads,
				wi->wi_ts_physio);
	} else {
		cloned_bio_dir = WRITE;
		cache_timer_add(&bc->bc_timer_cached_device_writes,
				wi->wi_ts_physio);
	}

	if (cloned_bio == original_bio) {
		/*
		 * bio has not been cloned, we are using the original one
		 * (this happens on bittern-initiated io requests like dirty
		 * writebacks)
		 */
		ASSERT((wi->wi_flags & WI_FLAG_BIO_NOT_CLONED) != 0);

		/*
		 * release original bio
		 */
		bio_put(cloned_bio);
		wi->wi_original_bio = NULL;
		wi->wi_cloned_bio = NULL;
		cloned_bio = NULL;
		original_bio = NULL;
	} else {
		ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);

		/*
		 * release cloned bio
		 */
		bio_put(cloned_bio);
		wi->wi_cloned_bio = NULL;
		cloned_bio = NULL;
	}

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);

	ASSERT(wi != NULL);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(cache_block->bcb_xid != 0);
	ASSERT(cache_block->bcb_xid == wi->wi_io_xid);

	ASSERT_CACHE_STATE(cache_block);
	ASSERT(is_sector_number_valid(cache_block->bcb_sector));

	/*
	 * FIXME: should split treatement of WI_FLAG_MAP_IO vs
	 * WI_FLAG_BIO_CLONED
	 */
	if (wi->wi_io_endio != NULL) {
		ASSERT((wi->wi_flags & WI_FLAG_BIO_NOT_CLONED) != 0);
		ASSERT((wi->wi_flags & WI_FLAG_HAS_ENDIO) != 0);
		ASSERT((wi->wi_flags & WI_FLAG_MAP_IO) == 0);
		ASSERT(wi->wi_io_endio != NULL);
		ASSERT(original_bio == NULL);
		ASSERT(wi->wi_original_bio == NULL);
		ASSERT(wi->wi_cloned_bio == NULL);
		BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, NULL, NULL,
			 "wi_io_endio=%p", wi->wi_io_endio);
	} else {
		ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
		ASSERT((wi->wi_flags & WI_FLAG_HAS_ENDIO) == 0);
		ASSERT((wi->wi_flags & WI_FLAG_MAP_IO) != 0);
		ASSERT(wi->wi_io_endio == NULL);
		ASSERT(original_bio != NULL);
		ASSERT(wi->wi_original_bio != NULL);
		ASSERT(cache_block->bcb_sector ==
		       bio_sector_to_cache_block_sector(original_bio));
	}

	cache_state_machine(bc, wi, original_bio);

	ASSERT_BITTERN_CACHE(bc);
}

/*!
 * handle completions which do not go thru the cache state machine
 * in this case we do not have a cache block
 */
void cache_bio_endio(struct bio *cloned_bio, int err)
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
	ASSERT((wi->wi_flags & WI_FLAG_MAP_IO) != 0);
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
	cache_wakeup_deferred(bc);

	cache_timer_add(&bc->bc_timer_cached_device_reads,
				wi->wi_ts_physio);

	cache_work_item_free(bc, wi);

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

	ASSERT(bio != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(wi->wi_cache_data.di_buffer != NULL);
	ASSERT(wi->wi_cache_data.di_page != NULL);
	ASSERT(atomic_read(&wi->wi_cache_data.di_busy) == 1);

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

		ASSERT(wi->wi_cache_data.di_buffer != NULL);
		ASSERT(wi->wi_cache_data.di_page != NULL);
		memcpy(bi_kaddr + bvec.bv_offset,
		       (char *)(wi->wi_cache_data.di_buffer) +
		       cache_block_copy_offset + biovec_offset, bvec.bv_len);

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

	if (hash_data != NULL) {
		ASSERT(wi->wi_cache_data.di_buffer != NULL);
		ASSERT(wi->wi_cache_data.di_page != NULL);
		*hash_data = murmurhash3_128(wi->wi_cache_data.di_buffer,
					     PAGE_SIZE);
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

	ASSERT(bio != NULL);
	ASSERT(hash_data != NULL);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(wi->wi_cache_data.di_buffer != NULL);
	ASSERT(wi->wi_cache_data.di_page != NULL);
	ASSERT(atomic_read(&wi->wi_cache_data.di_busy) == 1);

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
		M_ASSERT_FIXME(bi_kaddr != NULL);

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
		ASSERT(wi->wi_cache_data.di_buffer != NULL);
		ASSERT(wi->wi_cache_data.di_page != NULL);
		memcpy_nt((char *)(wi->wi_cache_data.di_buffer) +
			  cache_block_copy_offset + biovec_offset,
			  bi_kaddr + bvec.bv_offset, bvec.bv_len);

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

	ASSERT(wi->wi_cache_data.di_buffer != NULL);
	ASSERT(wi->wi_cache_data.di_page != NULL);
	*hash_data = murmurhash3_128(wi->wi_cache_data.di_buffer, PAGE_SIZE);
}

void cache_get_page_read_callback(struct bittern_cache *bc,
				  struct cache_block *cache_block,
				  struct data_buffer_info *dbi_data,
				  void *callback_context, int err)
{
	struct work_item *wi;
	struct bio *bio;

	ASSERT(bc != NULL);
	ASSERT(cache_block != NULL);
	ASSERT(dbi_data != NULL);
	ASSERT(callback_context != NULL);
	wi = (struct work_item *)callback_context;
	bio = wi->wi_original_bio;
	M_ASSERT_FIXME(err == 0);
	ASSERT(wi->wi_cache_block != NULL);
	ASSERT_CACHE_BLOCK(wi->wi_cache_block, bc);
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "err=%d, wi=%p, bc=%p, cache_block=%p, wi_original_cache_block=%p, wi_cache_block=%p, bio=%p",
		 err, wi, bc, cache_block, wi->wi_original_cache_block,
		 wi->wi_cache_block, bio);
	ASSERT(&wi->wi_cache_data == dbi_data);
	ASSERT(wi->wi_cache_data.di_buffer != NULL);
	ASSERT(wi->wi_cache_data.di_page != NULL);
	ASSERT(atomic_read(&wi->wi_cache_data.di_busy) == 1);

	/*
	 * cache_block arg could be wi_original_cache_block, but we always
	 * pass wi_cache_block to the state machine
	 */
	cache_state_machine(bc, wi, bio);
}

void cache_put_page_write_callback(struct bittern_cache *bc,
				   struct cache_block *cache_block,
				   struct data_buffer_info *dbi_data,
				   void *callback_context, int err)
{
	struct work_item *wi;
	struct bio *bio;

	ASSERT(bc != NULL);
	ASSERT(cache_block != NULL);
	ASSERT(dbi_data != NULL);
	ASSERT(callback_context != NULL);
	wi = (struct work_item *)callback_context;
	ASSERT_WORK_ITEM(wi, bc);
	bio = wi->wi_original_bio;
	M_ASSERT_FIXME(err == 0);
	ASSERT(wi->wi_cache_block != NULL);
	ASSERT_CACHE_BLOCK(wi->wi_cache_block, bc);
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "err=%d, wi=%p, bc=%p, cache_block=%p, wi_original_cache_block=%p, wi_cache_block=%p, bio=%p",
		 err, wi, bc, cache_block, wi->wi_original_cache_block,
		 wi->wi_cache_block, bio);
	ASSERT(&wi->wi_cache_data == dbi_data);
	ASSERT(wi->wi_cache_data.di_buffer == NULL);
	ASSERT(wi->wi_cache_data.di_page == NULL);
	ASSERT(atomic_read(&wi->wi_cache_data.di_busy) == 0);

	/*
	 * cache_block arg could be wi_original_cache_block, but we always
	 * pass wi_cache_block to the state machine
	 */
	cache_state_machine(bc, wi, bio);
}

void cache_metadata_write_callback(struct bittern_cache *bc,
				   struct cache_block *cache_block,
				   struct data_buffer_info *dbi_data,
				   void *callback_context, int err)
{
	struct work_item *wi;
	struct bio *bio;

	ASSERT(bc != NULL);
	ASSERT(cache_block != NULL);
	ASSERT(dbi_data != NULL);
	ASSERT(callback_context != NULL);
	wi = (struct work_item *)callback_context;
	ASSERT_WORK_ITEM(wi, bc);
	bio = wi->wi_original_bio;
	M_ASSERT_FIXME(err == 0);
	ASSERT(wi->wi_cache_block != NULL);
	ASSERT_CACHE_BLOCK(wi->wi_cache_block, bc);
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "err=%d, wi=%p, bc=%p, cache_block=%p, wi_original_cache_block=%p, wi_cache_block=%p, bio=%p",
		 err, wi, bc, cache_block, wi->wi_original_cache_block,
		 wi->wi_cache_block, bio);
	ASSERT(&wi->wi_cache_data == dbi_data);
	ASSERT(wi->wi_cache_data.di_buffer == NULL);
	ASSERT(wi->wi_cache_data.di_page == NULL);
	ASSERT(atomic_read(&wi->wi_cache_data.di_busy) == 0);

	/*
	 * cache_block arg could be wi_original_cache_block, but we always
	 * pass wi_cache_block to the state machine
	 */
	cache_state_machine(bc, wi, bio);
}

/*!
 * main state machine
 */
void cache_state_machine(struct bittern_cache *bc,
			 struct work_item *wi,
			 struct bio *bio)
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

	/* Right now we always need to allocate a page buffer even for DAX */
	ASSERT(wi->wi_cache_data.di_buffer_vmalloc_buffer != NULL);
	ASSERT(PAGE_ALIGNED(wi->wi_cache_data.di_buffer_vmalloc_buffer));
	ASSERT(wi->wi_cache_data.di_buffer_vmalloc_page != NULL);
	ASSERT(wi->wi_cache_data.di_buffer_vmalloc_page ==
	       vmalloc_to_page(wi->wi_cache_data.di_buffer_vmalloc_buffer));

	BT_TRACE(BT_LEVEL_TRACE2,
		 bc, wi, cache_block, bio, wi->wi_cloned_bio,
		 "enter");

	switch (cache_block->bcb_state) {
		/*
		 * read hit (wt/wb-clean) :
		 *
		 * VALID_CLEAN -->
		 * VALID_READ_HIT_COPY_FROM_CACHE_START -->
		 * VALID_READ_HIT_COPY_FROM_CACHE_END -->
		 * VALID_CLEAN
		 */
		/*
		 * read hit (wb-dirty):
		 *
		 * VALID_DIRTY -->
		 * VALID_DIRTY_READ_HIT_COPY_FROM_CACHE_START -->
		 * VALID_DIRTY_READ_HIT_COPY_FROM_CACHE_END -->
		 * VALID_DIRTY
		 */
		/*
		 * clean/dirty read hit path
		 *
		 * VALID_DIRTY/CLEAN_READ_HIT_COPY_FROM_CACHE_START:
		 *      start async get_page_read to make data from cache
		 *      available.
		 *      sm_read_hit_copy_from_cache_start().
		 * VALID_DIRTY/CLEAN_READ_HIT_COPY_FROM_CACHE_END:
		 *      copy data to userland.
		 *      terminate async transaction (put_page_read).
		 *      sm_read_hit_copy_from_cache_end().
		 */
	case CACHE_VALID_CLEAN_READ_HIT_COPY_FROM_CACHE_START:
	case CACHE_VALID_DIRTY_READ_HIT_COPY_FROM_CACHE_START:
		sm_read_hit_copy_from_cache_start(bc, wi, bio);
		break;

	case CACHE_VALID_CLEAN_READ_HIT_COPY_FROM_CACHE_END:
	case CACHE_VALID_DIRTY_READ_HIT_COPY_FROM_CACHE_END:
		sm_read_hit_copy_from_cache_end(bc, wi, bio);
		break;

		/*
		 * read miss (wt/wb-clean):
		 *
		 * INVALID -->
		 * VALID_CLEAN_NO_DATA -->
		 * VALID_CLEAN_READ_MISS_COPY_FROM_DEVICE_STARTIO -->
		 * VALID_CLEAN_READ_MISS_COPY_FROM_DEVICE_ENDIO -->
		 * VALID_CLEAN_READ_MISS_COPY_TO_CACHE_END -->
		 * VALID_CLEAN_READ_MISS_METADATA_UPDATE_END -->
		 * VALID_CLEAN
		 */
		/*
		 * read miss (wt/wb-clean):
		 *
		 * VALID_CLEAN_READ_MISS_COPY_FROM_DEVICE_STARTIO
		 *      get_page_write.
		 *      start async cached device disk read into cache.
		 *      sm_read_miss_copy_from_device_startio().
		 * VALID_CLEAN_READ_MISS_COPY_FROM_DEVICE_ENDIO
		 *      completes disk io.
		 *      copy data to userland.
		 *      start async put_page_write to write data to cache and
		 *      update metadata as well.
		 *      sm_read_miss_copy_from_device_endio().
		 * VALID_CLEAN_READ_MISS_COPY_TO_CACHE_END
		 *      terminates async transaction (put_page_write).
		 *      start async transaction to update metadata.
		 *      sm_read_miss_copy_to_cache_end().
		 */
	case CACHE_VALID_CLEAN_READ_MISS_COPY_FROM_DEVICE_STARTIO:
		sm_read_miss_copy_from_device_startio(bc, wi, bio);
		break;

	case CACHE_VALID_CLEAN_READ_MISS_COPY_FROM_DEVICE_ENDIO:
		sm_read_miss_copy_from_device_endio(bc, wi, bio);
		break;

	case CACHE_VALID_CLEAN_READ_MISS_COPY_TO_CACHE_END:
		sm_read_miss_copy_to_cache_end(bc, wi, bio);
		break;

		/*
		 * write miss (wb):
		 *
		 * INVALID
		 * VALID_DIRTY_NO_DATA
		 * VALID_DIRTY_WRITE_MISS_COPY_TO_CACHE_START
		 *      get_page_write.
		 *      copy data from userland.
		 *      start async put_page_write to write data to cache and
		 *      update metadata as well.
		 *      sm_dirty_write_miss_copy_to_cache_start().
		 * VALID_DIRTY_WRITE_MISS_COPY_TO_CACHE_END
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
		 * VALID_DIRTY_WRITE_HIT_COPY_TO_CACHE_START -->
		 * VALID_DIRTY_WRITE_HIT_COPY_TO_CACHE_END -->
		 * VALID_DIRTY
		 */
	case CACHE_VALID_DIRTY_WRITE_MISS_COPY_TO_CACHE_START:
	case CACHE_VALID_DIRTY_WRITE_HIT_COPY_TO_CACHE_START:
		sm_dirty_write_miss_copy_to_cache_start(bc, wi, bio);
		break;

	case CACHE_VALID_DIRTY_WRITE_MISS_COPY_TO_CACHE_END:
	case CACHE_VALID_DIRTY_WRITE_HIT_COPY_TO_CACHE_END:
		sm_dirty_write_miss_copy_to_cache_end(bc, wi, bio);
		break;

		/*
		 * write miss (wt):
		 *
		 * INVALID
		 * VALID_CLEAN_NO_DATA
		 * VALID_CLEAN_WRITE_MISS_COPY_TO_DEVICE_STARTIO
		 *      get_page_write.
		 *      copy data from userland.
		 *      start async write to cached device.
		 *      sm_clean_write_miss_copy_to_device_startio().
		 * VALID_CLEAN_WRITE_MISS_COPY_TO_DEVICE_ENDIO
		 *      completes i/o to cached device.
		 *      start async put_page_write to write data to cache and
		 *      update metadata as well.
		 *      sm_clean_write_miss_copy_to_device_endio().
		 * VALID_CLEAN_WRITE_MISS_COPY_TO_CACHE_END
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
		 * VALID_CLEAN_WRITE_HIT_COPY_TO_DEVICE_STARTIO  -->
		 * VALID_CLEAN_WRITE_HIT_COPY_TO_DEVICE_ENDIO  -->
		 * VALID_CLEAN_WRITE_HIT_COPY_TO_CACHE_END -->
		 * VALID_CLEAN
		 */
	case CACHE_VALID_CLEAN_WRITE_MISS_COPY_TO_DEVICE_STARTIO:
	case CACHE_VALID_CLEAN_WRITE_HIT_COPY_TO_DEVICE_STARTIO:
		sm_clean_write_miss_copy_to_device_startio(bc, wi, bio);
		break;

	case CACHE_VALID_CLEAN_WRITE_MISS_COPY_TO_DEVICE_ENDIO:
	case CACHE_VALID_CLEAN_WRITE_HIT_COPY_TO_DEVICE_ENDIO:
		sm_clean_write_miss_copy_to_device_endio(bc, wi, bio);
		break;

	case CACHE_VALID_CLEAN_WRITE_MISS_COPY_TO_CACHE_END:
	case CACHE_VALID_CLEAN_WRITE_HIT_COPY_TO_CACHE_END:
		sm_clean_write_miss_copy_to_cache_end(bc, wi, bio);
		break;

		/*
		 * [ partial write hit (wt) ] uses the same states as
		 * [ write miss (wb) ] plus the initial copy-from-cache phase
		 * write hit (wt):
		 *
		 * VALID_CLEAN -->
		 * VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_FROM_CACHE_START -->
		 * VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_TO_DEVICE_STARTIO  -->
		 * VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_TO_DEVICE_ENDIO  -->
		 * VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_TO_CACHE_END -->
		 * VALID_CLEAN
		 *
		 * the initial step will make the cache page available for
		 * read/write, all the next steps are the same as in
		 * [ write miss (wb) ].
		 */
	case CACHE_VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_FROM_CACHE_START:
		sm_clean_pwrite_hit_copy_from_cache_start(bc, wi, bio);
		break;

	case CACHE_VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_TO_DEVICE_STARTIO:
		sm_clean_write_miss_copy_to_device_startio(bc, wi, bio);
		break;

	case CACHE_VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_TO_DEVICE_ENDIO:
		sm_clean_write_miss_copy_to_device_endio(bc, wi, bio);
		break;

	case CACHE_VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_TO_CACHE_END:
		sm_clean_write_miss_copy_to_cache_end(bc, wi, bio);
		break;

		/*
		 * [ partial write hit (wb-clean) ] uses the same states as
		 * [ write miss (wb) ] plus the initial copy-from-cache phase.
		 *
		 * write hit (wb-clean):
		 *
		 * VALID_CLEAN -->
		 * VALID_DIRTY_PARTIAL_WRITE_HIT_COPY_FROM_CACHE_START -->
		 * VALID_DIRTY_PARTIAL_WRITE_HIT_COPY_TO_CACHE_START -->
		 * VALID_DIRTY_PARTIAL_WRITE_HIT_COPY_TO_CACHE_END -->
		 * VALID_DIRTY
		 *
		 * the initial step will make the cache page available for
		 * read/write, all the next steps are the same as in
		 * [ write miss (wb) ].
		 */
	case CACHE_VALID_DIRTY_PARTIAL_WRITE_HIT_COPY_FROM_CACHE_START:
		sm_clean_pwrite_hit_copy_from_cache_start(bc, wi, bio);
		break;

	case CACHE_VALID_DIRTY_PARTIAL_WRITE_HIT_COPY_TO_CACHE_START:
		sm_dirty_write_miss_copy_to_cache_start(bc, wi, bio);
		break;

	case CACHE_VALID_DIRTY_PARTIAL_WRITE_HIT_COPY_TO_CACHE_END:
		sm_dirty_write_miss_copy_to_cache_end(bc, wi, bio);
		break;

		/*
		 * dirty write hit (dirty write cloning - clone):
		 *
		 * VALID_DIRTY_NO_DATA -->
		 * VALID_DIRTY_WRITE_HIT_DWC_COPY_TO_CACHE_START -->
		 *      get_page_write.
		 *      copy data from userland.
		 *      start async write to cached device.
		 *      sm_dirty_write_hit_clone_copy_to_cache_start().
		 * VALID_DIRTY_WRITE_HIT_DWC_COPY_TO_CACHE_END -->
		 *      completes i/o to cached device.
		 *      start async put_page_write to write data to cache and
		 *      update metadata as well.
		 *      start async invalidate of original cache block.
		 *      sm_dirty_write_hit_clone_copy_to_cache_end().
		 * VALID_DIRTY
		 */
		/*
		 * handling of dirty partial write hit is the same as above,
		 * except for the initial data cloning step.
		 */
		/*
		 * partial dirty write hit (dirty write cloning - clone):
		 *
		 * VALID_DIRTY_NO_DATA -->
		 * VALID_DIRTY_PARTIAL_WRITE_HIT_DWC_COPY_FROM_ORIGINAL_CACHE_START -->
		 *      get_page_read.
		 *      start async read from cached device.
		 *      sm_dirty_pwrite_hit_clone_copy_from_cache_start().
		 * VALID_DIRTY_PARTIAL_WRITE_HIT_DWC_COPY_TO_CLONED_CACHE_START -->
		 *      clone read page into write page.
		 * VALID_DIRTY_PARTIAL_WRITE_HIT_DWC_COPY_TO_CLONED_CACHE_END -->
		 * VALID_DIRTY
		 */
	case CACHE_VALID_DIRTY_PARTIAL_WRITE_HIT_DWC_COPY_FROM_ORIGINAL_CACHE_START:
		sm_dirty_pwrite_hit_clone_copy_from_cache_start(bc, wi, bio);
		break;

	case CACHE_VALID_DIRTY_WRITE_HIT_DWC_COPY_TO_CACHE_START:
	case CACHE_VALID_DIRTY_PARTIAL_WRITE_HIT_DWC_COPY_TO_CLONED_CACHE_START:
		sm_dirty_write_hit_clone_copy_to_cache_start(bc, wi, bio);
		break;

	case CACHE_VALID_DIRTY_WRITE_HIT_DWC_COPY_TO_CACHE_END:
	case CACHE_VALID_DIRTY_PARTIAL_WRITE_HIT_DWC_COPY_TO_CLONED_CACHE_END:
		sm_dirty_write_hit_clone_copy_to_cache_end(bc, wi, bio);
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
		 * VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO -->
		 * VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO -->
		 * VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END -->
		 */
		/*
		 * partial write miss (wt):
		 *
		 * INVALID -->
		 * VALID_CLEAN_NO_DATA -->
		 * VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO -->
		 *      get_page_read.
		 *      start async read from cached device.
		 *      sm_pwrite_miss_copy_from_device_startio().
		 * VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO -->
		 *      copy data from userland to cache.
		 *      start async write to cached device.
		 *      sm_pwrite_miss_copy_from_device_startio().
		 * VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_TO_DEVICE_ENDIO -->
		 *      completes async write to cached device.
		 *      starts async write of data and metadata to cache.
		 *      sm_pwrite_miss_copy_to_device_endio().
		 * VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END -->
		 *      completes async write of data and metadata to cache.
		 *      sm_pwrite_miss_copy_to_cache_end().
		 * VALID_CLEAN
		 */
	case CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO:
	case CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO:
		sm_pwrite_miss_copy_from_device_startio(bc, wi, bio);
		break;

	case CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO:
	case CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO:
		sm_pwrite_miss_copy_from_device_endio(bc, wi, bio);
		break;

	case CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_TO_DEVICE_ENDIO:
		sm_pwrite_miss_copy_to_device_endio(bc, wi, bio);
		break;

	case CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END:
	case CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END:
		sm_pwrite_miss_copy_to_cache_end(bc, wi, bio);
		break;

		/*
		 * writeback_flush (wb):
		 *
		 * VALID_DIRTY -->
		 * VALID_DIRTY_WRITEBACK_COPY_FROM_CACHE_START -->
		 * VALID_DIRTY_WRITEBACK_COPY_FROM_CACHE_END -->
		 * VALID_DIRTY_WRITEBACK_COPY_TO_DEVICE_ENDIO -->
		 * VALID_DIRTY_WRITEBACK_UPDATE_METADATA_END -->
		 * VALID_CLEAN
		 *
		 * bgwriter-initiated state path to write out dirty data.
		 *
		 * VALID_DIRTY
		 * VALID_DIRTY_WRITEBACK_COPY_FROM_CACHE_START
		 *      setup cache buffer for read.
		 *      sm_writeback_copy_from_cache_start().
		 * VALID_DIRTY_WRITEBACK_COPY_FROM_CACHE_END
		 *      done setup cache buffer for read.
		 *      start io write to cached device.
		 *      sm_writeback_copy_from_cache_end().
		 * VALID_DIRTY_WRITEBACK_COPY_TO_DEVICE_ENDIO
		 *      done io write to cached device.
		 *      start async metadata update.
		 *      sm_writeback_copy_to_device_endio().
		 * VALID_DIRTY_WRITEBACK_UPDATE_METADATA_END
		 *      complete async metadata update.
		 *      sm_writeback_update_metadata_end().
		 * VALID_CLEAN
		 *
		 */
		/*
		 * writeback_flush (wb):
		 *
		 * VALID_DIRTY -->
		 * VALID_DIRTY_WRITEBACK_INV_COPY_FROM_CACHE_START -->
		 * VALID_DIRTY_WRITEBACK_INV_COPY_FROM_CACHE_END -->
		 * VALID_DIRTY_WRITEBACK_INV_COPY_TO_DEVICE_ENDIO -->
		 * VALID_DIRTY_WRITEBACK_INV_UPDATE_METADATA_END -->
		 * VALID_CLEAN
		 *
		 * writeback_invalidate is the same as writeback_flush,
		 * except that the final state is INVALID.
		 */
	case CACHE_VALID_DIRTY_WRITEBACK_COPY_FROM_CACHE_START:
	case CACHE_VALID_DIRTY_WRITEBACK_INV_COPY_FROM_CACHE_START:
		sm_writeback_copy_from_cache_start(bc, wi, bio);
		break;

	case CACHE_VALID_DIRTY_WRITEBACK_COPY_FROM_CACHE_END:
	case CACHE_VALID_DIRTY_WRITEBACK_INV_COPY_FROM_CACHE_END:
		sm_writeback_copy_from_cache_end(bc, wi, bio);
		break;

	case CACHE_VALID_DIRTY_WRITEBACK_COPY_TO_DEVICE_ENDIO:
	case CACHE_VALID_DIRTY_WRITEBACK_INV_COPY_TO_DEVICE_ENDIO:
		sm_writeback_copy_to_device_endio(bc, wi, bio);
		break;

	case CACHE_VALID_DIRTY_WRITEBACK_UPDATE_METADATA_END:
	case CACHE_VALID_DIRTY_WRITEBACK_INV_UPDATE_METADATA_END:
		sm_writeback_update_metadata_end(bc, wi, bio);
		break;

		/*
		 * clean invalidation (wt/wb):  VALID_CLEAN -->
		 *                              VALID_CLEAN_INVALIDATE_START,
		 *                              VALID_CLEAN_INVALIDATE_END,
		 *                              INVALID
		 *
		 * dirty invalidation (wb):     VALID_DIRTY -->
		 *                              VALID_DIRTY_INVALIDATE_START,
		 *                              VALID_DIRTY_INVALIDATE_END,
		 *                              INVALID
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
	case CACHE_VALID_CLEAN_INVALIDATE_START:
	case CACHE_VALID_DIRTY_INVALIDATE_START:
		sm_invalidate_start(bc, wi, bio);
		break;

	case CACHE_VALID_CLEAN_INVALIDATE_END:
	case CACHE_VALID_DIRTY_INVALIDATE_END:
		sm_invalidate_end(bc, wi, bio);
		break;

	case CACHE_INVALID:
	case CACHE_VALID_CLEAN_NO_DATA:
	case CACHE_VALID_DIRTY_NO_DATA:
	case CACHE_VALID_CLEAN:
	case CACHE_VALID_DIRTY:
	case CACHE_VALID_CLEAN_VERIFY:
	default:
		printk_err("unknown_cache_state: bc=%p, wi=%p, bio=0x%llx, cache_block=%p\n",
			   bc, wi, (long long)bio, cache_block);
		if (bio != NULL)
			printk_err("unknown_cache_state: bio_data_dir=0x%lx, bio_bi_sector=%lu\n",
				   bio_data_dir(bio), bio->bi_iter.bi_sector);
		printk_err("unknown_cache_state: wi_op_type=%c, wi_op_sector=%lu, wi_op_rw=0x%lx\n",
			   wi->wi_op_type, wi->wi_op_sector, wi->wi_op_rw);
		printk_err("unknown_cache_state: bcb_sector=%lu, cb_state=%d(%s)\n",
			   cache_block->bcb_sector,
			   cache_block->bcb_state,
			   cache_state_to_str(cache_block->bcb_state));
		M_ASSERT("unknown cache state" == NULL);
		break;
	}
}

void cache_handle_cache_hit(struct bittern_cache *bc, struct work_item *wi,
			    struct cache_block *cache_block, struct bio *bio)
{
	unsigned long flags, cache_flags;
	int partial_page;

	ASSERT(bc != NULL);
	ASSERT(cache_block != NULL);
	ASSERT(bio != NULL);
	ASSERT(wi != NULL);
	partial_page =
	    (is_request_cache_block
	     (bio->bi_iter.bi_sector, bio->bi_iter.bi_size) == 0);
	BT_TRACE(BT_LEVEL_TRACE3, bc, wi, cache_block, bio, NULL,
		 "enter, partial_page=%d", partial_page);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(wi->wi_original_bio == bio);
	ASSERT(bio_is_request_single_cache_block(bio));
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(wi->wi_cache_block == cache_block);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(cache_block->bcb_state == CACHE_VALID_CLEAN
	       || cache_block->bcb_state == CACHE_VALID_DIRTY);
	ASSERT(cache_block->bcb_transition_path ==
	       CACHE_TRANSITION_PATH_NONE);
	ASSERT(atomic_read(&cache_block->bcb_refcount) > 0);
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));

	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);

	/* set transaction xid */
	cache_block->bcb_xid = wi->wi_io_xid;
	if (bio_data_dir(bio) == WRITE) {
		atomic_inc(&bc->bc_total_write_hits);
		if (is_work_item_cache_mode_writeback(wi)) {
			/*
			 * this a clean write hit
			 * write cloning on dirty write hit is handled in a separate function
			 */
			ASSERT(cache_block->bcb_state != CACHE_VALID_DIRTY);
			ASSERT(cache_block->bcb_state == CACHE_VALID_CLEAN);
			ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) ==
			       0);
			ASSERT(wi->wi_original_cache_block == NULL);

			/*!
			 * \todo this is bad, should have a function to do this
			 * and the function should be in bittern_cache_getput()
			 * where these counters are manipulated.
			 */
			atomic_inc(&bc->bc_clean_write_hits);
			atomic_dec(&bc->bc_valid_entries_clean);
			atomic_inc(&bc->bc_valid_entries_dirty);

			M_ASSERT(atomic_read(&bc->bc_valid_entries_clean) >= 0);
			M_ASSERT(atomic_read(&bc->bc_valid_entries_dirty) <=
				 atomic_read(&bc->bc_total_entries));
			M_ASSERT(atomic_read(&bc->bc_valid_entries_clean) <=
				 atomic_read(&bc->bc_total_entries));
			M_ASSERT(atomic_read(&bc->bc_valid_entries) <=
				 atomic_read(&bc->bc_total_entries));
			M_ASSERT(atomic_read(&bc->bc_invalid_entries) <=
				 atomic_read(&bc->bc_total_entries));

			if (bio->bi_iter.bi_size == PAGE_SIZE) {
				/* full page write */
				/*
				 * [ write hit (wb-clean) ] uses the same states
				 * as [ write miss (wb) ]
				 *
				 * write hit (wb-clean):
				 *
				 * VALID_CLEAN -->
				 * VALID_DIRTY_WRITE_HIT_COPY_TO_CACHE_START -->
				 * VALID_DIRTY_WRITE_HIT_COPY_TO_CACHE_END -->
				 * VALID_DIRTY
				 */
				cache_state_transition_initial(bc, cache_block,
					CACHE_TRANSITION_PATH_WRITE_HIT_WB_CLEAN,
					CACHE_VALID_DIRTY_WRITE_HIT_COPY_TO_CACHE_START);
			} else {
				/* partial page write */
				atomic_inc(&bc->bc_clean_write_hits_rmw);
				/*
				 * [ partial write hit (wb-clean) ]
				 * uses the same states as [ write miss (wb) ]
				 * plus the initial copy-from-cache phase
				 *
				 * write hit (wb-clean):
				 *
				 * VALID_CLEAN -->
				 * VALID_DIRTY_PARTIAL_WRITE_HIT_COPY_FROM_CACHE_START -->
				 * VALID_DIRTY_PARTIAL_WRITE_HIT_COPY_TO_CACHE_START -->
				 * VALID_DIRTY_PARTIAL_WRITE_HIT_COPY_TO_CACHE_END -->
				 * VALID_DIRTY
				 */
				cache_state_transition_initial(bc, cache_block,
					CACHE_TRANSITION_PATH_PARTIAL_WRITE_HIT_WB_CLEAN,
					CACHE_VALID_DIRTY_PARTIAL_WRITE_HIT_COPY_FROM_CACHE_START);
			}
			/* add/move to the tail of the dirty list */
			list_del_init(&cache_block->bcb_entry_cleandirty);
			list_add_tail(&cache_block->bcb_entry_cleandirty,
				      &bc->bc_valid_entries_dirty_list);
		} else {
			ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) ==
			       0);
			ASSERT(cache_block->bcb_state == CACHE_VALID_CLEAN);
			ASSERT(wi->wi_original_cache_block == NULL);
			atomic_inc(&bc->bc_clean_write_hits);
			if (bio->bi_iter.bi_size == PAGE_SIZE) {
				/* full page write */
				/*
				 * [ write hit (wt) ] uses the same states as
				 * [ write miss (wb) ]
				 *
				 * write hit (wt):
				 * VALID_CLEAN -->
				 * VALID_CLEAN_WRITE_HIT_COPY_TO_DEVICE_STARTIO  -->
				 * VALID_CLEAN_WRITE_HIT_COPY_TO_DEVICE_ENDIO  -->
				 * VALID_CLEAN_WRITE_HIT_COPY_TO_CACHE_END -->
				 * VALID_CLEAN
				 */
				cache_state_transition_initial(bc, cache_block,
					CACHE_TRANSITION_PATH_WRITE_HIT_WT,
					CACHE_VALID_CLEAN_WRITE_HIT_COPY_TO_DEVICE_STARTIO);
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
				 * VALID_CLEAN -->
				 * VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_FROM_CACHE_START -->
				 * VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_TO_DEVICE_STARTIO  -->
				 * VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_TO_DEVICE_ENDIO  -->
				 * VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_TO_CACHE_END -->
				 * VALID_CLEAN
				 */
				cache_state_transition_initial(bc, cache_block,
					CACHE_TRANSITION_PATH_PARTIAL_WRITE_HIT_WT,
					CACHE_VALID_CLEAN_PARTIAL_WRITE_HIT_COPY_FROM_CACHE_START);
			}
			/* add/move to the tail of the clean list */
			list_del_init(&cache_block->bcb_entry_cleandirty);
			list_add_tail(&cache_block->bcb_entry_cleandirty,
				      &bc->bc_valid_entries_clean_list);
		}
	} else {
		atomic_inc(&bc->bc_total_read_hits);
		ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
		ASSERT(wi->wi_original_cache_block == NULL);
		if (wi->wi_bypass)
			atomic_inc(&bc->bc_seq_read.bypass_hit);
		if (cache_block->bcb_state == CACHE_VALID_DIRTY) {
			atomic_inc(&bc->bc_dirty_read_hits);
			/*
			 * read hit (wb-dirty):
			 *
			 * VALID_DIRTY -->
			 * VALID_DIRTY_READ_HIT_COPY_FROM_CACHE_START -->
			 * VALID_DIRTY_READ_HIT_COPY_FROM_CACHE_END -->
			 * VALID_DIRTY
			 */
			cache_state_transition_initial(bc, cache_block,
				CACHE_TRANSITION_PATH_READ_HIT_WB_DIRTY,
				CACHE_VALID_DIRTY_READ_HIT_COPY_FROM_CACHE_START);
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
			 * VALID_CLEAN_READ_HIT_COPY_FROM_CACHE_START -->
			 * VALID_CLEAN_READ_HIT_COPY_FROM_CACHE_END -->
			 * VALID_CLEAN
			 */
			cache_state_transition_initial(bc, cache_block,
				CACHE_TRANSITION_PATH_READ_HIT_WTWB_CLEAN,
				CACHE_VALID_CLEAN_READ_HIT_COPY_FROM_CACHE_START);
			/* add/move to the tail of the clean list */
			list_del_init(&cache_block->bcb_entry_cleandirty);
			list_add_tail(&cache_block->bcb_entry_cleandirty,
				      &bc->bc_valid_entries_clean_list);
		}
	}

	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "handle-cache-hit");
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	cache_state_machine(bc, wi, bio);
}

void cache_handle_cache_hit_write_clone(struct bittern_cache *bc,
					struct work_item *wi,
					struct cache_block *original_cache_block,
					struct bio *bio,
					struct cache_block *cloned_cache_block)
{
	unsigned long flags, cache_flags;

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
	ASSERT(original_cache_block->bcb_state == CACHE_VALID_DIRTY);

	ASSERT(cloned_cache_block != NULL);
	ASSERT(cloned_cache_block->bcb_sector ==
	       original_cache_block->bcb_sector);
	ASSERT(cloned_cache_block->bcb_state ==
	       CACHE_VALID_DIRTY_NO_DATA);
	ASSERT(atomic_read(&cloned_cache_block->bcb_refcount) > 0);

	/* set transaction xid */
	cloned_cache_block->bcb_xid = wi->wi_io_xid;

	wi->wi_flags |= WI_FLAG_WRITE_CLONING;
	wi->wi_original_cache_block = original_cache_block;
	wi->wi_cache_block = cloned_cache_block;

	atomic_inc(&bc->bc_total_write_hits);
	atomic_inc(&bc->bc_dirty_write_hits);

	/*
	 * set state for original cache block
	 */
	/*
	 * dirty invalidation (wb):
	 * [ also used to invalidate the original block on write cloning ]
	 *      VALID_DIRTY -->
	 *      VALID_DIRTY_INVALIDATE_START,
	 *      VALID_DIRTY_INVALIDATE_END,
	 *      INVALID
	 */
	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	spin_lock_irqsave(&original_cache_block->bcb_spinlock, cache_flags);
	cache_state_transition_initial(bc,
			       original_cache_block,
			       CACHE_TRANSITION_PATH_DIRTY_INVALIDATION_WB,
			       CACHE_VALID_DIRTY_INVALIDATE_START);
	/* add/move to the tail of the dirty list */
	list_del_init(&original_cache_block->bcb_entry_cleandirty);
	list_add_tail(&original_cache_block->bcb_entry_cleandirty,
		      &bc->bc_valid_entries_dirty_list);
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
		/* full page write */
		/*
		 * dirty write hit (dirty write cloning - clone):
		 *                              VALID_DIRTY_NO_DATA -->
		 *                              VALID_DIRTY_WRITE_HIT_DWC_COPY_TO_CACHE_START -->
		 *                              VALID_DIRTY_WRITE_HIT_DWC_COPY_TO_CACHE_END -->
		 *                              VALID_DIRTY_WRITE_HIT_DWC_INVALIDATE_ORIGINAL_END -->
		 *                              VALID_DIRTY
		 */
		cache_state_transition_initial(bc,
					       cloned_cache_block,
					       CACHE_TRANSITION_PATH_WRITE_HIT_WB_DIRTY_DWC_CLONE,
					       CACHE_VALID_DIRTY_WRITE_HIT_DWC_COPY_TO_CACHE_START);
	} else {
		/* partial page write */
		atomic_inc(&bc->bc_dirty_write_hits_rmw);
		/*
		 * partial dirty write hit (dirty write cloning - clone):
		 *                              VALID_DIRTY_NO_DATA -->
		 *                              VALID_DIRTY_PARTIAL_WRITE_HIT_DWC_COPY_FROM_ORIGINAL_CACHE_START -->
		 *                              VALID_DIRTY_PARTIAL_WRITE_HIT_DWC_COPY_TO_CLONED_CACHE_START -->
		 *                              VALID_DIRTY_PARTIAL_WRITE_HIT_DWC_COPY_TO_CLONED_CACHE_END -->
		 *                              VALID_DIRTY_PARTIAL_WRITE_HIT_DWC_INVALIDATE_ORIGINAL_END -->
		 *                              VALID_DIRTY
		 */
		cache_state_transition_initial(bc,
					       cloned_cache_block,
					       CACHE_TRANSITION_PATH_PARTIAL_WRITE_HIT_WB_DIRTY_DWC_CLONE,
					       CACHE_VALID_DIRTY_PARTIAL_WRITE_HIT_DWC_COPY_FROM_ORIGINAL_CACHE_START);
	}
	/* add/move to the tail of the dirty list */
	list_del_init(&cloned_cache_block->bcb_entry_cleandirty);
	list_add_tail(&cloned_cache_block->bcb_entry_cleandirty,
		      &bc->bc_valid_entries_dirty_list);
	spin_unlock_irqrestore(&cloned_cache_block->bcb_spinlock, cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, original_cache_block, bio, NULL,
		 "handle-cache-hit-original-cache-block");
	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, cloned_cache_block, bio, NULL,
		 "handle-cache-hit-cloned-cache-block");
	ASSERT(cloned_cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));

	ASSERT(wi->wi_cache_block == cloned_cache_block);
	cache_state_machine(bc, wi, bio);
}

void cache_handle_cache_miss(struct bittern_cache *bc,
			     struct work_item *wi, struct bio *bio,
			     struct cache_block *cache_block,
			     int got_invalidated)
{
	int partial_page = (bio_is_request_cache_block(bio) == 0);
	unsigned long flags, cache_flags;

	BT_TRACE(BT_LEVEL_TRACE3, bc, wi, cache_block, bio, NULL,
		 "enter: got_invalidated=%d, partial_page=%d", got_invalidated,
		 partial_page);

	ASSERT(bc != NULL);
	ASSERT(bio != NULL);
	ASSERT(wi != NULL);
	ASSERT(cache_block != NULL);
	ASSERT(got_invalidated == 0 || got_invalidated == 1);
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
	ASSERT(cache_block->bcb_state == CACHE_VALID_CLEAN_NO_DATA
	       || cache_block->bcb_state == CACHE_VALID_DIRTY_NO_DATA);
	ASSERT(cache_block->bcb_transition_path ==
	       CACHE_TRANSITION_PATH_NONE);

	ASSERT((wi->wi_flags & WI_FLAG_WRITE_CLONING) == 0);
	ASSERT(wi->wi_original_cache_block == NULL);

	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	spin_lock_irqsave(&cache_block->bcb_spinlock, cache_flags);

	/* set transaction xid */
	cache_block->bcb_xid = wi->wi_io_xid;
	wi->wi_cache_block = cache_block;
	if (bio_data_dir(bio) == WRITE) {
		atomic_inc(&bc->bc_total_write_misses);
		atomic_inc(&bc->bc_dirty_write_misses);
		if (is_work_item_cache_mode_writeback(wi)) {
			ASSERT(cache_block->bcb_state ==
			       CACHE_VALID_DIRTY_NO_DATA);
			atomic_inc(&bc->bc_dirty_write_misses);
			/*
			 * WB
			 */
			if (partial_page) {
				atomic_inc(&bc->bc_dirty_write_misses_rmw);
				/*
				 * partial write miss (wb):     INVALID -->
				 *                              VALID_DIRTY_NO_DATA -->
				 *                              VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO -->
				 *                              VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO -->
				 *                              VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END -->
				 */
				cache_state_transition_initial(bc,
							       cache_block,
							       CACHE_TRANSITION_PATH_PARTIAL_WRITE_MISS_WB,
							       CACHE_VALID_DIRTY_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO);
				BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block,
					 bio, NULL, "partial-write-rmw");
				/* add/move to the tail of the dirty list */
				list_del_init(&cache_block->
					      bcb_entry_cleandirty);
				list_add_tail(&cache_block->
					      bcb_entry_cleandirty,
					      &bc->bc_valid_entries_dirty_list);
			} else {
				/*
				 * write miss (wb):             INVALID -->
				 *                              VALID_DIRTY_NO_DATA -->
				 *                              VALID_DIRTY_WRITE_MISS_COPY_TO_CACHE_START -->
				 *                              VALID_DIRTY_WRITE_MISS_COPY_TO_CACHE_END -->
				 *                              VALID_DIRTY
				 */
				cache_state_transition_initial(bc,
							       cache_block,
							       CACHE_TRANSITION_PATH_WRITE_MISS_WB,
							       CACHE_VALID_DIRTY_WRITE_MISS_COPY_TO_CACHE_START);
				/* add/move to the tail of the dirty list */
				list_del_init(&cache_block->bcb_entry_cleandirty);
				list_add_tail(&cache_block->bcb_entry_cleandirty,
					      &bc->bc_valid_entries_dirty_list);
			}
		} else {
			ASSERT(cache_block->bcb_state ==
			       CACHE_VALID_CLEAN_NO_DATA);
			atomic_inc(&bc->bc_clean_write_misses);
			/*
			 * WT
			 */
			if (partial_page) {
				atomic_inc(&bc->bc_clean_write_misses_rmw);
				/*
				 * partial write miss (wt):     INVALID -->
				 *                              VALID_CLEAN_NO_DATA -->
				 *                              VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO -->
				 *                              VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_ENDIO -->
				 *                              VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_TO_DEVICE_ENDIO -->
				 *                              VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_TO_CACHE_END -->
				 *                              VALID_CLEAN
				 */
				cache_state_transition_initial(bc,
							       cache_block,
							       CACHE_TRANSITION_PATH_PARTIAL_WRITE_MISS_WT,
							       CACHE_VALID_CLEAN_PARTIAL_WRITE_MISS_COPY_FROM_DEVICE_STARTIO);
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
				 * write miss (wt):             INVALID -->
				 *                              VALID_CLEAN_NO_DATA -->
				 *                              VALID_CLEAN_WRITE_MISS_COPY_TO_DEVICE_STARTIO -->
				 *                              VALID_CLEAN_WRITE_MISS_COPY_TO_DEVICE_ENDIO -->
				 *                              VALID_CLEAN_WRITE_MISS_COPY_TO_CACHE_END -->
				 *                              VALID_CLEAN
				 */
				cache_state_transition_initial(bc,
							       cache_block,
							       CACHE_TRANSITION_PATH_WRITE_MISS_WT,
							       CACHE_VALID_CLEAN_WRITE_MISS_COPY_TO_DEVICE_STARTIO);
				/* add/move to the tail of the clean list */
				list_del_init(&cache_block->
					      bcb_entry_cleandirty);
				list_add_tail(&cache_block->
					      bcb_entry_cleandirty,
					      &bc->bc_valid_entries_clean_list);
			}
		}
	} else {
		ASSERT(cache_block->bcb_state ==
		       CACHE_VALID_CLEAN_NO_DATA);
		atomic_inc(&bc->bc_total_read_misses);
		atomic_inc(&bc->bc_read_misses);
		/*
		 * read miss (wt/wb-clean):
		 *
		 * INVALID -->
		 * VALID_CLEAN_NO_DATA -->
		 * VALID_CLEAN_READ_MISS_COPY_FROM_DEVICE_STARTIO -->
		 * VALID_CLEAN_READ_MISS_COPY_FROM_DEVICE_ENDIO -->
		 * VALID_CLEAN_READ_MISS_COPY_TO_CACHE_END -->
		 * VALID_CLEAN_READ_MISS_METADATA_UPDATE_END -->
		 * VALID_CLEAN
		 */
		cache_state_transition_initial(bc, cache_block,
			CACHE_TRANSITION_PATH_READ_MISS_WTWB_CLEAN,
			CACHE_VALID_CLEAN_READ_MISS_COPY_FROM_DEVICE_STARTIO);
		/* add/move to the tail of the clean list */
		list_del_init(&cache_block->bcb_entry_cleandirty);
		list_add_tail(&cache_block->bcb_entry_cleandirty,
			      &bc->bc_valid_entries_clean_list);
	}

	spin_unlock_irqrestore(&cache_block->bcb_spinlock, cache_flags);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, cache_block, bio, NULL,
		 "handling-cache-miss");
	ASSERT(cache_block->bcb_sector ==
	       bio_sector_to_cache_block_sector(bio));
	ASSERT(wi->wi_cache_block == cache_block);
	cache_state_machine(bc, wi, bio);
}

void cache_handle_bypass(struct bittern_cache *bc, struct work_item *wi,
			 struct bio *bio)
{
	struct bio *cloned_bio;

	ASSERT(bc != NULL);
	ASSERT(bio != NULL);
	ASSERT(wi != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(wi->wi_original_bio == bio);
	ASSERT(wi->wi_cache_block == NULL);
	/*
	 * sequential bypass, skip cache, but keep track of this io request
	 */
	ASSERT(wi->wi_bypass == 1);
	ASSERT(wi->wi_cloned_bio == NULL);
	ASSERT(wi->wi_original_bio == bio);
	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, NULL, bio, NULL,
		 "handle_bypass");
	ASSERT((wi->wi_flags & WI_FLAG_MAP_IO) != 0);
	ASSERT((wi->wi_flags & WI_FLAG_BIO_CLONED) != 0);
	/*
	 * clone bio
	 */
	cloned_bio = bio_clone(bio, GFP_NOIO | GFP_ATOMIC);
	M_ASSERT_FIXME(cloned_bio != NULL);
	cloned_bio->bi_bdev = bc->bc_dev->bdev;
	cloned_bio->bi_end_io = cache_bio_endio;
	cloned_bio->bi_private = wi;
	wi->wi_cloned_bio = cloned_bio;
	if (bio_data_dir(bio) == READ) {
		atomic_inc(&bc->bc_seq_read.bypass_count);
		atomic_inc(&bc->bc_pending_read_bypass_requests);
	} else {
		atomic_inc(&bc->bc_seq_write.bypass_count);
		atomic_inc(&bc->bc_pending_write_bypass_requests);
	}
	cache_track_hash_clear(bc, bio_sector_to_cache_block_sector(bio));
	/*
	 * schedule request
	 */
	wi->wi_ts_physio = current_kernel_time_nsec();
	generic_make_request(cloned_bio);
}

void bittern_dump_bio(struct bittern_cache *bc, struct bio *bio)
{
	struct bvec_iter bi_iterator;
	struct bio_vec bvec;

	printk_info("bc=%p, cached_dev=%p, bittern_dev=%p, dir=0x%lx, flags=0x%lx, bio=%p, bi_sector=%lu, bi_size=%u, bi_idx=%u, bi_vcnt=%u\n",
	     bc, bc->bc_dev->bdev, bio->bi_bdev, bio_data_dir(bio),
	     bio->bi_flags, bio, bio->bi_iter.bi_sector, bio->bi_iter.bi_size,
	     bio->bi_iter.bi_idx, bio->bi_vcnt);
	printk_info("bio_segments(bio)=%u\n", bio_segments(bio));
	bio_for_each_segment(bvec, bio, bi_iterator) {
		printk_info("         bio_vec:   bv_offset=%u, bv_len=%u, bv_offset+bv_len=%u, bv_page=%p\n",
		     bvec.bv_offset, bvec.bv_len, bvec.bv_offset + bvec.bv_len,
		     bvec.bv_page);
	}
}

static inline void cache_update_pending(struct bittern_cache *bc,
					struct bio *bio, bool is_sequential)
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
 * Read/write hit.
 * We found a cache block and it's idle. We can now start IO on it.
 */
int cache_map_workfunc_hit_idle(struct bittern_cache *bc,
				struct work_item *wi,
				struct cache_block *cache_block,
				struct bio *bio)
{
	if (bio_data_dir(bio) == WRITE &&
				cache_block->bcb_state == CACHE_VALID_DIRTY) {
		/*
		 * handle write cloning
		 */
		struct cache_block *cloned_cache_block = NULL;
		int r;

		r = cache_get_clone(bc, cache_block, &cloned_cache_block);
		if (r == CACHE_GET_RET_MISS_INVALID_IDLE) {
			BT_TRACE(BT_LEVEL_TRACE1, bc, wi,
				 cloned_cache_block, bio, NULL,
				 "cache-hit-cloned-block");
			ASSERT(cloned_cache_block != NULL);
			/*
			 * inc pending counters
			 */
			cache_update_pending(bc, bio, false);
			/*
			 * add to pending list and start state machine
			 */
			cache_work_item_add_pending_io(bc, wi, 'm',
						       cache_block->
						       bcb_sector,
						       bio->bi_rw);
			cache_handle_cache_hit_write_clone(bc,
							   wi,
							   cache_block,
							   bio,
							   cloned_cache_block);
			return 1;
		}
		/*
		 * couldn't get write clone, defer
		 */
		BT_TRACE(BT_LEVEL_TRACE1, bc, wi, cache_block,
			 bio, NULL,
			 "cache-hit-no-cloned-block-deferring");
		ASSERT(cloned_cache_block == NULL);
		ASSERT(r == CACHE_GET_RET_MISS);
		/*
		 * no cloned block, so we need to release
		 * original block and defer the request.
		 */
		cache_put_update_age(bc, cache_block, 1);
		atomic_inc(&bc->bc_write_hits_busy);
		/*
		 * defer request
		 */
		cache_queue_to_deferred(bc,
					&bc->bc_deferred_wait_page,
					wi);
		return 0;
	}
	/*
	 * inc pending counters
	 */
	cache_update_pending(bc, bio, false);
	/*
	 * add to pending list and start state machine
	 */
	cache_work_item_add_pending_io(bc, wi, 'm',
				       cache_block->bcb_sector,
				       bio->bi_rw);
	cache_handle_cache_hit(bc, wi, cache_block, bio);
	return 1;
}

/*!
 * Handle resource busy case (we have a cache hit, but the cache block
 * is in use). All we can do is queue to deferred for later execution.
 */
int cache_map_workfunc_hit_busy(struct bittern_cache *bc,
				struct work_item *wi,
				struct bio *bio)
{
	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, NULL, bio, NULL,
		 "cache-hit-busy");
	/*
	 * block is busy, we cannot do anything with it
	 */
	if (bio_data_dir(bio) == WRITE)
		atomic_inc(&bc->bc_write_hits_busy);
	else
		atomic_inc(&bc->bc_read_hits_busy);
	/*
	 * will have to reschedule this task again
	 */
	cache_queue_to_deferred(bc, &bc->bc_deferred_wait_busy, wi);
	return 0;
}

/*!
 * Handle miss case (in this case we missed, but an idle invalid block
 * has been reallocated, so we can handle cache miss, i.e. do a cache fill).
 * \todo should rename "_miss_invalid" to "_miss_fill".
 */
int cache_map_workfunc_miss_invalid_idle(struct bittern_cache *bc,
					 struct work_item *wi,
					 struct cache_block *cache_block,
					 struct bio *bio)
{
	/*
	 * if we got a cache block back,
	 * we'd better be not doing a bypass
	 * */
	ASSERT(wi->wi_bypass == 0);
	ASSERT(cache_block->bcb_transition_path == CACHE_TRANSITION_PATH_NONE);
	if (is_work_item_cache_mode_writeback(wi) &&
	    bio_data_dir(bio) == WRITE)
		ASSERT(cache_block->bcb_state == CACHE_VALID_DIRTY_NO_DATA);
	else
		ASSERT(cache_block->bcb_state == CACHE_VALID_CLEAN_NO_DATA);
	/*
	 * inc pending counters
	 */
	cache_update_pending(bc, bio, false);
	/*
	 * add to pending list and start state machine
	 */
	cache_work_item_add_pending_io(bc, wi, 'm',
				       cache_block->bcb_sector,
				       bio->bi_rw);
	/* 0 means no replacement */
	cache_handle_cache_miss(bc, wi, bio, cache_block, 0);
	return 1;
}

/*!
 * Handle complete miss case (not only we have a cache miss, we also
 * do not have a cache block).
 * Do bypass if requested, otherwise defer.
 */
int cache_map_workfunc_miss(struct bittern_cache *bc,
			    struct work_item *wi,
			    struct bio *bio)
{
	if (wi->wi_bypass != 0) {
		/*
		 * inc pending counters
		 */
		cache_update_pending(bc, bio, true);
		/*
		 * queue request
		 */
		cache_work_item_add_pending_io(bc, wi, 'b',
					       bio->bi_iter.
					       bi_sector,
					       bio->bi_rw);
		cache_handle_bypass(bc, wi, bio);
		return 1;
	}
	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, NULL, bio, NULL,
		 "cache-miss-all-blocks-busy: bc_invalid(count=%u, list_empty=%d)",
		 atomic_read(&bc->bc_invalid_entries),
		 list_empty(&bc->bc_invalid_entries_list));
	if (bio_data_dir(bio) == WRITE)
		atomic_inc(&bc->bc_write_misses_busy);
	else
		atomic_inc(&bc->bc_read_misses_busy);
	/*
	 * cannot process now, will have to reschedule this
	 * task again with delay
	 */
	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, NULL, bio, NULL,
		 "blocks-busy-reschedule-to-delayed queue (bc_invalid_entries=%u, list_empty=%d)",
		 atomic_read(&bc->bc_invalid_entries),
		 list_empty(&bc->bc_invalid_entries_list));
	cache_queue_to_deferred(bc, &bc->bc_deferred_wait_page, wi);
	return 0;
}

/*!
 * mark io request as pending and start processing it thru the main state
 * machine. returns 1 if item has been processed, or 0 it if hasn't. in
 * the latter case the item was queued into one of the deferred queues for
 * later processing.
 */
int cache_map_workfunc(struct work_item *wi)
{
	struct bittern_cache *bc;
	struct bio *bio;
	struct cache_block *cache_block;
	int ret;
	int cache_get_flags;
	int do_bypass;

	ASSERT(wi != NULL);
	bc = wi->wi_cache;
	bio = wi->wi_original_bio;
	ASSERT(bio != NULL);
	ASSERT_WORK_ITEM(wi, bc);
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, NULL, bio, NULL, "enter");
	ASSERT_BITTERN_CACHE(bc);

	/*
	 * right now we always allocate a page buffer even for pmem_ram or
	 * nvdimm
	 * */
	ASSERT(wi->wi_cache_data.di_buffer_vmalloc_buffer != NULL);
	ASSERT(PAGE_ALIGNED(wi->wi_cache_data.di_buffer_vmalloc_buffer));
	ASSERT(wi->wi_cache_data.di_buffer_vmalloc_page != NULL);
	ASSERT(wi->wi_cache_data.di_buffer_vmalloc_page ==
	       vmalloc_to_page(wi->wi_cache_data.di_buffer_vmalloc_buffer));

	if ((bio->bi_rw & REQ_FLUSH) && bio->bi_iter.bi_size == 0) {
		BT_TRACE(BT_LEVEL_TRACE1, bc, wi, NULL, bio, NULL,
			 "req-flush-no-data");
		ASSERT(bio_is_pureflush_request(bio));
		cache_work_item_free(bc, wi);
		atomic_inc(&bc->bc_completed_requests);
		/* wakeup possible waiters */
		cache_wakeup_deferred(bc);
		bio_endio(bio, 0);
		return 1;
	}

	if (bio->bi_rw & REQ_DISCARD) {
		BT_TRACE(BT_LEVEL_TRACE1, bc, wi, NULL, bio, NULL,
			 "req-discard");
		ASSERT(bio_is_discard_request(bio));
		cache_work_item_free(bc, wi);
		atomic_inc(&bc->bc_completed_requests);
		/*
		 * wakeup possible waiters
		 */
		cache_wakeup_deferred(bc);
		bio_endio(bio, 0);
		return 1;
	}

	ASSERT(bio_is_data_request(bio));

	/*
	 * This is a fundamental design feature of Bittern.
	 * We depend on DM to split the request such that there
	 * no requests which span multiple blocks.
	 */
	M_ASSERT(bio_is_request_single_cache_block(bio));

	/*
	 * detect sequential access and see if we want to bypass the cache.
	 * for this latter case we rely on the fact that the upper layer will
	 * never send a duplicate request Y for block N [ request(Y,N) ] while
	 * the current request X for the same block is pending [ request(X,N) ].
	 * this is standard block io cache layer behavior, and it should never
	 * be broken.
	 *
	 * there are however possible cases in which this could occur if raw io
	 * is made at the same time. because unix/posix gives no guarantees in
	 * such case, we do not give them either (too much of a hassle to
	 * track non-cache requests in flight).
	 */
	/*
	 * we need to carry the read_bypass status, because we still want to
	 * return a cache hit if we find the element in cache.
	 *
	 * FIXME sequential read will not be detected correctly if we have to
	 * defer the request. this is not a big problem because:
	 * - it's an optimization and not required for correctness
	 * - deferrals very rarely happens
	 */
	do_bypass = seq_bypass_is_sequential(bc, bio);
	ASSERT(do_bypass == 0 || do_bypass == 1);
	wi->wi_bypass = do_bypass;
	wi->wi_ts_started = current_kernel_time_nsec();

	if ((bio->bi_rw & REQ_FLUSH) != 0) {
		BT_TRACE(BT_LEVEL_TRACE2, bc, wi, NULL, bio, NULL,
			 "flush-request");
	}
	BT_TRACE(BT_LEVEL_TRACE2, bc, wi, NULL, bio, NULL, "enter");

	/*
	 * Here we are in the worker function (may or may not be in a thread
	 * context). By default we do want to get a "valid_no_valid" block if we
	 * miss, exception when sequential read/write bypass is set, in
	 * which case we want to bypass the cache and do the IO directly.
	 */
	cache_get_flags = CACHE_FL_HIT;
	if (wi->wi_bypass == 0) {
		cache_get_flags |= CACHE_FL_MISS;
		if (is_work_item_cache_mode_writeback(wi) &&
		    bio_data_dir(bio) == WRITE)
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
		wi->wi_cache_block = cache_block;
		BT_TRACE(BT_LEVEL_TRACE1, bc, wi, cache_block, bio, NULL,
			 "cache-hit-idle");
		ASSERT(cache_block != NULL);
		ASSERT_CACHE_BLOCK(cache_block, bc);
		ASSERT(cache_block->bcb_state == CACHE_VALID_CLEAN ||
		       cache_block->bcb_state == CACHE_VALID_DIRTY);
		ASSERT(cache_block->bcb_transition_path ==
		       CACHE_TRANSITION_PATH_NONE);
		return cache_map_workfunc_hit_idle(bc, wi, cache_block, bio);

	case CACHE_GET_RET_HIT_BUSY:
		/*
		 * Read/write hit.
		 * We found a cache block but it's busy, need to defer.
		 */
		ASSERT(cache_block == NULL);
		return cache_map_workfunc_hit_busy(bc, wi, bio);

	case CACHE_GET_RET_MISS_INVALID_IDLE:
		/*
		 * Read/write miss.
		 * There is no valid cache block, but got an invalid cache
		 * block on which we can start IO.
		 * \todo should set wi->wi_cache_block in here.
		 */
		ASSERT(cache_block != NULL);
		BT_TRACE(BT_LEVEL_TRACE1, bc, wi, cache_block, bio, NULL,
			 "cache-miss-invalid-idle");
		ASSERT_CACHE_BLOCK(cache_block, bc);
		return cache_map_workfunc_miss_invalid_idle(bc,
							    wi,
							    cache_block,
							    bio);

	case CACHE_GET_RET_MISS:
		/*
		 * Read/write miss.
		 * There is no valid cache block, and there are no free cache
		 * blocks. Do bypass if requested, otherwise defer.
		 */
		ASSERT(cache_block == NULL);
		return cache_map_workfunc_miss(bc, wi, bio);

	default:
		printk_err("xid=%llu, dev=%p, bittern_dev=%p, dir=0x%lx, flags=0x%lx, bio=%p, bi_sector=%lu, bi_vcnt=%u, bi_size=%u: unknown ret value=%d\n",
			   wi->wi_io_xid,
			   bc->bc_dev->bdev,
			   bio->bi_bdev,
			   bio_data_dir(bio),
			   bio->bi_flags,
			   bio,
			   bio->bi_iter.bi_sector,
			   bio->bi_vcnt,
			   bio->bi_iter.bi_size,
			   ret);
		M_ASSERT("unexpected value of cache_get()" == NULL);
		return 0;
	}

	M_ASSERT("internal error" == NULL);
	return 0;
}

/*!
 * this function is the main entry point for bittern cache.
 * all user-initiated requests go thru here.
 */
int bittern_cache_map(struct dm_target *ti, struct bio *bio)
{
	struct bittern_cache *bc = ti->private;
	struct work_item *wi;

	ASSERT_BITTERN_CACHE(bc);

	ASSERT((bio->bi_rw & REQ_WRITE_SAME) == 0);

	if ((bio->bi_rw & REQ_DISCARD) != 0) {
		ASSERT(bio_is_discard_request(bio));
		atomic_inc(&bc->bc_discard_requests);
	} else if ((bio->bi_rw & REQ_FLUSH) != 0 && bio->bi_iter.bi_size == 0) {
		ASSERT(bio_is_pureflush_request(bio));
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

	wi = cache_work_item_allocate(bc, NULL, bio,
				      (WI_FLAG_MAP_IO |
				       WI_FLAG_BIO_CLONED |
				       WI_FLAG_XID_NEW),
				      NULL, GFP_ATOMIC);
	ASSERT(wi != NULL);
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(wi->wi_io_xid != 0);
	ASSERT(wi->wi_original_bio == bio);
	ASSERT(wi->wi_cache == bc);

	wi->wi_ts_queued = current_kernel_time_nsec();
	/*
	 * copy cache mode.
	 * cache mode can change during an i/o operation, so we need a copy in
	 * each work item.
	 */
	wi->wi_cache_mode_writeback = bc->bc_cache_mode_writeback;

	/*
	 * defer to queued queue if pending queue is too high,
	 * or if any of the deferred queues are non-empty (so to avoid request
	 * starvation).
	 */
	if (atomic_read(&bc->bc_pending_requests) > bc->bc_max_pending_requests
	    || atomic_read(&bc->bc_deferred_requests) > 0) {
		BT_TRACE(BT_LEVEL_TRACE1, bc, wi, NULL, bio, NULL,
			 "queue-to-deferred (pending=%u, deferred=%u)",
			 atomic_read(&bc->bc_pending_requests),
			 atomic_read(&bc->bc_deferred_requests));
		/*
		 * queue to wait_pending deferred list.
		 * note this is the only case in which we do not bump up the
		 * pending request count.
		 */
		cache_queue_to_deferred(bc, &bc->bc_deferred_wait_page, wi);
		return DM_MAPIO_SUBMITTED;
	} else {
		/*
		 * now we always allocate in order to test the page allocation
		 * mechanism. later we'll only allocate if pmem layer requires
		 * double buffering. avoiding page allocation is only i
		 * beneficial for NVDIMM type memory, so until then there is no
		 * need to be fancy.
		 */
		pagebuf_allocate_dbi_nowait(bc, PGPOOL_MAP, &wi->wi_cache_data);
		if (wi->wi_cache_data.di_buffer_vmalloc_buffer == NULL) {
			/*
			 * allocation failed, queue to queued queue and let
			 * the deferred thread do the allocation
			 */
			BT_TRACE(BT_LEVEL_TRACE1, bc, wi, NULL, bio, NULL,
				 "kmem_alloc_page_nowait failed, queueing to queued queue");
			cache_queue_to_deferred(bc, &bc->bc_deferred_wait_page,
						wi);
			return DM_MAPIO_SUBMITTED;
		}
		/*
		 * we have a buffer, handle immmediately
		 */
		cache_map_workfunc(wi);

		return DM_MAPIO_SUBMITTED;
	}
}

/*!
 * waiter thread for busy deferred queues relies on bc_pending_requests
 * being incremented before we call this.
 */
void cache_wakeup_deferred(struct bittern_cache *bc)
{
	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		 "deferred_requests=%d, defer_wait_busy.curr_count=%d, defer_wait_page.curr_count=%d",
		 atomic_read(&bc->bc_deferred_requests),
		 bc->bc_deferred_wait_busy.bc_defer_curr_count,
		 bc->bc_deferred_wait_page.bc_defer_curr_count);
	if (bc->bc_deferred_wait_busy.bc_defer_curr_count != 0) {
		atomic_inc(&bc->bc_deferred_wait_busy.bc_defer_gennum);
		wake_up_interruptible(&bc->bc_deferred_wait_busy.bc_defer_wait);
	}
	if (bc->bc_deferred_wait_page.bc_defer_curr_count != 0) {
		atomic_inc(&bc->bc_deferred_wait_page.bc_defer_gennum);
		wake_up_interruptible(&bc->bc_deferred_wait_page.bc_defer_wait);
	}
}

/*! queue a request for deferred execution */
void cache_queue_to_deferred(struct bittern_cache *bc,
			     struct deferred_queue *queue,
			     struct work_item *wi)
{
	unsigned long flags;
	int val;

	ASSERT(queue == &bc->bc_deferred_wait_busy ||
	       queue == &bc->bc_deferred_wait_page);
	wi->wi_ts_queue = current_kernel_time_nsec();
	INIT_LIST_HEAD(&wi->wi_deferred_io_list);

	spin_lock_irqsave(&queue->bc_defer_lock, flags);
	list_add_tail(&wi->wi_deferred_io_list, &queue->bc_defer_list);
	atomic_inc(&bc->bc_total_deferred_requests);
	val = atomic_inc_return(&bc->bc_deferred_requests);
	atomic_set_if_higher(&bc->bc_highest_deferred_requests, val);
	queue->bc_defer_curr_count++;
	if (queue->bc_defer_curr_count > queue->bc_defer_max_count)
		queue->bc_defer_max_count = queue->bc_defer_curr_count;
	spin_unlock_irqrestore(&queue->bc_defer_lock, flags);

	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, NULL, NULL, NULL,
		 "%s: deferred_gennum=%d/%d",
		 (queue == &bc->bc_deferred_wait_busy ? "busy" : "page"),
		 queue->bc_defer_curr_gennum,
		 atomic_read(&queue->bc_defer_gennum));

	atomic_inc(&queue->bc_defer_gennum);
	wake_up_interruptible(&queue->bc_defer_wait);
}

struct work_item *cache_dequeue_from_deferred(struct bittern_cache *bc,
					      struct deferred_queue *queue)
{
	unsigned long flags;
	struct work_item *wi = NULL;

	ASSERT(queue == &bc->bc_deferred_wait_busy ||
	       queue == &bc->bc_deferred_wait_page);

	spin_lock_irqsave(&queue->bc_defer_lock, flags);
	if (list_non_empty(&queue->bc_defer_list)) {
		wi = list_first_entry(&queue->bc_defer_list, struct work_item,
				      wi_deferred_io_list);
		ASSERT(wi != NULL);
		list_del_init(&wi->wi_deferred_io_list);
		queue->bc_defer_curr_count--;
		atomic_dec(&bc->bc_deferred_requests);
		cache_timer_add(&queue->bc_defer_timer,
					wi->wi_ts_queue);
		ASSERT_WORK_ITEM(wi, bc);
	}
	ASSERT(atomic_read(&bc->bc_deferred_requests) >= 0);
	ASSERT(queue->bc_defer_curr_count >= 0);
	spin_unlock_irqrestore(&queue->bc_defer_lock, flags);

	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, NULL, NULL, NULL,
		 "%s: deferred_gennum=%d/%d",
		 (queue == &bc->bc_deferred_wait_busy ? "busy" : "page"),
		 queue->bc_defer_curr_gennum,
		 atomic_read(&queue->bc_defer_gennum));
	return wi;
}

typedef bool(*deferred_queue_has_work_f) (struct bittern_cache *bc);

/*!
 * returns true if there is work to do and if there enough resources
 * to queue work from this queue.
 */
bool cache_deferred_busy_has_work(struct bittern_cache *bc)
{
	int cc = bc->bc_deferred_wait_busy.bc_defer_curr_count;
	int can_queue = atomic_read(&bc->bc_pending_requests) <
	    bc->bc_max_pending_requests;
	return cc != 0 && can_queue != 0;
}

/*!
 * returns true if there is work to do and if there enough resources
 * to queue work from this queue.
 */
bool cache_deferred_page_has_work(struct bittern_cache *bc)
{
	int available_entries = atomic_read(&bc->bc_invalid_entries) +
				atomic_read(&bc->bc_valid_entries);
	int cc = bc->bc_deferred_wait_page.bc_defer_curr_count;
	int can_queue = atomic_read(&bc->bc_pending_requests) <
			bc->bc_max_pending_requests;
	bool can_alloc = pagebuf_can_allocate(bc, PGPOOL_BGWRITER);
	return cc != 0 && available_entries != 0 && can_queue != 0 && can_alloc;
}

/*! handle one or more deferred requests on a given queue */
int cache_handle_deferred(struct bittern_cache *bc,
			  struct deferred_queue *queue)
{
	int ret, count;
	struct work_item *wi = NULL;

	ASSERT(queue == &bc->bc_deferred_wait_busy ||
	       queue == &bc->bc_deferred_wait_page);

	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, NULL, NULL, NULL,
		 "enter: %s: deferred_gennum=%d/%d",
		 (queue == &bc->bc_deferred_wait_busy ? "busy" : "page"),
		 queue->bc_defer_curr_gennum,
		 atomic_read(&queue->bc_defer_gennum));

	wi = cache_dequeue_from_deferred(bc, queue);
	if (wi == NULL) {
		queue->bc_defer_no_work_count++;
		return 0;
	}

	ASSERT_WORK_ITEM(wi, bc);
	ASSERT(wi->wi_original_bio != NULL);

	BT_TRACE(BT_LEVEL_TRACE1, bc, wi, NULL, NULL, NULL,
		 "vmalloc_buffer=%p: allocated=%d, max=%d, can_alloc=%d",
		 wi->wi_cache_data.di_buffer_vmalloc_buffer,
		 pagebuf_in_use(bc, PGPOOL_MAP),
		 pagebuf_max_bufs(bc),
		 pagebuf_can_allocate(bc, PGPOOL_MAP));
	if (wi->wi_cache_data.di_buffer_vmalloc_buffer == NULL) {
		/* this should only happen for this queue */
		ASSERT(queue == &bc->bc_deferred_wait_page);
		/*
		 * the thread is woken up given certain conditions,
		 * more specifically for this case, available buffers.
		 * by the time we get here, however, we can potentially
		 * be out of buffers, so we need to check for this and
		 * requeue the request if we cannot allocate.
		 */
		if (pagebuf_can_allocate(bc, PGPOOL_MAP) == false) {
			cache_queue_to_deferred(bc, queue, wi);
			queue->bc_defer_requeue_count++;
			queue->bc_defer_nomem_count++;
			BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
				 "nomem: wait_%s: wi=%p, curr_c=%u, max_c=%u",
				 (queue ==
				  &bc->bc_deferred_wait_busy ? "busy" : "page"),
				 wi, queue->bc_defer_curr_count,
				 queue->bc_defer_max_count);
			return 0;
		}
		pagebuf_allocate_dbi_wait(bc, PGPOOL_MAP, &wi->wi_cache_data);
		ASSERT(wi->wi_cache_data.di_buffer_vmalloc_buffer != NULL);
	}

	ASSERT(wi->wi_cache_data.di_buffer_vmalloc_buffer != NULL);
	ASSERT(wi->wi_cache_data.di_buffer_vmalloc_page != NULL);
	/*
	 * now try resubmit the request. it's possible that it will be requeued
	 * again. in the latter case, make sure we don't wake up ourselves
	 * again.
	 */
	ret = cache_map_workfunc(wi);

	if (ret == 0) {
		queue->bc_defer_requeue_count++;
		count = 0;
	} else {
		queue->bc_defer_work_count++;
		count = 1;
	}

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		 "wait_%s: wi=%p, curr_c=%u, max_c=%u, ret=%d, count=%d",
		 (queue == &bc->bc_deferred_wait_busy ? "busy" : "pp"),
		 wi,
		 queue->bc_defer_curr_count,
		 queue->bc_defer_max_count, ret, count);

	return count;
}

/*! deferred io handler */
void cache_deferred_io_handler(struct bittern_cache *bc,
			       struct deferred_queue *queue,
			       deferred_queue_has_work_f queue_has_work)
{
	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	/*
	 * for as long as we have queued deferred requests and
	 * we can requeue, then we need to do this
	 */
	while ((*queue_has_work) (bc)) {
		int count;

		ASSERT(bc != NULL);
		ASSERT_BITTERN_CACHE(bc);

		count = cache_handle_deferred(bc, queue);

		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
			 "%d: completed=%u, pending=%u, deferred=%u",
			 count,
			 atomic_read(&bc->bc_completed_requests),
			 atomic_read(&bc->bc_pending_requests),
			 atomic_read(&bc->bc_deferred_requests));
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
			 "%d: io_curr_count=%u",
			 count, queue->bc_defer_curr_count);

		if (count == 0)
			break;
	}
	queue->bc_defer_loop_count++;
}

/*!
 * kernel thread to handle
 * 1. i/o requests which are waiting for busy blocks (wait_busy)
 * 2. i/o requests which are waiting for free cache blocks (wait_free)
 *
 * see comment in bittern_cache.h for theory of operation of deferred kthread
 *
 * TODO: this function can be easily made common with the one below
 */
int cache_deferred_busy_kthread(void *__bc)
{
	struct bittern_cache *bc = (struct bittern_cache *)__bc;
	struct deferred_queue *queue;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);

	set_user_nice(current, CACHE_DEFERRED_IO_THREAD_NICE);
	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL,
		 "enter, nice=%d", CACHE_DEFERRED_IO_THREAD_NICE);

	queue = &bc->bc_deferred_wait_busy;
	while (!kthread_should_stop()) {
		ASSERT(bc != NULL);
		ASSERT_BITTERN_CACHE(bc);

		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
			 "kthread_should_stop()=%d", kthread_should_stop());

		/*!
		 * we get woken up every time an io request or a writeback
		 * completes or a request is queued.
		 *
		 * we use completed requests here in liueu of the generation
		 * number, the reason being we want to be awakened only if at
		 * least one request completed from the last time we ran (that
		 * is, we want to be waken up only if we know we can do work).
		 *
		 */
		/*! \todo
		 * should we have another field in
		 * deferred_queue defer_queue struct to store
		 *bc->bc_completed_requests? it'd really be more of a "looking
		 * clean" thing than of any practical reason.
		 *
		 */
		wait_event_interruptible(queue->bc_defer_wait,
					 queue->bc_defer_curr_gennum !=
					 atomic_read(&bc->bc_completed_requests)
					 || kthread_should_stop());
		if (signal_pending(current))
			flush_signals(current);

		queue->bc_defer_curr_gennum =
		    atomic_read(&bc->bc_completed_requests);

		cache_deferred_io_handler(bc, queue,
					  cache_deferred_busy_has_work);

		schedule();
	}

	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL, "exit");
	bc->bc_deferred_wait_busy.bc_defer_task = NULL;

	/*
	 * when we exit here there'd be no deferred requests anymore.
	 * this relies on the fact that deferred requests can only be
	 * pending if the device is still open.
	 */
	M_ASSERT(atomic_read(&bc->bc_deferred_requests) == 0);
	return 0;
}

/*!
 * kernel thread to handle
 * 3. i/o requests which are waiting for a free buffer page (wait_page)
 * 4. i/o requests which are held for the pending queue to become un-saturated
 *    (wait_pending)
 *
 * see comment in bittern_cache.h for theory of operation of deferred kthread
 */
int cache_deferred_page_kthread(void *__bc)
{
	struct bittern_cache *bc = (struct bittern_cache *)__bc;
	struct deferred_queue *queue;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);

	set_user_nice(current, CACHE_DEFERRED_IO_THREAD_NICE);
	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL,
		 "enter, nice=%d", CACHE_DEFERRED_IO_THREAD_NICE);

	queue = &bc->bc_deferred_wait_page;
	while (!kthread_should_stop()) {
		ASSERT(bc != NULL);
		ASSERT_BITTERN_CACHE(bc);

		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
			 "kthread_should_stop()=%d", kthread_should_stop());

		/*
		 * we get woken up every time an io request or a writeback
		 * completes or a request is queued.
		 */
		wait_event_interruptible(queue->bc_defer_wait,
					 queue->bc_defer_curr_gennum !=
					 atomic_read(&queue->bc_defer_gennum)
					 || kthread_should_stop());
		if (signal_pending(current))
			flush_signals(current);

		queue->bc_defer_curr_gennum =
			atomic_read(&queue->bc_defer_gennum);

		cache_deferred_io_handler(bc, queue,
					  cache_deferred_page_has_work);

		schedule();
	}

	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL, "exit");
	bc->bc_deferred_wait_page.bc_defer_task = NULL;

	/*
	 * when we exit here there'd be no deferred requests anymore.
	 * this relies on the fact that deferred requests can only be
	 * pending if the device is still open.
	 */
	M_ASSERT(atomic_read(&bc->bc_deferred_requests) == 0);
	return 0;
}
