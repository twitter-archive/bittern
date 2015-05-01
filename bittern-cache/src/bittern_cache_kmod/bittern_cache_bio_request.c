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

int cache_bio_request_initialize(struct bittern_cache *bc,
				 struct cache_bio_request **bio_req)
{
	ASSERT_BITTERN_CACHE(bc);
	ASSERT(bio_req != NULL);
	*bio_req = kmem_zalloc(sizeof(struct cache_bio_request),
			       GFP_KERNEL | GFP_NOIO);
	M_ASSERT_FIXME((*bio_req) != NULL);
	(*bio_req)->bbr_magic = BBR_MAGIC;
	(*bio_req)->bbr_bc = bc;
	(*bio_req)->bbr_state = BBR_BIO_STATE_INITIALIZED;
	sema_init(&(*bio_req)->bbr_sema, 0);
	(*bio_req)->bbr_vmalloc_buffer_page = vmalloc(PAGE_SIZE);
	M_ASSERT_FIXME((*bio_req)->bbr_vmalloc_buffer_page != NULL);
	return 0;
}

void cache_bio_request_deinitialize(struct bittern_cache *bc,
				    struct cache_bio_request *bio_req)
{
	ASSERT_BITTERN_CACHE(bc);
	ASSERT(bio_req != NULL);
	/*
	 * this assert implies all the bio_req structures are statically
	 * allocated inside "struct bittern_cache"
	 */
	ASSERT(bio_req->bbr_magic == BBR_MAGIC);
	ASSERT(bio_req->bbr_bc == bc);
	ASSERT(bio_req->bbr_bio == NULL);
	ASSERT(bio_req->bbr_vmalloc_buffer_page != NULL);
	ASSERT(bio_req->bbr_state == BBR_BIO_STATE_INITIALIZED
	       || bio_req->bbr_state == BBR_BIO_STATE_IO_DONE);
	vfree(bio_req->bbr_vmalloc_buffer_page);
	kmem_free(bio_req, sizeof(struct cache_bio_request));
}

void cache_bio_request_endbio(struct bio *bio, int err)
{
	struct cache_bio_request *bio_req =
	    (struct cache_bio_request *)bio->bi_private;
	struct bittern_cache *bc = bio_req->bbr_bc;

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		 "endio: vmalloc_buffer_page=%p, bio=%p, bio_req=%p, err=%d, bbr_state=%d",
		 bio_req->bbr_vmalloc_buffer_page, bio, bio_req, err,
		 bio_req->bbr_state);
	ASSERT(bio_req != NULL);
	ASSERT(bio_req->bbr_magic == BBR_MAGIC);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT(bio == bio_req->bbr_bio);
	M_ASSERT_FIXME(err == 0);
	ASSERT(bio_req->bbr_state == BBR_BIO_STATE_IO_IN_PROGRESS);
	bio_req->bbr_state = BBR_BIO_STATE_IO_DONE;
	bio_req->bbr_bio = NULL;
	up(&bio_req->bbr_sema);
	bio_put(bio);
}

int cache_bio_request_start_async_page(struct bittern_cache *bc,
				       sector_t sector, int dir,
				       struct cache_bio_request *bio_req)
{
	ASSERT(bc != NULL);
	ASSERT(bio_req != NULL);
	ASSERT(bio_req->bbr_magic == BBR_MAGIC);
	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		 "enter, bio_req=%p, sector=%lu, dir=%c, bbr_state=%d, bbr_vmalloc_buffer_page=%p",
		 bio_req, sector, (dir == READ ? 'R' : 'W'), bio_req->bbr_state,
		 bio_req->bbr_vmalloc_buffer_page);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT(bio_req->bbr_bc == bc);
	ASSERT(bio_req->bbr_magic == BBR_MAGIC);
	ASSERT(bio_req->bbr_bio == NULL);
	ASSERT(dir == READ || dir == WRITE);
	ASSERT(bio_req->bbr_state == BBR_BIO_STATE_INITIALIZED
	       || bio_req->bbr_state == BBR_BIO_STATE_IO_DONE);
	ASSERT(bio_req->bbr_vmalloc_buffer_page != NULL);

	/*
	 * in this case the bio argument is the original bio.
	 * clone bio, start i/o to write data to device.
	 */
	bio_req->bbr_bio = bio_alloc(GFP_KERNEL, 1);
	M_ASSERT_FIXME(bio_req->bbr_bio != NULL);
	if (dir == READ)
		bio_set_data_dir_read(bio_req->bbr_bio);
	else
		bio_set_data_dir_write(bio_req->bbr_bio);
	bio_req->bbr_bio->bi_iter.bi_sector = sector;
	bio_req->bbr_bio->bi_iter.bi_size = PAGE_SIZE;
	bio_req->bbr_bio->bi_bdev = bc->bc_dev->bdev;
	bio_req->bbr_bio->bi_end_io = cache_bio_request_endbio;
	bio_req->bbr_bio->bi_private = (void *)bio_req;
	bio_req->bbr_bio->bi_io_vec[0].bv_page =
	    vmalloc_to_page(bio_req->bbr_vmalloc_buffer_page);
	ASSERT(bio_req->bbr_bio->bi_io_vec[0].bv_page != NULL);
	bio_req->bbr_bio->bi_io_vec[0].bv_len = PAGE_SIZE;
	bio_req->bbr_bio->bi_io_vec[0].bv_offset = 0;
	bio_req->bbr_bio->bi_vcnt = 1;
	ASSERT(bio_req->bbr_bio->bi_iter.bi_idx == 0);
	ASSERT(bio_req->bbr_bio->bi_vcnt == 1);

	bio_req->bbr_sector = sector;
	bio_req->bbr_datadir = dir;
	bio_req->bbr_state = BBR_BIO_STATE_IO_IN_PROGRESS;

	generic_make_request(bio_req->bbr_bio);

	return 0;
}

int cache_bio_request_wait_page(struct bittern_cache *bc,
				struct cache_bio_request *bio_req)
{
	ASSERT(bc != NULL);
	ASSERT(bio_req != NULL);
	ASSERT(bio_req->bbr_magic == BBR_MAGIC);
	BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL,
		 "enter, bio_req=%p, sector=%lu, dir=%c, bbr_state=%d, bio_req->bbr_vmalloc_buffer_page=%p",
		 bio_req, bio_req->bbr_sector,
		 (bio_req->bbr_datadir == READ ? 'R' : 'W'), bio_req->bbr_state,
		 bio_req->bbr_vmalloc_buffer_page);
	ASSERT(bio_req->bbr_datadir == READ || bio_req->bbr_datadir == WRITE);
	ASSERT(bc == bio_req->bbr_bc);
	ASSERT_BITTERN_CACHE(bc);

	down(&bio_req->bbr_sema);
	ASSERT(bio_req->bbr_state == BBR_BIO_STATE_IO_DONE);
	ASSERT(bio_req->bbr_bio == NULL);

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		 "done, bio_req=%p, sector=%lu, dir=%c, bbr_state=%d, bio_req->bbr_vmalloc_buffer_page=%p",
		 bio_req, bio_req->bbr_sector,
		 (bio_req->bbr_datadir == READ ? 'R' : 'W'), bio_req->bbr_state,
		 bio_req->bbr_vmalloc_buffer_page);

	return 0;
}
