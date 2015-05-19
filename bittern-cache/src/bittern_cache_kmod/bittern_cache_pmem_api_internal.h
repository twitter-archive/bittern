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

extern int pmem_read_sync(struct bittern_cache *bc,
			  uint64_t from_pmem_offset,
			  void *to_buffer,
			  size_t size);
extern int pmem_write_sync(struct bittern_cache *bc,
			   uint64_t to_pmem_offset,
			   void *from_buffer,
			   size_t size);

typedef int
(*pmem_allocate_f)(struct bittern_cache *bc,
		   struct block_device *blockdev);
typedef void
(*pmem_deallocate_f)(struct bittern_cache *bc);
typedef int
(*pmem_read_sync_f)(struct bittern_cache *bc,
		    uint64_t from_pmem_offset,
		    void *to_buffer,
		    size_t size);
typedef int
(*pmem_write_sync_f)(struct bittern_cache *bc,
		     uint64_t to_pmem_offset,
		     void *from_buffer,
		     size_t size);
typedef void
(*pmem_metadata_async_write_f)(struct bittern_cache *bc,
			       struct cache_block *cache_block,
			       struct pmem_context *pmem_ctx,
			       void *callback_context,
			       pmem_callback_t callback_function,
			       enum cache_state metadata_update_state);
typedef void
(*pmem_data_cache_get_page_read_f)(struct bittern_cache *bc,
				   struct cache_block *cache_block,
				   struct pmem_context *pmem_ctx,
				   void *callback_context,
				   pmem_callback_t callback_function);
typedef void
(*pmem_data_cache_put_page_read_f)(struct bittern_cache *bc,
				   struct cache_block *cache_block,
				   struct pmem_context *pmem_ctx);
typedef void
(*pmem_data_cache_convert_read_to_write_f)(struct bittern_cache *bc,
					   struct cache_block *cache_block,
					   struct pmem_context *pmem_ctx);
typedef void
(*pmem_data_cache_clone_read_to_write_f)(struct bittern_cache *bc,
					 struct cache_block *from_cache_block,
					 struct cache_block *to_cache_block,
					 struct pmem_context *pmem_ctx);
typedef void
(*pmem_data_cache_get_page_write_f)(struct bittern_cache *bc,
				    struct cache_block *cache_block,
				    struct pmem_context *pmem_ctx);
typedef void
(*pmem_data_cache_put_page_write_f)(struct bittern_cache *bc,
				    struct cache_block *cache_block,
				    struct pmem_context *pmem_ctx,
				    void *callback_context,
				    pmem_callback_t callback_function,
				    enum cache_state metadata_update_state);

/*!
 * papi interface callbacks
 */
struct cache_papi_interface {
	/*!
	 * interface name
	 */
	char interface_name[64];
	/*!
	 * true if pmem only supports PAGE_SIZE transfers
	 */
	bool page_size_transfer_only;
	/*!
	 * cache layout
	 */
	char cache_layout;
	/*
	 * interface callbacks
	 */
	pmem_allocate_f allocate_func;
	pmem_deallocate_f deallocate_func;
	pmem_read_sync_f read_sync;
	pmem_write_sync_f write_sync;
	pmem_metadata_async_write_f metadata_async_write;
	pmem_data_cache_get_page_read_f data_cache_get_page_read;
	pmem_data_cache_put_page_read_f data_cache_put_page_read;
	pmem_data_cache_convert_read_to_write_f
				data_cache_convert_read_to_write;
	pmem_data_cache_clone_read_to_write_f
				data_cache_clone_read_to_write;
	pmem_data_cache_get_page_write_f data_cache_get_page_write;
	pmem_data_cache_put_page_write_f data_cache_put_page_write;

	uint64_t magic;
};
#define PAPI_MAGIC 0x7b21bfd07ff68fe5ULL

/*!
 * Memory PMEM_API.
 * Supports NVDIMM hardwre (sometimes called "NV buffer") or any other
 * type of hardware with DRAM interface but which keeps the memory
 * content upon power failure (and restores it on power up).
 * If a device has asymmetric features (like for instance read from
 * PCIe-NVRAM device is very slow), then a hybrid approach could be
 * used provided that we make extensions to DAX to expose hardware
 * properties.
 */
extern const struct cache_papi_interface cache_papi_mem;
extern int pmem_allocate_papi_mem(struct bittern_cache *bc,
				  struct block_device *blockdev);

/*!
 * Block PMEM_API.
 * Supports standard NAND flash block devices, SSDs and NVMe devices.
 * For the latter we'll later split it into its own module and making
 * NVMe-specific optimizations if we ever find said hardware .....ooo000OOO....
 */
extern const struct cache_papi_interface cache_papi_block;
extern int pmem_allocate_papi_block(struct bittern_cache *bc,
				    struct block_device *blockdev);

/*!
 * convert block id to metadata byte offset into the cache device
 *
 * this function is also used during initialization, so be sure of not
 * using fields unless absolutely needed here.
 */
static inline uint64_t
__cache_block_id_2_metadata_pmem_offset_p(struct pmem_header *pm,
					  uint64_t block_id)
{
	uint64_t ret;
	ASSERT(pm != NULL);
	ASSERT(pm->lm_magic == LM_MAGIC);
	ASSERT(pm->lm_cache_block_size == PAGE_SIZE);
	ASSERT(pm->lm_cache_layout == CACHE_LAYOUT_INTERLEAVED ||
	       pm->lm_cache_layout == CACHE_LAYOUT_SEQUENTIAL);
	ASSERT(pm->lm_first_offset_bytes == CACHE_MEM_FIRST_OFFSET_BYTES);
	ASSERT(pm->lm_cache_blocks > 0);
	ASSERT(block_id > 0 && block_id <= pm->lm_cache_blocks);

	if (pm->lm_cache_layout == CACHE_LAYOUT_SEQUENTIAL) {
		/*
		 * sequential layout
		 */
		uint64_t m = pm->lm_first_offset_bytes;
		ASSERT(pm->lm_mcb_size_bytes ==
		       sizeof(struct pmem_block_metadata) ||
		       pm->lm_mcb_size_bytes == PAGE_SIZE);
		m += pm->lm_cache_blocks * pm->lm_mcb_size_bytes;
		m = round_up(m, PAGE_SIZE);
		ASSERT(m == pm->lm_first_data_block_offset_bytes);
		/*
		 * +---------------------------------------------+
		 * | hdr0 hdr1   metadata_blocks    data_blocks  |
		 * +---------------------------------------------+
		 */
		ret = pm->lm_first_offset_bytes;
		ret += (block_id - 1) * pm->lm_mcb_size_bytes;
	} else {
		/*
		 * interleaved layout
		 */
		/*
		 * +---------------------------------------------+
		 * | hdr0 hdr1   (data_metadata_pair)_blocks     |
		 * +---------------------------------------------+
		 */
		ret = pm->lm_first_offset_bytes;
		ret += (block_id - 1) * (PAGE_SIZE * 2) + PAGE_SIZE;
		ASSERT(pm->lm_mcb_size_bytes == PAGE_SIZE);
		ASSERT(pm->lm_first_data_block_offset_bytes ==
		       pm->lm_first_offset_bytes);
	}

	ASSERT(ret + PAGE_SIZE <= pm->lm_cache_size_bytes);
	return ret;
}

/*!
 * convert block id to data byte offset into the cache device
 *
 * this function is also used during initialization, so be sure of not
 * using fields unless absolutely needed here.
 */
static inline uint64_t
__cache_block_id_2_data_pmem_offset_p(struct pmem_header *pm,
				      uint64_t block_id)
{
	uint64_t ret;
	ASSERT(pm != NULL);
	ASSERT(pm->lm_magic == LM_MAGIC);
	ASSERT(pm->lm_cache_block_size == PAGE_SIZE);
	ASSERT(pm->lm_cache_layout == 'I' || pm->lm_cache_layout == 'S');
	ASSERT(pm->lm_first_offset_bytes == CACHE_MEM_FIRST_OFFSET_BYTES);
	ASSERT(pm->lm_cache_blocks > 0);
	ASSERT(block_id > 0 && block_id <= pm->lm_cache_blocks);

	if (pm->lm_cache_layout == CACHE_LAYOUT_SEQUENTIAL) {
		/*
		 * sequential layout
		 */
		uint64_t m = pm->lm_first_offset_bytes;
		m += pm->lm_cache_blocks * pm->lm_mcb_size_bytes;
		m = round_up(m, PAGE_SIZE);
		ASSERT(m == pm->lm_first_data_block_offset_bytes);
		/*
		 * +---------------------------------------------+
		 * | hdr0 hdr1   metadata_blocks    data_blocks  |
		 * +---------------------------------------------+
		 */
		ret = pm->lm_first_data_block_offset_bytes;
		ret += (block_id - 1) * PAGE_SIZE;
		ASSERT(pm->lm_first_data_block_offset_bytes <
		       pm->lm_cache_size_bytes);
	} else {
		/*
		 * interleaved layout
		 */
		/*
		 * +---------------------------------------------+
		 * | hdr0 hdr1   (data_metadata_pair)_blocks     |
		 * +---------------------------------------------+
		 */
		ret = pm->lm_first_data_block_offset_bytes;
		ret += (block_id - 1) * (PAGE_SIZE * 2);
		ASSERT(pm->lm_first_data_block_offset_bytes ==
		       pm->lm_first_offset_bytes);
	}

	ASSERT(pm->lm_first_data_block_offset_bytes < pm->lm_cache_size_bytes);
	ASSERT(ret + PAGE_SIZE <= pm->lm_cache_size_bytes);
	return ret;
}

/*!
 * convert block id to metadata byte offset into the cache device
 *
 * this function is also used during initialization, so be sure of not
 * using fields unless absolutely needed here.
 */
static inline uint64_t
__cache_block_id_2_metadata_pmem_offset(struct bittern_cache *bc,
					unsigned int block_id)
{
	uint64_t ret;
	struct pmem_api *pa = &bc->bc_papi;
	struct pmem_header *pm = &pa->papi_hdr;

	ASSERT(bc != NULL);
	ASSERT(bc->bc_magic1 == BC_MAGIC1);
	ASSERT(pm->lm_magic == LM_MAGIC);
	ASSERT(pm->lm_cache_blocks != 0);
	ASSERT(pa->papi_bdev_size_bytes > 0);
	ret = __cache_block_id_2_metadata_pmem_offset_p(pm, block_id);
	ASSERT(ret + PAGE_SIZE <= pa->papi_bdev_size_bytes);
	return ret;
}

static inline uint64_t
__cache_block_id_2_data_pmem_offset(struct bittern_cache *bc,
				    unsigned int block_id)
{
	uint64_t ret;
	struct pmem_api *pa = &bc->bc_papi;
	struct pmem_header *pm = &pa->papi_hdr;

	ASSERT(bc != NULL);
	ASSERT(bc->bc_magic1 == BC_MAGIC1);
	ASSERT(pm->lm_magic == LM_MAGIC);
	ASSERT(pm->lm_cache_blocks != 0);
	ASSERT(pa->papi_bdev_size_bytes > 0);
	ret = __cache_block_id_2_data_pmem_offset_p(pm, block_id);
	ASSERT(ret + PAGE_SIZE <= pa->papi_bdev_size_bytes);
	return ret;
}

static inline void pmem_set_dbi(struct data_buffer_info *dbi,
				int flags,
				void *vaddr,
				struct page *page)
{
	ASSERT((flags & CACHE_DI_FLAGS_DOUBLE_BUFFERING) == 0);
	ASSERT(vaddr != NULL);
	ASSERT(page != NULL);
	ASSERT(dbi->di_buffer == NULL);
	ASSERT(dbi->di_page == NULL);
	ASSERT(dbi->di_flags == 0x0);
	ASSERT(atomic_read(&dbi->di_busy) == 0);
	dbi->di_buffer = vaddr;
	dbi->di_page = page;
	dbi->di_flags = flags;
	atomic_inc(&dbi->di_busy);
}

static inline void pmem_set_dbi_double_buffering(struct data_buffer_info *dbi,
						 int flags)
{
	ASSERT((flags & CACHE_DI_FLAGS_DOUBLE_BUFFERING) != 0);
	ASSERT(dbi->di_buffer == NULL);
	ASSERT(dbi->di_page == NULL);
	ASSERT(dbi->di_flags == 0x0);
	ASSERT(atomic_read(&dbi->di_busy) == 0);
	ASSERT(dbi->di_buffer_vmalloc_buffer != NULL);
	ASSERT(PAGE_ALIGNED(dbi->di_buffer_vmalloc_buffer));
	ASSERT(dbi->di_buffer_vmalloc_page != NULL);
	ASSERT(dbi->di_buffer_vmalloc_page ==
	       virtual_to_page(dbi->di_buffer_vmalloc_buffer));
	dbi->di_buffer = dbi->di_buffer_vmalloc_buffer;
	dbi->di_page = dbi->di_buffer_vmalloc_page;
	dbi->di_flags = flags;
	atomic_inc(&dbi->di_busy);
}

static inline void __pmem_clear_dbi(struct data_buffer_info *dbi, int flags)
{
	__ASSERT_PMEM_DBI(dbi, flags);
	dbi->di_buffer = NULL;
	dbi->di_page = NULL;
	dbi->di_flags = 0x0;
	atomic_dec(&dbi->di_busy);
	ASSERT(atomic_read(&dbi->di_busy) == 0);
}

/*! use this macro when we know we are using double buffering */
#define pmem_clear_dbi_double_buffering(__dbi)			\
		__pmem_clear_dbi(__dbi, CACHE_DI_FLAGS_DOUBLE_BUFFERING)
#define pmem_clear_dbi(__dbi)					\
		__pmem_clear_dbi(__dbi, 0)
