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

#include "bittern_cache_pmem_header.h"
#include "bittern_cache_states.h"

extern int pmem_allocate(struct bittern_cache *bc,
			 struct block_device *blockdev);
extern void pmem_deallocate(struct bittern_cache *bc);

/*!
 * returns pmem_api name
 */
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
			      unsigned int block_id,
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
				struct data_buffer_info *dbi_data,
				void *callback_context,
				int err);

#define PMEM_ASYNC_CONTEXT_MAGIC1	0xf10c7a71
#define PMEM_ASYNC_CONTEXT_MAGIC3	0xf10c7a72
#define CACHE_PMEM_ASYNC_CONTEXT_MAGIC3	0xf10c7a73

/*! \todo remove this hack when we are completely done removing pmem_provider */
#define PM_DEV_MEM_ASYNC_CONTEXT_SIZE 160
/*! \todo remove this hack when we are completely done removing pmem_provider */
struct pmem_devm_async_context {
	char pmem_devm_opaque[PM_DEV_MEM_ASYNC_CONTEXT_SIZE];
};

/*!
 * the content of this data structure is only used within pmem_api layer.
 * no other functions outside pmem_api layer must know about the contents of
 * this structure.
 * caller of pmem_data_get_page_read() and
 * pmem_data_put_page_write() needs to pass this context
 * without reading from it or writing to it.
 * the context is used by the lower level APIs to keep track of async transfers.
 * caller may deallocate this context in the callback function, not before.
 */
struct async_context {
	unsigned int ma_magic1;
	/*!
	 * \todo remove this hack when we are completely done removing
	 * pmem_provider
	 */
	struct pmem_devm_async_context ma_devm_context;
	struct bio *ma_bio;
	struct work_struct ma_work;
	unsigned int ma_magic2;
	struct bittern_cache *ma_bc;
	struct cache_block *ma_cache_block;
	struct data_buffer_info *ma_dbi;
	void *ma_callback_context;
	pmem_callback_t ma_callback_function;
	uint64_t ma_start_timer;
	uint64_t ma_start_timer_2;
	/*! READ or WRITE */
	int ma_datadir;
	/*! desired metadata update state */
	enum cache_state ma_metadata_state;
	unsigned int ma_magic3;
};

/*!
 * sync read metadata
 */
extern int pmem_metadata_sync_read(struct bittern_cache *bc,
				   unsigned int block_id,
				   struct cache_block *cache_block,
				   struct pmem_block_metadata *out_pmbm_mem);
/*!
 * sync write metadata
 */
extern int pmem_metadata_sync_write(struct bittern_cache *bc,
				    unsigned int block_id,
				    struct cache_block *cache_block,
				    enum cache_state metadata_update_state);

extern int pmem_data_get_page_read(struct bittern_cache *bc,
				   unsigned int block_id,
				   struct cache_block *cache_block,
				   struct data_buffer_info *dbi_data,
				   struct async_context *async_context,
				   void *callback_context,
				   pmem_callback_t callback_function);

/*!
 * async metadata update API
 */
extern int pmem_metadata_async_write(struct bittern_cache *bc,
				     unsigned int block_id,
				     struct cache_block *cache_block,
				     struct data_buffer_info *dbi_data,
				     struct async_context *async_context,
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
extern int pmem_data_get_page_read(struct bittern_cache *bc,
				   unsigned int block_id,
				   struct cache_block *cache_block,
				   struct data_buffer_info *dbi_data,
				   struct async_context *async_context,
				   void *callback_context,
				   pmem_callback_t callback_function);
extern int pmem_data_put_page_read(struct bittern_cache *bc,
				   unsigned int block_id,
				   struct cache_block *cache_block,
				   struct data_buffer_info *dbi_data);

/*!
 * convert page obtained for read to page for write.
 * this is used for rmw cycles
 */
extern int pmem_data_convert_read_to_write(struct bittern_cache *bc,
					   unsigned int block_id,
					   struct cache_block *cache_block,
					   struct data_buffer_info *dbi_data);
extern int pmem_data_clone_read_to_write(struct bittern_cache *bc,
					 unsigned int from_block_id,
					 struct cache_block *from_cache_block,
					 unsigned int to_block_id,
					 struct cache_block *to_cache_block,
					 struct data_buffer_info *dbi_data);

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
extern int pmem_data_get_page_write(struct bittern_cache *bc,
				    unsigned int block_id,
				    struct cache_block *cache_block,
				    struct data_buffer_info *dbi_data);
extern int pmem_data_put_page_write(struct bittern_cache *bc,
				    unsigned int block_id,
				    struct cache_block *cache_block,
				    struct data_buffer_info *dbi_data,
				    struct async_context *async_context,
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
