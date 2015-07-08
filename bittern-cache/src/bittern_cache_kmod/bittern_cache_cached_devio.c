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

static bool __xxxyyy = false;

#define FLUSH_META_MAGIC	0xf10c9a21
/*! passed as bi_private field to the pure flush bio */
struct flush_meta {
	int magic;
	struct bittern_cache *bc;
	struct work_struct work;
	uint64_t gennum;
};

/*! completes all requests which have gennum <= gennum */
static void cached_devio_flush_end_bio_process(struct bittern_cache *bc, uint64_t gennum)
{
	bool processed;

	ASSERT_BITTERN_CACHE(bc);
	/*
	 * ack pending writes upto gennum
	 */
	do {
		struct work_item *wi;
		struct bio *bio;
		int c;
		unsigned long flags;
		int cc = 0;

		processed = false;
		spin_lock_irqsave(&bc->bc_dev_spinlock, flags);
		list_for_each_entry(wi,
				    &bc->bc_dev_flush_pending_list,
				    devio_pending_list) {
			ASSERT_WORK_ITEM(wi, bc);
			bio = wi->wi_cloned_bio;
			if (wi->devio_gennum <= gennum) {
				M_ASSERT(!list_empty(&bc->bc_dev_flush_pending_list));
				list_del_init(&wi->devio_pending_list);
				bc->bc_dev_flush_pending_count--;
				M_ASSERT(bc->bc_dev_flush_pending_count >= 0);
				if (bc->bc_dev_flush_pending_count == 0)
					M_ASSERT(list_empty(&bc->bc_dev_flush_pending_list));
				else
					M_ASSERT(!list_empty(&bc->bc_dev_flush_pending_list));
				if(__xxxyyy)printk_debug("end_bio_process: PROCESS bio %p bi_sector=%lu, gennum=%llu/%llu, flush wait done\n",
					     bio,
					     bio->bi_iter.bi_sector,
					     wi->devio_gennum,
					     gennum);
				spin_unlock_irqrestore(&bc->bc_dev_spinlock, flags);
				cached_dev_make_request_endio(wi, bio, 0);
				spin_lock_irqsave(&bc->bc_dev_spinlock, flags);
				processed = true;
				break;
			} else {
				if(__xxxyyy)printk_debug("end_bio_process: do not process bio %p bi_sector=%lu, gennum=%llu/%llu, flush still wait\n",
					     bio,
					     bio->bi_iter.bi_sector,
					     wi->devio_gennum,
					     gennum);
			}
			M_ASSERT(cc++ < 10000);
		}
		spin_unlock_irqrestore(&bc->bc_dev_spinlock, flags);
	} while (processed);
}

/*! handles completion of pureflush request */
static void cached_devio_flush_end_bio(struct bio *bio, int err)
{
	struct flush_meta *meta;
	struct bittern_cache *bc;
	uint64_t gennum;
	unsigned long flags;

	meta = bio->bi_private;
	ASSERT(meta->magic == FLUSH_META_MAGIC);
	bc = meta->bc;
	gennum = meta->gennum;
	ASSERT_BITTERN_CACHE(bc);

	spin_lock_irqsave(&bc->bc_dev_spinlock, flags);
	atomic_dec(&bc->bc_dev_pure_flush_pending_count);
	spin_unlock_irqrestore(&bc->bc_dev_spinlock, flags);

	if(__xxxyyy)printk_debug("flush_end_bio: need to ack up to gennum = %llu\n", gennum);

	bio_put(bio);

	kmem_free(meta, sizeof(struct flush_meta));

	cached_devio_flush_end_bio_process(bc, gennum);
}

static void cached_devio_flush_worker(struct work_struct *work)
{
	struct bittern_cache *bc;
	struct bio *bio;
	struct flush_meta *meta;
	unsigned long flags;
	uint64_t gennum;
	struct work_item *wi;

	ASSERT(!in_irq());
	ASSERT(!in_softirq());

	meta = container_of(work, struct flush_meta, work);
	ASSERT(meta->magic == FLUSH_META_MAGIC);
	bc = meta->bc;
	ASSERT_BITTERN_CACHE(bc);

	BT_DEV_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
		     "in_irq=%lu, in_softirq=%lu, bc=%p, work=%p, gennum=%llu",
		     in_irq(),
		     in_softirq(),
		     bc,
		     &meta->work,
		     meta->gennum);

	bio = bio_alloc(GFP_NOIO, 1);
#if 0
	/*TODO_ADD_ERROR_INJECTION*/
	if (bio == NULL) {
		BT_DEV_TRACE(BT_LEVEL_ERROR, bc, NULL, cache_block, NULL, NULL,
			     "cannot allocate bio, wi=%p",
			     wi);
		printk_err("%s: cannot allocate bio\n", bc->bc_name);
		/*
		 * Allocation failed, bubble up error to state machine.
		 */
		cache_state_machine(bc, wi, -ENOMEM);
		return;
	}
#endif
	M_ASSERT_FIXME(bio != NULL);

	bio->bi_rw |= REQ_FLUSH | REQ_FUA;
	bio_set_data_dir_write(bio);
	bio->bi_iter.bi_sector = 0;
	bio->bi_iter.bi_size = 0;
	bio->bi_bdev = bc->bc_dev->bdev;
	bio->bi_end_io = cached_devio_flush_end_bio;
	bio->bi_private = meta;
	bio->bi_vcnt = 0;

	if(__xxxyyy)printk_debug("worker: ISSUE pure flush gennum=%llu\n", meta->gennum);

	generic_make_request(bio);
}

/*! end_bio function used by @ref cached_devio_do_make_request */
static void cached_devio_make_request_end_bio(struct bio *bio, int err)
{
	struct bittern_cache *bc;
	struct cache_block *cache_block;
	struct work_item *wi;
	uint64_t gennum;
	unsigned long flags;
	int c;

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

	spin_lock_irqsave(&bc->bc_dev_spinlock, flags);

	M_ASSERT(!list_empty(&bc->bc_dev_pending_list));
	list_del_init(&wi->devio_pending_list);
	bc->bc_dev_pending_count--;
	M_ASSERT(bc->bc_dev_pending_count >= 0);
	if (bc->bc_dev_pending_count == 0)
		M_ASSERT(list_empty(&bc->bc_dev_pending_list));
	else
		M_ASSERT(!list_empty(&bc->bc_dev_pending_list));

	if (bio_data_dir(bio) == READ) {
		if(__xxxyyy)printk_debug("end_bio: process read bio %p\n", bio);

		spin_unlock_irqrestore(&bc->bc_dev_spinlock, flags);

		/*
		 * Process READ request acks immediately.
		 */
		cached_dev_make_request_endio(wi, bio, err);
		return;
	}

	/*
	 * If this is a flush, acknowledge all pending writes which
	 * have a gennum lower that the current flush.
	 * If not, leave the write in pending_flush state until we get
	 * the next flush acknowledge.
	 */
	M_ASSERT((wi->devio_flags & (REQ_FLUSH|REQ_FUA)) == (REQ_FLUSH|REQ_FUA) ||
		 (wi->devio_flags & (REQ_FLUSH|REQ_FUA)) == 0);
	if ((wi->devio_flags & (REQ_FLUSH | REQ_FUA)) != 0) {
		/*
		 * ack previously pending writes, then ack current work_item.
		 */
		if(__xxxyyy)printk_debug("end_bio: bio %p bi_sector=%lu, gennum=%llu, write+flush done\n",
			     bio,
			     bio->bi_iter.bi_sector,
			     wi->devio_gennum);

		spin_unlock_irqrestore(&bc->bc_dev_spinlock, flags);

		cached_devio_flush_end_bio_process(bc, wi->devio_gennum);

		cached_dev_make_request_endio(wi, bio, err);

	} else {
		/*
		 * Just wait until a flush is processed. If this is the only
		 * write request in the queue, issue an explicit flush.
		 */
		if(__xxxyyy)printk_debug("end_bio: add bio %p bi_sector=%lu, gennum=%llu, waiting for flush\n",
			     bio,
			     bio->bi_iter.bi_sector,
			     wi->devio_gennum);

		list_add_tail(&wi->devio_pending_list, &bc->bc_dev_flush_pending_list);
		bc->bc_dev_flush_pending_count++;

		M_ASSERT(bc->bc_dev_flush_pending_count >= 1);

		spin_unlock_irqrestore(&bc->bc_dev_spinlock, flags);
	}

	spin_lock_irqsave(&bc->bc_dev_spinlock, flags);

	if (gennum > bc->bc_dev_gennum_flush) {
		int ret;
		struct flush_meta *meta;

		bc->bc_dev_gennum_flush = gennum;

		bc->bc_dev_pure_flush_pending_count++;
		bc->bc_dev_explicit_flush_total_count++;

		if(__xxxyyy)printk_debug("end_bio: last request: issue explicit flush up to gennum=%llu\n", gennum);

		/* defer to worker thread, which will start io */

		/*
		 * Trying to do error handling for such a small struct
		 * seems overkill.
		 */
		meta = kmem_alloc(sizeof (struct flush_meta), GFP_ATOMIC);
		M_ASSERT(meta != NULL);

		meta->magic = FLUSH_META_MAGIC;
		meta->bc = bc;
		INIT_WORK(&meta->work, cached_devio_flush_worker);
		meta->gennum = gennum;
		ret = queue_work(bc->bc_dev_flush_wq, &meta->work);
		ASSERT(ret == 1);
	}

	spin_unlock_irqrestore(&bc->bc_dev_spinlock, flags);
}

void cached_devio_make_request(struct bittern_cache *bc,
			       struct work_item *wi,
			       struct bio *bio)
{
	unsigned long flags;

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_WORK_ITEM(wi, bc);
	bio->bi_end_io = cached_devio_make_request_end_bio;
	bio->bi_bdev = bc->bc_dev->bdev;

	spin_lock_irqsave(&bc->bc_dev_spinlock, flags);

	/*
	 * Add to bc_dev_pending_list.
	 */
	list_add_tail(&wi->devio_pending_list, &bc->bc_dev_pending_list);
	bc->bc_dev_pending_count++;

	if (bio_data_dir_write(bio)) {
		wi->devio_gennum = ++bc->bc_dev_gennum;
		if ((wi->devio_gennum - bc->bc_dev_gennum_flush) > 4) {
			bc->bc_dev_implicit_flush_total_count++;
			/*
			 * Issue flush
			 */
			bio->bi_rw |= REQ_FLUSH | REQ_FUA;
			bc->bc_dev_gennum_flush = wi->devio_gennum;
			if(__xxxyyy)printk_debug("ISSUE write+flush bio %p bi_sector=%lu, gennum=%llu\n",
				     bio,
				     bio->bi_iter.bi_sector,
				     wi->devio_gennum);
		} else {
			if(__xxxyyy)printk_debug("ISSUE write bio %p bi_sector=%lu, gennum=%llu\n",
				     bio,
				     bio->bi_iter.bi_sector,
				     wi->devio_gennum);
		}
	}

	spin_unlock_irqrestore(&bc->bc_dev_spinlock, flags);

	wi->devio_flags = bio->bi_rw;

	generic_make_request(bio);
}
