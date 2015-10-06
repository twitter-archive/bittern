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

/*! used to carry state to delayed worker */
struct flush_meta {
	struct bittern_cache *bc;
	/*! work struct for explicit flushes */
	struct delayed_work dwork;
	/*! pass as arg to delayed worker */
	uint64_t gennum;
};

/*!
 * if err == 0, complete requests which have gennum <= gennum.
 * if err != 0, complete all requests as it may not be possible to issue
 * an explicit flush.
 */
static void cached_devio_flush_end_bio_process(struct bittern_cache *bc,
					       uint64_t gennum,
					       int err)
{
	bool processed;

	ASSERT_BITTERN_CACHE(bc);
	/*
	 * ack pending writes upto gennum
	 */
	do {
		struct work_item *wi;
		struct bio *bio;
		unsigned long flags;
		int cc = 0;

		processed = false;

		spin_lock_irqsave(&bc->devio.spinlock, flags);
		list_for_each_entry(wi,
				    &bc->devio.flush_pending_list,
				    devio_pending_list) {
			ASSERT_WORK_ITEM(wi, bc);
			bio = wi->wi_cloned_bio;
			if (wi->devio_gennum <= gennum) {
				M_ASSERT(!list_empty(&bc->devio.flush_pending_list));
				list_del_init(&wi->devio_pending_list);
				bc->devio.flush_pending_count--;
				M_ASSERT(bc->devio.flush_pending_count >= 0);
				if (bc->devio.flush_pending_count == 0)
					M_ASSERT(list_empty(&bc->devio.flush_pending_list));
				else
					M_ASSERT(!list_empty(&bc->devio.flush_pending_list));
				BT_TRACE(BT_LEVEL_TRACE1,
					 bc, NULL, NULL, bio, NULL,
					 "bi_sector=%lu, gennum=%llu/%llu, flush wait done, err=%d",
					 bio->bi_iter.bi_sector,
					 wi->devio_gennum,
					 gennum,
					 err);

				spin_unlock_irqrestore(&bc->devio.spinlock, flags);

				cache_timer_add(&bc->bc_timer_cached_device_flushes, wi->wi_ts_physio_flush);
				cached_dev_make_request_endio(wi, bio, 0);

				processed = true;
				goto inner_out;
			} else {
				BT_TRACE(BT_LEVEL_TRACE1,
					 bc, NULL, NULL, bio, NULL,
					 "not processing bi_sector=%lu, gennum=%llu/%llu",
					     bio->bi_iter.bi_sector,
					 wi->devio_gennum, gennum);
			}
			M_ASSERT(cc++ < 10000);
		}
		spin_unlock_irqrestore(&bc->devio.spinlock, flags);

inner_out:
		;

	} while (processed);
}

/*! handles completion of pureflush request */
static void cached_devio_flush_end_bio(struct bio *bio, int err)
{
	unsigned long flags;
	struct flush_meta *flush_meta = bio->bi_private;

	ASSERT_BITTERN_CACHE(flush_meta->bc);

	spin_lock_irqsave(&flush_meta->bc->devio.spinlock, flags);
	flush_meta->bc->devio.pure_flush_pending_count--;
        M_ASSERT(flush_meta->bc->devio.pure_flush_pending_count >= 0);
	spin_unlock_irqrestore(&flush_meta->bc->devio.spinlock, flags);

	BT_TRACE(BT_LEVEL_TRACE1, flush_meta->bc, NULL, NULL, bio, NULL,
		 "ack up to delayed flush gennum = %llu, err=%d",
		 flush_meta->gennum,
		 err);

	bio_put(bio);

#warning "add error injection here when done merging with error handling"
	cached_devio_flush_end_bio_process(flush_meta->bc,
					   flush_meta->gennum,
					   err);

	kmem_free(flush_meta, sizeof(struct flush_meta));
}

void cached_devio_flush_delayed_worker(struct work_struct *work)
{
	int ret;
	struct bittern_cache *bc;
	struct bio *bio;
	unsigned long flags;
	struct delayed_work *dwork = to_delayed_work(work);
	struct flush_meta *flush_meta;

	bc = container_of(dwork,
			  struct bittern_cache,
			  devio.flush_delayed_work);
	ASSERT(bc != NULL);

	if (bc->devio.flush_pending_count == 0 &&
	    bc->devio.pending_count == 0) {
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
			 "nothing pending, no work to do");
		goto out;
	}

	ASSERT_BITTERN_CACHE(bc);

	flush_meta = kmem_alloc(sizeof(struct flush_meta), GFP_NOIO);
#warning "add error injection here when done merging with error handling"
	if (flush_meta == NULL) {
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "cannot allocate flush_meta");
		printk_err("%s: cannot allocate flush_meta\n", bc->bc_name);
		kmem_free(flush_meta, sizeof(struct flush_meta));
		/*
		 * Allocation failed, bubble up error to state machine.
		 */
		cached_devio_flush_end_bio_process(bc, 0, -ENOMEM);
		return;
	}
	flush_meta->bc = bc;

	bio = bio_alloc(GFP_NOIO, 1);
#warning "add error injection here when done merging with error handling"
	if (bio == NULL) {
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, NULL, NULL, NULL,
			     "cannot allocate bio");
		printk_err("%s: cannot allocate bio\n", bc->bc_name);
		kmem_free(flush_meta, sizeof(struct flush_meta));
		/*
		 * Allocation failed, bubble up error to state machine.
		 */
		cached_devio_flush_end_bio_process(bc, 0, -ENOMEM);
		return;
	}

	bio->bi_rw |= REQ_FLUSH | REQ_FUA;
	bio_set_data_dir_write(bio);
	bio->bi_iter.bi_sector = 0;
	bio->bi_iter.bi_size = 0;
	bio->bi_bdev = bc->devio.dm_dev->bdev;
	bio->bi_end_io = cached_devio_flush_end_bio;
	bio->bi_private = flush_meta;
	bio->bi_vcnt = 0;

	spin_lock_irqsave(&bc->devio.spinlock, flags);
	bc->devio.gennum_flush = bc->devio.gennum;
	flush_meta->gennum = bc->devio.gennum;
	bc->devio.pure_flush_pending_count++;
	bc->devio.pure_flush_total_count++;
	spin_unlock_irqrestore(&bc->devio.spinlock, flags);

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, bio, NULL,
		 "delayed_worker: issue pure flush gennum=%llu",
		 flush_meta->gennum);

	generic_make_request(bio);

out:
	ret = schedule_delayed_work(&bc->devio.flush_delayed_work,
				msecs_to_jiffies(bc->devio.conf_worker_delay));
	M_ASSERT(ret == 1);
}

/*! end_bio function used by @ref cached_devio_do_make_request */
static void cached_devio_make_request_end_bio(struct bio *bio, int err)
{
	struct bittern_cache *bc;
	struct cache_block *cache_block;
	struct work_item *wi;
	uint64_t gennum;
	unsigned long flags;

	ASSERT(bio != NULL);
	wi = bio->bi_private;
	ASSERT(wi != NULL);
	gennum = wi->devio_gennum;

	bc = wi->wi_cache;
	ASSERT_BITTERN_CACHE(bc);

	cache_block = wi->wi_cache_block;
	ASSERT_WORK_ITEM(wi, bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ASSERT(cache_block->bcb_xid != 0);
	ASSERT(cache_block->bcb_xid == wi->wi_io_xid);
	ASSERT(is_sector_number_valid(cache_block->bcb_sector));
	M_ASSERT(bio == wi->wi_cloned_bio);
	ASSERT(bio_data_dir(bio) == READ ||
	       bio_data_dir(bio) == WRITE);
	ASSERT(cache_block->bcb_xid != 0);
	ASSERT(cache_block->bcb_xid == wi->wi_io_xid);
	ASSERT_CACHE_STATE(cache_block);
	ASSERT(is_sector_number_valid(cache_block->bcb_sector));

	spin_lock_irqsave(&bc->devio.spinlock, flags);

	M_ASSERT(!list_empty(&bc->devio.pending_list));
	list_del_init(&wi->devio_pending_list);
	bc->devio.pending_count--;
	M_ASSERT(bc->devio.pending_count >= 0);
	if (bc->devio.pending_count == 0)
		M_ASSERT(list_empty(&bc->devio.pending_list));
	else
		M_ASSERT(!list_empty(&bc->devio.pending_list));

	if (bio_data_dir(bio) == READ) {

		spin_unlock_irqrestore(&bc->devio.spinlock, flags);

		/*
		 * Process READ request acks immediately.
		 */
		cached_dev_make_request_endio(wi, bio, err);
		return;
	}

	wi->wi_ts_physio_flush = current_kernel_time_nsec();

	/*
	 * If this is a flush, acknowledge all pending writes which
	 * have a gennum lower that the current flush.
	 * If not, leave the write in pending_flush state until we get
	 * the next flush acknowledge.
	 * In case of error ack every pending request, as there may not be
	 * another notification.
	 */
	if (err != 0 || (wi->devio_flags & (REQ_FLUSH | REQ_FUA)) != 0) {
		/*
		 * ack previously pending writes, then ack current work_item.
		 */
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, bio, NULL,
			 "endbio: write+flush bi_sector=%lu, gennum=%llu done, err=%d",
			     bio->bi_iter.bi_sector,
			 wi->devio_gennum,
			 err);

		spin_unlock_irqrestore(&bc->devio.spinlock, flags);

		/*
		 * Ack all flush pending requests which have
		 * @ref devio_gennum <= current flush wi->devio_gennum.
		 */
		cached_devio_flush_end_bio_process(bc, wi->devio_gennum, err);

		/*
		 * note in this case current work_item hasn't been added to
		 * the pending list, so process separately.
		 */
		cache_timer_add(&bc->bc_timer_cached_device_flushes,
				wi->wi_ts_physio_flush);
		cached_dev_make_request_endio(wi, bio, err);

		spin_lock_irqsave(&bc->devio.spinlock, flags);

	} else {
		/*
		 * Just wait until a flush is processed. If this is the only
		 * write request in the queue, issue an explicit flush.
		 */
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, bio, NULL,
			 "endbio: write+flush bi_sector=%lu, gennum=%llu, waiting for flush",
			     bio->bi_iter.bi_sector,
			     wi->devio_gennum);

		list_add_tail(&wi->devio_pending_list,
			      &bc->devio.flush_pending_list);
		bc->devio.flush_pending_count++;

		M_ASSERT(bc->devio.flush_pending_count >= 1);
	}

	spin_unlock_irqrestore(&bc->devio.spinlock, flags);
}

void cached_devio_make_request(struct bittern_cache *bc,
			       struct work_item *wi,
			       struct bio *bio)
{
	unsigned long flags;

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_WORK_ITEM(wi, bc);
	bio->bi_end_io = cached_devio_make_request_end_bio;
	bio->bi_bdev = bc->devio.dm_dev->bdev;

	spin_lock_irqsave(&bc->devio.spinlock, flags);

	/*
	 * Add to devio.pending_list.
	 */
	list_add_tail(&wi->devio_pending_list, &bc->devio.pending_list);
	bc->devio.pending_count++;

	if (bio_data_dir_write(bio)) {
		wi->devio_gennum = ++bc->devio.gennum;
		if ((wi->devio_gennum - bc->devio.gennum_flush) >
		    bc->devio.conf_fua_insert) {
			bc->devio.gennum_flush = wi->devio_gennum;
			bc->devio.flush_total_count++;
			/*
			 * Issue flush
			 */
			bio->bi_rw |= REQ_FLUSH | REQ_FUA;
			BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, bio, NULL,
				 "issue write+flush bi_sector=%lu, gennum=%llu",
				     bio->bi_iter.bi_sector,
				     wi->devio_gennum);
		} else {
			BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, bio, NULL,
				 "issue write bi_sector=%lu, gennum=%llu",
				     bio->bi_iter.bi_sector,
				     wi->devio_gennum);
		}
	}

	spin_unlock_irqrestore(&bc->devio.spinlock, flags);

	wi->devio_flags = bio->bi_rw;

	generic_make_request(bio);
}
