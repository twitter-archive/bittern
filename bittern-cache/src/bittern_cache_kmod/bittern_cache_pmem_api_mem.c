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

#include "bittern_cache.h"
#include "bittern_cache_pmem_api_internal.h"

#include <linux/blkdev.h>

/*
 * pmem allocate/deallocate functions
 */
int pmem_allocate_papi_mem(struct bittern_cache *bc,
			   struct block_device *blockdev)
{
	struct pmem_api *pa = &bc->bc_papi;
	size_t blockdev_size_bytes;

	ASSERT(blockdev->bd_disk->fops->direct_access != NULL);

	blockdev_size_bytes = blockdev->bd_part->nr_sects * SECTOR_SIZE;

	printk_info("memory device size %lu\n", blockdev_size_bytes);

	pa->papi_bdev = blockdev;
	pa->papi_bdev_size_bytes = blockdev_size_bytes;
	pa->papi_bdev_actual_size_bytes = blockdev_size_bytes;

	return 0;
}

void pmem_deallocate_papi_mem(struct bittern_cache *bc)
{
	struct pmem_api *pa = &bc->bc_papi;

	M_ASSERT(pa->papi_interface == &cache_papi_mem);
}

/*
 * sync read from cache.
 */
int pmem_read_sync_mem(struct bittern_cache *bc,
		       uint64_t from_pmem_offset,
		       void *to_buffer, size_t size)
{
	uint64_t ts_started = current_kernel_time_nsec();
	struct pmem_api *pa = &bc->bc_papi;
	uint64_t end_offset = from_pmem_offset + size - 1;
	void *dax_addr;
	long dax_pfn, dax_ret;

	ASSERT(bc != NULL);
	ASSERT(size > 0);

	M_ASSERT((end_offset & PAGE_MASK) == (from_pmem_offset & PAGE_MASK));

	dax_ret = bdev_direct_access(
		pa->papi_bdev,
		(from_pmem_offset & PAGE_MASK) / SECTOR_SIZE,
		&dax_addr, &dax_pfn, PAGE_SIZE);
	M_ASSERT_FIXME(dax_ret == PAGE_SIZE);

	BT_DEV_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL,
		     "from_pmem_offset=%llu, to_buffer=%p, size=%lu, dax_addr=%p, dax_pfn=%ld",
		     from_pmem_offset, to_buffer, size, dax_addr, dax_pfn);

	memcpy(to_buffer, dax_addr + (from_pmem_offset % PAGE_SIZE), size);

	if (size == PAGE_SIZE) {
		atomic_dec(&pa->papi_stats.pmem_read_4k_pending);
		cache_timer_add(&pa->papi_stats.pmem_read_4k_timer,
					ts_started);
	} else {
		atomic_dec(&pa->papi_stats.pmem_read_not4k_pending);
		cache_timer_add(&pa->papi_stats.
					pmem_read_not4k_timer, ts_started);
	}

	return 0;
}

int pmem_write_sync_mem(struct bittern_cache *bc,
			uint64_t to_pmem_offset,
			void *from_buffer, size_t size)
{
	uint64_t ts_started = current_kernel_time_nsec();
	struct pmem_api *pa = &bc->bc_papi;
	uint64_t end_offset = to_pmem_offset + size - 1;
	void *dax_addr;
	long dax_pfn, dax_ret;

	ASSERT(bc != NULL);
	ASSERT(size > 0);

	M_ASSERT((end_offset & PAGE_MASK) == (to_pmem_offset & PAGE_MASK));

	if (size == PAGE_SIZE) {
		atomic_inc(&pa->papi_stats.pmem_write_4k_count);
		atomic_inc(&pa->papi_stats.pmem_write_4k_pending);
	} else {
		atomic_inc(&pa->papi_stats.pmem_write_not4k_count);
		atomic_inc(&pa->papi_stats.pmem_write_not4k_pending);
	}

	dax_ret = bdev_direct_access(
		pa->papi_bdev,
		(to_pmem_offset & PAGE_MASK) / SECTOR_SIZE,
		&dax_addr, &dax_pfn, PAGE_SIZE);
	M_ASSERT_FIXME(dax_ret == PAGE_SIZE);

	BT_DEV_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL,
		     "to_pmem_offset=%llu, from_buffer=%p, size=%lu, dax_addr=%p, dax_pfn=%ld",
		     to_pmem_offset, from_buffer, size, dax_addr, dax_pfn);

	memcpy_nt(dax_addr + (to_pmem_offset % PAGE_SIZE), from_buffer, size);

	if (size == PAGE_SIZE) {
		atomic_dec(&pa->papi_stats.pmem_write_4k_pending);
		cache_timer_add(&pa->papi_stats.
					pmem_write_4k_timer, ts_started);
	} else {
		atomic_dec(&pa->papi_stats.pmem_write_not4k_pending);
		cache_timer_add(&pa->papi_stats.
					pmem_write_not4k_timer,
					ts_started);
	}
	return 0;
}

void pmem_metadata_async_write_mem(struct bittern_cache *bc,
				   struct cache_block *cache_block,
				   struct pmem_context *pmem_ctx,
				   void *callback_context,
				   pmem_callback_t callback_function,
				   enum cache_state metadata_update_state)
{
	uint64_t ts_started = current_kernel_time_nsec();
	struct pmem_block_metadata *pmbm;
	struct pmem_api *pa = &bc->bc_papi;
	uint64_t to_offset, end_offset;
	void *dax_addr;
	long dax_pfn, dax_ret;
	struct data_buffer_info *dbi_data;
	struct async_context *ctx;
	unsigned int block_id;

	M_ASSERT(pmem_ctx != NULL);
	M_ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	dbi_data = &pmem_ctx->dbi;
	ctx = &pmem_ctx->async_ctx;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(pa->papi_hdr.lm_cache_blocks != 0);
	ASSERT(cache_block->bcb_state != S_INVALID);
	ASSERT(callback_context != NULL);
	ASSERT(callback_function != NULL);

	block_id = cache_block->bcb_block_id;

	atomic_inc(&pa->papi_stats.metadata_write_async_count);

	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "metadata_update_state=%d(%s)",
		     metadata_update_state,
		     cache_state_to_str(metadata_update_state));
	ASSERT(metadata_update_state == S_INVALID ||
	       metadata_update_state == S_CLEAN ||
	       metadata_update_state == S_DIRTY);

	ASSERT(is_sector_number_valid(cache_block->bcb_sector));

	ASSERT(pa->papi_hdr.lm_mcb_size_bytes ==
	       sizeof(struct pmem_block_metadata));

	pmbm = &pmem_ctx->pmbm;
	M_ASSERT(pmbm != NULL);

	pmbm->pmbm_magic = MCBM_MAGIC;
	pmbm->pmbm_block_id = block_id;
	pmbm->pmbm_status = metadata_update_state;
	if (metadata_update_state == S_INVALID) {
		pmbm->pmbm_device_sector = -1;
	} else {
		ASSERT(is_sector_number_valid(cache_block->bcb_sector));
		pmbm->pmbm_device_sector = cache_block->bcb_sector;
	}
	pmbm->pmbm_xid = cache_block->bcb_xid;
	pmbm->pmbm_hash_data = cache_block->bcb_hash_data;
	pmbm->pmbm_hash_metadata = murmurhash3_128(pmbm,
					   PMEM_BLOCK_METADATA_HASHING_SIZE);

	to_offset = __cache_block_id_2_metadata_pmem_offset(bc, block_id);
	end_offset = to_offset + sizeof(*pmbm) - 1;
	M_ASSERT((to_offset & PAGE_MASK) == (end_offset & PAGE_MASK));

	dax_ret = bdev_direct_access(
		pa->papi_bdev,
		(to_offset & PAGE_MASK) / SECTOR_SIZE,
		&dax_addr, &dax_pfn, PAGE_SIZE);
	M_ASSERT_FIXME(dax_ret == PAGE_SIZE);

	BT_DEV_TRACE(BT_LEVEL_TRACE2, bc, NULL, cache_block, NULL, NULL,
		     "to_offset=%llu, dax_addr=%p, dax_pfn=%ld",
		     to_offset, dax_addr, dax_pfn);

	memcpy_nt(dax_addr + (to_offset % PAGE_SIZE), pmbm, sizeof(*pmbm));

	cache_timer_add(&pa->papi_stats.metadata_write_async_timer, ts_started);

	/*
	 * just call the higher level callback
	 */
	(*callback_function)(bc,
			     cache_block,
			     pmem_ctx,
			     callback_context,
			     0);
}

/*
 * async read accessors (get_page_read()/put_page_read())
 *
 * caller calls get_page_read() to setup the necessary context and to start
 * an asynchronous cache read transfer. when the asynchronous transfer is done
 * and the data is available in dbi_data, the get_page_read_done() callback
 * is called.
 * the callback could be called even before this function returns [this is
 * guaranteed to happen for this provider, which uses straight memory access].
 *
 * the data pointed by dbi_data is guaranteed to be valid until
 * put_page_read() is called.
 *
 * caller must call put_page_read() when it's done in accessing the data page
 * and to release the data context in dbi_data.
 * caller must not modify the data with the get_page_read()/put_page_read() APIs
 *
 */
void pmem_data_get_page_read_mem(struct bittern_cache *bc,
				 struct cache_block *cache_block,
				 struct pmem_context *pmem_ctx,
				 void *callback_context,
				 pmem_callback_t callback_function)
{
	uint64_t start_timer = current_kernel_time_nsec();
	struct pmem_api *pa = &bc->bc_papi;
	void *cache_vaddr;
	struct page *cache_page;
	unsigned long from_offset;
	long dax_pfn, dax_ret;
	struct data_buffer_info *dbi_data;
	struct async_context *ctx;
	unsigned int block_id;

	M_ASSERT(pmem_ctx != NULL);
	M_ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	dbi_data = &pmem_ctx->dbi;
	ctx = &pmem_ctx->async_ctx;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(pa->papi_hdr.lm_cache_blocks != 0);
	ASSERT(cache_block->bcb_state != S_INVALID);
	ASSERT(callback_context != NULL);
	ASSERT(callback_function != NULL);

	block_id = cache_block->bcb_block_id;

	atomic_inc(&pa->papi_stats.data_get_page_read_count);
	atomic_inc(&pa->papi_stats.data_get_put_page_pending_count);
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "data_get_put_page_pending_count=%u",
		     atomic_read(&pa->papi_stats.
				 data_get_put_page_pending_count));

	/*
	 * everything is directly accessible.
	 * just expose the virtual address and page, and we're done.
	 */
	from_offset = __cache_block_id_2_data_pmem_offset(bc, block_id);
	ASSERT(from_offset % PAGE_SIZE == 0);

	dax_ret = bdev_direct_access(
		pa->papi_bdev,
		from_offset / SECTOR_SIZE,
		&cache_vaddr, &dax_pfn, PAGE_SIZE);
	M_ASSERT_FIXME(dax_ret == PAGE_SIZE);

	BT_DEV_TRACE(BT_LEVEL_TRACE2, bc, NULL, cache_block, NULL, NULL,
		     "from_offset=%lu, dax_addr=%p, dax_pfn=%ld",
		     from_offset, cache_vaddr, dax_pfn);

	cache_page = pfn_to_page(dax_pfn);
	ASSERT(cache_vaddr != NULL);
	ASSERT(cache_page != NULL);
	pmem_set_dbi(dbi_data,
		     CACHE_DI_FLAGS_PMEM_READ,
		     cache_vaddr,
		     cache_page);
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "di_buffer_vmalloc=%p, di_buffer=%p, di_page=%p, di_flags=0x%x, callback_context=%p, callback_function=%p",
		     dbi_data->di_buffer_vmalloc_buffer,
		     dbi_data->di_buffer,
		     dbi_data->di_page,
		     dbi_data->di_flags, callback_context, callback_function);

	/*
	 * call callback directly, as there is no async thread to do it.
	 */
	(*callback_function)(bc,
			     cache_block,
			     pmem_ctx,
			     callback_context,
			     0);

	cache_timer_add(&pa->papi_stats.data_get_page_read_async_timer,
				start_timer);

	cache_timer_add(&pa->papi_stats.data_get_page_read_timer, start_timer);
}

/* put_page_read */
/*
 * async read accessors (get_page_read()/put_page_read())
 *
 * caller calls get_page_read() to setup the necessary context and to start
 * an asynchronous cache read transfer. when the asynchronous transfer is done
 * and the data is available in dbi_data, the get_page_read_done() callback
 * is called.
 * the callback could be called even before this function returns [this is
 * guaranteed to happen for this provider, which uses straight memory access].
 *
 * the data pointed by dbi_data is guaranteed to be valid until
 * put_page_read() is called.
 *
 * caller must call put_page_read() when it's done in accessing the data page
 * and to release the data context in dbi_data.
 */
void pmem_data_put_page_read_mem(struct bittern_cache *bc,
				 struct cache_block *cache_block,
				 struct pmem_context *pmem_ctx)
{
	uint64_t start_timer = current_kernel_time_nsec();
	struct pmem_api *pa = &bc->bc_papi;
	struct data_buffer_info *dbi_data;
	struct async_context *ctx;
	unsigned int block_id;

	M_ASSERT(pmem_ctx != NULL);
	M_ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	dbi_data = &pmem_ctx->dbi;
	ctx = &pmem_ctx->async_ctx;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(pa->papi_hdr.lm_cache_blocks != 0);
	ASSERT(cache_block->bcb_state != S_INVALID);
	ASSERT(dbi_data != NULL);

	block_id = cache_block->bcb_block_id;

	atomic_inc(&pa->papi_stats.data_put_page_read_count);
	atomic_dec(&pa->papi_stats.data_get_put_page_pending_count);
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "data_get_put_page_pending_count=%u",
		     atomic_read(&pa->papi_stats.
				 data_get_put_page_pending_count));

	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "di_buffer_vmalloc=%p, di_buffer=%p, di_page=%p, di_flags=0x%x",
		     dbi_data->di_buffer_vmalloc_buffer,
		     dbi_data->di_buffer,
		     dbi_data->di_page, dbi_data->di_flags);

	ASSERT(dbi_data->di_buffer != NULL);
	ASSERT(dbi_data->di_page != NULL);
	ASSERT(dbi_data->di_flags == CACHE_DI_FLAGS_PMEM_READ);

	BT_DEV_TRACE(BT_LEVEL_TRACE2, bc, NULL, cache_block, NULL, NULL,
		     "before clear page");

	pmem_clear_dbi(dbi_data);

	BT_DEV_TRACE(BT_LEVEL_TRACE2, bc, NULL, cache_block, NULL, NULL,
		     "after clear page");

	cache_timer_add(&pa->papi_stats.data_put_page_read_timer, start_timer);
}

/*
 * convert page obtained for read to page for write -- this is used for rmw
 * cycles
 */
void pmem_data_convert_read_to_write_mem(struct bittern_cache *bc,
					 struct cache_block *cache_block,
					 struct pmem_context *pmem_ctx)
{
	struct pmem_api *pa = &bc->bc_papi;
	struct data_buffer_info *dbi_data;
	struct async_context *ctx;
	unsigned int block_id;

	M_ASSERT(pmem_ctx != NULL);
	M_ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	dbi_data = &pmem_ctx->dbi;
	ctx = &pmem_ctx->async_ctx;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(pa->papi_hdr.lm_cache_blocks != 0);
	ASSERT(cache_block->bcb_state != S_INVALID);

	block_id = cache_block->bcb_block_id;

	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "di_buffer_vmalloc=%p, di_buffer=%p, di_page=%p, di_flags=0x%x",
		     dbi_data->di_buffer_vmalloc_page,
		     dbi_data->di_buffer,
		     dbi_data->di_page, dbi_data->di_flags);
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "data_get_put_page_pending_count=%u",
		     atomic_read(&pa->papi_stats.
				 data_get_put_page_pending_count));

	atomic_inc(&pa->papi_stats.data_convert_page_read_to_write_count);

	ASSERT_PMEM_DBI(dbi_data);
	ASSERT((dbi_data->di_flags & CACHE_DI_FLAGS_PMEM_READ) != 0);
	ASSERT((dbi_data->di_flags & CACHE_DI_FLAGS_PMEM_WRITE) == 0);

	dbi_data->di_flags |= CACHE_DI_FLAGS_PMEM_WRITE;
}

/*
 * clone read page into a write page, similar to convert except that
 * the data is cloned -- this is used for rmw cycles
 */
void pmem_data_clone_read_to_write_mem(struct bittern_cache *bc,
				       struct cache_block *from_cache_block,
				       struct cache_block *to_cache_block,
				       struct pmem_context *pmem_ctx)
{
	void *to_buffer;
	struct page *to_page;
	struct pmem_api *pa = &bc->bc_papi;
	unsigned long to_offset;
	long dax_pfn, dax_ret;
	struct data_buffer_info *dbi_data;
	struct async_context *ctx;
	unsigned int from_block_id;
	unsigned int to_block_id;

	M_ASSERT(pmem_ctx != NULL);
	M_ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	dbi_data = &pmem_ctx->dbi;
	ctx = &pmem_ctx->async_ctx;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(from_cache_block, bc);
	ASSERT_CACHE_BLOCK(to_cache_block, bc);
	ASSERT(pa->papi_hdr.lm_cache_blocks != 0);
	ASSERT(from_cache_block->bcb_state != S_INVALID);
	ASSERT(to_cache_block->bcb_state != S_INVALID);

	from_block_id = from_cache_block->bcb_block_id;
	to_block_id = to_cache_block->bcb_block_id;
	ASSERT(to_block_id != from_block_id);

	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		     "di_buffer_vmalloc=%p, di_buffer=%p, di_page=%p, di_flags=0x%x",
		     dbi_data->di_buffer_vmalloc_page,
		     dbi_data->di_buffer,
		     dbi_data->di_page, dbi_data->di_flags);
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		     "data_get_put_page_pending_count=%u",
		     atomic_read(&pa->papi_stats.
				 data_get_put_page_pending_count));
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, from_cache_block, NULL, NULL,
		     "from_cache_block");
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, to_cache_block, NULL, NULL,
		     "to_cache_block");

	atomic_inc(&pa->papi_stats.
		   data_clone_read_page_to_write_page_count);

	ASSERT_PMEM_DBI(dbi_data);
	ASSERT((dbi_data->di_flags & CACHE_DI_FLAGS_PMEM_READ) != 0);
	ASSERT((dbi_data->di_flags & CACHE_DI_FLAGS_PMEM_WRITE) == 0);

	to_offset = __cache_block_id_2_data_pmem_offset(bc, to_block_id);
	ASSERT(to_offset % PAGE_SIZE == 0);

	dax_ret = bdev_direct_access(
		pa->papi_bdev,
		to_offset / SECTOR_SIZE,
		&to_buffer, &dax_pfn, PAGE_SIZE);
	M_ASSERT_FIXME(dax_ret == PAGE_SIZE);

	BT_DEV_TRACE(BT_LEVEL_TRACE2, bc, NULL, to_cache_block, NULL, NULL,
		     "to_offset=%lu, dax_addr=%p, dax_pfn=%ld",
		     to_offset, &to_buffer, dax_pfn);

	to_page = pfn_to_page(dax_pfn);
	ASSERT(to_buffer != NULL);
	ASSERT(to_page != NULL);
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, to_cache_block, NULL, NULL,
		     "copying to_cache_block from %p to %p",
		     dbi_data->di_buffer, to_buffer);

	/*
	 * there is no double buffering, so we actually need to copy the buffer
	 * FIXME: need a PMEM memcpy api which exposes both dest and source
	 */
	memcpy_nt(to_buffer, dbi_data->di_buffer, PAGE_SIZE);
	/*
	 * update buffer pointers
	 */
	dbi_data->di_buffer = to_buffer;
	dbi_data->di_page = to_page;

	dbi_data->di_flags |= CACHE_DI_FLAGS_PMEM_WRITE;
}

/* get_page_write */
/*
 * async write accessors (get_page_write()/put_page_write())
 *
 * caller calls get_page_write() to setup the necessary context to start
 * an asynchronous cache write transfer.
 * the buffer pointed by dbi_data is guaranteed to be valid until
 * put_page_write() is called. caller can then write to such buffer.
 *
 * caller calls put_page_write() to actually start the asynchronous cache write
 * transfer. the callback could be called even before this function returns
 * [this is guaranteed to happen for this pmem_api implementation].
 *
 * before the callback is called, the data context described in dbi_data
 * is released.
 */
void pmem_data_get_page_write_mem(struct bittern_cache *bc,
				  struct cache_block *cache_block,
				  struct pmem_context *pmem_ctx)
{
	uint64_t start_timer = current_kernel_time_nsec();
	struct pmem_api *pa = &bc->bc_papi;
	void *cache_vaddr;
	struct page *cache_page;
	unsigned long to_offset;
	long dax_pfn, dax_ret;
	struct data_buffer_info *dbi_data;
	struct async_context *ctx;
	unsigned int block_id;

	M_ASSERT(pmem_ctx != NULL);
	M_ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	dbi_data = &pmem_ctx->dbi;
	ctx = &pmem_ctx->async_ctx;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(pa->papi_hdr.lm_cache_blocks != 0);
	ASSERT(cache_block->bcb_state != S_INVALID);
	ASSERT(dbi_data != NULL);

	block_id = cache_block->bcb_block_id;

	atomic_inc(&pa->papi_stats.data_get_page_write_count);
	atomic_inc(&pa->papi_stats.data_get_put_page_pending_count);
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "data_get_put_page_pending_count=%u",
		     atomic_read(&pa->papi_stats.
				 data_get_put_page_pending_count));

	to_offset = __cache_block_id_2_data_pmem_offset(bc, block_id);
	ASSERT(to_offset % PAGE_SIZE == 0);

	dax_ret = bdev_direct_access(
		pa->papi_bdev,
		to_offset / SECTOR_SIZE,
		&cache_vaddr, &dax_pfn, PAGE_SIZE);
	M_ASSERT_FIXME(dax_ret == PAGE_SIZE);

	BT_DEV_TRACE(BT_LEVEL_TRACE2, bc, NULL, cache_block, NULL, NULL,
		     "to_offset=%lu, dax_addr=%p, dax_pfn=%ld",
		     to_offset, &cache_vaddr, dax_pfn);

	cache_page = pfn_to_page(dax_pfn);
	ASSERT(cache_vaddr != NULL);
	ASSERT(cache_page != NULL);
	pmem_set_dbi(dbi_data,
		     CACHE_DI_FLAGS_PMEM_WRITE,
		     cache_vaddr,
		     cache_page);
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "di_buffer_vmalloc=%p, di_buffer=%p, di_page=%p, di_flags=0x%x",
		     dbi_data->di_buffer_vmalloc_buffer,
		     dbi_data->di_buffer,
		     dbi_data->di_page, dbi_data->di_flags);

	cache_timer_add(&pa->papi_stats.data_get_page_write_timer, start_timer);
}

/* put_page_write */
/*
 * async write accessors (get_page_write()/put_page_write())
 *
 * caller calls get_page_write() to setup the necessary context to start
 * an asynchronous cache write transfer. the buffer pointed by dbi_data is
 * guaranteed to be valid until put_page_write() is called.
 * caller can then write to such buffer.
 *
 * caller calls put_page_write() to actually start the asynchronous cache
 * write transfer. the callback could be called even before this function
 * returns [this is guaranteed to happen for this pmem_api implementation].
 *
 * before the callback is called, the data context described in dbi_data
 * is released.
 */
void pmem_data_put_page_write_mem(struct bittern_cache *bc,
				  struct cache_block *cache_block,
				  struct pmem_context *pmem_ctx,
				  void *callback_context,
				  pmem_callback_t callback_function,
				  enum cache_state metadata_update_state)
{
	uint64_t start_timer = current_kernel_time_nsec();
	struct pmem_api *pa = &bc->bc_papi;
	struct data_buffer_info *dbi_data;
	struct async_context *ctx;
	unsigned int block_id;

	M_ASSERT(pmem_ctx != NULL);
	M_ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	dbi_data = &pmem_ctx->dbi;
	ctx = &pmem_ctx->async_ctx;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(pa->papi_hdr.lm_cache_blocks != 0);
	ASSERT(cache_block->bcb_state != S_INVALID);
	ASSERT(callback_context != NULL);
	ASSERT(callback_function != NULL);

	ASSERT_PMEM_DBI(dbi_data);

	block_id = cache_block->bcb_block_id;

	ASSERT(metadata_update_state == S_CLEAN
	       || metadata_update_state == S_DIRTY);
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "metadata_update_state=%d(%s)", metadata_update_state,
		     cache_state_to_str(metadata_update_state));

	ASSERT(dbi_data->di_buffer_vmalloc_buffer != NULL);
	ASSERT(dbi_data->di_buffer_vmalloc_page != NULL);

	atomic_dec(&pa->papi_stats.data_get_put_page_pending_count);
	atomic_inc(&pa->papi_stats.data_put_page_write_count);
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "data_get_put_page_pending_count=%u",
		     atomic_read(&pa->papi_stats.
				 data_get_put_page_pending_count));

	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "di_buffer_vmalloc=%p, di_buffer=%p, di_page=%p, di_flags=0x%x, callback_context=%p, callback_function=%p",
		     dbi_data->di_buffer_vmalloc_buffer,
		     dbi_data->di_buffer,
		     dbi_data->di_page,
		     dbi_data->di_flags, callback_context, callback_function);
	/*
	 * regular transfer
	 */
	ASSERT(dbi_data->di_buffer != NULL);
	ASSERT(dbi_data->di_page != NULL);
	ASSERT((dbi_data->di_flags & CACHE_DI_FLAGS_PMEM_WRITE) != 0);

	BT_DEV_TRACE(BT_LEVEL_TRACE2, bc, NULL, cache_block, NULL, NULL,
		     "dbi_data=%p", dbi_data);

	/*
	 * update metadata
	 */
	{
		uint64_t to_offset, end_offset;
		void *to_buffer;
		long dax_pfn, dax_ret;
		struct pmem_block_metadata *pmbm = &pmem_ctx->pmbm;

		ASSERT(is_sector_number_valid(cache_block->bcb_sector));
		memset(pmbm, 0, sizeof(*pmbm));
		pmbm->pmbm_magic = MCBM_MAGIC;
		pmbm->pmbm_block_id = block_id;
		pmbm->pmbm_status = metadata_update_state;
		pmbm->pmbm_device_sector = cache_block->bcb_sector;
		pmbm->pmbm_xid = cache_block->bcb_xid;
		pmbm->pmbm_hash_data = cache_block->bcb_hash_data;
		pmbm->pmbm_hash_metadata = murmurhash3_128(pmbm,
					   PMEM_BLOCK_METADATA_HASHING_SIZE);

		to_offset = __cache_block_id_2_metadata_pmem_offset(bc,
								    block_id);
		end_offset = to_offset + sizeof(*pmbm) - 1;
		M_ASSERT((end_offset & PAGE_MASK) == (to_offset & PAGE_MASK));

		dax_ret = bdev_direct_access(
			pa->papi_bdev,
			(to_offset & PAGE_MASK) / SECTOR_SIZE,
			&to_buffer, &dax_pfn, PAGE_SIZE);
		M_ASSERT_FIXME(dax_ret == PAGE_SIZE);

		BT_DEV_TRACE(BT_LEVEL_TRACE2, bc, NULL, cache_block, NULL, NULL,
			     "to_offset=%llu, dax_addr=%p, dax_pfn=%ld",
			     to_offset, &to_buffer, dax_pfn);

		memcpy_nt(to_buffer + (to_offset % PAGE_SIZE),
			  pmbm,
			  sizeof(struct pmem_block_metadata));

		cache_timer_add(&pa->papi_stats.
					metadata_write_async_timer,
					start_timer);
		atomic_inc(&pa->papi_stats.metadata_write_async_count);
	}

	BT_DEV_TRACE(BT_LEVEL_TRACE2, bc, NULL, cache_block, NULL, NULL,
		     "before clear page, dbi_data=%p", dbi_data);

	pmem_clear_dbi(dbi_data);

	BT_DEV_TRACE(BT_LEVEL_TRACE2, bc, NULL, cache_block, NULL, NULL,
		     "after clear page");

	/*
	 * call callback directly, as there is no async thread to do it.
	 */
	(*callback_function)(bc,
			     cache_block,
			     pmem_ctx,
			     callback_context,
			     0);

	cache_timer_add(&pa->papi_stats.data_put_page_write_async_timer,
				start_timer);

	cache_timer_add(&pa->papi_stats.data_put_page_write_timer, start_timer);
}

const struct cache_papi_interface cache_papi_mem = {
	"mem",
	false,
	CACHE_LAYOUT_SEQUENTIAL,
	pmem_allocate_papi_mem,
	pmem_deallocate_papi_mem,
	pmem_read_sync_mem,
	pmem_write_sync_mem,
	pmem_metadata_async_write_mem,
	pmem_data_get_page_read_mem,
	pmem_data_put_page_read_mem,
	pmem_data_convert_read_to_write_mem,
	pmem_data_clone_read_to_write_mem,
	pmem_data_get_page_write_mem,
	pmem_data_put_page_write_mem,
	PAPI_MAGIC,
};
