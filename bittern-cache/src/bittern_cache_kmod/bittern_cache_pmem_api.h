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

#ifndef BITTERN_CACHE_PMEM_API_H
#define BITTERN_CACHE_PMEM_API_H

/*forward*/ struct bittern_cache;
/*forward*/ struct cache_block;
/*forward*/ struct data_buffer_info;
/*forward*/ struct pmem_context;

#include "bittern_cache_pmem_header.h"
#include "bittern_cache_states.h"

/*! allocate PMEM resources */
extern int pmem_allocate(struct bittern_cache *bc,
			 struct block_device *blockdev);

/* deallocate PMEM resources */
extern void pmem_deallocate(struct bittern_cache *bc);

/*! start workqueue for periodic update of pmem header */
extern void pmem_header_update_start_workqueue(struct bittern_cache *bc);

/*! stop workqueue for periodic update of pmem header */
extern void pmem_header_update_stop_workqueue(struct bittern_cache *bc);

/*! returns pmem_api name */
extern const char *pmem_api_name(struct bittern_cache *bc);

/*!
 * Return true if only PAGE_SIZE transfers are supported,
 * false if any arbitrary cache-aligned,cache-multiple transfer is supported.
 */
extern bool pmem_page_size_transfer_only(struct bittern_cache *bc);

/*! returns cache layout */
extern enum cache_layout pmem_cache_layout(struct bittern_cache *bc);

/*!
 * initialize header
 */
extern int pmem_header_initialize(struct bittern_cache *bc);
/*!
 * initialize specified metadata block
 */
extern int pmem_metadata_initialize(struct bittern_cache *bc,
				    unsigned int block_id);

/*!
 * pmem_header restore function.
 */
extern int pmem_header_restore(struct bittern_cache *bc);
/*!
 * pmem_block restore function.
 */
extern int pmem_block_restore(struct bittern_cache *bc,
			      struct cache_block *in_out_cache_block);

/*!
 * synchronously update header
 */
extern int pmem_header_update(struct bittern_cache *bc, int update_both);

/*!
 * callback function prototype
 */
typedef void (*pmem_callback_t)(struct bittern_cache *bc,
				struct cache_block *cache_block,
				struct pmem_context *pmem_ctx,
				void *callback_context,
				int err);

/*!
 * data buffer info holds virtual address and page struct pointer to cache data
 * being transferred.  if the pmem hardware does not support direct dma access
 * and/or direct memory access, then we allocate a page buffer to do double
 * buffering during memory copies.
 */
struct data_buffer_info {
	/*! pointer to kmem buffer, if needed */
	void *di_buffer_vmalloc_buffer;
	/*! pointer to kmem buffer page, if needed */
	struct page *di_buffer_vmalloc_page;
	/*!
	 * buffer pool used to allocate the vmalloc buffer
	 * (valid if vmalloc buffer has been allocated)
	 */
	struct kmem_cache *di_buffer_slab;
	/*! buffer pointer used for PMEM accesses by bittern */
	void *di_buffer;
	/*! page pointer used for PMEM accesses by bittern */
	struct page *di_page;
	/*! pmem flags */
	int di_flags;
	/*! 1 if buffer is in use, 0 otherwise */
	atomic_t di_busy;
};

/*! @defgroup di_flags_bitvalues data_buffer_info di_flags bitmask values
 * @{
 */
/*! doing double buffering (vmalloc buffer is in use) */
#define CACHE_DI_FLAGS_DOUBLE_BUFFERING 0x1
/*! we are reading from PMEM into memory */
#define CACHE_DI_FLAGS_PMEM_READ 0x2
/*! we are writing from memory to PMEM */
#define CACHE_DI_FLAGS_PMEM_WRITE 0x4
/*! we are reading and writing from memory to PMEM and viceversa */
#define CACHE_DI_FLAGS_PMEM_READWRITE (CACHE_DI_FLAGS_PMEM_READ | \
		CACHE_DI_FLAGS_PMEM_WRITE)
/*! @} */

#define __ASSERT_PMEM_DBI(__dbi, __flags) ({				\
	struct data_buffer_info *__db = (__dbi);			\
	ASSERT(__db->di_buffer_vmalloc_buffer != NULL);			\
	ASSERT(PAGE_ALIGNED(__db->di_buffer_vmalloc_buffer));		\
	ASSERT(__db->di_buffer_vmalloc_page != NULL);			\
	ASSERT(__db->di_buffer_vmalloc_page ==				\
	       virtual_to_page(__db->di_buffer_vmalloc_buffer));	\
	ASSERT((__db->di_flags & (__flags)) == (__flags));		\
	if ((__db->di_flags & CACHE_DI_FLAGS_DOUBLE_BUFFERING) != 0) {	\
		ASSERT(__db->di_buffer == __db->di_buffer_vmalloc_buffer); \
		ASSERT(__db->di_page ==	__db->di_buffer_vmalloc_page);	\
	} else {							\
		ASSERT(__db->di_buffer != NULL);			\
		ASSERT(__db->di_page != NULL);				\
	}								\
	ASSERT(__db->di_flags != 0x0);					\
	ASSERT(atomic_read(&__db->di_busy) == 1);			\
})
#define ASSERT_PMEM_DBI(__dbi)						\
		__ASSERT_PMEM_DBI(__dbi, 0x0)
#define ASSERT_PMEM_DBI_DOUBLE_BUFFERING(__dbi)				\
		__ASSERT_PMEM_DBI(__dbi, CACHE_DI_FLAGS_DOUBLE_BUFFERING)

#define ASYNC_CONTEXT_MAGIC1	0xf10c7a71
#define ASYNC_CONTEXT_MAGIC2	0xf10c7a72

/*!
 * The content of this data structure is only used within pmem_api layer.
 * The context is used by the pmem APIs to keep track of async calls.
 */
struct async_context {
	unsigned int ma_magic1;
	struct bio *ma_bio;
	struct work_struct ma_work;
	struct bittern_cache *ma_bc;
	struct cache_block *ma_cache_block;
	void *ma_callback_context;
	pmem_callback_t ma_callback_function;
	uint64_t ma_start_timer;
	uint64_t ma_start_timer_2;
	/*! READ or WRITE */
	int ma_datadir;
	/*! desired metadata update state */
	enum cache_state ma_metadata_state;
	unsigned int ma_magic2;
};

#define PMEM_CONTEXT_MAGIC1 0xf10c2af1
#define PMEM_CONTEXT_MAGIC2 0xf10ca21f
/*!
 * Per-request pmem related context. Higher level code (the cache layer)
 * is responsible for providing and storing this context across the
 * lifecycle for each request. This struct is stored by the caller in
 * @ref work_item.
 * All the code outside pmem_api needs to treat this structure as being
 * completely opaque. The only allowed access outside pmem modules is via
 * the getter functions @ref pmem_context_data_vaddr and
 * @ref pmem_context_data_page .
 */
struct pmem_context {
	int magic1;
	struct async_context async_ctx;
	struct pmem_block_metadata pmbm;
	int magic2;
	struct data_buffer_info dbi;
	/*! bi_datadir is passed as context for make request */
	int bi_datadir;
	/*! bi_sector is passed as context for make request */
	sector_t bi_sector;
	/*! ctx_endio is passed as context for make request */
	void (*ctx_endio)(struct pmem_context *ctx, int err);
	/*! timer */
	uint64_t bi_started;
};

/*!
 * Use this function to access the data virtual address in pmem_context.
 * NOTE: caller must guarantee that buffer has been previously set.
 */
static inline void *pmem_context_data_vaddr(struct pmem_context *ctx)
{
	M_ASSERT(ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	ASSERT_PMEM_DBI(&ctx->dbi);
	ASSERT(ctx->dbi.di_buffer != NULL);
	ASSERT(ctx->dbi.di_page != NULL);
	return ctx->dbi.di_buffer;
}

/*!
 * Use this function to access the data page pointer in pmem_context.
 * NOTE: caller must guarantee that buffer has been previously set.
 */
static inline void *pmem_context_data_page(struct pmem_context *ctx)
{
	M_ASSERT(ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(ctx->magic2 == PMEM_CONTEXT_MAGIC2);
	ASSERT_PMEM_DBI(&ctx->dbi);
	ASSERT(ctx->dbi.di_buffer != NULL);
	ASSERT(ctx->dbi.di_page != NULL);
	return ctx->dbi.di_page;
}

/*! initializes empty pmem context */
extern void pmem_context_initialize(struct pmem_context *ctx);

/*! sets up resources for pmem context */
extern int pmem_context_setup(struct bittern_cache *bc,
			      struct kmem_cache *kmem_slab,
			      struct cache_block *cache_block,
			      struct cache_block *cloned_cache_block,
			      struct pmem_context *ctx);

/*! destroy resources setup with @ref pmem_context_setup */
extern void pmem_context_destroy(struct bittern_cache *bc,
				 struct pmem_context *ctx);

/*!
 * sync read metadata
 */
extern int pmem_metadata_sync_read(struct bittern_cache *bc,
				   struct cache_block *cache_block,
				   struct pmem_block_metadata *out_pmbm_mem);

/*!
 * async metadata update API
 */
extern void pmem_metadata_async_write(struct bittern_cache *bc,
				      struct cache_block *cache_block,
				      struct pmem_context *pmem_ctx,
				      void *callback_context,
				      pmem_callback_t callback_function,
				      enum cache_state metadata_update_state);

/*!
 * async read accessors (get_page_read()/put_page_read())
 *
 * caller calls get_page_read() to setup the necessary context and to start an
 * asynchronous cache read transfer. when the asynchronous transfer is done and
 * the data is available in dbi_data, the get_page_read_done() callback is
 * called.
 * the callback could be called even before this function returns [this is
 * guaranteed to happen if the pmem implements data transfers withmemory copy].
 * the data pointed by dbi_data is guaranteed to be valid until put_page_read()
 * is called.
 *
 * caller must call put_page_read() when it's done in accessing the data page
 * in order to release the data context in dbi_data.
 * caller must not modify the data with the get_page_read()/put_page_read() APIs
 *
 */
extern void pmem_data_get_page_read(struct bittern_cache *bc,
				    struct cache_block *cache_block,
				    struct pmem_context *pmem_ctx,
				    void *callback_context,
				    pmem_callback_t callback_function);
extern void pmem_data_put_page_read(struct bittern_cache *bc,
				    struct cache_block *cache_block,
				    struct pmem_context *pmem_ctx);

/*!
 * convert page obtained for read to page for write.
 * this is used for rmw cycles
 */
extern void pmem_data_convert_read_to_write(struct bittern_cache *bc,
					    struct cache_block *cache_block,
					    struct pmem_context *pmem_ctx);
extern void pmem_data_clone_read_to_write(struct bittern_cache *bc,
					  struct cache_block *from_cache_block,
					  struct cache_block *to_cache_block,
					  struct pmem_context *pmem_ctx);

/*!
 * async write accessors (get_page_write()/put_page_write())
 *
 * caller calls get_page_write() to setup the necessary context to start an
 * asynchronous cache write transfer. the buffer pointed by dbi_data is
 * guaranteed to be valid until put_page_write() is called.
 * caller can then write to such buffer.
 *
 * caller calls put_page_write() to actually start the asynchronous cache write
 * transfer. the callback could be called even before this function returns
 * [this is guaranteed to happen if pmem provider implements data transfers
 * with memory copy].
 * before the callback is called, the data context described in dbi_data is
 * released.
 *
 * caller can modify the data with these APIs (duh!)
 */
extern void pmem_data_get_page_write(struct bittern_cache *bc,
				     struct cache_block *cache_block,
				     struct pmem_context *pmem_ctx);
extern void pmem_data_put_page_write(struct bittern_cache *bc,
				     struct cache_block *cache_block,
				     struct pmem_context *pmem_ctx,
				     void *callback_context,
				     pmem_callback_t callback_function,
				     enum cache_state metadata_update_state);

struct pmem_info {
	uint32_t restore_header_valid;
	uint32_t restore_header0_valid;
	uint32_t restore_header1_valid;
	uint32_t restore_corrupt_metadata_blocks;
	uint32_t restore_valid_clean_metadata_blocks;
	uint32_t restore_valid_dirty_metadata_blocks;
	uint32_t restore_invalid_metadata_blocks;
	uint32_t restore_pending_metadata_blocks;
	uint32_t restore_invalid_data_blocks;
	uint32_t restore_valid_clean_data_blocks;
	uint32_t restore_valid_dirty_data_blocks;
	uint32_t restore_hash_corrupt_metadata_blocks;
	uint32_t restore_hash_corrupt_data_blocks;

	atomic_t metadata_read_async_count;
	atomic_t metadata_write_async_count;
	atomic_t data_get_put_page_pending_count;
	atomic_t data_get_page_read_count;
	atomic_t data_put_page_read_count;
	atomic_t data_get_page_write_count;
	atomic_t data_put_page_write_count;
	atomic_t data_put_page_write_metadata_count;
	atomic_t data_convert_page_read_to_write_count;
	atomic_t data_clone_read_page_to_write_page_count;

	struct cache_timer metadata_read_async_timer;
	struct cache_timer metadata_write_async_timer;
	struct cache_timer data_get_page_read_timer;
	struct cache_timer data_get_page_read_async_timer;
	struct cache_timer data_get_page_write_timer;
	struct cache_timer data_put_page_read_timer;
	struct cache_timer data_put_page_write_async_timer;
	struct cache_timer data_put_page_write_async_metadata_timer;
	struct cache_timer data_put_page_write_timer;

	atomic_t pmem_read_not4k_count;
	atomic_t pmem_read_not4k_pending;
	atomic_t pmem_write_not4k_count;
	atomic_t pmem_write_not4k_pending;
	atomic_t pmem_read_4k_count;
	atomic_t pmem_read_4k_pending;
	atomic_t pmem_write_4k_count;
	atomic_t pmem_write_4k_pending;

	struct cache_timer pmem_read_not4k_timer;
	struct cache_timer pmem_write_not4k_timer;
	struct cache_timer pmem_read_4k_timer;
	struct cache_timer pmem_write_4k_timer;

	atomic_t pmem_make_req_wq_count;
	struct cache_timer pmem_make_req_wq_timer;
};

/*!
 * pmem_info initialize function
 */
extern void pmem_info_initialize(struct bittern_cache *bc);
/*!
 * pmem_info deinitialize function
 */
extern void pmem_info_deinitialize(struct bittern_cache *bc);

/*!
 * update timeout callback, called every 30 seconds by task timeout handler
 */
extern void pmem_header_update_timeout(struct bittern_cache *bc);

#endif /* BITTERN_CACHE_PMEM_API_H */
