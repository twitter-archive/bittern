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
#include "bittern_cache_module.h"

int cache_calculate_max_pending(struct bittern_cache *bc, int max_requests)
{
	bc->bc_max_pending_requests = max_requests;
	if (bc->bc_max_pending_requests < CACHE_MAX_PENDING_REQUESTS_MIN)
		bc->bc_max_pending_requests = CACHE_MAX_PENDING_REQUESTS_MIN;
	if (bc->bc_max_pending_requests > CACHE_MAX_PENDING_REQUESTS_MAX)
		bc->bc_max_pending_requests = CACHE_MAX_PENDING_REQUESTS_MAX;
	/*
	 * this should not really happen in production, but it's useful for
	 * test runs when we have a very small cache
	 */
	if (bc->bc_max_pending_requests > (bc->bc_papi.papi_hdr.lm_cache_blocks / 10))
		bc->bc_max_pending_requests = bc->bc_papi.papi_hdr.lm_cache_blocks / 10;
	printk_info("max_requests=%u, minmax(%u,%u), lm_cache_blocks_slash_10=%u\n",
		    max_requests,
		    CACHE_MAX_PENDING_REQUESTS_MIN,
		    CACHE_MAX_PENDING_REQUESTS_MAX,
		    (unsigned int)(bc->bc_papi.papi_hdr.lm_cache_blocks / 10));
	ASSERT(bc->bc_max_pending_requests > 0);
	return 0;
}

/*
 * if min_count is zero, calculate using defaults
 */
int cache_calculate_min_invalid(struct bittern_cache *bc, int min_invalid_count)
{
	if (min_invalid_count == 0)
		min_invalid_count = INVALIDATOR_DEFAULT_INVALID_COUNT;
	if (min_invalid_count < INVALIDATOR_MIN_INVALID_COUNT)
		min_invalid_count = INVALIDATOR_MIN_INVALID_COUNT;
	if (min_invalid_count > INVALIDATOR_MAX_INVALID_COUNT)
		min_invalid_count = INVALIDATOR_MAX_INVALID_COUNT;
	bc->bc_invalidator_conf_min_invalid_count = min_invalid_count;
	printk_info("conf_min_invalid_count=%u:%u [%u..%u]\n",
		    min_invalid_count,
		    bc->bc_invalidator_conf_min_invalid_count,
		    INVALIDATOR_MIN_INVALID_COUNT,
		    INVALIDATOR_MAX_INVALID_COUNT);
	return 0;
}

static void __cache_block_initialize(struct bittern_cache *bc,
				     unsigned block_id,
				     struct cache_block *bcb)
{
	__ASSERT_BITTERN_CACHE(bc);
	M_ASSERT(bcb != NULL);
	M_ASSERT(block_id >= 1);
	M_ASSERT(block_id <= bc->bc_papi.papi_hdr.lm_cache_blocks);
	M_ASSERT(bcb == &bc->bc_cache_blocks[block_id - 1]);
	memset(bcb, 0, sizeof(struct cache_block));
	bcb->bcb_block_id = block_id;
	bcb->bcb_magic1 = BCB_MAGIC1;
	bcb->bcb_magic3 = BCB_MAGIC3;
	spin_lock_init(&bcb->bcb_spinlock);
	atomic_set(&bcb->bcb_refcount, 0);
	bcb->bcb_sector = SECTOR_NUMBER_INVALID;
	bcb->bcb_state = S_INVALID;
	bcb->bcb_cache_transition = TS_NONE;
	bcb->bcb_xid = 0ULL;
	RB_CLEAR_NODE(&bcb->bcb_rb_node);
	INIT_LIST_HEAD(&bcb->bcb_entry);
	INIT_LIST_HEAD(&bcb->bcb_entry_cleandirty);
	bcb->bcb_hash_data = UINT128_ZERO;
}

static void __cache_block_invalidate(struct bittern_cache *bc,
				     struct cache_block *bcb)
{
	M_ASSERT(is_sector_number_valid(bcb->bcb_sector));
	/*
	 * remove cache block
	 */
	atomic_dec(&bc->bc_total_entries);
	atomic_dec(&bc->bc_valid_entries);
	if (bcb->bcb_state == S_CLEAN) {
		atomic_dec(&bc->bc_valid_entries_clean);
	} else {
		M_ASSERT(bcb->bcb_state == S_DIRTY);
		atomic_dec(&bc->bc_valid_entries_dirty);
	}
	M_ASSERT(RB_NON_EMPTY_ROOT(&bc->bc_rb_root));
	M_ASSERT(RB_NON_EMPTY_NODE(&bcb->bcb_rb_node));
	cache_rb_remove(bc, bcb);
	M_ASSERT(RB_EMPTY_NODE(&bcb->bcb_rb_node));
	list_del_init(&bcb->bcb_entry_cleandirty);
	list_del_init(&bcb->bcb_entry);
	/*
	 * invalidate cache block
	 */
	bcb->bcb_sector = SECTOR_NUMBER_INVALID;
	bcb->bcb_state = S_INVALID;
	bcb->bcb_hash_data = UINT128_ZERO;
	bcb->bcb_xid = 0ULL;
	/*
	 * reinsert as invalid
	 */
	atomic_inc(&bc->bc_total_entries);
	atomic_inc(&bc->bc_invalid_entries);
	list_del_init(&bcb->bcb_entry);
	list_add(&bcb->bcb_entry, &bc->bc_invalid_entries_list);
	M_ASSERT(RB_EMPTY_NODE(&bcb->bcb_rb_node));
}

static void __cache_block_add(struct bittern_cache *bc, struct cache_block *bcb)
{
	M_ASSERT(bcb->bcb_state == S_INVALID ||
		 bcb->bcb_state == S_CLEAN ||
		 bcb->bcb_state == S_DIRTY);
	switch (bcb->bcb_state) {
	case S_INVALID:
		M_ASSERT(is_sector_number_invalid(bcb->bcb_sector));
		M_ASSERT(bcb->bcb_sector == SECTOR_NUMBER_INVALID);
		/*
		 * don't add invalid entries to the rb tree
		 */
		atomic_inc(&bc->bc_invalid_entries);
		list_add_tail(&bcb->bcb_entry, &bc->bc_invalid_entries_list);
		RB_CLEAR_NODE(&bcb->bcb_rb_node);
		M_ASSERT(uint128_z(bcb->bcb_hash_data));
		break;
	case S_CLEAN:
		cache_track_hash_set(bc, bcb, bcb->bcb_hash_data);
		M_ASSERT(is_sector_number_valid(bcb->bcb_sector));
		M_ASSERT(bcb->bcb_sector >= 0);
		atomic_inc(&bc->bc_valid_entries);
		atomic_inc(&bc->bc_valid_entries_clean);
		list_add_tail(&bcb->bcb_entry, &bc->bc_valid_entries_list);
		RB_CLEAR_NODE(&bcb->bcb_rb_node);
		cache_rb_insert(bc, bcb);
		M_ASSERT(RB_NON_EMPTY_NODE(&bcb->bcb_rb_node));
		M_ASSERT(RB_NON_EMPTY_ROOT(&bc->bc_rb_root));
		/* add to clean list */
		list_add_tail(&bcb->bcb_entry_cleandirty,
			      &bc->bc_valid_entries_clean_list);
		break;
	case S_DIRTY:
		cache_track_hash_set(bc, bcb, bcb->bcb_hash_data);
		M_ASSERT(is_sector_number_valid(bcb->bcb_sector));
		M_ASSERT(bcb->bcb_sector >= 0);
		atomic_inc(&bc->bc_valid_entries);
		atomic_inc(&bc->bc_valid_entries_dirty);
		list_add_tail(&bcb->bcb_entry, &bc->bc_valid_entries_list);
		RB_CLEAR_NODE(&bcb->bcb_rb_node);
		cache_rb_insert(bc, bcb);
		M_ASSERT(RB_NON_EMPTY_NODE(&bcb->bcb_rb_node));
		M_ASSERT(RB_NON_EMPTY_ROOT(&bc->bc_rb_root));
		/* add to dirty list */
		list_add_tail(&bcb->bcb_entry_cleandirty,
			      &bc->bc_valid_entries_dirty_list);
		break;
	default:
		M_ASSERT("unexpected cache state in _ctr switch" == NULL);
		break;
	}
	atomic_inc(&bc->bc_total_entries);
}

int cache_ctr_restore_block(struct bittern_cache *bc,
			    unsigned int block_id,
			    struct cache_block *bcb)
{
	struct cache_block *old_bcb;
	unsigned int old_block_id;
	int ret;
	unsigned long flags;

	__cache_block_initialize(bc, block_id, bcb);

	ASSERT(block_id == bcb->bcb_block_id);
	ret = pmem_block_restore(bc, bcb);
	if (ret < 0) {
		/*
		 * Data corruption -- we'll need to fail the whole restore.
		 */
		printk_err("cache entry id=#%u restore failed, corrupt or bad data\n",
			   block_id);
		return ret;
	}

	spin_lock_irqsave(&bc->bc_entries_lock, flags);

	if (ret == 0) {
		/*
		 * Entry not restored.
		 * This because there was an incomplete transaction.
		 */
		printk_warning("cache entry id=#%u transaction rolled back, re-initializing\n",
			       block_id);

		M_ASSERT(bcb->bcb_state == S_INVALID);
		M_ASSERT(is_sector_number_invalid(bcb->bcb_sector));
		__cache_block_add(bc, bcb);

		spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

		ret = pmem_metadata_initialize(bc, block_id);
		M_ASSERT(ret == 0);

		return 0;
	}

	/*
	 * We can only restore finalized transactions which have a correct
	 * checksum. Anything else would be a transaction in progress
	 */

	M_ASSERT(bcb->bcb_state == S_INVALID ||
		 bcb->bcb_state == S_CLEAN ||
		 bcb->bcb_state == S_DIRTY);

	if (bcb->bcb_state == S_INVALID) {
		/*
		 * Entry is invalid, all done here.
		 */

		__cache_block_add(bc, bcb);
		spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

		printk_debug_ratelimited("cache entry #%d is invalid, nothing to restore\n",
					 block_id);
		return 0;
	}

	M_ASSERT(is_sector_number_valid(bcb->bcb_sector));

	/*
	 * Now check for an old cache_block. If we find one, it means
	 * that we have two different copies of the same cache block. The most
	 * recent copy is indicated by the fact it has the most recent XID,
	 * i.e., the highest number in a monotonically increasing fashion.
	 *
	 * This is a 64 bits quantity. Assuming 10,000,000 IOPS, this number
	 * will rollover approximately in the year 3315. This bug can very
	 * simply be fixed by adopting the same scheme that is used for TCP
	 * sequence numbers and by making sure we'll never have the oldest
	 * entry with a logically lower XID. This can also be very easily fixed
	 * by forcing evicting entries every 100 years or so.
	 * No matter what, it's probably not a priority for the first production
	 * version.
	 */
	old_bcb = cache_rb_lookup(bc, bcb->bcb_sector);
	if (old_bcb == NULL) {

		/*
		 * No old cache block, it's all good here.
		 */

		__cache_block_add(bc, bcb);
		spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

		printk_debug_ratelimited("cache entry id=#%u, sector=%lu, state=%d(%s) restored\n",
					 block_id,
					 bcb->bcb_sector,
					 bcb->bcb_state,
					 cache_state_to_str(bcb->bcb_state));

		return 0;
	}

	/*
	 * Oooops, two cache blocks caching the same data.
	 * Use XID to find out which version is the most recent
	 * one, and discard the other one.
	 */

	old_block_id = old_bcb->bcb_block_id;
	M_ASSERT(is_sector_number_valid(old_bcb->bcb_sector));
	M_ASSERT(old_bcb->bcb_sector == bcb->bcb_sector);

	printk_info("cache_block id=#%u, xid=#%llu, sector=%lu, state=%d(%s): old bcb found\n",
		    bcb->bcb_block_id,
		    bcb->bcb_xid,
		    bcb->bcb_sector,
		    bcb->bcb_state,
		    cache_state_to_str(bcb->bcb_state));
	printk_info("block old_bcb=%p, old_id=#%d, old_xid=#%llu, old_cache_block=%lu, old_state=%d(%s): old bcb found\n",
		    old_bcb, old_bcb->bcb_block_id,
		    old_bcb->bcb_xid, old_bcb->bcb_sector,
		    old_bcb->bcb_state,
		    cache_state_to_str(old_bcb->bcb_state));
	M_ASSERT(old_bcb->bcb_state == S_CLEAN ||
		 old_bcb->bcb_state == S_DIRTY);
	/*
	 * This just cannot happen.
	 * Period.
	 */
	if (bcb->bcb_xid == old_bcb->bcb_xid) {

		__cache_block_add(bc, bcb);
		__cache_block_invalidate(bc, bcb);

		spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

		/*
		 * Don't reinitialize, otherwise if we try to restore
		 * again we won't hit the error and possibly corrupt data
		 * even more.
		 */

		printk_err("fatal error: old_bcb=%p block_id #%llu is the same as bcb=%p\n",
			   old_bcb,
			   old_bcb->bcb_xid,
			   old_bcb);
		/*
		 * I think this errno code makes sense. Besides,
		 * how many times in your life you get to use this one?
		 */
		return -EL2NSYNC;
	}

	M_ASSERT(is_sector_number_valid(old_bcb->bcb_sector));

	/*
	 * If the new cache_block XID is less than the old cache block XID,
	 * wipe out new cache_block.
	 */
	if (bcb->bcb_xid < old_bcb->bcb_xid) {

		__cache_block_add(bc, bcb);
		__cache_block_invalidate(bc, bcb);

		spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

		printk_info_ratelimited("keeping old_cache_block, old_xid=#%llu",
					old_bcb->bcb_xid);

		ret = pmem_metadata_initialize(bc, block_id);
		M_ASSERT(ret == 0);

		return 0;
	}

	M_ASSERT(bcb->bcb_xid > old_bcb->bcb_xid);

	/*
	 * New cache_block XID is higher than the old cache block XID,
	 * wipe out old cache_block.
	 */

	__cache_block_invalidate(bc, old_bcb);
	__cache_block_add(bc, bcb);

	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	printk_info_ratelimited("cache restore: keeping new cache_block, reinitializing cache entry id=#%u\n",
				old_block_id);

	ret = pmem_metadata_initialize(bc, old_block_id);
	M_ASSERT(ret == 0);

	return 0;
}

enum cache_device_op {
	CACHE_DEVICE_OP_CREATE,
	CACHE_DEVICE_OP_RESTORE,
};

int cache_ctr_restore_or_init_block(struct bittern_cache *bc,
				    unsigned int block_id,
				    enum cache_device_op cache_operation,
				    const char *cache_operation_str)
{
	/* block_id starts from 1, array starts from 0 */
	struct cache_block *bcb = &bc->bc_cache_blocks[block_id - 1];
	unsigned long flags;
	int ret;

	if (cache_operation == CACHE_DEVICE_OP_RESTORE) {
		/*
		 * Cache restore
		 */
		ret = cache_ctr_restore_block(bc, block_id, bcb);
		if (ret < 0)
			return ret;
	} else {
		/*
		 * Cache create
		 */
		printk_debug_ratelimited("cache create: initializing cache entry %u state %s\n",
					 block_id,
					 cache_state_to_str(bcb->bcb_state));

		spin_lock_irqsave(&bc->bc_entries_lock, flags);

		__cache_block_initialize(bc, block_id, bcb);
		__cache_block_add(bc, bcb);

		spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

		ret = pmem_metadata_initialize(bc, block_id);
		M_ASSERT(ret == 0);
	}

	spin_lock_irqsave(&bc->bc_entries_lock, flags);

	if (bcb->bcb_xid > cache_xid_get(bc)) {
		printk_info_ratelimited("block id #%u: id=%u, status=%u(%s), xid=%llu, sector=%lu: xid=%llu, bc_xid=%llu, updating bc_xid\n",
					block_id,
					bcb->bcb_block_id,
					bcb->bcb_state,
					cache_state_to_str(bcb->bcb_state),
					bcb->bcb_xid,
					bcb->bcb_sector,
					bcb->bcb_xid,
					cache_xid_get(bc));
		cache_xid_set(bc, bcb->bcb_xid);
	}
	__ASSERT_CACHE_BLOCK(bcb, bc);

	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);

	printk_info_ratelimited("'%s' bcb=%p, id=#%d, cache_block=%lu, state=%d(%s)\n",
				cache_operation_str,
				bcb,
				bcb->bcb_block_id,
				bcb->bcb_sector,
				bcb->bcb_state,
				cache_state_to_str(bcb->bcb_state));

	return 0;
}

/*!
 * It's most likely not very important the exact amount of restore threads,
 * all it matters is the ability of keeping a minimum amount of in-flight
 * requests. Once the throughput of the cache device is saturated, there is no
 * further reason to increase parallelism.
 */
#define RESTORE_WORKQUEUES		128
#define RESTORE_WORKQUEUE_MAGIC	0xf10c7c34
struct restore_workqueue {
	unsigned int magic;
	unsigned int modulo_offset;
	struct bittern_cache *bc;
	char *cache_op_str;
	enum cache_device_op cache_op;
	struct workqueue_struct *workqueue;
	struct work_struct work;
	unsigned int restored;
	int ret;
};
#define RESTORE_WORKQUEUES_SIZE_BYTES	\
		(sizeof(struct restore_workqueue) * RESTORE_WORKQUEUES)

void cache_ctr_restore_or_init(struct bittern_cache *bc,
			       struct restore_workqueue *r_wq)
{
	unsigned int mod_base;
	int ret = 0;

	printk_debug("start '%s' modulo_offset %d: ret=%d\n",
		     r_wq->cache_op_str,
		     r_wq->modulo_offset,
		     r_wq->ret);

	ASSERT(r_wq->magic == RESTORE_WORKQUEUE_MAGIC);
	ASSERT(r_wq->cache_op == CACHE_DEVICE_OP_CREATE ||
	       r_wq->cache_op == CACHE_DEVICE_OP_RESTORE);
	__ASSERT_BITTERN_CACHE(bc);

	for (mod_base = 1;
	     mod_base <= bc->bc_papi.papi_hdr.lm_cache_blocks;
	     mod_base += RESTORE_WORKQUEUES) {
		unsigned int block_id = mod_base + r_wq->modulo_offset;

		if (block_id > bc->bc_papi.papi_hdr.lm_cache_blocks) {
			/*
			 * This happens when the number of cache blocks is
			 * not an exact multiple of RESTORE_WORKQUEUE_MODULO.
			 */
			break;
		}

		M_ASSERT(block_id >= 1);
		M_ASSERT(block_id <= bc->bc_papi.papi_hdr.lm_cache_blocks);
		ret = cache_ctr_restore_or_init_block(bc,
						      block_id,
						      r_wq->cache_op,
						      r_wq->cache_op_str);
		if (ret != 0) {
			printk_err("cache entry %u:%u '%s' failed: ret=%d (corrupt/bad data)\n",
				   block_id,
				   r_wq->modulo_offset,
				   r_wq->cache_op_str,
				   ret);
			break;
		}
		printk_debug_ratelimited("cache entry %u:%u '%s' ok: ret=%d\n",
					 block_id,
					 r_wq->modulo_offset,
					 r_wq->cache_op_str,
					 ret);
		r_wq->restored++;
	}

	r_wq->ret = ret;
	printk_debug("done '%s' modulo_offset %d: restored=%u, ret=%d\n",
		     r_wq->cache_op_str,
		     r_wq->modulo_offset,
		     r_wq->restored,
		     r_wq->ret);
}

void cache_ctr_restore_or_init_wq(struct work_struct *work)
{
	struct restore_workqueue *r_wq;

	ASSERT(work != NULL);
	r_wq = container_of(work, struct restore_workqueue, work);
	ASSERT(r_wq->magic == RESTORE_WORKQUEUE_MAGIC);
	ASSERT(r_wq->cache_op == CACHE_DEVICE_OP_CREATE ||
	       r_wq->cache_op == CACHE_DEVICE_OP_RESTORE);
	__ASSERT_BITTERN_CACHE(r_wq->bc);

	printk_info("restore_or_init_wq: '%s' block offsets modulo_offset %d\n",
		    r_wq->cache_op_str,
		    r_wq->modulo_offset);

	cache_ctr_restore_or_init(r_wq->bc, r_wq);

	ASSERT(r_wq->magic == RESTORE_WORKQUEUE_MAGIC);
	ASSERT(r_wq->cache_op == CACHE_DEVICE_OP_CREATE ||
	       r_wq->cache_op == CACHE_DEVICE_OP_RESTORE);
	__ASSERT_BITTERN_CACHE(r_wq->bc);
	printk_info("restore_or_init_wq: done '%s' block offsets modulo_offset %d: ret=%d\n",
		    r_wq->cache_op_str,
		    r_wq->modulo_offset,
		    r_wq->ret);
}

int cache_ctr_restore_or_init_workqueues(struct bittern_cache *bc,
					 char *cache_operation_str,
					 enum cache_device_op cache_operation)
{
	int i;
	struct restore_workqueue *workqueues;
	uint64_t tstamp, tstamp_end;
	int ret = 0;
	unsigned int total_restored = 0;
	unsigned int block_id;

	printk_info("restore_or_init_workqueues: '%s': workqueues=%d\n",
		    cache_operation_str,
		    RESTORE_WORKQUEUES);

	/*
	 * Fill it with garbage so when we can verify all blocks have been
	 * initialized after parallel restore/init.
	 */
	memset(bc->bc_cache_blocks,
	       0xac,
	       (sizeof(struct cache_block) *
		bc->bc_papi.papi_hdr.lm_cache_blocks));

	tstamp = current_kernel_time_nsec();

	workqueues = kmem_zalloc(RESTORE_WORKQUEUES_SIZE_BYTES, GFP_NOIO);
	if (workqueues == NULL) {
		printk_err("%s: cannot allocate workqueue array\n",
			   bc->bc_name);
		return -ENOMEM;
	}

	/*
	 * Step #1: start restore on multiple independent workqueues.
	 */

	for (i = 0; i < RESTORE_WORKQUEUES; i++) {
		struct restore_workqueue *r_wq = &workqueues[i];
		int ret;

		r_wq->magic = RESTORE_WORKQUEUE_MAGIC;
		r_wq->modulo_offset = i;
		r_wq->bc = bc;
		r_wq->cache_op_str = cache_operation_str;
		r_wq->cache_op = cache_operation;
		r_wq->workqueue = alloc_workqueue("r_wkq:%s",
					(WQ_UNBOUND | WQ_CPU_INTENSIVE),
					1,
					bc->bc_name);
		if (r_wq->workqueue == NULL) {
			printk_err("%s: cannot allocate workqueue\n",
				   bc->bc_name);
			ret = -ENOMEM;
			break;
		}

		INIT_WORK(&r_wq->work, cache_ctr_restore_or_init_wq);
		ret = queue_work(r_wq->workqueue, &r_wq->work);
		printk_info("started restore_workqueue[%d] = %d: %p\n",
			    i,
			    ret,
			    r_wq->workqueue);

		M_ASSERT(ret == 1);
	}

	for (i = 0; i < RESTORE_WORKQUEUES; i++) {
		struct restore_workqueue *r_wq = &workqueues[i];

		if (r_wq->workqueue == NULL)
			break;

		M_ASSERT(r_wq->magic == RESTORE_WORKQUEUE_MAGIC);
		M_ASSERT(r_wq->cache_op == CACHE_DEVICE_OP_CREATE ||
			 r_wq->cache_op == CACHE_DEVICE_OP_RESTORE);
		printk_info("flushing restore_workqueue[%d] %p\n",
			    i,
			    r_wq->workqueue);
		flush_workqueue(r_wq->workqueue);
		printk_info("done flushing restore_workqueue[%d] %p: restored=%u, ret=%d\n",
			    i,
			    r_wq->workqueue,
			    r_wq->restored,
			    r_wq->ret);
		destroy_workqueue(r_wq->workqueue);
		if (r_wq->ret != 0) {
			ret = r_wq->ret;
			ASSERT(ret < 0);
		}
		total_restored += r_wq->restored;
	}

	kmem_free(workqueues, RESTORE_WORKQUEUES_SIZE_BYTES);

	tstamp_end = current_kernel_time_nsec();

	printk_info("restore_or_init_workqueues: '%s': workqueues=%d: %llu milliseconds\n",
		    cache_operation_str,
		    RESTORE_WORKQUEUES,
		    (tstamp_end - tstamp) / 1000000ULL);
	printk_info("restore_or_init_workqueues: '%s': workqueues=%d: ret=%d\n",
		    cache_operation_str,
		    RESTORE_WORKQUEUES,
		    ret);
	printk_info("restore_or_init_workqueues: '%s': workqueues=%d: total_restored=%u/%llu\n",
		    cache_operation_str,
		    RESTORE_WORKQUEUES,
		    total_restored,
		    bc->bc_papi.papi_hdr.lm_cache_blocks);

	if (ret < 0)
		return ret;

	M_ASSERT(total_restored ==
		 bc->bc_papi.papi_hdr.lm_cache_blocks);
	for (block_id = 1;
	     block_id <= bc->bc_papi.papi_hdr.lm_cache_blocks;
	     block_id++) {
		struct cache_block *bcb = &bc->bc_cache_blocks[block_id - 1];

		__ASSERT_CACHE_BLOCK(bcb, bc);
		M_ASSERT(bcb->bcb_state == S_INVALID ||
			 bcb->bcb_state == S_CLEAN ||
			 bcb->bcb_state == S_DIRTY);
	}
	M_ASSERT(atomic_read(&bc->bc_total_entries) ==
		 bc->bc_papi.papi_hdr.lm_cache_blocks);
	M_ASSERT(atomic_read(&bc->bc_total_entries) ==
		 (atomic_read(&bc->bc_invalid_entries) +
		  atomic_read(&bc->bc_valid_entries_clean) +
		  atomic_read(&bc->bc_valid_entries_dirty)));

	return 0;
}

/*
 * Mapping parameters:
 *    <cache_operation> <device_being_cached> <cache_device_name>
 *    <cache_device_size> <cache_device_type> <cache_device_blockdev_path>
 *
 * Example:
 *
 * create /dev/sdc1 bitcache0 1024 pmem_nvdimm /dev/adrbd0
 *
 * The above arguments tell bittern to create a cache named "bitcache0",
 * the device being cached is /dev/sdc1,
 * the cache size is 1024 mbytes, of type NVDIMM, which has a device path of
 * /dev/adrbd0
 *
 */
int cache_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct bittern_cache *bc;
	int i, ret;
	char *cached_device_name;
	char *cache_device_blockdev_path;
	char *cache_operation_str;
	enum cache_device_op cache_operation;

	printk_info("argc %d\n", argc);
	for (i = 0; i < argc; i++)
		printk_info("argv[%d] = '%s'\n", i, argv[i]);
	if (argc != 3) {
		ti->error = "requires exactly 3 arguments";
		return -EINVAL;
	}

	M_ASSERT(MAX_IO_LEN_PAGES == 1);

	cache_operation_str = argv[0];
	cached_device_name = argv[1];
	cache_device_blockdev_path = argv[2];

	if (strcmp(cache_operation_str, "create") == 0) {
		cache_operation = CACHE_DEVICE_OP_CREATE;
		cache_operation_str = "cache-create";
	} else if (strcmp(cache_operation_str, "restore") == 0) {
		cache_operation = CACHE_DEVICE_OP_RESTORE;
		cache_operation_str = "cache-restore";
	} else {
		ti->error = "unknown operation requested";
		printk_err("error : %s (supported operations are 'create' and 'restore'\n",
			   ti->error);
		return -EINVAL;
	}
	printk_info("cache_operation=0x%x\n", cache_operation);

	if (strlen(cached_device_name) >= BC_NAMELEN) {
		ti->error = "cached device name too long";
		printk_err("error : %s (max len is %d)\n",
			   ti->error,
			   BC_NAMELEN - 1);
		return -EINVAL;
	}

	printk_info("cached_device_name=%s\n", cached_device_name);
	printk_info("cache device blockdev path: '%s'\n",
		    cache_device_blockdev_path);

	printk_info("device: ti->begin=%lu\n", ti->begin);
	printk_info("device: ti->len=%lu\n", ti->len);
	printk_info("device: ti->begin=%lu mbytes\n", ti->begin / 2048);
	printk_info("device: ti->len=%lu mbytes\n", ti->len / 2048);
	if (ti->begin != 0) {
		printk_err("error : device: non-zero begin=%lu is not supported\n",
			   ti->begin);
		return -EINVAL;
	}
	if ((ti->len % SECTORS_PER_CACHE_BLOCK) != 0) {
		printk_err("error : cached device %s size is not a multiple of cache block size\n",
			   cached_device_name);
		ti->error =
		    "cached device size is not a multiple of cache block size";
		return -EINVAL;
	}

	/*
	 * The initialization code assumes a zeroed bittern_cache structure.
	 */
	bc = vzalloc(sizeof(struct bittern_cache));
	if (bc == NULL) {
		ti->error = "cannot allocate context";
		printk_err("error : %s\n", ti->error);
		return -ENOMEM;
	}
	printk_info("vmalloc: bc = %p (sizeof = %lu)\n",
		    bc,
		    (unsigned long)sizeof(struct bittern_cache));

	bc->bc_magic1 = BC_MAGIC1;
	bc->bc_magic2 = BC_MAGIC2;
	bc->bc_magic3 = BC_MAGIC3;
	bc->bc_magic4 = BC_MAGIC4;

	/*
	 * Need to initialize sysfs before actually trying anything else,
	 * otherwise deinit call might fail in the error handling path.
	 */
	cache_sysfs_init(bc);

	ret = dm_get_device(ti,
			    cache_device_blockdev_path,
			    FMODE_READ | FMODE_WRITE | FMODE_EXCL,
			    &bc->bc_cache_dev);
	if (ret != 0) {
		printk_err("cache device lookup %s failed: ret=%d\n",
			   cache_device_blockdev_path,
			   ret);
		ti->error = "cache device lookup failed";
		goto bad_0;
	}

	/*
	 * FIXME: need to make these string names in bittern_cache struct
	 * consistent with arg names
	 */
	strlcpy(bc->bc_name,
		dm_device_name(dm_table_get_md(ti->table)),
		sizeof(bc->bc_name));
	strlcpy(bc->bc_cache_device_name,
		cache_device_blockdev_path,
		sizeof(bc->bc_cache_device_name));
	strlcpy(bc->bc_cached_device_name,
		cached_device_name,
		sizeof(bc->bc_cached_device_name));

	bc->bc_replacement_mode = CACHE_REPLACEMENT_MODE_DEFAULT;
	bc->bc_cache_mode_writeback = 1; /* we now default to writeback */
	bc->bc_enable_req_fua = true;

	cache_xid_set(bc, 1);

	bc->error_state = ES_NOERROR;

	atomic_set(&bc->error_count, 0);
	atomic_set(&bc->bc_total_deferred_requests, 0);
	atomic_set(&bc->bc_deferred_requests, 0);
	atomic_set(&bc->bc_highest_deferred_requests, 0);
	atomic_set(&bc->bc_read_requests, 0);
	atomic_set(&bc->bc_write_requests, 0);
	atomic_set(&bc->bc_pending_requests, 0);
	atomic_set(&bc->bc_pending_read_requests, 0);
	atomic_set(&bc->bc_pending_read_bypass_requests, 0);
	atomic_set(&bc->bc_pending_write_requests, 0);
	atomic_set(&bc->bc_pending_writeback_requests, 0);
	atomic_set(&bc->bc_pending_invalidate_requests, 0);
	atomic_set(&bc->bc_highest_pending_requests, 0);
	atomic_set(&bc->bc_highest_pending_invalidate_requests, 0);
	atomic_set(&bc->bc_completed_requests, 0);
	atomic_set(&bc->bc_completed_read_requests, 0);
	atomic_set(&bc->bc_completed_write_requests, 0);
	atomic_set(&bc->bc_completed_writebacks, 0);
	atomic_set(&bc->bc_completed_invalidations, 0);
	atomic_set(&bc->bc_read_cached_device_requests, 0);
	atomic_set(&bc->bc_write_cached_device_requests, 0);
	atomic_set(&bc->bc_pending_cached_device_requests, 0);
	atomic_set(&bc->bc_highest_pending_cached_device_requests, 0);
	atomic_set(&bc->bc_total_read_misses, 0);
	atomic_set(&bc->bc_total_read_hits, 0);
	atomic_set(&bc->bc_total_write_misses, 0);
	atomic_set(&bc->bc_total_write_hits, 0);
	atomic_set(&bc->bc_clean_read_hits, 0);
	atomic_set(&bc->bc_read_misses, 0);
	atomic_set(&bc->bc_clean_write_hits, 0);
	atomic_set(&bc->bc_clean_write_hits_rmw, 0);
	atomic_set(&bc->bc_clean_write_misses, 0);
	atomic_set(&bc->bc_clean_write_misses_rmw, 0);
	atomic_set(&bc->bc_dirty_read_hits, 0);
	atomic_set(&bc->bc_dirty_write_hits, 0);
	atomic_set(&bc->bc_dirty_write_hits_rmw, 0);
	atomic_set(&bc->bc_dirty_write_misses, 0);
	atomic_set(&bc->bc_dirty_write_misses_rmw, 0);
	atomic_set(&bc->bc_read_hits_busy, 0);
	atomic_set(&bc->bc_write_hits_busy, 0);
	atomic_set(&bc->bc_read_misses_busy, 0);
	atomic_set(&bc->bc_write_misses_busy, 0);
	atomic_set(&bc->bc_writebacks, 0);
	atomic_set(&bc->bc_writebacks_clean, 0);
	atomic_set(&bc->bc_writebacks_invalid, 0);
	atomic_set(&bc->bc_writebacks_stalls, 0);
	atomic_set(&bc->bc_invalidations, 0);
	atomic_set(&bc->bc_idle_invalidations, 0);
	atomic_set(&bc->bc_busy_invalidations, 0);
	atomic_set(&bc->bc_no_invalidations_all_blocks_busy, 0);
	atomic_set(&bc->bc_invalidations_map, 0);
	atomic_set(&bc->bc_invalidations_invalidator, 0);
	atomic_set(&bc->bc_invalidations_writeback, 0);
	atomic_set(&bc->bc_invalid_blocks_busy, 0);
	atomic_set(&bc->bc_flush_requests, 0);
	atomic_set(&bc->bc_pure_flush_requests, 0);
	atomic_set(&bc->bc_discard_requests, 0);
	atomic_set(&bc->bc_dirty_write_clone_alloc_ok, 0);
	atomic_set(&bc->bc_dirty_write_clone_alloc_fail, 0);

	for (i = 0; i < __TS_NUM; i++)
		atomic_set(&bc->bc_cache_transitions_counters[i], 0);
	for (i = 0; i < __CACHE_STATES_NUM; i++)
		atomic_set(&bc->bc_cache_states_counters[i], 0);

	bc->bc_ti = ti;

	atomic_set(&bc->bc_valid_entries, 0);
	atomic_set(&bc->bc_valid_entries_dirty, 0);
	atomic_set(&bc->bc_valid_entries_clean, 0);
	atomic_set(&bc->bc_invalid_entries, 0);
	atomic_set(&bc->bc_total_entries, 0);
	INIT_LIST_HEAD(&bc->bc_invalid_entries_list);
	INIT_LIST_HEAD(&bc->bc_valid_entries_list);
	INIT_LIST_HEAD(&bc->bc_valid_entries_dirty_list);
	INIT_LIST_HEAD(&bc->bc_valid_entries_clean_list);
	INIT_LIST_HEAD(&bc->bc_pending_requests_list);
	bc->bc_rb_root = RB_ROOT;
	/*
	 * this spinlock protects both bc_entries_list and bc_rb_root
	 */
	spin_lock_init(&bc->bc_entries_lock);

	bc->bc_rb_hit_loop_sum = 0;
	bc->bc_rb_miss_loop_sum = 0;
	bc->bc_rb_hit_loop_count = 0;
	bc->bc_rb_miss_loop_count = 0;
	bc->bc_rb_hit_loop_max = 0;
	bc->bc_rb_miss_loop_max = 0;

	cache_timer_init(&bc->bc_timer_reads);
	cache_timer_init(&bc->bc_timer_writes);
	cache_timer_init(&bc->bc_timer_read_hits);
	cache_timer_init(&bc->bc_timer_write_hits);
	cache_timer_init(&bc->bc_timer_read_misses);
	cache_timer_init(&bc->bc_timer_write_misses);
	cache_timer_init(&bc->bc_timer_write_dirty_misses);
	cache_timer_init(&bc->bc_timer_write_clean_misses);
	cache_timer_init(&bc->bc_timer_read_clean_hits);
	cache_timer_init(&bc->bc_timer_write_clean_hits);
	cache_timer_init(&bc->bc_timer_read_dirty_hits);
	cache_timer_init(&bc->bc_timer_write_dirty_hits);
	cache_timer_init(&bc->bc_timer_cached_device_reads);
	cache_timer_init(&bc->bc_timer_cached_device_writes);
	cache_timer_init(&bc->bc_timer_cached_device_flushes);
	cache_timer_init(&bc->bc_timer_writebacks);
	cache_timer_init(&bc->bc_timer_invalidations);
	cache_timer_init(&bc->bc_timer_pending_queue);
	cache_timer_init(&bc->bc_timer_resource_alloc_reads);
	cache_timer_init(&bc->bc_timer_resource_alloc_writes);
	cache_timer_init(&bc->defer_busy.timer);
	cache_timer_init(&bc->defer_page.timer);

	bc->devio.conf_worker_delay = CACHED_DEV_WORKER_DELAY_DEFAULT;
	bc->devio.conf_fua_insert = CACHED_DEV_FUA_INSERT_DEFAULT;
	spin_lock_init(&bc->devio.spinlock);
	INIT_LIST_HEAD(&bc->devio.pending_list);
	INIT_LIST_HEAD(&bc->devio.flush_pending_list);
	INIT_DELAYED_WORK(&bc->devio.flush_delayed_work, cached_devio_flush_delayed_worker);
	bc->devio.flush_wq = alloc_workqueue("b_dvf:%s",
					      WQ_UNBOUND,
					      1,
					      bc->bc_name);
	if (bc->devio.flush_wq == NULL) {
		printk_err("%s: cannot allocate dev flush workqueue\n",
			   bc->bc_name);
		ret = -ENOMEM;
		ti->error = "cannot allocate dev flush workqueue";
		goto bad_0;
	}

	pmem_info_initialize(bc);

	ret = pmem_allocate(bc, bc->bc_cache_dev->bdev);
	if (ret != 0) {
		ti->error = "cannot allocate pmem resource";
		goto bad_0;
	}

	ret = seq_bypass_initialize(bc);
	if (ret != 0) {
		ti->error = "cannot allocate seq_bypass resources";
		goto bad_1;
	}

	ret = dm_get_device(ti,
			    cached_device_name,
			    FMODE_EXCL | FMODE_READ | FMODE_WRITE,
			    &bc->devio.dm_dev);
	if (ret != 0) {
		printk_err("cached device lookup %s failed: ret=%d\n",
			   cached_device_name,
			   ret);
		ti->error = "cached device lookup failed";
		goto bad_1;
	}
	printk_info("cached device lookup %s ok\n", cached_device_name);
	M_ASSERT(bc->devio.dm_dev != NULL);
	printk_info("cached device devio.dm_dev=%p, devio.dm_dev->bdev=%p\n",
		    bc->devio.dm_dev,
		    bc->devio.dm_dev->bdev);
	M_ASSERT(bc->devio.dm_dev->bdev != NULL);
	printk_info("cached device bc_dev->bdev->bd_disk=%p, bc_dev->bdev->bd_queue=%p\n",
		    bc->devio.dm_dev->bdev->bd_disk,
		    bc->devio.dm_dev->bdev->bd_queue);
	M_ASSERT(bc->devio.dm_dev->bdev->bd_part != NULL);
	bc->bc_cached_device_size_bytes =
				bc->devio.dm_dev->bdev->bd_part->nr_sects *
				SECTOR_SIZE;
	bc->bc_cached_device_size_mbytes = bc->bc_cached_device_size_bytes /
					   (1024ULL * 1024ULL);
	M_ASSERT(bc->bc_cached_device_size_bytes > 0);
	M_ASSERT(bc->bc_cached_device_size_mbytes > 0);
	printk_info("cached device size = %llu bytes (%llu mbytes)\n",
		    bc->bc_cached_device_size_bytes,
		    bc->bc_cached_device_size_mbytes);

#ifdef ENABLE_TRACK_CRC32C
	bc->bc_tracked_hashes_num = bc->bc_cached_device_size_bytes / PAGE_SIZE;
	printk_info("need %lu entries to track crc32c checksums on cached device\n",
		    bc->bc_tracked_hashes_num);
	if (bc->bc_tracked_hashes_num > CACHE_MAX_TRACK_HASH_CHECKSUMS) {
		printk_info("%lu entries is greater than maximum allowed %lu, clamping\n",
			    bc->bc_tracked_hashes_num,
			    CACHE_MAX_TRACK_HASH_CHECKSUMS);
		bc->bc_tracked_hashes_num = CACHE_MAX_TRACK_HASH_CHECKSUMS;
	}
	bc->bc_tracked_hashes = vmalloc(round_up
		    (((bc->bc_tracked_hashes_num + 4UL) * sizeof(uint128_t)),
			PAGE_SIZE));
	printk_info("vmalloc: bc_tracked_hashes=%p, bc_tracked_hashes_num=%lu, space used=%lu bytes\n",
	     bc->bc_tracked_hashes, bc->bc_tracked_hashes_num,
	     round_up(((bc->bc_tracked_hashes_num + 4UL) * sizeof(uint128_t)),
		      PAGE_SIZE));
	if (bc->bc_tracked_hashes == NULL) {
		ti->error = "cannot allocate crc32c tracking info";
		goto bad_1;
	}
	M_ASSERT_FIXME(bc->bc_tracked_hashes != NULL);
	memset(bc->bc_tracked_hashes, 0,
	       ((bc->bc_tracked_hashes_num + 4) * sizeof(uint128_t)));
	bc->bc_tracked_hashes[0] = CACHE_TRACK_HASH_MAGIC0;
	bc->bc_tracked_hashes[1] = CACHE_TRACK_HASH_MAGIC1;
	bc->bc_tracked_hashes[bc->bc_tracked_hashes_num + 2] =
		CACHE_TRACK_HASH_MAGICN;
	bc->bc_tracked_hashes[bc->bc_tracked_hashes_num + 3] =
		CACHE_TRACK_HASH_MAGICN1;
	atomic_set(&bc->bc_tracked_hashes_set, 0);
	atomic_set(&bc->bc_tracked_hashes_clear, 0);
	atomic_set(&bc->bc_tracked_hashes_null, 0);
	atomic_set(&bc->bc_tracked_hashes_ok, 0);
	atomic_set(&bc->bc_tracked_hashes_bad, 0);
#endif /*ENABLE_TRACK_CRC32C */

#if defined(ENABLE_EXTRA_CHECKSUM_CHECK) || defined(ENABLE_TRACK_CRC32C)
	/*
	 * this is also runtime configurable - although we do not allow
	 * disabling if ENABLE_TRACK_CRC32C is defined
	 */
	bc->bc_enable_extra_checksum_check = 1;
#else /*ENABLE_EXTRA_CHECKSUM_CHECK */
	bc->bc_enable_extra_checksum_check = 0;
#endif /*ENABLE_EXTRA_CHECKSUM_CHECK */

	bc->bc_kmem_map = kmem_cache_create("bc_kmem_map",
					    PAGE_SIZE,
					    PAGE_SIZE,
					    0,
					    NULL);
	printk_info("kem_cache_create: bc_kmem_map=%p\n", bc->bc_kmem_map);
	M_ASSERT_FIXME(bc->bc_kmem_map != NULL);

	bc->bc_kmem_threads = kmem_cache_create("bc_kmem_threads",
						PAGE_SIZE,
						PAGE_SIZE,
						0,
						NULL);
	printk_info("kem_cache_create: bc_kmem_threads=%p\n",
		    bc->bc_kmem_threads);
	M_ASSERT_FIXME(bc->bc_kmem_threads != NULL);

	/*
	 * this is also used very early
	 * \todo should have its own init function for this
	 */
	spin_lock_init(&bc->defer_lock);
	bio_list_init(&bc->defer_busy.list);
	bio_list_init(&bc->defer_page.list);
	bc->defer_wq = alloc_workqueue("dfr_wk:%s", WQ_UNBOUND, 1, bc->bc_name);
	if (bc->defer_wq == NULL) {
		ti->error = "cannot allocate dfr_wk workqueue";
		printk_err("%s: cannot allocate dfr_wk workqueue\n",
			   bc->bc_name);
		goto bad_1;
	}
	INIT_WORK(&bc->defer_work, cache_deferred_worker);

	/*
	 * we do a header restore no matter what.
	 * if the operation is create and restore is successful (which means
	 * there is an existing cache), then we need to fail.
	 */
	M_ASSERT(cache_operation == CACHE_DEVICE_OP_CREATE ||
		 cache_operation == CACHE_DEVICE_OP_RESTORE);
	ret = pmem_header_restore(bc);
	printk_info("pmem_header_restore: ret=%d\n", ret);

	if (ret == 0 && cache_operation == CACHE_DEVICE_OP_CREATE) {
		printk_err("cache create will overwrite existing cache\n");
		ti->error = "create create will overwrite existing cache";
		goto bad_1;
	}

	if (ret != 0 && cache_operation == CACHE_DEVICE_OP_RESTORE) {
		printk_err("cache restore failed (no header), ret=%d\n", ret);
		ti->error = "cache restore failed (no header)";
		goto bad_1;
	}

	/*
	 * for cache create we need to initialize metadata
	 */
	if (cache_operation == CACHE_DEVICE_OP_CREATE) {
		printk_info("%s: initializing in-struct metadata header and ram metadata header copies\n",
			    cache_operation_str);
		ret = pmem_header_initialize(bc);
		M_ASSERT_FIXME(ret == 0);
		M_ASSERT(bc->bc_papi.papi_hdr.lm_cache_blocks > 0);
	}

	M_ASSERT(bc->bc_papi.papi_hdr.lm_cache_blocks > 0);
	printk_info("bc->bc_papi.papi_hdr.lm_cache_blocks=%llu\n",
		    bc->bc_papi.papi_hdr.lm_cache_blocks);
	printk_info("bc->bc_papi.papi_hdr.lm_mcb_size_bytes=%llu\n",
		    bc->bc_papi.papi_hdr.lm_mcb_size_bytes);

	bc->bc_cache_blocks = vmalloc(sizeof(struct cache_block) *
				      bc->bc_papi.papi_hdr.lm_cache_blocks);

	printk_info("vmalloc: bc->bc_cache_blocks = %p\n", bc->bc_cache_blocks);
	printk_info("vmalloc: bc->bc_cache_blocks = %llu bytes\n",
		    sizeof(struct cache_block) *
		    bc->bc_papi.papi_hdr.lm_cache_blocks);

	if (bc->bc_cache_blocks == NULL) {
		ti->error = "cannot allocate memory for cache_blocks";
		printk_err("error : %s\n", ti->error);
		goto bad_1;
	}

	cache_calculate_max_pending(bc, CACHE_MAX_PENDING_REQUESTS_DEFAULT);
	M_ASSERT(bc->bc_max_pending_requests > 0);

	printk_info("bc_empty_root=%d\n", RB_EMPTY_ROOT(&bc->bc_rb_root));
	printk_info("cache_rb_first=%p\n", cache_rb_first(bc));
	printk_info("cache_rb_last=%p\n", cache_rb_last(bc));
	M_ASSERT(RB_EMPTY_ROOT(&bc->bc_rb_root));
	M_ASSERT(cache_rb_first(bc) == NULL);
	M_ASSERT(cache_rb_last(bc) == NULL);

	/*
	 * initialize all the task variables and especially waitqueues before
	 * starting all the kthreads
	 */
	printk_info("initializing all kthread structures\n");

	bc->bc_verifier_running = 0;
	bc->bc_verifier_task = NULL;
	bc->bc_verifier_scan_delay_ms =
				CACHE_VERIFIER_BLOCK_SCAN_DELAY_DEFAULT_MS;
	bc->bc_verifier_bug_on_verify_errors = 1;
	init_waitqueue_head(&bc->bc_verifier_wait);

	/*
	 * deferred io variables have been initialized earlier
	 */

#if 1
	printk_info("TEMPORARY -- REMOVE ME -- setting flush_on_exit\n");
	bc->bc_bgwriter_conf_flush_on_exit = 1;
#endif

	bc->bc_bgwriter_conf_cluster_size = CACHE_BGWRITER_DEFAULT_CLUSTER_SIZE;
	/* medium gredyness */
	bc->bc_bgwriter_conf_greedyness = 0;
	bc->bc_bgwriter_conf_max_queue_depth_pct =
		CACHE_BGWRITER_DEFAULT_QUEUE_DEPTH_PCT;
	M_ASSERT(bc->bc_max_pending_requests > 0);
	init_waitqueue_head(&bc->bc_bgwriter_wait);

	cache_bgwriter_policy_init(bc);

	init_waitqueue_head(&bc->bc_invalidator_wait);
	cache_calculate_min_invalid(bc, INVALIDATOR_DEFAULT_INVALID_COUNT);

	printk_info("initializing workqueues\n");
	/*
	 * TODO:
	 * these alloc_workqueue params are the same as create_workqueue().
	 * Should play with (WQ_UNBOUND, WQ_RECLAIM, WQ_HIGHPRI) and count.
	 * (testing with WQ_HIGHPRI set shows perf degradation of about 7%).
	 * NOTE we are no longer using WQ_SYSFS, as the namespace is not unique.
	 */
	bc->bc_make_request_wq = alloc_workqueue("b_wkq:%s",
						 WQ_MEM_RECLAIM,
						 1, bc->bc_name);
	M_ASSERT_FIXME(bc->bc_make_request_wq != NULL);
	cache_timer_init(&bc->bc_make_request_wq_timer);
	atomic_set(&bc->bc_make_request_wq_count, 0);

	ret = schedule_delayed_work(&bc->devio.flush_delayed_work, msecs_to_jiffies(1));
	ASSERT(ret == 1);


	/*
	 * now we have initialized everything we can show sysfs.
	 */
	ret = cache_sysfs_add(bc);
	if (ret < 0) {
		ti->error = "sysfs_add";
		printk_err("sysfs_add failed: ret=%d\n", ret);
		goto bad_2;
	}

	/*
	 * now restore or initialize
	 */
	printk_info("'%s' (0x%x) %llu cache entries\n",
		    cache_operation_str,
		    cache_operation,
		    bc->bc_papi.papi_hdr.lm_cache_blocks);

	ret = cache_ctr_restore_or_init_workqueues(bc,
						   cache_operation_str,
						   cache_operation);
	if (ret < 0) {
		ti->error = "corrupt cache entry or bad data";
		printk_err("restore_or_init failed: ret=%d\n", ret);
		goto bad_2;
	}

	printk_info("'%s' of incore metadata complete\n", cache_operation_str);
	printk_info("done '%s' lm_cache_blocks=%llu/total_entries=%u, valid_entries=%u, valid_entries_dirty=%u, valid_entries_clean=%u, invalid_entries=%u\n",
		    cache_operation_str,
		    bc->bc_papi.papi_hdr.lm_cache_blocks,
		    atomic_read(&bc->bc_total_entries),
		    atomic_read(&bc->bc_valid_entries),
		    atomic_read(&bc->bc_valid_entries_dirty),
		    atomic_read(&bc->bc_valid_entries_clean),
		    atomic_read(&bc->bc_invalid_entries));

	printk_info("done %s '%s': %d+%d(%d+%d)=%d cache entries\n",
		    cache_operation_str,
		    bc->bc_name,
		    atomic_read(&bc->bc_invalid_entries),
		    atomic_read(&bc->bc_valid_entries),
		    atomic_read(&bc->bc_valid_entries_dirty),
		    atomic_read(&bc->bc_valid_entries_clean),
		    atomic_read(&bc->bc_total_entries));
	printk_info("done %s '%s': invalid_entries=%u, valid_entries=%u (valid_entries_dirty=%u + valid_entries_clean=%u), total_entries=%u/%llu\n",
		    cache_operation_str, bc->bc_name,
		    atomic_read(&bc->bc_invalid_entries),
		    atomic_read(&bc->bc_valid_entries),
		    atomic_read(&bc->bc_valid_entries_dirty),
		    atomic_read(&bc->bc_valid_entries_clean),
		    atomic_read(&bc->bc_total_entries),
		    bc->bc_papi.papi_hdr.lm_cache_blocks);

	M_ASSERT(bc->bc_papi.papi_hdr.lm_cache_blocks ==
		 atomic_read(&bc->bc_total_entries));
	M_ASSERT(atomic_read(&bc->bc_valid_entries) ==
		 (atomic_read(&bc->bc_valid_entries_dirty) +
		  atomic_read(&bc->bc_valid_entries_clean)));
	M_ASSERT(atomic_read(&bc->bc_total_entries) ==
		 (atomic_read(&bc->bc_valid_entries) +
		  atomic_read(&bc->bc_invalid_entries)));

	printk_info("updating pmem headers\n");
	ret = pmem_header_update(bc, 1);
	M_ASSERT(ret == 0);
	printk_info("done updating pmem headers\n");

	ASSERT_BITTERN_CACHE(bc);

	ti->max_io_len = MAX_IO_LEN_SECTORS;

	/* DISCARD */
	ti->num_discard_bios = 1;
	ti->discards_supported = true;
	ti->discard_zeroes_data_unsupported = true;

#if 0
	/*
	 * we don't support write_same yet
	 */
	/* WRITE_SAME */
	ti->num_write_same_bios = true;
#endif

	/* FLUSH */
	ti->flush_supported = true;
	ti->num_flush_bios = 1;

	printk_info("device: max_io_len=%u\n", ti->max_io_len);
	printk_info("device: num_flush_bios=%u\n", ti->num_flush_bios);
	printk_info("device: num_discard_bios=%u\n", ti->num_discard_bios);
	printk_info("device: num_write_same_bios=%u\n",
		    ti->num_write_same_bios);
	printk_info("device: per_bio_data_size=%u\n", ti->per_bio_data_size);
	printk_info("device: num_write_bios()=%p\n", ti->num_write_bios);
	printk_info("device: flush_supported=%u\n", ti->flush_supported);
	printk_info("device: discards_supported=%u\n", ti->discards_supported);
	printk_info("device: split_discard_bios=%u\n", ti->split_discard_bios);
	printk_info("device: discard_zeroes_data_unsupported=%u\n",
		    ti->discard_zeroes_data_unsupported);

	ti->private = bc;

	/*
	 * start workqueues
	 */
	seq_bypass_start_workqueue(bc);
	pmem_header_update_start_workqueue(bc);

	/*
	 * now start off the kernel threads
	 */
	printk_info("starting off kernel threads\n");

	bc->bc_verifier_task = kthread_create(cache_block_verifier_kthread,
					      bc,
					      "b_vrf/%s",
					      bc->bc_name);
	M_ASSERT_FIXME(bc->bc_verifier_task != NULL);
	printk_info("verifier instantiated, task=%p\n", bc->bc_verifier_task);
	wake_up_process(bc->bc_verifier_task);

	bc->bc_bgwriter_task = kthread_create(cache_bgwriter_kthread,
					      bc,
					      "b_bgw/%s",
					      bc->bc_name);
	M_ASSERT_FIXME(bc->bc_bgwriter_task != NULL);
	printk_info("bgwriter instantiated, task=%p\n", bc->bc_bgwriter_task);
	wake_up_process(bc->bc_bgwriter_task);

	bc->bc_invalidator_task = kthread_create(cache_invalidator_kthread,
						 bc,
						 "b_inv/%s",
						 bc->bc_name);
	M_ASSERT_FIXME(bc->bc_invalidator_task != NULL);
	printk_info("invalidator instantiated, task=%p\n",
		    bc->bc_invalidator_task);
	wake_up_process(bc->bc_invalidator_task);

	printk_info("exit\n");

	return 0;

	/*! \todo this can be made common with _dtr() code */
bad_2:
	if (bc->bc_make_request_wq != NULL) {
		printk_info("destroying make_request workqueue\n");
		flush_workqueue(bc->bc_make_request_wq);
		destroy_workqueue(bc->bc_make_request_wq);
	}
	cancel_delayed_work(&bc->devio.flush_delayed_work);

	cache_sysfs_deinit(bc);

bad_1:
	printk_info("destroying slabs\n");
	if (bc->bc_kmem_map != NULL)
		kmem_cache_destroy(bc->bc_kmem_map);
	if (bc->bc_kmem_threads != NULL)
		kmem_cache_destroy(bc->bc_kmem_threads);

	if (bc->defer_wq != NULL) {
		flush_workqueue(bc->defer_wq);
		printk_info("destroying deferred workqueue\n");
		destroy_workqueue(bc->defer_wq);
	}

	pmem_deallocate(bc);
	printk_info("mem_info_deinitialize()\n");
	pmem_info_deinitialize(bc);
	printk_info("done mem_info_deinitialize()\n");

	seq_bypass_deinitialize(bc);

bad_0:
	printk_err("error: %s\n", ti->error);
	M_ASSERT(ti->error != NULL);

	if (bc->devio.flush_wq != NULL) {
		flush_workqueue(bc->devio.flush_wq);
		printk_info("destroying make_request workqueue\n");
		destroy_workqueue(bc->devio.flush_wq);
	}
	if (bc->devio.dm_dev != NULL) {
		printk_err("dm_put_device devio.dm_dev\n");
		dm_put_device(ti, bc->devio.dm_dev);
	}
	if (bc->bc_cache_dev != NULL) {
		printk_err("dm_put_device for cache\n");
		dm_put_device(ti, bc->bc_cache_dev);
	}
	if (bc->bc_cache_blocks != NULL)
		vfree(bc->bc_cache_blocks);
#ifdef ENABLE_TRACK_CRC32C
	if (bc->bc_tracked_hashes != NULL)
		vfree(bc->bc_tracked_hashes);
#endif /*ENABLE_TRACK_CRC32C */

	printk_err("error : bad\n");
	vfree(bc);

	return -EINVAL;
}
