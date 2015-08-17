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

/* !\file */

#include "bittern_cache.h"
#include "bittern_cache_pmem_api_internal.h"

void pmem_info_initialize(struct bittern_cache *bc)
{
	struct pmem_info *ps = &bc->bc_papi.papi_stats;

	printk_info("%p: enter\n", bc);
	ASSERT(bc != NULL);
	ASSERT(bc->bc_magic1 == BC_MAGIC1);
	ps->restore_header_valid = 0;
	ps->restore_header0_valid = 0;
	ps->restore_header1_valid = 0;
	ps->restore_corrupt_metadata_blocks = 0;
	ps->restore_valid_clean_metadata_blocks = 0;
	ps->restore_valid_dirty_metadata_blocks = 0;
	ps->restore_invalid_metadata_blocks = 0;
	ps->restore_pending_metadata_blocks = 0;
	ps->restore_invalid_data_blocks = 0;
	ps->restore_valid_clean_data_blocks = 0;
	ps->restore_valid_dirty_data_blocks = 0;
	ps->restore_hash_corrupt_metadata_blocks = 0;
	ps->restore_hash_corrupt_data_blocks = 0;
	atomic_set(&ps->metadata_read_async_count, 0);
	atomic_set(&ps->metadata_write_async_count, 0);
	atomic_set(&ps->data_get_put_page_pending_count, 0);
	atomic_set(&ps->data_get_page_read_count, 0);
	atomic_set(&ps->data_put_page_read_count, 0);
	atomic_set(&ps->data_get_page_write_count, 0);
	atomic_set(&ps->data_put_page_write_count, 0);
	atomic_set(&ps->data_put_page_write_metadata_count, 0);
	atomic_set(&ps->data_convert_page_read_to_write_count, 0);
	atomic_set(&ps->data_clone_read_page_to_write_page_count, 0);
	cache_timer_init(&ps->metadata_read_async_timer);
	cache_timer_init(&ps->metadata_write_async_timer);
	cache_timer_init(&ps->data_get_page_read_timer);
	cache_timer_init(&ps->data_get_page_read_async_timer);
	cache_timer_init(&ps->data_get_page_write_timer);
	cache_timer_init(&ps->data_put_page_read_timer);
	cache_timer_init(&ps->data_put_page_write_async_timer);
	cache_timer_init(&ps->data_put_page_write_async_metadata_timer);
	cache_timer_init(&ps->data_put_page_write_timer);
	atomic_set(&ps->pmem_read_not4k_count, 0);
	atomic_set(&ps->pmem_read_not4k_pending, 0);
	atomic_set(&ps->pmem_write_not4k_count, 0);
	atomic_set(&ps->pmem_write_not4k_pending, 0);
	atomic_set(&ps->pmem_read_4k_count, 0);
	atomic_set(&ps->pmem_read_4k_pending, 0);
	atomic_set(&ps->pmem_write_4k_count, 0);
	atomic_set(&ps->pmem_write_4k_pending, 0);
	cache_timer_init(&ps->pmem_read_not4k_timer);
	cache_timer_init(&ps->pmem_write_not4k_timer);
	cache_timer_init(&ps->pmem_read_4k_timer);
	cache_timer_init(&ps->pmem_write_4k_timer);
	atomic_set(&ps->pmem_make_req_wq_count, 0);
	cache_timer_init(&ps->pmem_make_req_wq_timer);
	printk_info("%p: done\n", bc);
}

void pmem_info_deinitialize(struct bittern_cache *bc)
{
	struct pmem_info *ps = &bc->bc_papi.papi_stats;

	printk_info("%p: enter\n", bc);
	ASSERT(bc != NULL);
	ASSERT(bc->bc_magic1 == BC_MAGIC1);
	printk_info("%p: data_get_put_page_pending_count=%u\n",
		    bc, atomic_read(&ps->data_get_put_page_pending_count));
	ASSERT(atomic_read(&ps->data_get_put_page_pending_count) == 0);
	printk_info("%p: done\n", bc);
}

/*! initializes empty pmem context */
void pmem_context_initialize(struct pmem_context *ctx)
{
	memset(ctx, 0, sizeof(struct pmem_context));
	ctx->magic1 = PMEM_CONTEXT_MAGIC1;
	ctx->magic2 = PMEM_CONTEXT_MAGIC2;
	ctx->dbi.di_buffer_vmalloc_buffer = NULL;
	ctx->dbi.di_buffer_vmalloc_page = NULL;
	ctx->dbi.di_buffer_slab = NULL;
	ctx->dbi.di_buffer = NULL;
	ctx->dbi.di_page = NULL;
	ctx->dbi.di_flags = 0x0;
	atomic_set(&ctx->dbi.di_busy, 0);
}

/*!
 * Sets up resources for pmem context.
 * Later this will be split into implementation specific code,
 * one for pmem_block, one for pmem_mem.
 * The pmem_block implementation will allocate a double buffer,
 * the pmem_mem implementation will call DAX to retrieve the virtual
 * addresses for data and metadata for "cache_block" and "cloned_cache_block".
 */
int pmem_context_setup(struct bittern_cache *bc,
		       struct kmem_cache *kmem_slab,
		       struct cache_block *cache_block,
		       struct cache_block *cloned_cache_block,
		       struct pmem_context *ctx)
{
	struct data_buffer_info *dbi;

	ASSERT_BITTERN_CACHE(bc);
	ASSERT(kmem_slab == bc->bc_kmem_map ||
	       kmem_slab == bc->bc_kmem_threads);
	ASSERT(ctx != NULL);
	M_ASSERT(ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	dbi = &ctx->dbi;
	/*
	 * this code copied from pagebuf_allocate_dbi()
	 * in bittern_cache_main.h
	 */
	ASSERT(dbi->di_buffer_vmalloc_buffer == NULL);
	ASSERT(dbi->di_buffer_vmalloc_page == NULL);
	ASSERT(dbi->di_buffer_slab == NULL);
	ASSERT(dbi->di_buffer == NULL);
	ASSERT(dbi->di_page == NULL);
	ASSERT(dbi->di_flags == 0x0);
	ASSERT(atomic_read(&dbi->di_busy) == 0);

	dbi->di_buffer_vmalloc_buffer = kmem_cache_alloc(kmem_slab, GFP_NOIO);
	/*TODO_ADD_ERROR_INJECTION*/
	if (dbi->di_buffer_vmalloc_buffer == NULL) {
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, cache_block, NULL, NULL,
			     "kmem_cache_alloc kmem_slab failed");
		printk_err("%s: kmem_cache_alloc kmem_slab failed\n",
			   bc->bc_name);
		return -ENOMEM;
	}

	ASSERT(PAGE_ALIGNED(dbi->di_buffer_vmalloc_buffer));
	dbi->di_buffer_vmalloc_page =
				virtual_to_page(dbi->di_buffer_vmalloc_buffer);
	ASSERT(dbi->di_buffer_vmalloc_page != NULL);
	dbi->di_buffer_slab = kmem_slab;

	return 0;
}

/*! destroys pmem_context resources */
void pmem_context_destroy(struct bittern_cache *bc,
			  struct pmem_context *ctx)
{
	struct data_buffer_info *dbi;

	ASSERT_BITTERN_CACHE(bc);
	ASSERT(ctx != NULL);
	M_ASSERT(ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	dbi = &ctx->dbi;
	/*
	 * this code copied from pagebuf_free_dbi()
	 * in bittern_cache_main.h.
	 * we also add conditional free for the cases we didn't allocate
	 * the buffer yet.
	 */
	ASSERT(dbi->di_buffer == NULL);
	ASSERT(dbi->di_page == NULL);
	ASSERT(dbi->di_flags == 0x0);
	ASSERT(atomic_read(&dbi->di_busy) == 0);
	if (dbi->di_buffer_vmalloc_buffer != NULL) {
		ASSERT(dbi->di_buffer_vmalloc_page != NULL);
		ASSERT(dbi->di_buffer_slab != NULL);
		ASSERT(dbi->di_buffer_slab == bc->bc_kmem_map ||
		       dbi->di_buffer_slab == bc->bc_kmem_threads);
		kmem_cache_free(dbi->di_buffer_slab,
				dbi->di_buffer_vmalloc_buffer);
		dbi->di_buffer_vmalloc_buffer = NULL;
		dbi->di_buffer_vmalloc_page = NULL;
		dbi->di_buffer_slab = NULL;
	}
}


void pmem_initialize_pmem_header_sizes(struct bittern_cache *bc,
				       uint64_t device_cache_size_bytes)
{
	struct pmem_api *pa = &bc->bc_papi;
	struct pmem_header *pm = &pa->papi_hdr;
	size_t data_metadata_size;
	size_t cache_blocks;

	ASSERT(sizeof(struct pmem_header) <= PAGE_SIZE);
	ASSERT(device_cache_size_bytes > 0);
	ASSERT(pmem_cache_layout(bc) == CACHE_LAYOUT_INTERLEAVED ||
	       pmem_cache_layout(bc) == CACHE_LAYOUT_SEQUENTIAL);

	printk_info("device_cache_size_bytes=%llu (0x%llx) (%llu mbytes)\n",
		    device_cache_size_bytes,
		    device_cache_size_bytes,
		    (device_cache_size_bytes / 1024 / 1024));
	printk_info("sizeof (struct pmem_header) = %lu\n",
		    sizeof(struct pmem_header));
	printk_info("sizeof (struct pmem_block_metadata) = %lu\n",
		    sizeof(struct pmem_block_metadata));

	/*
	 * The code which calculates the number of cache blocks has an
	 * implicit assumption of having the cache device size being
	 * a multiple of PAGE_SIZE.
	 *
	 * Rather modifying the code to deal with this, we just round down
	 * the cache size to the nearest NAND flash erase block just to be nice
	 * to the hardware.
	 */
	data_metadata_size = round_down(device_cache_size_bytes,
					CACHE_NAND_FLASH_ERASE_BLOCK_SIZE);
	printk_info("data_metadata_size = %lu (rounded down)\n",
		    data_metadata_size);
	M_ASSERT(data_metadata_size > 0);

	data_metadata_size -= CACHE_MEM_FIRST_OFFSET_BYTES;
	printk_info("data_metadata_size = %lu (after header subtract)\n",
		    data_metadata_size);
	M_ASSERT(data_metadata_size > 0);

	pm->lm_cache_block_size = PAGE_SIZE;
	pm->lm_header_size_bytes = sizeof(struct pmem_header);
	pm->lm_first_offset_bytes = CACHE_MEM_FIRST_OFFSET_BYTES;

	if (pmem_cache_layout(bc) == CACHE_LAYOUT_SEQUENTIAL) {
		pm->lm_cache_layout = CACHE_LAYOUT_SEQUENTIAL;
		/*
		 * +---------------------------------------------+
		 * | hdr0 hdr1   metadata_blocks    data_blocks  |
		 * +---------------------------------------------+
		 */
		if (pmem_page_size_transfer_only(bc))
			pm->lm_mcb_size_bytes = PAGE_SIZE;
		else
			pm->lm_mcb_size_bytes =
					sizeof(struct pmem_block_metadata);
		printk_info("pm->lm_mcb_size_bytes = %llu\n",
			    pm->lm_mcb_size_bytes);
		/*
		 * This calculation will break if the device size is not
		 * a multiple of PAGE_SIZE in the case in which the metadata
		 * size is not PAGE_SIZE.
		 * The code above truncates the cache size to megabyte boundary,
		 * there is no need for truncation here.
		 */
		cache_blocks = data_metadata_size /
			       ((uint64_t)PAGE_SIZE + pm->lm_mcb_size_bytes);
		pm->lm_cache_blocks = cache_blocks;
		printk_info("pm->lm_cache_blocks = %llu\n", pm->lm_cache_blocks);
		pm->lm_first_offset_bytes = CACHE_MEM_FIRST_OFFSET_BYTES;
		pm->lm_first_data_block_offset_bytes =
					pm->lm_first_offset_bytes +
					(cache_blocks * pm->lm_mcb_size_bytes);
		pm->lm_first_data_block_offset_bytes =
			round_up(pm->lm_first_data_block_offset_bytes, (uint64_t)PAGE_SIZE);
		ASSERT(pm->lm_first_offset_bytes <
		       pm->lm_first_data_block_offset_bytes);
		pm->lm_cache_size_bytes = pm->lm_first_data_block_offset_bytes;
		pm->lm_cache_size_bytes += (cache_blocks * (uint64_t)PAGE_SIZE);
		printk_info("pm->lm_cache_size_bytes = %llu\n",
			    pm->lm_cache_size_bytes);
	} else {
		pm->lm_cache_layout = CACHE_LAYOUT_INTERLEAVED;
		/*
		 * +---------------------------------------------+
		 * | hdr0 hdr1   (data_metadata_pair)_blocks     |
		 * +---------------------------------------------+
		 */
		/* 'I' layout only makes sense for page_size transfers only */
		ASSERT(pmem_page_size_transfer_only(bc));
		pm->lm_mcb_size_bytes = PAGE_SIZE;
		printk_info("pm->lm_mcb_size_bytes = %llu\n",
			    pm->lm_mcb_size_bytes);
		cache_blocks = data_metadata_size / ((uint64_t)PAGE_SIZE * 2);
		pm->lm_cache_blocks = cache_blocks;
		printk_info("pm->lm_cache_blocks = %llu\n",
			    pm->lm_cache_blocks);
		pm->lm_first_offset_bytes = CACHE_MEM_FIRST_OFFSET_BYTES;
		pm->lm_first_data_block_offset_bytes =
						CACHE_MEM_FIRST_OFFSET_BYTES;
		pm->lm_cache_size_bytes = pm->lm_first_offset_bytes;
		pm->lm_cache_size_bytes += cache_blocks *
					   ((uint64_t)PAGE_SIZE * 2);
		printk_info("pm->lm_cache_size_bytes = %llu\n",
			    pm->lm_cache_size_bytes);
	}
	printk_info("cache_layout('%c'): cache_blocks = %lu\n",
		    pm->lm_cache_layout,
		    cache_blocks);
	ASSERT(cache_blocks > 0);
	ASSERT(pm->lm_first_offset_bytes == CACHE_MEM_FIRST_OFFSET_BYTES);
	ASSERT(pm->lm_first_offset_bytes <=
	       pm->lm_first_data_block_offset_bytes);
	ASSERT(pm->lm_first_data_block_offset_bytes < pm->lm_cache_size_bytes);
	ASSERT(pm->lm_mcb_size_bytes == sizeof(struct pmem_block_metadata) ||
		pm->lm_mcb_size_bytes == PAGE_SIZE);
	printk_info("pm->lm_cache_size_bytes = %llu (%llu mbytes)\n",
		    pm->lm_cache_size_bytes,
		    pm->lm_cache_size_bytes / (1024ULL * 1024ULL));
	printk_info("device_cache_size_bytes = %llu (%llu mbytes)\n",
		    device_cache_size_bytes,
		    device_cache_size_bytes / (1024ULL * 1024ULL));
	M_ASSERT(pm->lm_cache_size_bytes <= device_cache_size_bytes);
}

/*!
 * This function is used to ASSERT the integrity of the functions
 * which compute the metadata and data offsets. For each given
 * block id [1 .. N], there is corresponding correct metadata
 * offset 'm' and a data offset 'd'. The offets need to be distinct
 * (checking that is left to eyeballs), and always contained within
 * the cache size, something which we assert on here.
 * The called functions have more elaborate checking, so just by
 * calling them we test a lot of logic.
 */
void __pmem_assert_offsets(struct bittern_cache *bc)
{
	struct pmem_api *pa = &bc->bc_papi;
	struct pmem_header *pm = &pa->papi_hdr;
	off_t m, d;
	uint64_t cache_blocks = pm->lm_cache_blocks;

	m = __cache_block_id_2_metadata_pmem_offset(bc, 1);
	d = __cache_block_id_2_data_pmem_offset(bc, 1);
	printk_info("cache_block #1 m=%lu(%lu), d=%lu(%lu)\n",
			m, m / PAGE_SIZE, d, d / PAGE_SIZE);
	M_ASSERT(m <= pm->lm_cache_size_bytes);
	M_ASSERT(d <= pm->lm_cache_size_bytes);

	m = __cache_block_id_2_metadata_pmem_offset(bc, 2);
	d = __cache_block_id_2_data_pmem_offset(bc, 2);
	printk_info("cache_block #2 m=%lu(%lu), d=%lu(%lu)\n",
			m, m / PAGE_SIZE, d, d / PAGE_SIZE);
	M_ASSERT(m <= pm->lm_cache_size_bytes);
	M_ASSERT(d <= pm->lm_cache_size_bytes);

	m = __cache_block_id_2_metadata_pmem_offset(bc, cache_blocks / 2);
	d = __cache_block_id_2_data_pmem_offset(bc, cache_blocks / 2);
	printk_info("cache_block #cache_blocks/2 m=%lu(%lu), d=%lu(%lu)\n",
			m, m / PAGE_SIZE, d, d / PAGE_SIZE);
	M_ASSERT(m <= pm->lm_cache_size_bytes);
	M_ASSERT(d <= pm->lm_cache_size_bytes);

	m = __cache_block_id_2_metadata_pmem_offset(bc, cache_blocks / 2 + 1);
	d = __cache_block_id_2_data_pmem_offset(bc, cache_blocks / 2 + 1);
	printk_info("cache_block #cache_blocks/2+1 m=%lu(%lu), d=%lu(%lu)\n",
			m, m / PAGE_SIZE, d, d / PAGE_SIZE);
	M_ASSERT(m <= pm->lm_cache_size_bytes);
	M_ASSERT(d <= pm->lm_cache_size_bytes);

	m = __cache_block_id_2_metadata_pmem_offset(bc, cache_blocks - 100);
	d = __cache_block_id_2_data_pmem_offset(bc, cache_blocks - 100);
	printk_info("cache_block #cache_blocks-100 m=%lu(%lu), d=%lu(%lu)\n",
			m, m / PAGE_SIZE, d, d / PAGE_SIZE);
	M_ASSERT(m <= pm->lm_cache_size_bytes);
	M_ASSERT(d <= pm->lm_cache_size_bytes);

	m = __cache_block_id_2_metadata_pmem_offset(bc, cache_blocks - 10);
	d = __cache_block_id_2_data_pmem_offset(bc, cache_blocks - 10);
	printk_info("cache_block #cache_blocks-10 m=%lu(%lu), d=%lu(%lu)\n",
			m, m / PAGE_SIZE, d, d / PAGE_SIZE);
	M_ASSERT(m <= pm->lm_cache_size_bytes);
	M_ASSERT(d <= pm->lm_cache_size_bytes);

	m = __cache_block_id_2_metadata_pmem_offset(bc, cache_blocks - 2);
	d = __cache_block_id_2_data_pmem_offset(bc, cache_blocks - 2);
	printk_info("cache_block #cache_blocks-2 m=%lu(%lu), d=%lu(%lu)\n",
			m, m / PAGE_SIZE, d, d / PAGE_SIZE);
	M_ASSERT(m <= pm->lm_cache_size_bytes);
	M_ASSERT(d <= pm->lm_cache_size_bytes);

	m = __cache_block_id_2_metadata_pmem_offset(bc, cache_blocks - 1);
	d = __cache_block_id_2_data_pmem_offset(bc, cache_blocks - 1);
	printk_info("cache_block #cache_blocks-1 m=%lu(%lu), d=%lu(%lu)\n",
			m, m / PAGE_SIZE, d, d / PAGE_SIZE);
	M_ASSERT(m <= pm->lm_cache_size_bytes);
	M_ASSERT(d <= pm->lm_cache_size_bytes);

	m = __cache_block_id_2_metadata_pmem_offset(bc, cache_blocks);
	d = __cache_block_id_2_data_pmem_offset(bc, cache_blocks);
	printk_info("cache_block #cache_blocks m=%lu(%lu), d=%lu(%lu)\n",
			m, m / PAGE_SIZE, d, d / PAGE_SIZE);
	M_ASSERT(m <= pm->lm_cache_size_bytes);
	M_ASSERT(d <= pm->lm_cache_size_bytes);
}

int __pmem_header_restore(struct bittern_cache *bc,
			  int header_block_number,
			  uint64_t *out_xid)
{
	uint32_t header_block_offset_bytes;
	uint128_t computed_hash;
	int ret;
	struct pmem_api *pa = &bc->bc_papi;
	struct pmem_header *pm = &pa->papi_hdr;

	M_ASSERT(bc != NULL);
	M_ASSERT(sizeof(struct pmem_header) == PAGE_SIZE);
	M_ASSERT(header_block_number == 0 || header_block_number == 1);
	M_ASSERT(bc != NULL);
	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);
	ASSERT(pmem_cache_layout(bc) == CACHE_LAYOUT_INTERLEAVED ||
	       pmem_cache_layout(bc) == CACHE_LAYOUT_SEQUENTIAL);

	header_block_offset_bytes = header_block_number == 0 ?
	    CACHE_MEM_HEADER_0_OFFSET_BYTES :
	    CACHE_MEM_HEADER_1_OFFSET_BYTES;
	printk_info("[%d]: header_block_offset_bytes=%u\n",
		    header_block_number, header_block_offset_bytes);

	/*
	 * load requested block
	 */
	M_ASSERT(sizeof(struct pmem_header) <= PAGE_SIZE);
	ret = pmem_read_sync(bc,
			     header_block_offset_bytes,
			     pm,
			     sizeof(struct pmem_header));
	/*TODO_ADD_ERROR_INJECTION*/
	if (ret != 0) {
		ASSERT(ret < 0);
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "pmem_read_sync failed, ret=%d",
			     ret);
		printk_err("%s: pmem_read_sync failed, ret=%d\n",
			   bc->bc_name,
			   ret);
		return ret;
	}

	/*TODO_ADD_ERROR_INJECTION*/
	if (pm->lm_magic != LM_MAGIC) {
		printk_err("[%d]: magic number invalid (0x%x/0x%x)\n",
			   header_block_number,
			   pm->lm_magic,
			   LM_MAGIC);
		return -EBADMSG;
	}
	/*TODO_ADD_ERROR_INJECTION*/
	if (pm->lm_version != LM_VERSION) {
		printk_err("[%d]: error: version number is incorrect %d/%d\n",
			   header_block_number, pm->lm_version, LM_VERSION);
		return -EBADMSG;
	}

	computed_hash = murmurhash3_128(pm, PMEM_HEADER_HASHING_SIZE);
	/*TODO_ADD_ERROR_INJECTION*/
	if (uint128_ne(computed_hash, pm->lm_hash)) {
		printk_err("[%d]: hash mismatch: stored_hash=" UINT128_FMT ", computed_hash" UINT128_FMT "\n",
			    header_block_number,
			    UINT128_ARG(pm->lm_hash),
			    UINT128_ARG(computed_hash));
		return -EBADMSG;
	}
	printk_info("[%d]: stored_hash=" UINT128_FMT ", computed_hash" UINT128_FMT "\n",
		    header_block_number,
		    UINT128_ARG(pm->lm_hash),
		    UINT128_ARG(computed_hash));

	printk_info("[%d]: restore: xid_first=%llu, xid_current=%llu: %llu\n",
		    header_block_number,
		    pm->lm_xid_first,
		    pm->lm_xid_current, cache_xid_get(bc));

	*out_xid = pm->lm_xid_current;

	return 0;
}

int pmem_header_restore(struct bittern_cache *bc)
{
	uint64_t hdr_0_xid, hdr_1_xid;
	int ret;
	struct pmem_api *pa = &bc->bc_papi;
	struct pmem_header *pm = &pa->papi_hdr;

	ASSERT(bc != NULL);
	ASSERT(sizeof(struct pmem_header) == PAGE_SIZE);
	ASSERT(bc != NULL);
	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);
	ASSERT(pmem_cache_layout(bc) == CACHE_LAYOUT_INTERLEAVED ||
	       pmem_cache_layout(bc) == CACHE_LAYOUT_SEQUENTIAL);

	/*
	 * try restore from header #0
	 */
	ret = __pmem_header_restore(bc, 0, &hdr_0_xid);
	printk_info("[0]: ret=%d, hdr_0_xid=%llu\n", ret, hdr_0_xid);
	if (ret == 0)
		pa->papi_stats.restore_header0_valid = 1;

	/*
	 * try restore from header #1
	 */
	ret = __pmem_header_restore(bc, 1, &hdr_1_xid);
	printk_info("[1]: ret=%d, hdr_1_xid=%llu\n", ret, hdr_1_xid);
	if (ret == 0)
		pa->papi_stats.restore_header1_valid = 1;

	/*
	 * use the header with the highest xid
	 */
	if (pa->papi_stats.restore_header0_valid == 0 &&
	    pa->papi_stats.restore_header1_valid == 0) {
		printk_err("error: both headers invalid, ret=%d\n", ret);
		M_ASSERT(ret < 0);
		return ret;
	}

	printk_info("hdr_0_xid=%llu\n", hdr_0_xid);
	printk_info("hdr_1_xid=%llu\n", hdr_1_xid);

	if (pa->papi_stats.restore_header1_valid == 0) {
		/* only header0 valid */
		printk_info("[0/1]: using hdr_0_xid %llu\n", hdr_0_xid);
		ret = __pmem_header_restore(bc, 0, &hdr_0_xid);
		printk_info("[0/1]: using hdr_0_xid %llu\n", hdr_0_xid);
		cache_xid_set(bc, hdr_0_xid + 1);
	} else if (pa->papi_stats.restore_header0_valid == 0) {
		/* only header0 valid */
		printk_info("[1/0]: using hdr_1_xid %llu\n", hdr_1_xid);
		ret = __pmem_header_restore(bc, 1, &hdr_1_xid);
		printk_info("[1/0]: using hdr_1_xid %llu\n", hdr_1_xid);
		cache_xid_set(bc, hdr_1_xid + 1);
	} else if (hdr_0_xid > hdr_1_xid) {
		/* both headers valid, use header0 as it has highest xid */
		printk_info("[1/1]: using hdr_0_xid=%llu\n", hdr_0_xid);
		ret = __pmem_header_restore(bc, 0, &hdr_0_xid);
		printk_info("[1/1]: using hdr_0_xid=%llu\n", hdr_0_xid);
		cache_xid_set(bc, hdr_0_xid + 1);
	} else {
		/* both headers valid, use header1 as it's highest or equal */
		printk_info("[1/1]: using hdr_1_xid=%llu\n", hdr_1_xid);
		ret = __pmem_header_restore(bc, 1, &hdr_1_xid);
		printk_info("[1/1]: using hdr_1_xid=%llu\n", hdr_1_xid);
		cache_xid_set(bc, hdr_1_xid + 1);
	}

	/*TODO_ADD_ERROR_INJECTION*/
	if (ret != 0) {
		printk_err("error: header re-read failed, ret=%d\n", ret);
		return ret;
	}
	M_ASSERT(pa->papi_stats.restore_header0_valid == 1 ||
		pa->papi_stats.restore_header1_valid == 1);

	cache_xid_inc(bc);

	printk_info("bc->bc_xid=%lu\n", atomic64_read(&bc->bc_xid));
	printk_info("pm->lm_cache_layout='%c'(0x%x)\n",
		    pm->lm_cache_layout,
		    pm->lm_cache_layout);
	printk_info("pm->lm_cache_block_size=%llu\n", pm->lm_cache_block_size);
	printk_info("pm->lm_xid_current=%llu\n", pm->lm_xid_current);

	printk_info("bc->bc_name=%s\n", bc->bc_name);
	printk_info("bc->bc_cache_device_name=%s\n", bc->bc_cache_device_name);
	printk_info("bc->bc_cached_device_name=%s\n",
		    bc->bc_cached_device_name);
	printk_info("pm->lm_name=%s\n", pm->lm_name);
	printk_info("pm->lm_uuid=%pUb\n", pm->lm_uuid);
	printk_info("pm->lm_device_name=%s\n", pm->lm_device_name);
	printk_info("pm->lm_device_uuid=%pUb\n", pm->lm_device_uuid);
	printk_info("pm->lm_cache_size_bytes=%llu\n", pm->lm_cache_size_bytes);
	printk_info("pm->lm_mcb_size_bytes=%llu\n", pm->lm_mcb_size_bytes);

	/*TODO_ADD_ERROR_INJECTION*/
	if (pm->lm_header_size_bytes != sizeof(struct pmem_header)) {
		printk_err("lm_header_size mismatch %u/%lu\n",
			   pm->lm_header_size_bytes,
			   sizeof(struct pmem_header));
		return -EBADMSG;
	}

	if (pm->lm_cache_block_size != PAGE_SIZE) {
		printk_err("lm_header_cache_block_size mismatch %llu/%lu\n",
			   pm->lm_cache_block_size,
			   PAGE_SIZE);
		return -EBADMSG;
	}

	if (pm->lm_cache_layout != pmem_cache_layout(bc)) {
		printk_err("lm_cache_layout mismatch 0x%x/0x%x\n",
			   pm->lm_cache_layout,
			   pmem_cache_layout(bc));
		return -EBADMSG;
	}

	if (pm->lm_mcb_size_bytes != sizeof(struct pmem_block_metadata) &&
	    pm->lm_mcb_size_bytes != PAGE_SIZE) {
		printk_err("lm_mcb_size mismatch %llu:%lu/%lu\n",
			   pm->lm_mcb_size_bytes,
			   sizeof(struct pmem_header), PAGE_SIZE);
		return -EBADMSG;
	}

	if (pmem_page_size_transfer_only(bc)) {
		if (pm->lm_mcb_size_bytes != PAGE_SIZE) {
			printk_err("lm_mcb_size is %llu, provider only hass PAGE_SIZE transfers\n",
				   pm->lm_mcb_size_bytes);
			return -EINVAL;
		}
	} else {
		if (pm->lm_mcb_size_bytes !=
		    sizeof(struct pmem_block_metadata)) {
			printk_err("lm_mcb_size %llu does not match struct\n",
				   pm->lm_mcb_size_bytes);
			return -EINVAL;
		}
	}

	if (pm->lm_first_offset_bytes != CACHE_MEM_FIRST_OFFSET_BYTES) {
		printk_err("lm_first_offset_bytes mismatch %llu/%lu\n",
			   pm->lm_first_offset_bytes,
			   CACHE_MEM_FIRST_OFFSET_BYTES);
		return -EBADMSG;
	}

	if (pm->lm_cache_layout == CACHE_LAYOUT_SEQUENTIAL) {
		uint64_t m = pm->lm_first_offset_bytes;

		m += pm->lm_cache_blocks * pm->lm_mcb_size_bytes;
		m = round_up(m, PAGE_SIZE);
		if (m != pm->lm_first_data_block_offset_bytes) {
			printk_err("first_data_block_offset mismatch %llu\n",
				   pm->lm_first_data_block_offset_bytes);
			return -EBADMSG;
		}
		m += pm->lm_cache_blocks * PAGE_SIZE;
		if (m > pm->lm_cache_size_bytes) {
			printk_err("last offset exceeds cache size %llu/%llu\n",
				   m,
				   pm->lm_cache_size_bytes);
			return -EBADMSG;
		}
	} else {
		uint64_t m = pm->lm_first_offset_bytes;

		m += pm->lm_cache_blocks * (PAGE_SIZE * 2);
		ASSERT(pm->lm_cache_layout == 'I');
		if (pm->lm_first_data_block_offset_bytes !=
						CACHE_MEM_FIRST_OFFSET_BYTES) {
			printk_err("first_data_block_offset mismatch %llu\n",
				   pm->lm_first_data_block_offset_bytes);
			return -EBADMSG;
		}
		if (m > pm->lm_cache_size_bytes) {
			printk_err("last offset exceeds cache size %llu/%llu\n",
				   m,
				   pm->lm_cache_size_bytes);
			return -EBADMSG;
		}
	}

	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);
	if (pm->lm_cache_size_bytes < pa->papi_bdev_size_bytes) {
		printk_warning("size %llu less than allocated size %lu\n",
				pm->lm_cache_size_bytes,
				pa->papi_bdev_size_bytes);
		printk_warning("size %llumb less than allocated size %llumb\n",
				pm->lm_cache_size_bytes / 1024ULL / 1024ULL,
				pa->papi_bdev_size_bytes / 1024ULL / 1024ULL);
	}
	if (pm->lm_cache_size_bytes > pa->papi_bdev_size_bytes) {
		printk_err("device size %llu exceeds allocated size %lu\n",
				pm->lm_cache_size_bytes,
				pa->papi_bdev_size_bytes);
		printk_err("device size %llumb exceeds allocated size %llumb\n",
				pm->lm_cache_size_bytes / 1024ULL / 1024ULL,
				pa->papi_bdev_size_bytes / 1024ULL / 1024ULL);
		return -EBADMSG;
	}
	if (pm->lm_cache_size_bytes == pa->papi_bdev_size_bytes) {
		printk_info("device size %llu equals allocated size %lu\n",
				pm->lm_cache_size_bytes,
				pa->papi_bdev_size_bytes);
		printk_info("device size %llumb equals allocated size %llumb\n",
				pm->lm_cache_size_bytes / 1024ULL / 1024ULL,
				pa->papi_bdev_size_bytes / 1024ULL / 1024ULL);
	}

	__pmem_assert_offsets(bc);

	printk_info("cache '%s' on '%s' restore ok, %llu cache blocks\n",
			pm->lm_name,
			pm->lm_device_name,
			pm->lm_cache_blocks);

	pa->papi_stats.restore_header_valid = 1;

	return 0;
}

/*
 * return values:
 * - negative errno values for unrecoverable data corruption.
 * - 1 for successful restore.
 * - 0 for no restore (crash occurred in the middle of a transaction).
 */
int pmem_block_restore(struct bittern_cache *bc,
		       struct cache_block *cache_block)
{
	struct pmem_block_metadata *pmbm;
	uint128_t hash_metadata, hash_data;
	int ret;
	void *buffer_vaddr;
	struct page *buffer_page;
	struct pmem_api *pa = &bc->bc_papi;
	int block_id;

	ASSERT(bc != NULL);
	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);
	ASSERT(sizeof(struct pmem_header) == PAGE_SIZE);

	block_id = cache_block->bcb_block_id;

	ASSERT(pa->papi_hdr.lm_cache_blocks != 0);
	ASSERT(block_id >= 1 && block_id <= pa->papi_hdr.lm_cache_blocks);
	ASSERT(cache_block != NULL);
	ASSERT(cache_block->bcb_block_id == block_id);

	pmbm = kmem_alloc(sizeof(struct pmem_block_metadata), GFP_NOIO);
	/*TODO_ADD_ERROR_INJECTION*/
	if (pmbm == NULL) {
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, cache_block, NULL, NULL,
			     "kmem_alloc pmem_block_metadata failed");
		printk_err("%s: kmem_alloc pmem_block_metadata failed\n",
			   bc->bc_name);
		return -ENOMEM;
	}

	ret = pmem_read_sync(bc,
			__cache_block_id_2_metadata_pmem_offset(bc, block_id),
			pmbm,
			sizeof(struct pmem_block_metadata));
	/*TODO_ADD_ERROR_INJECTION*/
	if (ret != 0) {
		ASSERT(ret < 0);
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "pmem_read_sync failed, ret=%d",
			     ret);
		printk_err("%s: pmem_read_sync failed, ret=%d\n",
			   bc->bc_name,
			   ret);
		kmem_free(pmbm, sizeof(struct pmem_block_metadata));
		return ret;
	}

	/*
	 * this can only happen if pmem is corrupt
	 */
	if (pmbm->pmbm_magic != MCBM_MAGIC) {
		pa->papi_stats.restore_corrupt_metadata_blocks++;
		printk_err("block id #%u: error: magic number(s) mismatch, magic=0x%x/0x%x\n",
			   block_id,
			   pmbm->pmbm_magic,
			   MCBM_MAGIC);
		kmem_free(pmbm, sizeof(struct pmem_block_metadata));
		return -EHWPOISON;
	}

	hash_metadata = murmurhash3_128(pmbm, PMEM_BLOCK_METADATA_HASHING_SIZE);

	if (uint128_ne(hash_metadata, pmbm->pmbm_hash_metadata)) {
		printk_err("block id #%u: metadata hash mismatch: stored_hash_metadata=" UINT128_FMT ", computed_hash_metadata" UINT128_FMT "\n",
			   block_id,
			   UINT128_ARG(pmbm->pmbm_hash_metadata),
			   UINT128_ARG(hash_metadata));
		pa->papi_stats.restore_hash_corrupt_metadata_blocks++;
		kmem_free(pmbm, sizeof(struct pmem_block_metadata));
		return -EHWPOISON;
	}

	if (CACHE_STATE_VALID(pmbm->pmbm_status)) {
		printk_info_ratelimited("block id #%u: metadata cache status valid %u(%s)\n",
					block_id,
					pmbm->pmbm_status,
					cache_state_to_str(pmbm->pmbm_status));
	} else {
		/*
		 * this can only happen if pmem is corrupt
		 */
		pa->papi_stats.restore_corrupt_metadata_blocks++;
		printk_err("block id #%u: error: metadata cache status invalid %u(%s)\n",
			   block_id,
			   pmbm->pmbm_status,
		     cache_state_to_str(pmbm->pmbm_status));
		kmem_free(pmbm, sizeof(struct pmem_block_metadata));
		return -EHWPOISON;
	}

	if (pmbm->pmbm_status == S_INVALID) {
		printk_info_ratelimited("block id #%u: warning: metadata cache status is %u(%s), nothing to restore\n",
					block_id,
					pmbm->pmbm_status,
					cache_state_to_str(pmbm->pmbm_status));
		pa->papi_stats.restore_invalid_metadata_blocks++;
		pa->papi_stats.restore_invalid_data_blocks++;
		kmem_free(pmbm, sizeof(struct pmem_block_metadata));
		/*
		 * restore ok
		 */
		return 1;
	}

	if (pmbm->pmbm_status != S_CLEAN && pmbm->pmbm_status != S_DIRTY) {
		printk_info_ratelimited("block id #%u: warning: metadata cache status is %u(%s) (transaction in progress), nothing to restore\n",
					block_id,
					pmbm->pmbm_status,
					cache_state_to_str(pmbm->pmbm_status));
		pa->papi_stats.restore_pending_metadata_blocks++;
		kmem_free(pmbm, sizeof(struct pmem_block_metadata));
		/*
		 * Intermediate state (crashed during a transaction).
		 * Caller will ignore this restore and reinitialize.
		 */
		return 0;
	}

	if (pmbm->pmbm_status == S_CLEAN) {
		pa->papi_stats.restore_valid_clean_metadata_blocks++;
	} else {
		ASSERT(pmbm->pmbm_status == S_DIRTY);
		pa->papi_stats.restore_valid_dirty_metadata_blocks++;
	}

	/*
	 * if the metadata crc32c is ok, none of this should ever happen.
	 */
	ASSERT(block_id == pmbm->pmbm_block_id);
	ASSERT(is_sector_cache_aligned(pmbm->pmbm_device_sector));

	buffer_vaddr = kmem_cache_alloc(bc->bc_kmem_map, GFP_NOIO);
	/*TODO_ADD_ERROR_INJECTION*/
	if (buffer_vaddr == NULL) {
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, cache_block, NULL, NULL,
			     "kmem_alloc kmem_map failed");
		printk_err("%s: kmem_alloc kmem_map failed\n", bc->bc_name);
		kmem_free(pmbm, sizeof(struct pmem_block_metadata));
		return -ENOMEM;
	}

	ASSERT(PAGE_ALIGNED(buffer_vaddr));
	buffer_page = virtual_to_page(buffer_vaddr);
	M_ASSERT(buffer_page != NULL);

	ret = pmem_read_sync(bc,
			     __cache_block_id_2_data_pmem_offset(bc, block_id),
			     buffer_vaddr,
			     PAGE_SIZE);
	/*TODO_ADD_ERROR_INJECTION*/
	if (ret != 0) {
		ASSERT(ret < 0);
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "pmem_read_sync failed, ret=%d",
			     ret);
		printk_err("%s: pmem_read_sync failed, ret=%d\n",
			   bc->bc_name,
			   ret);
		kmem_cache_free(bc->bc_kmem_map, buffer_vaddr);
		kmem_free(pmbm, sizeof(struct pmem_block_metadata));
		return ret;
	}

	hash_data = murmurhash3_128(buffer_vaddr, PAGE_SIZE);

	ASSERT(PAGE_ALIGNED(buffer_vaddr));
	ASSERT(buffer_page != NULL);
	ASSERT(buffer_page == virtual_to_page(buffer_vaddr));

	kmem_cache_free(bc->bc_kmem_map, buffer_vaddr);

	if (uint128_ne(hash_data, pmbm->pmbm_hash_data)) {
		printk_err("block id #%u: data hash mismatch: stored_hash_data=" UINT128_FMT ", computed_hash_data" UINT128_FMT "\n",
			   block_id,
			   UINT128_ARG(pmbm->pmbm_hash_data),
			   UINT128_ARG(hash_data));
		pa->papi_stats.restore_hash_corrupt_data_blocks++;
		kmem_free(pmbm, sizeof(struct pmem_block_metadata));
		return -EHWPOISON;
	}

	if (pmbm->pmbm_status == S_CLEAN) {
		pa->papi_stats.restore_valid_clean_data_blocks++;
	} else {
		ASSERT(pmbm->pmbm_status == S_DIRTY);
		pa->papi_stats.restore_valid_dirty_data_blocks++;
	}

	/*
	 * every checks out, restore metadata info into cache_block descriptor
	 */
	cache_block->bcb_sector = pmbm->pmbm_device_sector;
	cache_block->bcb_state = pmbm->pmbm_status;
	cache_block->bcb_xid = pmbm->pmbm_xid;
	cache_block->bcb_hash_data = pmbm->pmbm_hash_data;
	ASSERT(cache_block->bcb_state == S_CLEAN ||
	       cache_block->bcb_state == S_DIRTY);
	ASSERT(cache_block->bcb_sector != -1);
	ASSERT(is_sector_number_valid(cache_block->bcb_sector));
	ASSERT(cache_block->bcb_sector >= 0);

	printk_info_ratelimited("block id #%u: status=%u(%s), xid=%llu, sector=%llu, hash_metadata=" UINT128_FMT ", hash_data=" UINT128_FMT ": restore ok\n",
				pmbm->pmbm_block_id,
				pmbm->pmbm_status,
				cache_state_to_str(pmbm->pmbm_status),
				pmbm->pmbm_xid,
				pmbm->pmbm_device_sector,
				UINT128_ARG(pmbm->pmbm_hash_metadata),
				UINT128_ARG(pmbm->pmbm_hash_data));

	kmem_free(pmbm, sizeof(struct pmem_block_metadata));

	return 1;
}

int pmem_header_initialize(struct bittern_cache *bc)
{
	int ret;
	struct pmem_api *pa = &bc->bc_papi;
	struct pmem_header *pm = &pa->papi_hdr;
	size_t cache_size_bytes;

	ASSERT(bc != NULL);
	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);
	ASSERT(sizeof(struct pmem_header) == PAGE_SIZE);

	cache_size_bytes = pa->papi_bdev_size_bytes;
	printk_info("cache_size_bytes=%lu, cache_size_mbytes=%lu\n",
		    cache_size_bytes, cache_size_bytes / (1024 * 1024));

	memset(pm, 0, sizeof(struct pmem_header));
	pm->lm_magic = LM_MAGIC;
	pm->lm_version = LM_VERSION;
	pm->lm_cache_block_size = PAGE_SIZE;

	printk_info("pmem_layout='%c'\n", pmem_cache_layout(bc));
	ASSERT(pmem_cache_layout(bc) == CACHE_LAYOUT_INTERLEAVED ||
	       pmem_cache_layout(bc) == CACHE_LAYOUT_SEQUENTIAL);

	pmem_initialize_pmem_header_sizes(bc, cache_size_bytes);

	ASSERT(LM_NAME_SIZE == sizeof(bc->bc_name));
	ASSERT(sizeof(pm->lm_uuid) == 16);
	ASSERT(sizeof(pm->lm_device_uuid) == 16);

	generate_random_uuid(pm->lm_uuid);
	snprintf(pm->lm_name, LM_NAME_SIZE, "%s", bc->bc_name);

	generate_random_uuid(pm->lm_device_uuid);
	snprintf(pm->lm_device_name,
		 LM_NAME_SIZE, "%s", bc->bc_cached_device_name);

	printk_info("pm->lm_name=%s\n", pm->lm_name);
	printk_info("pm->lm_uuid=%pUb\n", pm->lm_uuid);
	printk_info("pm->lm_device_name=%s\n", pm->lm_device_name);
	printk_info("pm->lm_device_uuid=%pUb\n", pm->lm_device_uuid);
	printk_info("pm->lm_cache_size_bytes=%llu\n",
		    pm->lm_cache_size_bytes);

	pm->lm_xid_first = 1ULL;
	pm->lm_xid_current = 1ULL;

	__pmem_assert_offsets(bc);

	/*
	 * initialize mem copy #0
	 */
	pm->lm_xid_current++;
	pm->lm_hash = murmurhash3_128(pm, PMEM_HEADER_HASHING_SIZE);
	ASSERT(sizeof(struct pmem_header) <= PAGE_SIZE);
	ret = pmem_write_sync(bc,
			      CACHE_MEM_HEADER_0_OFFSET_BYTES,
			      pm,
			      sizeof(struct pmem_header));
	/*TODO_ADD_ERROR_INJECTION*/
	if (ret != 0) {
		ASSERT(ret < 0);
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "pmem_write_sync header0 failed, ret=%d",
			     ret);
		printk_err("%s: pmem_write_sync header0 failed, ret=%d\n",
			   bc->bc_name,
			   ret);
		return ret;
	}

	/*
	 * initialize mem copy #1
	 */
	pm->lm_xid_current++;
	pm->lm_hash = murmurhash3_128(pm, PMEM_HEADER_HASHING_SIZE);
	ASSERT(sizeof(struct pmem_header) <= PAGE_SIZE);
	ret = pmem_write_sync(bc,
			      CACHE_MEM_HEADER_1_OFFSET_BYTES,
			      pm,
			      sizeof(struct pmem_header));
	/*TODO_ADD_ERROR_INJECTION*/
	if (ret != 0) {
		ASSERT(ret < 0);
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "pmem_write_sync header0 failed, ret=%d",
			     ret);
		printk_err("%s: pmem_write_sync header0 failed, ret=%d\n",
			   bc->bc_name,
			   ret);
		return ret;
	}

	/*
	 * also initialize xid and bc_buffer_entries
	 */
	cache_xid_set(bc, pm->lm_xid_current + 1);

	printk_info("cache_blocks=%llu\n", pm->lm_cache_blocks);

	return 0;
}

int pmem_metadata_initialize(struct bittern_cache *bc, unsigned int block_id)
{
	int ret;
	struct pmem_api *pa = &bc->bc_papi;
	struct pmem_block_metadata *pmbm;

	pa = pa; /* shutoff compiler warning (used in dev build) */
	ASSERT(bc != NULL);
	ASSERT(sizeof(struct pmem_header) == PAGE_SIZE);
	ASSERT(pa->papi_hdr.lm_cache_blocks != 0);
	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);

	pmbm = kmem_zalloc(sizeof(struct pmem_block_metadata), GFP_NOIO);
	/*TODO_ADD_ERROR_INJECTION*/
	if (pmbm == NULL) {
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "kmem_zalloc pmem_block_metadata block_id=%u failed",
			     block_id);
		printk_err("%s: kmem_zalloc pmem_block_metadata block_id=%u failed\n",
			   bc->bc_name,
			   block_id);
		kmem_free(pmbm, sizeof(struct pmem_block_metadata));
		return -ENOMEM;
	}

	pmbm->pmbm_magic = MCBM_MAGIC;
	pmbm->pmbm_block_id = block_id;
	pmbm->pmbm_status = S_INVALID;
	pmbm->pmbm_device_sector = -1;
	pmbm->pmbm_xid = 0;
	pmbm->pmbm_hash_data = UINT128_ZERO;
	pmbm->pmbm_hash_metadata = murmurhash3_128(pmbm,
					PMEM_BLOCK_METADATA_HASHING_SIZE);

	BT_DEV_TRACE(BT_LEVEL_TRACE3, bc, NULL, NULL, NULL, NULL,
		     "block_id = %u", block_id);

	ret = pmem_write_sync(bc,
			__cache_block_id_2_metadata_pmem_offset(bc, block_id),
			pmbm,
			sizeof(struct pmem_block_metadata));
	/*TODO_ADD_ERROR_INJECTION*/
	if (ret != 0) {
		ASSERT(ret < 0);
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "pmem_write_sync block_id=%u failed, ret=%d",
			     block_id,
			     ret);
		printk_err("%s: pmem_write_sync block_id=%u failed, ret=%d\n",
			   bc->bc_name,
			   block_id,
			   ret);
		return ret;
	}

	kmem_free(pmbm, sizeof(struct pmem_block_metadata));

	return 0;
}

int pmem_header_update(struct bittern_cache *bc, int update_both)
{
	int ret;
	struct pmem_api *pa = &bc->bc_papi;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);
	ASSERT(sizeof(struct pmem_header) == PAGE_SIZE);

	M_ASSERT(pa->papi_hdr.lm_xid_current <= cache_xid_get(bc));

	if (pa->papi_hdr.lm_xid_current == cache_xid_get(bc))
		return 0;

	pa->papi_hdr.lm_xid_current = cache_xid_get(bc);

	if (pa->papi_hdr_updated_last == 1 || update_both) {
		/*
		 * update mem copy #0
		 */
		pa->papi_hdr.lm_hash = murmurhash3_128(&pa->papi_hdr,
						PMEM_HEADER_HASHING_SIZE);
		ret = pmem_write_sync(bc,
				      CACHE_MEM_HEADER_0_OFFSET_BYTES,
				      &pa->papi_hdr,
				      sizeof(struct pmem_header));
		/*TODO_ADD_ERROR_INJECTION*/
		if (ret != 0) {
			ASSERT(ret < 0);
			BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
				     "pmem_write_sync header0 failed, ret=%d",
				     ret);
			printk_err("%s: pmem_write_sync header0 failed, ret=%d\n",
				   bc->bc_name,
				   ret);
			return ret;
		}
	}
	if (pa->papi_hdr_updated_last == 0 || update_both) {
		/*
		 * update mem copy #1
		 */
		pa->papi_hdr.lm_hash = murmurhash3_128(&pa->papi_hdr,
						PMEM_HEADER_HASHING_SIZE);
		ret = pmem_write_sync(bc,
				      CACHE_MEM_HEADER_1_OFFSET_BYTES,
				      &pa->papi_hdr,
				      sizeof(struct pmem_header));
		/*TODO_ADD_ERROR_INJECTION*/
		if (ret != 0) {
			ASSERT(ret < 0);
			BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
				     "pmem_write_sync header1 failed, ret=%d",
				     ret);
			printk_err("%s: pmem_write_sync header1 failed, ret=%d\n",
				   bc->bc_name,
				   ret);
			return ret;
		}
	}
	pa->papi_hdr_updated_last = (pa->papi_hdr_updated_last + 1) % 2;

	return 0;
}

static void pmem_header_update_worker(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct bittern_cache *bc;
	int ret;

	bc = container_of(dwork,
			  struct bittern_cache,
			  bc_pmem_update_work);
	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	M_ASSERT(bc->bc_pmem_update_workqueue != NULL);

	if (bc->error_state == ES_NOERROR) {
		BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL, "bc=%p", bc);
		ret = pmem_header_update(bc, 0);

		/* should make this a common function */
		if (ret != 0) {
			printk_err("%s: cannot update header: %d. will fail all future requests\n",
				   bc->bc_name,
				   ret);
			bc->error_state = ES_ERROR_FAIL_ALL;
		}
	}

	schedule_delayed_work(&bc->bc_pmem_update_work,
			      msecs_to_jiffies(30000));
}

void pmem_header_update_start_workqueue(struct bittern_cache *bc)
{
	printk_debug("%s: pmem_header_start_workqueue\n", bc->bc_name);
	M_ASSERT(bc->bc_pmem_update_workqueue != NULL);
	INIT_DELAYED_WORK(&bc->bc_pmem_update_work, pmem_header_update_worker);
	schedule_delayed_work(&bc->bc_pmem_update_work,
			      msecs_to_jiffies(30000));
}

void pmem_header_update_stop_workqueue(struct bittern_cache *bc)
{
	printk_debug("%s: pmem_header_stop_workqueue\n", bc->bc_name);
	M_ASSERT(bc->bc_pmem_update_workqueue != NULL);
	printk_debug("%s: cancel_delayed_work\n", bc->bc_name);
	cancel_delayed_work(&bc->bc_pmem_update_work);
	printk_debug("%s: flush_workqueue\n", bc->bc_name);
	flush_workqueue(bc->bc_pmem_update_workqueue);
}

static inline
const struct cache_papi_interface *__pmem_api_interface(struct pmem_api *pa)
{
	ASSERT(pa->papi_interface != NULL);
	ASSERT(pa->papi_interface->magic == PAPI_MAGIC);
	return pa->papi_interface;
}

/*! allocate PMEM resources */
int pmem_allocate(struct bittern_cache *bc, struct block_device *blockdev)
{
	const struct cache_papi_interface *pp;
	int ret;
	struct pmem_api *pa = &bc->bc_papi;

	ASSERT(pa->papi_interface == NULL);

	bc->bc_pmem_update_workqueue = alloc_workqueue("b_pu/%s",
					       WQ_MEM_RECLAIM,
					       1,
					       bc->bc_name);
	if (bc->bc_pmem_update_workqueue == NULL) {
		printk_err("%s: cannot allocate pmem_update workqueue\n",
			   bc->bc_name);
		return -ENOMEM;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
	/* direct_access() calling convention changed in 4.0 */
	if (blockdev->bd_disk->fops->direct_access != NULL) {
		printk_info("%s: detected direct_access-based pmem API implementation\n",
			    bc->bc_name);
		ret = pmem_allocate_papi_mem(bc, blockdev);
		pp = &cache_papi_mem;
	} else
#endif /* LINUX_VERSION_CODE >= 4.0.0 */
	{
		printk_info("%s: detected blockdev-based pmem API implementation\n",
			    bc->bc_name);
		ret = pmem_allocate_papi_block(bc, blockdev);
		pp = &cache_papi_block;
	}

	if (ret != 0) {
		destroy_workqueue(bc->bc_pmem_update_workqueue);
		bc->bc_pmem_update_workqueue = NULL;
		printk_err("%s: %s: allocate_func error: ret=%d\n",
			   bc->bc_name,
			   pp->interface_name,
			   ret);
		return ret;
	}

	printk_info("%s: %s: allocate_func ok\n",
		    bc->bc_name,
		    pp->interface_name);
	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);
	/*
	 * everything ok, record index and context
	 */
	pa->papi_interface = pp;
	return 0;
}

/*! deallocate PMEM resources */
void pmem_deallocate(struct bittern_cache *bc)
{
	struct pmem_api *pa = &bc->bc_papi;
	const struct cache_papi_interface *pp = __pmem_api_interface(pa);

	printk_debug("%s: deallocate\n", bc->bc_name);

	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);
	ASSERT(bc->bc_pmem_update_workqueue != NULL);

	destroy_workqueue(bc->bc_pmem_update_workqueue);
	bc->bc_pmem_update_workqueue = NULL;

	printk_info("%s: deallocate_func\n", pp->interface_name);
	(*pp->deallocate_func)(bc);
}

const char *pmem_api_name(struct bittern_cache *bc)
{
	struct pmem_api *pa = &bc->bc_papi;

	return __pmem_api_interface(pa)->interface_name;
}

bool pmem_page_size_transfer_only(struct bittern_cache *bc)
{
	struct pmem_api *pa = &bc->bc_papi;

	return __pmem_api_interface(pa)->page_size_transfer_only;
}

enum cache_layout pmem_cache_layout(struct bittern_cache *bc)
{
	struct pmem_api *pa = &bc->bc_papi;

	return __pmem_api_interface(pa)->cache_layout;
}

int pmem_read_sync(struct bittern_cache *bc,
		   uint64_t from_pmem_offset,
		   void *to_buffer,
		   size_t size)
{
	struct pmem_api *pa = &bc->bc_papi;
	const struct cache_papi_interface *pp = __pmem_api_interface(pa);

	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);
	return (*pp->read_sync)(bc, from_pmem_offset, to_buffer, size);
}

int pmem_write_sync(struct bittern_cache *bc,
		    uint64_t to_pmem_offset,
		    void *from_buffer,
		    size_t size)
{
	struct pmem_api *pa = &bc->bc_papi;
	const struct cache_papi_interface *pp = __pmem_api_interface(pa);

	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);

	return (*pp->write_sync)(bc, to_pmem_offset, from_buffer, size);
}

int pmem_metadata_sync_read(struct bittern_cache *bc,
			    struct cache_block *cache_block,
			    struct pmem_block_metadata *out_pmbm_mem)
{
	int ret;
	struct pmem_api *pa = &bc->bc_papi;
	int block_id = cache_block->bcb_block_id;

	pa = pa; /* shutoff compiler warning (used in dev build) */
	ASSERT(out_pmbm_mem != NULL);
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "out_pmbm_mem=%p", out_pmbm_mem);
	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);
	ret = pmem_read_sync(bc,
			__cache_block_id_2_metadata_pmem_offset(bc, block_id),
			out_pmbm_mem,
			sizeof(struct pmem_block_metadata));
	/*TODO_ADD_ERROR_INJECTION*/
	if (ret != 0) {
		ASSERT(ret < 0);
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "pmem_read_sync failed, ret=%d",
			     ret);
		printk_err("%s: pmem_read_sync failed, ret=%d\n",
			   bc->bc_name,
			   ret);
	}

	return ret;
}

void pmem_metadata_async_write(struct bittern_cache *bc,
			       struct cache_block *cache_block,
			       struct pmem_context *pmem_ctx,
			       void *callback_context,
			       pmem_callback_t callback_function,
			       enum cache_state metadata_update_state)
{
	struct pmem_api *pa = &bc->bc_papi;
	const struct cache_papi_interface *pp = __pmem_api_interface(pa);

	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);

	(*pp->metadata_async_write)(bc,
				    cache_block,
				    pmem_ctx,
				    callback_context,
				    callback_function,
				    metadata_update_state);
}

void pmem_data_get_page_read(struct bittern_cache *bc,
			     struct cache_block *cache_block,
			     struct pmem_context *pmem_ctx,
			     void *callback_context,
			     pmem_callback_t callback_function)
{
	struct pmem_api *pa = &bc->bc_papi;
	const struct cache_papi_interface *pp = __pmem_api_interface(pa);

	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);

	(*pp->data_cache_get_page_read)(bc,
					cache_block,
					pmem_ctx,
					callback_context,
					callback_function);
}

void pmem_data_put_page_read(struct bittern_cache *bc,
			     struct cache_block *cache_block,
			     struct pmem_context *pmem_ctx)
{
	struct pmem_api *pa = &bc->bc_papi;
	const struct cache_papi_interface *pp = __pmem_api_interface(pa);

	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);

	(*pp->data_cache_put_page_read)(bc,
					cache_block,
					pmem_ctx);
}

void pmem_data_convert_read_to_write(struct bittern_cache *bc,
				     struct cache_block *cache_block,
				     struct pmem_context *pmem_ctx)
{
	struct pmem_api *pa = &bc->bc_papi;
	const struct cache_papi_interface *pp = __pmem_api_interface(pa);

	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);

	(*pp->data_cache_convert_read_to_write)(bc,
						cache_block,
						pmem_ctx);
}

void pmem_data_clone_read_to_write(struct bittern_cache *bc,
				   struct cache_block *from_cache_block,
				   struct cache_block *to_cache_block,
				   struct pmem_context *pmem_ctx)
{
	struct pmem_api *pa = &bc->bc_papi;
	const struct cache_papi_interface *pp = __pmem_api_interface(pa);

	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);

	(*pp->data_cache_clone_read_to_write)(bc,
					      from_cache_block,
					      to_cache_block,
					      pmem_ctx);
}

void pmem_data_get_page_write(struct bittern_cache *bc,
			      struct cache_block *cache_block,
			      struct pmem_context *pmem_ctx)
{
	struct pmem_api *pa = &bc->bc_papi;
	const struct cache_papi_interface *pp = __pmem_api_interface(pa);

	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);

	(*pp->data_cache_get_page_write)(bc,
					 cache_block,
					 pmem_ctx);
}

void pmem_data_put_page_write(struct bittern_cache *bc,
			      struct cache_block *cache_block,
			      struct pmem_context *pmem_ctx,
			      void *callback_context,
			      pmem_callback_t callback_function,
			      enum cache_state metadata_update_state)
{
	struct pmem_api *pa = &bc->bc_papi;
	const struct cache_papi_interface *pp = __pmem_api_interface(pa);

	ASSERT(pa->papi_bdev_size_bytes > 0);
	ASSERT(pa->papi_bdev != NULL);

	(*pp->data_cache_put_page_write)(bc,
					 cache_block,
					 pmem_ctx,
					 callback_context,
					 callback_function,
					 metadata_update_state);
}
