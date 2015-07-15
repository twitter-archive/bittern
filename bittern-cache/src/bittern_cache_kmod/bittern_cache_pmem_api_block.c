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

#define PMEM_BLOCKDEV_ASYNC_CONTEXT_MAGIC1	0xf10c7c31

/*
 * pmem allocate/deallocate functions
 */
int pmem_allocate_papi_block(struct bittern_cache *bc,
			     struct block_device *blockdev)
{
	struct pmem_api *pa = &bc->bc_papi;
	size_t blockdev_size_bytes;

	printk_info("%s: partition: %p\n", bc->bc_name, blockdev->bd_part);
	M_ASSERT(blockdev->bd_part != NULL);
	printk_info("%s: device has %lu sectors\n",
		    bc->bc_name,
		    blockdev->bd_part->nr_sects);
	blockdev_size_bytes = blockdev->bd_part->nr_sects * SECTOR_SIZE;

	printk_info("%s: device size %lu(%lumb)\n",
		    bc->bc_name,
		    blockdev_size_bytes,
		    blockdev_size_bytes / (1024 * 1024));

	pa->papi_bdev = blockdev;
	pa->papi_bdev_size_bytes = blockdev_size_bytes;
	pa->papi_bdev_actual_size_bytes = blockdev_size_bytes;

	printk_info("%s: initializing workqueue\n", bc->bc_name);
	/*
	 * TODO:
	 * these alloc_workqueue params are the same as create_workqueue().
	 * Should play with (WQ_UNBOUND, WQ_RECLAIM, WQ_HIGHPRI) and count.
	 * (testing with WQ_HIGHPRI set shows perf degradation of about 7%).
	 * NOTE we are no longer using WQ_SYSFS, as the namespace is not unique.
	 */
	pa->papi_make_request_wq = alloc_workqueue("b_wkq_blk:%s",
						   WQ_MEM_RECLAIM,
						   1,
						   bc->bc_name);
	/*TODO_ADD_ERROR_INJECTION*/
	if (pa->papi_make_request_wq == NULL) {
		printk_err("%s: alloc workqueue failed\n", bc->bc_name);
		return -ENOMEM;
	}

	return 0;
}

void pmem_deallocate_papi_block(struct bittern_cache *bc)
{
	printk_info("%s: bc->bc_papi.papi_bdev=%p\n",
		    bc->bc_name,
		    bc->bc_papi.papi_bdev);

	printk_info("%s: flushing make_request workqueue\n", bc->bc_name);
	M_ASSERT(bc->bc_papi.papi_make_request_wq != NULL);
	flush_workqueue(bc->bc_papi.papi_make_request_wq);
	printk_info("%s: destroying make_request workqueue\n", bc->bc_name);
	destroy_workqueue(bc->bc_papi.papi_make_request_wq);
}

/*
 * bio private context for pmem_read_sync_block() and
 * pmem_write_sync_block()
 */
#define PMEM_RW_PAPI_CTX_MAGIC 0xf10c8a91
struct pmem_rw_sync_block_ctx {
	int papi_ctx_magic;
	struct semaphore papi_ctx_sema;
	int papi_ctx_err;
	struct bio *papi_ctx_bio;
};

/*
 * bio endio callback for pmem_read_sync_block() and
 * pmem_write_sync_block()
 */
void pmem_rw_sync_block_endio(struct bio *bio, int err)
{
	struct pmem_rw_sync_block_ctx *ctx = bio->bi_private;

	M_ASSERT(ctx->papi_ctx_magic == PMEM_RW_PAPI_CTX_MAGIC);
	ASSERT(bio == ctx->papi_ctx_bio);
	ctx->papi_ctx_err = err;
	up(&ctx->papi_ctx_sema);
	bio_put(bio);
}

/*
 * sync read from cache.
 * this API does double buffering.
 */
int pmem_read_sync_block(struct bittern_cache *bc,
			 uint64_t from_pmem_offset,
			 void *to_buffer,
			 size_t size)
{
	int ret;
	struct pmem_rw_sync_block_ctx *ctx = NULL;
	void *buffer_vaddr = NULL;
	struct page *buffer_page;
	uint64_t ts_started;
	struct pmem_api *pa = &bc->bc_papi;
	struct bio *bio;

	ASSERT(bc != NULL);
	ASSERT(size > 0 && size <= PAGE_SIZE);
	ASSERT((from_pmem_offset % PAGE_SIZE) == 0);
	ASSERT(pa->papi_bdev != NULL);
	ASSERT(size + PAGE_SIZE <= pa->papi_bdev_size_bytes);

	BT_DEV_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL,
		     "from_pmem_offset=%llu, to_buffer=%p, size=%lu",
		     from_pmem_offset, to_buffer, size);

	ts_started = current_kernel_time_nsec();
	if (size == PAGE_SIZE) {
		atomic_inc(&pa->papi_stats.pmem_read_4k_count);
		atomic_inc(&pa->papi_stats.pmem_read_4k_pending);
	} else {
		atomic_inc(&pa->papi_stats.pmem_read_not4k_count);
		atomic_inc(&pa->papi_stats.pmem_read_not4k_pending);
	}

	/*
	 * requester doesn't necessarily have a page aligned buffer (or size),
	 * so we need to use an intermediate buffer
	 */
	buffer_vaddr = kmem_cache_alloc(bc->bc_kmem_map, GFP_NOIO);
	/*TODO_ADD_ERROR_INJECTION*/
	if (buffer_vaddr == NULL) {
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "cannot allocate_buffer_vaddr");
		printk_err("%s: cannot allocate buffer_vaddr\n", bc->bc_name);
		ret = -ENOMEM;
		goto done;
	}
	ASSERT(PAGE_ALIGNED(buffer_vaddr));
	buffer_page = virtual_to_page(buffer_vaddr);
	M_ASSERT(buffer_page != NULL);

	/*
	 * setup bio context, alloc bio and start io
	 * */
	ctx = kmem_alloc(sizeof(struct pmem_rw_sync_block_ctx), GFP_NOIO);
	/*TODO_ADD_ERROR_INJECTION*/
	if (ctx == NULL) {
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "cannot allocate synchronous context");
		printk_err("%s: cannot allocate synchronous context\n",
			   bc->bc_name);
		ret = -ENOMEM;
		goto done;
	}
	ctx->papi_ctx_magic = PMEM_RW_PAPI_CTX_MAGIC;
	sema_init(&ctx->papi_ctx_sema, 0);

	bio = bio_alloc(GFP_NOIO, 1);
	/*TODO_ADD_ERROR_INJECTION*/
	if (bio == NULL) {
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "cannot allocate bio struct");
		printk_err("%s: cannot allocate bio struct\n", bc->bc_name);
		ret = -ENOMEM;
		goto done;
	}
	ctx->papi_ctx_bio = bio;
	bio_set_data_dir_read(bio);
	bio->bi_iter.bi_idx = 0;
	bio->bi_iter.bi_sector = from_pmem_offset / SECTOR_SIZE;
	bio->bi_iter.bi_size = PAGE_SIZE;
	bio->bi_bdev = pa->papi_bdev;
	bio->bi_end_io = pmem_rw_sync_block_endio;
	bio->bi_private = (void *)ctx;
	bio->bi_io_vec[0].bv_page = buffer_page;
	bio->bi_io_vec[0].bv_len = PAGE_SIZE;
	bio->bi_io_vec[0].bv_offset = 0;
	bio->bi_vcnt = 1;

	generic_make_request(bio);

	/*
	 * wait completion
	 */
	down(&ctx->papi_ctx_sema);

	ret = ctx->papi_ctx_err;
	BT_DEV_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL,
		     "from_pmem_offset=%llu, to_buffer=%p, size=%lu: ret=%d",
		     from_pmem_offset, to_buffer, size, ret);

	memcpy(to_buffer, buffer_vaddr, size);

done:
	if (ctx != NULL)
		kmem_free(ctx, sizeof(struct pmem_rw_sync_block_ctx));

	if (buffer_vaddr != NULL)
		kmem_cache_free(bc->bc_kmem_map, buffer_vaddr);

	if (size == PAGE_SIZE) {
		atomic_dec(&pa->papi_stats.pmem_read_4k_pending);
		cache_timer_add(&pa->papi_stats.pmem_read_4k_timer, ts_started);
	} else {
		atomic_dec(&pa->papi_stats.pmem_read_not4k_pending);
		cache_timer_add(&pa->papi_stats.pmem_read_not4k_timer,
				ts_started);
	}
	return ret;
}

/*
 * sync write to cache.
 * this API does double buffering.
 */
int pmem_write_sync_block(struct bittern_cache *bc,
			  uint64_t to_pmem_offset,
			  void *from_buffer,
			  size_t size)
{
	int ret;
	struct pmem_rw_sync_block_ctx *ctx = NULL;
	void *buffer_vaddr = NULL;
	struct page *buffer_page;
	uint64_t ts_started;
	struct pmem_api *pa = &bc->bc_papi;
	struct bio *bio;

	ASSERT(bc != NULL);
	ASSERT(size > 0 && size <= PAGE_SIZE);
	ASSERT((to_pmem_offset % PAGE_SIZE) == 0);
	ASSERT(pa->papi_bdev != NULL);
	ASSERT(size + PAGE_SIZE <= pa->papi_bdev_size_bytes);

	BT_DEV_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL,
		     "to_pmem_offset=%llu, from_buffer=%p, size=%lu",
		     to_pmem_offset, from_buffer, size);

	ts_started = current_kernel_time_nsec();
	if (size == PAGE_SIZE) {
		atomic_inc(&pa->papi_stats.pmem_write_4k_count);
		atomic_inc(&pa->papi_stats.pmem_write_4k_pending);
	} else {
		atomic_inc(&pa->papi_stats.pmem_write_not4k_count);
		atomic_inc(&pa->papi_stats.pmem_write_not4k_pending);
	}

	/*
	 * requester doesn't necessarily have a page aligned buffer (or size),
	 * so we need to use an intermediate buffer
	 */
	buffer_vaddr = kmem_cache_alloc(bc->bc_kmem_map, GFP_NOIO);
	/*TODO_ADD_ERROR_INJECTION*/
	if (buffer_vaddr == NULL) {
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "cannot allocate_buffer_vaddr");
		printk_err("%s: cannot allocate buffer_vaddr\n", bc->bc_name);
		ret = -ENOMEM;
		goto done;
	}
	ASSERT(PAGE_ALIGNED(buffer_vaddr));
	buffer_page = virtual_to_page(buffer_vaddr);
	M_ASSERT(buffer_page != NULL);

	if (size < PAGE_SIZE) {
		/*
		 * this is going to cache, so zero out
		 * to prevent information leak
		 */
		memset(buffer_vaddr, 0, PAGE_SIZE);
	}

	memcpy(buffer_vaddr, from_buffer, size);

	/*
	 * setup bio context, alloc bio and start io
	 * */
	ctx = kmem_alloc(sizeof(struct pmem_rw_sync_block_ctx), GFP_NOIO);
	/*TODO_ADD_ERROR_INJECTION*/
	if (ctx == NULL) {
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "cannot allocate synchronous context");
		printk_err("%s: cannot allocate synchronous context\n",
			   bc->bc_name);
		ret = -ENOMEM;
		goto done;
	}
	ctx->papi_ctx_magic = PMEM_RW_PAPI_CTX_MAGIC;
	sema_init(&ctx->papi_ctx_sema, 0);

	bio = bio_alloc(GFP_NOIO, 1);
	/*TODO_ADD_ERROR_INJECTION*/
	if (bio == NULL) {
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "cannot allocate bio struct");
		printk_err("%s: cannot allocate bio struct\n", bc->bc_name);
		ret = -ENOMEM;
		goto done;
	}
	ctx->papi_ctx_bio = bio;
	bio_set_data_dir_write(bio);
	bio->bi_iter.bi_idx = 0;
	bio->bi_iter.bi_sector = to_pmem_offset / SECTOR_SIZE;
	bio->bi_iter.bi_size = PAGE_SIZE;
	bio->bi_bdev = pa->papi_bdev;
	bio->bi_end_io = pmem_rw_sync_block_endio;
	bio->bi_private = (void *)ctx;
	bio->bi_io_vec[0].bv_page = buffer_page;
	bio->bi_io_vec[0].bv_len = PAGE_SIZE;
	bio->bi_io_vec[0].bv_offset = 0;
	bio->bi_vcnt = 1;

	generic_make_request(bio);

	/*
	 * wait completion
	 */
	down(&ctx->papi_ctx_sema);

	ret = ctx->papi_ctx_err;
	BT_DEV_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL,
		     "to_pmem_offset=%llu, from_buffer=%p, size=%lu: ret=%d",
		     to_pmem_offset, from_buffer, size, ret);

done:
	if (ctx != NULL)
		kmem_free(ctx, sizeof(struct pmem_rw_sync_block_ctx));

	if (buffer_vaddr != NULL)
		kmem_cache_free(bc->bc_kmem_map, buffer_vaddr);

	if (size == PAGE_SIZE) {
		atomic_dec(&pa->papi_stats.pmem_write_4k_pending);
		cache_timer_add(&pa->papi_stats.pmem_write_4k_timer,
				ts_started);
	} else {
		atomic_dec(&pa->papi_stats.pmem_write_not4k_pending);
		cache_timer_add(&pa->papi_stats.pmem_write_not4k_timer,
				ts_started);
	}
	return ret;
}

static void pmem_do_make_request_block_endbio(struct bio *bio, int err)
{
	struct pmem_context *pmem_ctx;
	struct async_context *ctx;

	pmem_ctx = (struct pmem_context *)bio->bi_private;
	M_ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	ctx = &pmem_ctx->async_ctx;

	M_ASSERT(ctx->ma_magic1 == ASYNC_CONTEXT_MAGIC1);
	ASSERT(ctx->ma_magic2 == ASYNC_CONTEXT_MAGIC2);
	ASSERT(ctx->ma_bio == bio);
	bio_put(bio);

	M_ASSERT(pmem_ctx->ctx_endio != NULL);
	(*pmem_ctx->ctx_endio)(pmem_ctx, err);
}

/*! allocate bio and make the request */
void pmem_do_make_request_block(struct bittern_cache *bc,
				struct pmem_context *pmem_ctx)
{
	struct async_context *ctx = &pmem_ctx->async_ctx;
	struct data_buffer_info *dbi_data = &pmem_ctx->dbi;
	struct pmem_api *pa;
	struct bio *bio;

	ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	ASSERT(ctx->ma_magic1 == ASYNC_CONTEXT_MAGIC1);
	ASSERT(ctx->ma_magic2 == ASYNC_CONTEXT_MAGIC2);

	ASSERT(bc == ctx->ma_bc);
	ASSERT_BITTERN_CACHE(bc);
	pa = &bc->bc_papi;

	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		     "in_irq=%lu, in_softirq=%lu, bc=%p, pmem_ctx=%p, work=%p",
		     in_irq(),
		     in_softirq(),
		     bc,
		     pmem_ctx,
		     &ctx->ma_work);

	ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	ASSERT(ctx->ma_magic1 == ASYNC_CONTEXT_MAGIC1);
	ASSERT(ctx->ma_magic2 == ASYNC_CONTEXT_MAGIC2);
	ASSERT_BITTERN_CACHE(bc);
	M_ASSERT(!in_irq());
	M_ASSERT(!in_softirq());

	bio = bio_alloc(GFP_NOIO, 1);
	/*TODO_ADD_ERROR_INJECTION*/
	if (bio == NULL) {
		printk_err("%s: failed to allocate bio struct\n", bc->bc_name);
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "failed to allocate bio struct");
		/*
		 * Allocation failed, bubble up the error.
		 */
		(*pmem_ctx->ctx_endio)(pmem_ctx, -ENOMEM);
		return;
	}

	ctx->ma_bio = bio;
	if (pmem_ctx->bi_datadir == WRITE)
		bio_set_data_dir_write(bio);
	else
		bio_set_data_dir_read(bio);
	bio->bi_iter.bi_idx = 0;
	bio->bi_iter.bi_sector = pmem_ctx->bi_sector;
	bio->bi_iter.bi_size = PAGE_SIZE;
	bio->bi_bdev = pa->papi_bdev;
	ASSERT(pmem_ctx->ctx_endio != NULL);
	bio->bi_end_io = pmem_do_make_request_block_endbio;

	bio->bi_private = (void *)pmem_ctx;
	bio->bi_io_vec[0].bv_page = dbi_data->di_page;
	bio->bi_io_vec[0].bv_len = PAGE_SIZE;
	bio->bi_io_vec[0].bv_offset = 0;
	bio->bi_vcnt = 1;

	generic_make_request(bio);
}

void pmem_make_request_worker_block(struct work_struct *work)
{
	struct async_context *ctx;
	struct pmem_context *pmem_ctx;
	struct bittern_cache *bc;
	struct pmem_api *pa;

	ctx = container_of(work, struct async_context, ma_work);
	ASSERT(ctx->ma_magic1 == ASYNC_CONTEXT_MAGIC1);
	ASSERT(ctx->ma_magic2 == ASYNC_CONTEXT_MAGIC2);
	pmem_ctx = container_of(ctx, struct pmem_context, async_ctx);
	ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	bc = ctx->ma_bc;
	ASSERT_BITTERN_CACHE(bc);
	pa = &bc->bc_papi;

	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		     "in_irq=%lu, in_softirq=%lu, bc=%p, pmem_ctx=%p, work=%p",
		     in_irq(),
		     in_softirq(),
		     bc,
		     pmem_ctx,
		     &ctx->ma_work);

	cache_timer_add(&pa->papi_stats.pmem_make_req_wq_timer,
			pmem_ctx->bi_started);

	pmem_do_make_request_block(bc, pmem_ctx);
}

/*!
 * This function indirectly calls generic_make_request. Because call to
 * generic_make_request() cannot be done in softirq, we defer it to a
 * work_queue in such case.
 */
void pmem_make_request_defer_block(struct bittern_cache *bc,
				   struct pmem_context *pmem_ctx)
{
	struct async_context *ctx = &pmem_ctx->async_ctx;
	struct pmem_api *pa = &bc->bc_papi;
	int ret;

	ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	ASSERT(ctx->ma_magic1 == ASYNC_CONTEXT_MAGIC1);
	ASSERT(ctx->ma_magic2 == ASYNC_CONTEXT_MAGIC2);
	ASSERT_BITTERN_CACHE(bc);

	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		     "in_irq=%lu, in_softirq=%lu, bc=%p, pmem_ctx=%p, work=%p",
		     in_irq(),
		     in_softirq(),
		     bc,
		     pmem_ctx,
		     &ctx->ma_work);

	atomic_inc(&pa->papi_stats.pmem_make_req_wq_count);
	pmem_ctx->bi_started = current_kernel_time_nsec();

	/* defer to worker thread, which will start io */
	INIT_WORK(&ctx->ma_work, pmem_make_request_worker_block);
	ret = queue_work(pa->papi_make_request_wq, &ctx->ma_work);
	M_ASSERT(ret == 1);
}

static void pmem_metadata_async_write_endio(struct pmem_context *pmem_ctx,
					    int err)
{
	struct async_context *ctx;
	struct bittern_cache *bc;
	struct cache_block *cache_block;
	struct data_buffer_info *dbi_data;
	void *f_callback_context;
	pmem_callback_t f_callback_function;
	struct pmem_api *pa;

	M_ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	ctx = &pmem_ctx->async_ctx;
	dbi_data = &pmem_ctx->dbi;

	M_ASSERT(ctx->ma_magic1 == ASYNC_CONTEXT_MAGIC1);
	ASSERT(ctx->ma_magic2 == ASYNC_CONTEXT_MAGIC2);
	bc = ctx->ma_bc;
	pa = &bc->bc_papi;
	ASSERT(pa->papi_bdev != NULL);
	cache_block = ctx->ma_cache_block;
	f_callback_context = ctx->ma_callback_context;
	f_callback_function = ctx->ma_callback_function;
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		     "callback_context=%p, callback_function=%p, ma_metadata_state=%d, err=%d",
		     f_callback_context,
		     f_callback_function, ctx->ma_metadata_state, err);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(dbi_data != NULL);
	ASSERT(dbi_data->di_flags ==
	       (CACHE_DI_FLAGS_DOUBLE_BUFFERING |
		CACHE_DI_FLAGS_PMEM_WRITE));
	ASSERT(f_callback_function != NULL);
	ASSERT(f_callback_context != NULL);
	ASSERT_PMEM_DBI_DOUBLE_BUFFERING(dbi_data);

	cache_timer_add(&pa->papi_stats.metadata_write_async_timer,
			ctx->ma_start_timer);

	pmem_clear_dbi_double_buffering(dbi_data);

	/*
	 * just call the higher level callback
	 */
	(*f_callback_function)(bc,
			       cache_block,
			       pmem_ctx,
			       f_callback_context,
			       err);
}

void pmem_metadata_async_write_block(struct bittern_cache *bc,
				     struct cache_block *cache_block,
				     struct pmem_context *pmem_ctx,
				     void *callback_context,
				     pmem_callback_t callback_function,
				     enum cache_state metadata_update_state)
{
	struct pmem_block_metadata *pmbm;
	off_t to_pmem_offset;
	struct pmem_api *pa = &bc->bc_papi;
	struct data_buffer_info *dbi_data;
	struct async_context *ctx;
	unsigned int block_id;

	M_ASSERT(pmem_ctx != NULL);
	M_ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	dbi_data = &pmem_ctx->dbi;
	ctx = &pmem_ctx->async_ctx;

	ASSERT(pa->papi_bdev != NULL);
	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(pa->papi_hdr.lm_cache_blocks != 0);
	ASSERT(cache_block->bcb_state != S_INVALID);
	ASSERT(dbi_data != NULL);
	ASSERT(callback_context != NULL);
	ASSERT(callback_function != NULL);

	block_id = cache_block->bcb_block_id;

	atomic_inc(&pa->papi_stats.metadata_write_async_count);

	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "metadata_update_state=%d", metadata_update_state);
	ASSERT(metadata_update_state == S_INVALID ||
	       metadata_update_state == S_CLEAN ||
	       metadata_update_state == S_DIRTY);

	/* required because we use the page to hold the metadata buffer */
	ASSERT(dbi_data->di_buffer_vmalloc_buffer != NULL);
	ASSERT(PAGE_ALIGNED(dbi_data->di_buffer_vmalloc_buffer));
	ASSERT(dbi_data->di_buffer == NULL);
	ASSERT(dbi_data->di_page == NULL);

	pmem_set_dbi_double_buffering(dbi_data,
				      CACHE_DI_FLAGS_DOUBLE_BUFFERING |
				      CACHE_DI_FLAGS_PMEM_WRITE);
	ASSERT(dbi_data->di_buffer != NULL);
	ASSERT(dbi_data->di_page != NULL);

	ASSERT(is_sector_number_valid(cache_block->bcb_sector));
	ASSERT(pa->papi_hdr.lm_mcb_size_bytes == PAGE_SIZE);

	/* zero out the whole buffer to prevent information leak */
	memset(dbi_data->di_buffer, 0, PAGE_SIZE);

	pmbm = (struct pmem_block_metadata *)(dbi_data->di_buffer);
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

	/*
	 * setup context descriptor and start async transfer.
	 */
	ASSERT(ctx != NULL);
	ctx->ma_magic1 = ASYNC_CONTEXT_MAGIC1;
	ctx->ma_magic2 = ASYNC_CONTEXT_MAGIC2;
	ctx->ma_bc = bc;
	ctx->ma_cache_block = cache_block;
	ctx->ma_callback_context = callback_context;
	ctx->ma_callback_function = callback_function;
	ctx->ma_datadir = WRITE;
	ctx->ma_start_timer = current_kernel_time_nsec();
	ctx->ma_metadata_state = metadata_update_state;

	to_pmem_offset = __cache_block_id_2_metadata_pmem_offset(bc, block_id);

	/*
	 * defer request to a worker thread
	 */
	pmem_ctx->bi_datadir = WRITE;
	pmem_ctx->bi_sector = to_pmem_offset / SECTOR_SIZE;
	pmem_ctx->ctx_endio = pmem_metadata_async_write_endio;
	pmem_make_request_defer_block(bc, pmem_ctx);
}

/*
 * endio function for pmem_data_get_page_read()
 */
static void pmem_data_get_page_read_endio(struct pmem_context *pmem_ctx,
					  int err)
{
	struct async_context *ctx;
	struct bittern_cache *bc;
	struct cache_block *cache_block;
	struct data_buffer_info *dbi_data;
	void *f_callback_context;
	pmem_callback_t f_callback_function;
	struct pmem_api *pa;

	M_ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	ctx = &pmem_ctx->async_ctx;
	dbi_data = &pmem_ctx->dbi;

	M_ASSERT(ctx->ma_magic1 == ASYNC_CONTEXT_MAGIC1);
	ASSERT(ctx->ma_magic2 == ASYNC_CONTEXT_MAGIC2);
	bc = ctx->ma_bc;
	pa = &bc->bc_papi;
	ASSERT(pa->papi_bdev != NULL);
	cache_block = ctx->ma_cache_block;
	f_callback_context = ctx->ma_callback_context;
	f_callback_function = ctx->ma_callback_function;
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		     "callback_context=%p, callback_function=%p, err=%d",
		     f_callback_context, f_callback_function, err);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(dbi_data->di_flags ==
	       (CACHE_DI_FLAGS_DOUBLE_BUFFERING |
		CACHE_DI_FLAGS_PMEM_READ));
	ASSERT(f_callback_function != NULL);
	ASSERT(f_callback_context != NULL);
	ASSERT_PMEM_DBI(dbi_data);

	cache_timer_add(&pa->papi_stats.data_get_page_read_async_timer,
			ctx->ma_start_timer);

	/*
	 * just call the higher level callback
	 */
	(*f_callback_function)(bc,
			       cache_block,
			       pmem_ctx,
			       f_callback_context,
			       err);
}

/*
 * async read accessors (get_page_read()/put_page_read())
 *
 * caller calls get_page_read() to setup the necessary context and to start
 * an asynchronous cache read transfer. when the asynchronous transfer is done
 * and the data is available in dbi_data, get_page_read_done() callback is
 * called.
 * the callback could be called even before this function returns [this is
 * guaranteed to happen for mem type implementations].
 * the data pointed by dbi_data is guaranteed to be valid until put_page_read()
 * is called.
 *
 * caller must call put_page_read() when it's done in accessing the data page
 * and to release the data context in dbi_data.
 * caller must not modify the data with the get_page_read()/put_page_read() APIs
 *
 */
void pmem_data_get_page_read_block(struct bittern_cache *bc,
				   struct cache_block *cache_block,
				   struct pmem_context *pmem_ctx,
				   void *callback_context,
				   pmem_callback_t callback_function)
{
	uint64_t ts_started;
	off_t from_pmem_offset;
	struct pmem_api *pa = &bc->bc_papi;
	unsigned int block_id;
	struct data_buffer_info *dbi_data;
	struct async_context *ctx;

	ASSERT(pmem_ctx != NULL);
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
	ASSERT(ctx != NULL);
	ASSERT(callback_context != NULL);
	ASSERT(callback_function != NULL);

	ts_started = current_kernel_time_nsec();

	block_id = cache_block->bcb_block_id;

	atomic_inc(&pa->papi_stats.data_get_page_read_count);
	atomic_inc(&pa->papi_stats.data_get_put_page_pending_count);
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		"data_get_put_page_pending_count=%u",
		atomic_read(&pa->papi_stats.data_get_put_page_pending_count));

	/*
	 * setup context descriptor and start async transfer
	 */
	pmem_set_dbi_double_buffering(dbi_data,
				      CACHE_DI_FLAGS_DOUBLE_BUFFERING |
				      CACHE_DI_FLAGS_PMEM_READ);

	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "di_buffer_vmalloc=%p, di_buffer=%p, di_page=%p, di_flags=0x%x, callback_context=%p, callback_function=%p",
		     dbi_data->di_buffer_vmalloc_buffer,
		     dbi_data->di_buffer,
		     dbi_data->di_page,
		     dbi_data->di_flags, callback_context, callback_function);

	/*
	 * start async transfer
	 */
	ASSERT(ctx != NULL);
	ctx->ma_magic1 = ASYNC_CONTEXT_MAGIC1;
	ctx->ma_magic2 = ASYNC_CONTEXT_MAGIC2;
	ctx->ma_bc = bc;
	ctx->ma_cache_block = cache_block;
	ctx->ma_callback_context = callback_context;
	ctx->ma_callback_function = callback_function;
	ctx->ma_datadir = READ;
	ctx->ma_start_timer = ts_started;

	from_pmem_offset = __cache_block_id_2_data_pmem_offset(bc, block_id);

	/*
	 * defer request to a worker thread
	 */
	pmem_ctx->bi_datadir = READ;
	pmem_ctx->bi_sector = from_pmem_offset / SECTOR_SIZE;
	pmem_ctx->ctx_endio = pmem_data_get_page_read_endio;
	pmem_make_request_defer_block(bc, pmem_ctx);

	cache_timer_add(&pa->papi_stats.data_get_page_read_timer, ts_started);
}

/* put_page_read */
/*
 * async read accessors (get_page_read()/put_page_read())
 *
 * caller calls get_page_read() to setup the necessary context and to start an
 * asynchronous cache read transfer.
 * when the asynchronous transfer is done and the data is available in dbi_data,
 * get_page_read_done() callback is called. the callback could be called even
 * before this function returns [this is guaranteed for pmem_api mem
 * implementations].
 * the data pointed by dbi_data is guaranteed to be valid until put_page_read()
 * is called.
 *
 * caller must call put_page_read() when it's done in accessing the data page
 * and to release the data context in dbi_data.
 */
void pmem_data_put_page_read_block(struct bittern_cache *bc,
				   struct cache_block *cache_block,
				   struct pmem_context *pmem_ctx)
{
	uint64_t ts_started = current_kernel_time_nsec();
	struct pmem_api *pa = &bc->bc_papi;
	unsigned int block_id;
	struct data_buffer_info *dbi_data;
	struct async_context *ctx;

	ASSERT(pmem_ctx != NULL);
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

	atomic_inc(&pa->papi_stats.data_put_page_read_count);
	atomic_dec(&pa->papi_stats.data_get_put_page_pending_count);
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "data_get_put_page_pending_count=%u",
		     atomic_read(&pa->papi_stats.
				 data_get_put_page_pending_count));

	/*
	 * double buffering
	 */
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "di_buffer_vmalloc=%p, di_buffer=%p, di_page=%p, di_flags=0x%x",
		     dbi_data->di_buffer_vmalloc_buffer,
		     dbi_data->di_buffer,
		     dbi_data->di_page, dbi_data->di_flags);
	ASSERT(dbi_data->di_flags ==
	       (CACHE_DI_FLAGS_DOUBLE_BUFFERING |
		CACHE_DI_FLAGS_PMEM_READ));

	pmem_clear_dbi_double_buffering(dbi_data);

	cache_timer_add(&pa->papi_stats.data_put_page_read_timer, ts_started);
}

/*
 * convert page obtained for read to page for write.
 * this is used for rmw cycles.
 */
void pmem_data_convert_read_to_write_block(struct bittern_cache *bc,
					   struct cache_block *cache_block,
					   struct pmem_context *pmem_ctx)
{
	struct pmem_api *pa = &bc->bc_papi;
	unsigned int block_id;
	struct data_buffer_info *dbi_data;
	struct async_context *ctx;

	ASSERT(pmem_ctx != NULL);
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

	atomic_inc(&pa->papi_stats.data_convert_page_read_to_write_count);

	ASSERT_PMEM_DBI(dbi_data);
	ASSERT((dbi_data->di_flags & CACHE_DI_FLAGS_PMEM_READ) != 0);
	ASSERT((dbi_data->di_flags & CACHE_DI_FLAGS_PMEM_WRITE) == 0);

	dbi_data->di_flags |= CACHE_DI_FLAGS_PMEM_WRITE;
}

/*
 * clone read page into a write page,
 * similar to convert except that the data is cloned.
 * this is used for rmw cycles.
 */
void pmem_data_clone_read_to_write_block(struct bittern_cache *bc,
					 struct cache_block *from_cache_block,
					 struct cache_block *to_cache_block,
					 struct pmem_context *pmem_ctx)
{
	struct pmem_api *pa = &bc->bc_papi;
	unsigned int from_block_id;
	unsigned int to_block_id;
	struct data_buffer_info *dbi_data;
	struct async_context *ctx;

	ASSERT(pmem_ctx != NULL);
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
	ASSERT(dbi_data != NULL);

	from_block_id = from_cache_block->bcb_block_id;
	to_block_id = to_cache_block->bcb_block_id;
	ASSERT(to_block_id != from_block_id);

	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		     "di_buffer_vmalloc=%p, di_buffer=%p, di_page=%p, di_flags=0x%x",
		     dbi_data->di_buffer_vmalloc_page,
		     dbi_data->di_buffer,
		     dbi_data->di_page, dbi_data->di_flags);
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, from_cache_block, NULL, NULL,
		     "from_cache_block");
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, to_cache_block, NULL, NULL,
		     "to_cache_block");

	atomic_inc(&pa->papi_stats.
		   data_clone_read_page_to_write_page_count);

	ASSERT_PMEM_DBI(dbi_data);
	ASSERT((dbi_data->di_flags & CACHE_DI_FLAGS_PMEM_READ) != 0);
	ASSERT((dbi_data->di_flags & CACHE_DI_FLAGS_PMEM_WRITE) == 0);

	/*
	 * nothing to do here, except for changing the di_flags state
	 */

	dbi_data->di_flags |= CACHE_DI_FLAGS_PMEM_WRITE;
}

/* get_page_write */
/*
 * async write accessors (get_page_write()/put_page_write())
 *
 * caller calls get_page_write() to setup the necessary context to start an asynchronous cache write transfer.
 * the buffer pointed by dbi_data is guaranteed to be valid until put_page_write() is called.
 * caller can then write to such buffer.
 *
 * caller calls put_page_write() to actually start the asynchronous cache write transfer.
 * the callback could be called even before this function returns [this is guaranteed to happen if the pmem provider
 * implements data transfers with memory copy.
 * before the callback is called, the data context described in dbi_data is released.
 */
void pmem_data_get_page_write_block(struct bittern_cache *bc,
				    struct cache_block *cache_block,
				    struct pmem_context *pmem_ctx)
{
	uint64_t ts_started = current_kernel_time_nsec();
	struct pmem_api *pa = &bc->bc_papi;
	unsigned int block_id;
	struct data_buffer_info *dbi_data;
	struct async_context *ctx;

	ASSERT(pmem_ctx != NULL);
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

	/*
	 * double buffering required
	 */
	pmem_set_dbi_double_buffering(dbi_data,
				      CACHE_DI_FLAGS_DOUBLE_BUFFERING |
				      CACHE_DI_FLAGS_PMEM_WRITE);
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "di_buffer_vmalloc=%p, di_buffer=%p, di_page=%p, di_flags=0x%x",
		     dbi_data->di_buffer_vmalloc_buffer,
		     dbi_data->di_buffer, dbi_data->di_page,
		     dbi_data->di_flags);

	cache_timer_add(&pa->papi_stats.data_get_page_write_timer, ts_started);
}

/*
 * callback function for pmem_data_put_page_write_done()
 * this callback serves the identical purpose as
 * pmem_data_put_page_write_done(), except that it handles
 * metadata write completion.
 */
static
void pmem_data_put_page_write_metadata_endio(struct pmem_context *pmem_ctx,
					     int err)
{
	struct async_context *ctx;
	struct bittern_cache *bc;
	struct cache_block *cache_block;
	struct data_buffer_info *dbi_data;
	void *f_callback_context;
	pmem_callback_t f_callback_function;
	struct pmem_api *pa;

	M_ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	dbi_data = &pmem_ctx->dbi;
	ctx = &pmem_ctx->async_ctx;

	M_ASSERT(ctx->ma_magic1 == ASYNC_CONTEXT_MAGIC1);
	ASSERT(ctx->ma_magic2 == ASYNC_CONTEXT_MAGIC2);
	bc = ctx->ma_bc;
	ASSERT(bc != NULL);
	pa = &bc->bc_papi;
	cache_block = ctx->ma_cache_block;
	ASSERT(cache_block != NULL);
	f_callback_context = ctx->ma_callback_context;
	f_callback_function = ctx->ma_callback_function;
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		     "callback_context=%p, callback_function=%p, ma_metadata_state=%d(%s), err=%d",
		     f_callback_context, f_callback_function,
		     ctx->ma_metadata_state,
		     cache_state_to_str(ctx->ma_metadata_state), err);

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT((dbi_data->di_flags & CACHE_DI_FLAGS_DOUBLE_BUFFERING) != 0);
	ASSERT((dbi_data->di_flags & CACHE_DI_FLAGS_PMEM_WRITE) != 0);
	ASSERT(f_callback_function != NULL);
	ASSERT(f_callback_context != NULL);

	ASSERT_PMEM_DBI_DOUBLE_BUFFERING(dbi_data);

	cache_timer_add(
		&pa->papi_stats.data_put_page_write_async_metadata_timer,
		ctx->ma_start_timer_2);
	cache_timer_add(&pa->papi_stats.data_put_page_write_async_timer,
			ctx->ma_start_timer);

	/*
	 * mark async context as free
	 */
	pmem_clear_dbi(dbi_data);

	/*
	 * just call the higher level callback
	 */
	(*f_callback_function)(bc,
			       cache_block,
			       pmem_ctx,
			       f_callback_context,
			       err);
}

/*
 * callback function for pmem_data_put_page_write()
 * this code is just a simple wrapper, the real work is done in the callback
 * code called from here.
 */
static void pmem_data_put_page_write_endio(struct pmem_context *pmem_ctx,
					   int err)
{
	struct async_context *ctx;
	struct bittern_cache *bc;
	struct cache_block *cache_block;
	struct data_buffer_info *dbi_data;
	struct pmem_api *pa;
	off_t to_pmem_offset;
	uint64_t ts_started;
	struct pmem_block_metadata *pmbm;

	M_ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	dbi_data = &pmem_ctx->dbi;

	ctx = &pmem_ctx->async_ctx;
	M_ASSERT(ctx->ma_magic1 == ASYNC_CONTEXT_MAGIC1);
	ASSERT(ctx->ma_magic2 == ASYNC_CONTEXT_MAGIC2);

	bc = ctx->ma_bc;
	pa = &bc->bc_papi;
	cache_block = ctx->ma_cache_block;
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		     "ma_metadata_state=%d, err=%d",
		     ctx->ma_metadata_state, err);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(dbi_data != NULL);
	ASSERT((dbi_data->di_flags & CACHE_DI_FLAGS_DOUBLE_BUFFERING) != 0);
	ASSERT((dbi_data->di_flags & CACHE_DI_FLAGS_PMEM_WRITE) != 0);
	ASSERT_PMEM_DBI_DOUBLE_BUFFERING(dbi_data);

	if (err != 0) {
		void *f_callback_context;
		pmem_callback_t f_callback_function;

		f_callback_context = ctx->ma_callback_context;
		f_callback_function = ctx->ma_callback_function;
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "callback_context=%p, callback_function=%p, ma_metadata_state=%d(%s), err=%d",
			     f_callback_context,
			     f_callback_function,
			     ctx->ma_metadata_state,
			     cache_state_to_str(ctx->ma_metadata_state),
			     err);
		printk_err("%s: put_page failed err=%d\n", bc->bc_name, err);

		pmem_clear_dbi(dbi_data);

		/*
		 * Bubble up error to higher level callback
		 */
		(*f_callback_function)(bc,
				       cache_block,
				       pmem_ctx,
				       f_callback_context,
				       err);
		return;
	}

	/*
	 * we are done using the vmalloc buffer,
	 * so we can temporarily reuse it for pmbm write buffer
	 */
	pmbm = dbi_data->di_buffer;
	ts_started = current_kernel_time_nsec();

	ASSERT(dbi_data->di_buffer != NULL);
	ASSERT(dbi_data->di_page != NULL);
	ASSERT(pmbm != NULL);
	ASSERT(pa->papi_hdr.lm_mcb_size_bytes == PAGE_SIZE);

	/*
	 * when writing data, it only makes sense to update metadata
	 * to VALID_CLEAN or VALID_DIRTY
	 */
	ASSERT(ctx->ma_metadata_state == S_CLEAN ||
	       ctx->ma_metadata_state == S_DIRTY);
	ASSERT(is_sector_number_valid(cache_block->bcb_sector));

	/* zero out the whole buffer to prevent information leak */
	memset(dbi_data->di_buffer, 0, PAGE_SIZE);

	pmbm->pmbm_magic = MCBM_MAGIC;
	pmbm->pmbm_block_id = cache_block->bcb_block_id;
	pmbm->pmbm_status = ctx->ma_metadata_state;
	pmbm->pmbm_device_sector = cache_block->bcb_sector;
	pmbm->pmbm_xid = cache_block->bcb_xid;
	pmbm->pmbm_hash_data = cache_block->bcb_hash_data;
	pmbm->pmbm_hash_metadata = murmurhash3_128(pmbm,
					PMEM_BLOCK_METADATA_HASHING_SIZE);

	ctx->ma_start_timer_2 = ts_started;
	atomic_inc(&pa->papi_stats.data_put_page_write_metadata_count);

	to_pmem_offset = __cache_block_id_2_metadata_pmem_offset(bc,
						cache_block->bcb_block_id);

	/*
	 * defer request to a worker thread
	 */
	pmem_ctx->bi_datadir = WRITE;
	pmem_ctx->bi_sector = to_pmem_offset / SECTOR_SIZE;
	pmem_ctx->ctx_endio = pmem_data_put_page_write_metadata_endio;
	pmem_make_request_defer_block(bc, pmem_ctx);
}

/* put_page_write */
/*
 * async write accessors (get_page_write()/put_page_write())
 *
 * caller calls get_page_write() to setup the necessary context to start an
 * asynchronous cache write transfer. the buffer pointed by dbi_data is
 * guaranteed to be valid until put_page_write() is called. caller can then
 * write to such buffer.
 *
 * caller calls put_page_write() to actually start the asynchronous cache write
 * transfer. the callback could be called even before this function returns
 * [this is guaranteed to happen for pmem_api mem implementations].
 * before the callback is called, the data context described in dbi_data is
 * released.
 */
void pmem_data_put_page_write_block(struct bittern_cache *bc,
				    struct cache_block *cache_block,
				    struct pmem_context *pmem_ctx,
				    void *callback_context,
				    pmem_callback_t callback_function,
				    enum cache_state metadata_update_state)
{
	uint64_t ts_started = current_kernel_time_nsec();
	off_t to_pmem_offset;
	struct pmem_api *pa = &bc->bc_papi;
	unsigned int block_id;
	struct data_buffer_info *dbi_data;
	struct async_context *ctx;

	ASSERT(pmem_ctx != NULL);
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
	ASSERT(ctx != NULL);
	ASSERT(callback_context != NULL);
	ASSERT(callback_function != NULL);
	ASSERT_PMEM_DBI(dbi_data);

	ASSERT(metadata_update_state == S_CLEAN ||
	       metadata_update_state == S_DIRTY);

	block_id = cache_block->bcb_block_id;

	/*
	 * required because we use the page to hold a temporary copy of the
	 * metadata buffer
	 */
	ASSERT(dbi_data->di_buffer_vmalloc_buffer != NULL);
	ASSERT(dbi_data->di_buffer_vmalloc_page != NULL);

	atomic_dec(&pa->papi_stats.data_get_put_page_pending_count);
	atomic_inc(&pa->papi_stats.data_put_page_write_count);

	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "metadata_update_state=%d", metadata_update_state);
	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		     "di_buffer_vmalloc=%p, di_buffer=%p, di_page=%p, di_flags=0x%x, callback_context=%p, callback_function=%p",
		     dbi_data->di_buffer_vmalloc_buffer,
		     dbi_data->di_buffer,
		     dbi_data->di_page,
		     dbi_data->di_flags, callback_context, callback_function);

	/*
	 * setup context descriptor and start async transfer
	 */
	ASSERT_PMEM_DBI_DOUBLE_BUFFERING(dbi_data);
	ASSERT((dbi_data->di_flags & CACHE_DI_FLAGS_DOUBLE_BUFFERING) != 0);
	ASSERT((dbi_data->di_flags & CACHE_DI_FLAGS_PMEM_WRITE) != 0);
	ASSERT(ctx != NULL);
	ctx->ma_magic1 = ASYNC_CONTEXT_MAGIC1;
	ctx->ma_magic2 = ASYNC_CONTEXT_MAGIC2;
	ctx->ma_bc = bc;
	ctx->ma_cache_block = cache_block;
	ctx->ma_callback_context = callback_context;
	ctx->ma_callback_function = callback_function;
	ctx->ma_datadir = READ;
	ctx->ma_start_timer = ts_started;
	ctx->ma_metadata_state = metadata_update_state;

	to_pmem_offset = __cache_block_id_2_data_pmem_offset(bc, block_id),

	/*
	 * defer request to a worker thread
	 */
	pmem_ctx->bi_datadir = WRITE;
	pmem_ctx->bi_sector = to_pmem_offset / SECTOR_SIZE;
	pmem_ctx->ctx_endio = pmem_data_put_page_write_endio;
	pmem_make_request_defer_block(bc, pmem_ctx);

	cache_timer_add(&pa->papi_stats.data_put_page_write_timer, ts_started);
}

const struct cache_papi_interface cache_papi_block = {
	"block",
	true,
	CACHE_LAYOUT_INTERLEAVED,
	pmem_allocate_papi_block,
	pmem_deallocate_papi_block,
	pmem_read_sync_block,
	pmem_write_sync_block,
	pmem_metadata_async_write_block,
	pmem_data_get_page_read_block,
	pmem_data_put_page_read_block,
	pmem_data_convert_read_to_write_block,
	pmem_data_clone_read_to_write_block,
	pmem_data_get_page_write_block,
	pmem_data_put_page_write_block,
	PAPI_MAGIC,
};
