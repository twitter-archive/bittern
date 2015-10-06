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

/*
 * here we do all the operations which can be done while still having sysfs entries
 * (helps with tracking what's going on)
 * basically, we flush data if required, and stop all the kernel threads.
 */
void cache_dtr_pre(struct dm_target *ti)
{
	struct bittern_cache *bc = ti->private;
	int ret;

	printk_info("enter\n");
	printk_info("bc = %p\n", bc);
	printk_info("bc->bc_magic1 = 0x%x\n", bc->bc_magic1);
	ASSERT_BITTERN_CACHE(bc);

	M_ASSERT(bc->bc_papi.papi_hdr.lm_cache_blocks ==
		 atomic_read(&bc->bc_total_entries));

	if (bc->bc_bgwriter_conf_flush_on_exit) {
		printk_info("flushing dirty blocks\n");
		cache_bgwriter_flush_dirty_blocks(bc);
	} else {
		printk_info("not flushing dirty blocks\n");
	}

	/*
	 * stop all kernel threads
	 */

	printk_info("bc_deferred_requests=%d\n",
		    atomic_read(&bc->bc_deferred_requests));
	M_ASSERT(atomic_read(&bc->bc_deferred_requests) == 0);

	printk_info("stopping invalidator task (task=%p)\n",
		    bc->bc_invalidator_task);
	ret = kthread_stop(bc->bc_invalidator_task);
	M_ASSERT(bc->bc_invalidator_task == NULL);
	printk_info("stopped invalidator task (task=%p): ret=%d\n",
		    bc->bc_invalidator_task, ret);

	printk_info("stopping bgwriter task (task=%p)\n", bc->bc_bgwriter_task);
	ret = kthread_stop(bc->bc_bgwriter_task);
	M_ASSERT(bc->bc_bgwriter_task == NULL);
	printk_info("stopped bgwriter task (task=%p): ret=%d\n",
		    bc->bc_bgwriter_task, ret);

	/* there can be no pending deferred requests anymore */
	M_ASSERT(atomic_read(&bc->bc_deferred_requests) == 0);

	M_ASSERT(bc->defer_wq != NULL);
	flush_workqueue(bc->defer_wq);
	printk_info("destroying deferred workqueue\n");
	destroy_workqueue(bc->defer_wq);

	printk_info("deferred_queues(%u/%u)\n",
		    bc->defer_busy.curr_count,
		    bc->defer_page.curr_count);
	M_ASSERT(bc->defer_busy.curr_count == 0);
	M_ASSERT(bio_list_empty(&bc->defer_busy.list));
	M_ASSERT(bc->defer_page.curr_count == 0);
	M_ASSERT(bio_list_empty(&bc->defer_page.list));
	printk_info("deferred_requests=%u\n",
		    atomic_read(&bc->bc_deferred_requests));
	M_ASSERT(atomic_read(&bc->bc_deferred_requests) == 0);

	printk_info("stopping cache_block_verifier task (running=%d, task=%p)\n",
		    bc->bc_verifier_running,
		    bc->bc_verifier_task);
	ret = kthread_stop(bc->bc_verifier_task);
	M_ASSERT(bc->bc_verifier_task == NULL);
	M_ASSERT(bc->bc_verifier_running == 0);
	printk_info("stopped cache_block_verifier task (running=%d, task=%p): ret=%d\n",
		    bc->bc_verifier_running,
		    bc->bc_verifier_task,
		    ret);

	/* stop workqueues */
	pmem_header_update_stop_workqueue(bc);
	seq_bypass_stop_workqueue(bc);

	printk_info("pending_read_requests=%u\n",
		    atomic_read(&bc->bc_pending_read_requests));
	printk_info("pending_read_bypass_requests=%u\n",
		    atomic_read(&bc->bc_pending_read_bypass_requests));
	printk_info("pending_write_bypass_requests=%u\n",
		    atomic_read(&bc->bc_pending_write_bypass_requests));
	printk_info("pending_write_requests=%u\n",
		    atomic_read(&bc->bc_pending_write_requests));
	printk_info("pending_writeback_requests=%u\n",
		    atomic_read(&bc->bc_pending_writeback_requests));
	printk_info("pending_requests=%u\n",
		    atomic_read(&bc->bc_pending_requests));
	M_ASSERT(atomic_read(&bc->bc_pending_read_requests) == 0);
	M_ASSERT(atomic_read(&bc->bc_pending_read_bypass_requests) == 0);
	M_ASSERT(atomic_read(&bc->bc_pending_write_requests) == 0);
	M_ASSERT(atomic_read(&bc->bc_pending_write_bypass_requests) == 0);
	M_ASSERT(atomic_read(&bc->bc_pending_writeback_requests) == 0);
	M_ASSERT(atomic_read(&bc->bc_pending_requests) == 0);

	printk_info("pending_invalidate_requests=%u\n",
		    atomic_read(&bc->bc_pending_invalidate_requests));
	M_ASSERT(atomic_read(&bc->bc_pending_invalidate_requests) == 0);

	printk_info("flushing make_request workqueue\n");
	M_ASSERT(bc->bc_make_request_wq != NULL);
	flush_workqueue(bc->bc_make_request_wq);
	printk_info("destroying make_request workqueue\n");
	destroy_workqueue(bc->bc_make_request_wq);

	M_ASSERT(bc->bc_magic1 == BC_MAGIC1);

	printk_info("updating pmem headers\n");
	ret = pmem_header_update(bc, 1);
	printk_info("done updating pmem headers\n");
	M_ASSERT(ret == 0);

	printk_info("cancelling dev_flush delayed_work\n");
	cancel_delayed_work(&bc->devio.flush_delayed_work);
	printk_info("flushing dev_flush workqueue\n");
	M_ASSERT(bc->devio.flush_wq != NULL);
	flush_workqueue(bc->devio.flush_wq);
	printk_info("destroying dev_flush workqueue\n");
	destroy_workqueue(bc->devio.flush_wq);

	printk_info("exit\n");
}

/*
 * whereas here we do everything else
 */
void cache_dtr(struct dm_target *ti)
{
	struct bittern_cache *bc = ti->private;
	int i;
	unsigned int entries_invalid = 0;
	unsigned int entries_valid_clean = 0, entries_valid_dirty = 0;
	unsigned int entries = 0, bc_total_entries = 0;
	unsigned char *entries_state_map;
	int orphan_count;

	printk_info("enter\n");

	printk_info("bc_deferred_requests=%d\n",
		    atomic_read(&bc->bc_deferred_requests));
	M_ASSERT(atomic_read(&bc->bc_deferred_requests) == 0);

	printk_info("bc = %p\n", bc);
	printk_info("bc->bc_magic1 = 0x%x\n", bc->bc_magic1);
	ASSERT_BITTERN_CACHE(bc);

	M_ASSERT(bc->bc_papi.papi_hdr.lm_cache_blocks ==
		 atomic_read(&bc->bc_total_entries));

	entries_state_map = vmalloc(atomic_read(&bc->bc_total_entries) + 1);
	M_ASSERT_FIXME(entries_state_map != NULL);
	memset(entries_state_map, 0xff, atomic_read(&bc->bc_total_entries) + 1);

	ASSERT_BITTERN_CACHE(bc);
	ASSERT(bc->bc_cache_dev->bdev == bc->bc_papi.papi_bdev);

	bc_total_entries = atomic_read(&bc->bc_total_entries);

	printk_info("deallocating cache entries\n");
	while (list_non_empty(&bc->bc_invalid_entries_list) ||
	       list_non_empty(&bc->bc_valid_entries_list)) {
		struct cache_block *bcb = NULL;

		if (list_non_empty(&bc->bc_invalid_entries_list)) {
			bcb = list_first_entry(&bc->bc_invalid_entries_list,
					       struct cache_block,
					       bcb_entry);
			M_ASSERT(bcb->bcb_state == S_INVALID);
			M_ASSERT(is_sector_number_invalid(bcb->bcb_sector));
		}
		if (bcb == NULL && list_non_empty(&bc->bc_valid_entries_list)) {
			bcb =
			    list_first_entry(&bc->bc_valid_entries_list,
					     struct cache_block,
					     bcb_entry);
			M_ASSERT(bcb->bcb_state != S_INVALID);
			M_ASSERT(is_sector_number_valid(bcb->bcb_sector));
		}
		M_ASSERT(bcb != NULL);
		M_ASSERT(bcb->bcb_state == S_INVALID ||
			 bcb->bcb_state == S_CLEAN ||
			 bcb->bcb_state == S_DIRTY);
		M_ASSERT(bcb->bcb_cache_transition == TS_NONE);
		M_ASSERT(bcb->bcb_block_id >= 1);
		M_ASSERT(bcb->bcb_block_id <=
			 bc->bc_papi.papi_hdr.lm_cache_blocks);
		printk_info_ratelimited("deallocating cache block_id=#%d, bcb_sector=%lu, state=%d(%s), refcount=%d, hash_data=" UINT128_FMT "\n",
					bcb->bcb_block_id,
					bcb->bcb_sector,
					bcb->bcb_state,
					cache_state_to_str(bcb->bcb_state),
					atomic_read(&bcb->bcb_refcount),
					UINT128_ARG(bcb->bcb_hash_data));
		M_ASSERT(bcb != NULL);
		__ASSERT_CACHE_BLOCK(bcb, bc);
		list_del_init(&bcb->bcb_entry);

		entries_state_map[bcb->bcb_block_id] = bcb->bcb_state;

		switch (bcb->bcb_state) {
		case S_INVALID:
			atomic_dec(&bc->bc_invalid_entries);
			entries_invalid++;
			M_ASSERT(RB_EMPTY_NODE(&bcb->bcb_rb_node));
			break;
		case S_CLEAN:
			cache_track_hash_check(bc, bcb, bcb->bcb_hash_data);
			atomic_dec(&bc->bc_valid_entries);
			atomic_dec(&bc->bc_valid_entries_clean);
			list_del_init(&bcb->bcb_entry_cleandirty);
			entries_valid_clean++;
			M_ASSERT(RB_NON_EMPTY_ROOT(&bc->bc_rb_root));
			M_ASSERT(RB_NON_EMPTY_NODE(&bcb->bcb_rb_node));
			cache_rb_remove(bc, bcb);
			M_ASSERT(RB_EMPTY_NODE(&bcb->bcb_rb_node));
			break;
		case S_DIRTY:
			cache_track_hash_check(bc, bcb, bcb->bcb_hash_data);
			atomic_dec(&bc->bc_valid_entries);
			atomic_dec(&bc->bc_valid_entries_dirty);
			entries_valid_dirty++;
			list_del_init(&bcb->bcb_entry_cleandirty);
			M_ASSERT(RB_NON_EMPTY_ROOT(&bc->bc_rb_root));
			M_ASSERT(RB_NON_EMPTY_NODE(&bcb->bcb_rb_node));
			cache_rb_remove(bc, bcb);
			M_ASSERT(RB_EMPTY_NODE(&bcb->bcb_rb_node));
			break;
		default:
			M_ASSERT("unexpected cache state in _dtr switch" ==
				 NULL);
			break;
		}
		entries++;
		atomic_dec(&bc->bc_total_entries);

		__ASSERT_CACHE_BLOCK(bcb, bc);
		M_ASSERT(atomic_read(&bcb->bcb_refcount) == 0);

		M_ASSERT(atomic_read(&bc->bc_valid_entries_dirty) >= 0);
		M_ASSERT(atomic_read(&bc->bc_valid_entries_clean) >= 0);
		M_ASSERT(atomic_read(&bc->bc_valid_entries) >= 0);
		M_ASSERT(atomic_read(&bc->bc_invalid_entries) >= 0);
		M_ASSERT(atomic_read(&bc->bc_valid_entries_dirty) <=
			 atomic_read(&bc->bc_total_entries));
		M_ASSERT(atomic_read(&bc->bc_valid_entries_clean) <=
			 atomic_read(&bc->bc_total_entries));
		M_ASSERT(atomic_read(&bc->bc_valid_entries) <=
			 atomic_read(&bc->bc_total_entries));
		M_ASSERT(atomic_read(&bc->bc_invalid_entries) <=
			 atomic_read(&bc->bc_total_entries));

		/*
		 * pre-empt ourselves so we won't keep the CPU stuck during
		 * this (potentially) long loop
		 */
		schedule();
	}

	printk_info("done deallocating '%s': %u+%u(%u+%u)=%u cache entries\n",
		    bc->bc_name,
		    entries_invalid,
		    (entries_valid_clean + entries_valid_dirty),
		    entries_valid_clean, entries_valid_dirty, entries);
	printk_info("done '%s': invalid_entries=%u, valid_entries=%u (valid_entries_dirty=%u + valid_entries_clean=%u), total_entries=%u/%u\n",
		    bc->bc_name,
		    entries_invalid,
		    (entries_valid_clean + entries_valid_dirty),
		    entries_valid_dirty,
		    entries_valid_clean,
		    entries,
		    bc_total_entries);

	printk_info("bc_empty_root=%d\n", RB_EMPTY_ROOT(&bc->bc_rb_root));
	printk_info("list_empty(invalid_entries)=%d, list_empty(valid_entries)=%d, list_empty(valid_entries_clean)=%d, list_empty(valid_entries_dirty)=%d\n",
		    list_empty(&bc->bc_invalid_entries_list),
		    list_empty(&bc->bc_valid_entries_list),
		    list_empty(&bc->bc_valid_entries_clean_list),
		    list_empty(&bc->bc_valid_entries_dirty_list));
	printk_info("list_empty(pending_requests)=%d\n",
		    list_empty(&bc->bc_pending_requests_list));

	printk_info("looking for orphan entries\n");
	orphan_count = 0;
	for (i = 1; i <= bc->bc_papi.papi_hdr.lm_cache_blocks; i++) {
		M_ASSERT(entries_state_map[i] == 0xff ||
			 entries_state_map[i] == S_INVALID ||
			 entries_state_map[i] == S_CLEAN ||
			 entries_state_map[i] == S_DIRTY);
		if (entries_state_map[i] == 0xff) {
			/* block_id starts from 1, array starts from 0 */
			struct cache_block *bcb = &bc->bc_cache_blocks[i - 1];

			printk_err("orphan entry cache block_id=#%d, bcb_sector=%lu, state=%d(%s), refcount=%d, hash_data=" UINT128_FMT "\n",
				    bcb->bcb_block_id,
				    bcb->bcb_sector,
				    bcb->bcb_state,
				    cache_state_to_str(bcb->bcb_state),
				    atomic_read(&bcb->bcb_refcount),
				    UINT128_ARG(bcb->bcb_hash_data));
			orphan_count++;
		}
	}
	vfree(entries_state_map);
	if (orphan_count != 0)
		printk_err("error: found %d orphan entries\n", orphan_count);
	else
		printk_info("no orphan entries found\n");
	M_ASSERT(orphan_count == 0);

	printk_info("bc_empty_root=%d\n", RB_EMPTY_ROOT(&bc->bc_rb_root));
	printk_info("cache_rb_first=%p\n", cache_rb_first(bc));
	printk_info("cache_rb_last=%p\n", cache_rb_last(bc));
	M_ASSERT(RB_EMPTY_ROOT(&bc->bc_rb_root));
	M_ASSERT(cache_rb_first(bc) == NULL);
	M_ASSERT(cache_rb_last(bc) == NULL);

	M_ASSERT(entries ==
		 entries_invalid + entries_valid_clean + entries_valid_dirty);
	M_ASSERT(entries == bc_total_entries);
	M_ASSERT(atomic_read(&bc->bc_total_entries) == 0);
	M_ASSERT(atomic_read(&bc->bc_valid_entries) == 0);
	M_ASSERT(atomic_read(&bc->bc_valid_entries_clean) == 0);
	M_ASSERT(atomic_read(&bc->bc_valid_entries_dirty) == 0);
	M_ASSERT(atomic_read(&bc->bc_invalid_entries) == 0);
	M_ASSERT(list_empty(&bc->bc_invalid_entries_list));
	M_ASSERT(list_empty(&bc->bc_valid_entries_list));
	M_ASSERT(list_empty(&bc->bc_valid_entries_clean_list));
	M_ASSERT(list_empty(&bc->bc_valid_entries_dirty_list));
	M_ASSERT(list_empty(&bc->bc_pending_requests_list));

	/* deinitialize seq_bypass */
	seq_bypass_deinitialize(bc);

	printk_info("pmem_deallocate\n");
	pmem_deallocate(bc);
	printk_info("mem_info_deinitialize()\n");
	pmem_info_deinitialize(bc);
	printk_info("done mem_info_deinitialize()\n");

	printk_info("destroying slabs\n");
	M_ASSERT(bc->bc_kmem_map != NULL);
	kmem_cache_destroy(bc->bc_kmem_map);
	M_ASSERT(bc->bc_kmem_threads != NULL);
	kmem_cache_destroy(bc->bc_kmem_threads);

	printk_info("dm_put_device devio.dm_dev\n");
	dm_put_device(ti, bc->devio.dm_dev);

	printk_info("dm_put_device bc_cache_dev\n");
	dm_put_device(ti, bc->bc_cache_dev);

#ifdef ENABLE_TRACK_CRC32C
	M_ASSERT(bc->bc_tracked_hashes != NULL);
	M_ASSERT(uint128_eq(bc->bc_tracked_hashes[0], CACHE_TRACK_HASH_MAGIC0));
	M_ASSERT(uint128_eq(bc->bc_tracked_hashes[1], CACHE_TRACK_HASH_MAGIC1));
	M_ASSERT(uint128_eq(bc->bc_tracked_hashes[bc->bc_tracked_hashes_num+2],
			    CACHE_TRACK_HASH_MAGICN));
	M_ASSERT(uint128_eq(bc->bc_tracked_hashes[bc->bc_tracked_hashes_num+3],
			    CACHE_TRACK_HASH_MAGICN1));
	printk_info("vfree(bc->bc_tracked_hashes)\n");
	vfree(bc->bc_tracked_hashes);
#endif /*ENABLE_TRACK_CRC32C */

	printk_info("vfree(bc->bc_cache_blocks)\n");
	M_ASSERT(bc->bc_cache_blocks != NULL);
	vfree(bc->bc_cache_blocks);

	printk_info("vfree(bc)\n");
	M_ASSERT(bc != NULL);
	vfree(bc);

	printk_info("exit\n");
}
