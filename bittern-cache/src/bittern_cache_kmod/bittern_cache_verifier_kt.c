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

void cache_block_verify_callback(struct bittern_cache *bc,
				 struct cache_block *cache_block,
				 struct pmem_context *pmem_ctx,
				 void *callback_context,
				 int err)
{
	struct semaphore *sema = (struct semaphore *)callback_context;
	ASSERT(pmem_ctx != NULL);
	M_ASSERT(pmem_ctx->magic1 == PMEM_CONTEXT_MAGIC1);
	M_ASSERT(pmem_ctx->magic2 == PMEM_CONTEXT_MAGIC2);

	M_ASSERT_FIXME(err == 0);
	up(sema);
}

int cache_block_verify(struct bittern_cache *bc, int block_id,
			       struct cache_block *cache_block,
			       int do_bt_level_trace)
{
	int errors = 0;
	int ret;
	struct semaphore sema;
	uint64_t t_start, t_end;	/*FIXME need to put timers */
	struct pmem_context *pmem_ctx;
	char *cache_vaddr;

	M_ASSERT(bc != NULL);
	M_ASSERT(block_id >= 1 &&
		 block_id <= atomic_read(&bc->bc_total_entries));
	M_ASSERT(cache_block != NULL);
	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	M_ASSERT(block_id == cache_block->bcb_block_id);

	pmem_ctx = kmem_zalloc(sizeof(struct pmem_context), GFP_NOIO);
	M_ASSERT_FIXME(pmem_ctx != NULL);

	pmem_context_initialize(pmem_ctx);

	ret = pmem_context_setup(bc,
				 bc->bc_kmem_threads,
				 cache_block,
				 NULL,
				 pmem_ctx);
	M_ASSERT_FIXME(ret == 0);

	t_start = current_kernel_time_nsec();
	sema_init(&sema, 0);
	ret = pmem_data_get_page_read(bc,
				      cache_block,
				      pmem_ctx,
				      &sema, /*callback context */
				      cache_block_verify_callback);
	M_ASSERT_FIXME(ret == 0);
	down(&sema);
	t_end = current_kernel_time_nsec();

	cache_vaddr = pmem_context_data_vaddr(pmem_ctx);

	BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
		 "verifying block id #%d, dbi_data->di_buffer=%p",
		 cache_block->bcb_block_id,
		 cache_vaddr);

	/*
	 * check hash
	 */
	cache_track_hash_check(bc, cache_block, cache_block->bcb_hash_data);

	/*
	 * verify data hash
	 */
	errors += cache_verify_hash_data_buffer_ret(bc,
						    cache_block,
						    cache_vaddr);

	/*
	 * check in memory descriptor against cache memory.
	 */
	{
		struct pmem_block_metadata *pmbm = &pmem_ctx->pmbm;
		uint128_t computed_hash_metadata;
		int ret;

		ASSERT(block_id == cache_block->bcb_block_id);
		ret = pmem_metadata_sync_read(bc, cache_block, pmbm);
		M_ASSERT_FIXME(ret == 0);
		BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, cache_block, NULL, NULL,
			 "id #%d: m=0x%x, id=%u, status=%d(%s), xid=%llu, sector=%llu, hash_data=" UINT128_FMT ", hash_metadata=" UINT128_FMT,
			 block_id,
			 pmbm->pmbm_magic,
			 pmbm->pmbm_block_id,
			 pmbm->pmbm_status,
			 cache_state_to_str(pmbm->pmbm_status),
			 pmbm->pmbm_xid,
			 pmbm->pmbm_device_sector,
			 UINT128_ARG(pmbm->pmbm_hash_metadata),
			 UINT128_ARG(pmbm->pmbm_hash_data));
		if (pmbm->pmbm_block_id != cache_block->bcb_block_id ||
		    pmbm->pmbm_status != CACHE_VALID_CLEAN ||
		    pmbm->pmbm_device_sector != cache_block->bcb_sector) {
			BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, cache_block, NULL,
				 NULL,
				 "block id #%d mem metadata information mismatch versus cache_pmem metadata",
				 cache_block->bcb_block_id);
			printk_err("block id #%d: cache mem descriptor: m=0x%x, id=%u, status=%d(%s), xid=%llu, sector=%llu, hash=" UINT128_FMT ", hash_data=" UINT128_FMT "\n",
				   block_id,
				   pmbm->pmbm_magic,
				   pmbm->pmbm_block_id,
				   pmbm->pmbm_status,
				   cache_state_to_str(pmbm->pmbm_status),
				   pmbm->pmbm_xid,
				   pmbm->pmbm_device_sector,
				   UINT128_ARG(pmbm->pmbm_hash_metadata),
				   UINT128_ARG(pmbm->pmbm_hash_data));
			errors++;
		} else {
			BT_TRACE(do_bt_level_trace, bc, NULL, cache_block, NULL,
				 NULL,
				 "block id #%d mem metadata information matches cache_pmem metadata",
				 cache_block->bcb_block_id);
		}
		computed_hash_metadata = murmurhash3_128(pmbm,
					   PMEM_BLOCK_METADATA_HASHING_SIZE);
		if (uint128_ne(computed_hash_metadata,
				pmbm->pmbm_hash_metadata)) {
			BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, cache_block, NULL,
				 NULL,
				 "block id #%d hash_metadata mismatch computed_hash_metadata=" UINT128_FMT ", stored_hash_metadata=" UINT128_FMT,
				 cache_block->bcb_block_id,
				 UINT128_ARG(computed_hash_metadata),
				 UINT128_ARG(pmbm->pmbm_hash_metadata));
			printk_err("block id #%d: cache mem descriptor: m=0x%x, id=%u, status=%d(%s), xid=%llu, sector=%llu, computed_hash_metadata=" UINT128_FMT ", stored_hash_metadata=" UINT128_FMT "\n",
				   block_id,
				   pmbm->pmbm_magic,
				   pmbm->pmbm_block_id,
				   pmbm->pmbm_status,
				   cache_state_to_str(pmbm->pmbm_status),
				   pmbm->pmbm_xid,
				   pmbm->pmbm_device_sector,
				   UINT128_ARG(computed_hash_metadata),
				   UINT128_ARG(pmbm->pmbm_hash_metadata));
			errors++;
		} else {
			BT_TRACE(do_bt_level_trace, bc, NULL, cache_block, NULL,
				 NULL,
				 "block id #%d cache_pmem hash_metadata matches, computed_hash_metadata=" UINT128_FMT ", stored_hash_metadata=" UINT128_FMT "\n",
				 cache_block->bcb_block_id,
				 UINT128_ARG(computed_hash_metadata),
				 UINT128_ARG(pmbm->pmbm_hash_metadata));
		}

		if (uint128_ne(cache_block->bcb_hash_data,
				pmbm->pmbm_hash_data)) {
			BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, cache_block, NULL,
				 NULL,
				 "block id #%d hash_data mismatch incore_hash_data=" UINT128_FMT ", stored_hash_data=" UINT128_FMT,
				 cache_block->bcb_block_id,
				 UINT128_ARG(cache_block->bcb_hash_data),
				 UINT128_ARG(pmbm->pmbm_hash_data));
			printk_err("block id #%d: cache mem descriptor: m=0x%x, id=%u, status=%d(%s), xid=%llu, sector=%llu, incore_hash_data=" UINT128_FMT ", stored_hash_data=" UINT128_FMT "\n",
				   block_id,
				   pmbm->pmbm_magic,
				   pmbm->pmbm_block_id,
				   pmbm->pmbm_status,
				   cache_state_to_str(pmbm->pmbm_status),
				   pmbm->pmbm_xid,
				   pmbm->pmbm_device_sector,
				   UINT128_ARG(cache_block->bcb_hash_data),
				   UINT128_ARG(pmbm->pmbm_hash_data));
			errors++;
		} else {
			BT_TRACE(do_bt_level_trace, bc, NULL, cache_block, NULL,
				 NULL,
				 "block id #%d cache_pmem hash_data matches, incore_hash_data=" UINT128_FMT ", stored_hash_data=" UINT128_FMT,
				 cache_block->bcb_block_id,
				 UINT128_ARG(cache_block->bcb_hash_data),
				 UINT128_ARG(pmbm->pmbm_hash_data));
		}
	}

	/*
	 * read disk block and verify crc
	 */
	{
		int ret;
		struct cache_bio_request *bc_bio_req;

		ret = cache_bio_request_initialize(bc, &bc_bio_req);
		M_ASSERT(ret == 0);

		ret = cache_bio_request_start_async_page(bc,
						cache_block->bcb_sector,
						READ,
						bc_bio_req);
		M_ASSERT_FIXME(ret == 0);
		ret = cache_bio_request_wait_page(bc, bc_bio_req);
		M_ASSERT(ret == 0);

		errors += cache_verify_hash_data_buffer_ret(bc,
				cache_block,
				cache_bio_request_get_buffer(bc_bio_req));

		if (memcmp(cache_bio_request_get_buffer(bc_bio_req),
			   cache_vaddr, PAGE_SIZE) == 0) {
			BT_TRACE(do_bt_level_trace, bc, NULL, cache_block, NULL,
				 NULL,
				 "device block #%d data compare ok, hash_data=" UINT128_FMT,
				 cache_block->bcb_block_id,
				 UINT128_ARG(cache_block->bcb_hash_data));
		} else {
			BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, cache_block, NULL,
				 NULL,
				 "device block #%d data compare mismatch, hash_data=" UINT128_FMT,
				 cache_block->bcb_block_id,
				 UINT128_ARG(cache_block->bcb_hash_data));
			printk_err
			    ("error: block id #%d device block data compare mismatch\n",
			     block_id);
			errors++;

#ifdef CACHE_VERIFIER_DUMP_BLOCK_BYTES
			{
				unsigned char *disk_p =
				    cache_bio_request_get_buffer
				    (bc_bio_req);
				unsigned char *cache_p = dbi_data->di_buffer;
				int i, bytes_equal_count =
				    0, bytes_different_count = 0;
				for (i = 0; i < PAGE_SIZE; i++) {
					if (*disk_p++ == *cache_p++)
						bytes_equal_count++;
					else
						bytes_different_count++;
				}
				BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, cache_block,
					 NULL, NULL,
					 "device block #%d data compare mismatch, crc32c=0x%x, bytes_equal_count=%d, bytes_different_count=%d",
					 cache_block->bcb_block_id,
					 device_crc32c, bytes_equal_count,
					 bytes_different_count);
				printk_err("error: block id #%d device block data compare mismatch, bytes_equal_count=%d, bytes_different_count=%d\n",
				     block_id, bytes_equal_count,
				     bytes_different_count);
				disk_p =
				    cache_bio_request_get_buffer
				    (bc_bio_req);
				cache_p = dbi_data->di_buffer;
				for (i = 0; i < PAGE_SIZE; i += 32) {
					printk_err("error: block id #%d: cache[%d]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
					     cache_block->bcb_block_id, i,
					     cache_p[i + 0], cache_p[i + 1],
					     cache_p[i + 2], cache_p[i + 3],
					     cache_p[i + 4], cache_p[i + 5],
					     cache_p[i + 6], cache_p[i + 7],
					     cache_p[i + 8], cache_p[i + 9],
					     cache_p[i + 10], cache_p[i + 11],
					     cache_p[i + 12], cache_p[i + 13],
					     cache_p[i + 14], cache_p[i + 15],
					     cache_p[i + 16], cache_p[i + 17],
					     cache_p[i + 18], cache_p[i + 19],
					     cache_p[i + 20], cache_p[i + 21],
					     cache_p[i + 22], cache_p[i + 23],
					     cache_p[i + 24], cache_p[i + 25],
					     cache_p[i + 26], cache_p[i + 27],
					     cache_p[i + 28], cache_p[i + 29],
					     cache_p[i + 30], cache_p[i + 31]);
					printk_err("error: block id #%d:  disk[%d]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
					     cache_block->bcb_block_id, i,
					     disk_p[i + 0], disk_p[i + 1],
					     disk_p[i + 2], disk_p[i + 3],
					     disk_p[i + 4], disk_p[i + 5],
					     disk_p[i + 6], disk_p[i + 7],
					     disk_p[i + 8], disk_p[i + 9],
					     disk_p[i + 10], disk_p[i + 11],
					     disk_p[i + 12], disk_p[i + 13],
					     disk_p[i + 14], disk_p[i + 15],
					     disk_p[i + 16], disk_p[i + 17],
					     disk_p[i + 18], disk_p[i + 19],
					     disk_p[i + 20], disk_p[i + 21],
					     disk_p[i + 22], disk_p[i + 23],
					     disk_p[i + 24], disk_p[i + 25],
					     disk_p[i + 26], disk_p[i + 27],
					     disk_p[i + 28], disk_p[i + 29],
					     disk_p[i + 30], disk_p[i + 31]);
				}
			}
#endif
		}

		cache_bio_request_deinitialize(bc, bc_bio_req);
	}

	pmem_data_put_page_read(bc,
				cache_block,
				pmem_ctx);

	pmem_context_destroy(bc, pmem_ctx);

	M_ASSERT(pmem_ctx != NULL);
	kmem_free(pmem_ctx, sizeof(struct pmem_context));

	return errors;
}

void cache_block_verifier(struct bittern_cache *bc)
{
	int block_id;
	M_ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);

	bc->bc_cache_block_verifier_scan_started = jiffies;
	bc->bc_cache_block_verifier_scan_completed = 0;
	bc->bc_cache_block_verifier_scan_last_block = jiffies;
	bc->bc_cache_block_verifier_blocks_verified = 0;
	bc->bc_cache_block_verifier_blocks_not_verified_dirty = 0;
	bc->bc_cache_block_verifier_blocks_not_verified_busy = 0;
	bc->bc_cache_block_verifier_blocks_not_verified_invalid = 0;
	bc->bc_cache_block_verifier_verify_errors = 0;

	/* TODO -- check mem header */
	/* FIXME -- check mem header */

	BT_TRACE(BT_LEVEL_TRACE3, bc, NULL, NULL, NULL, NULL, "enter");
	for (block_id = 1; block_id <= atomic_read(&bc->bc_total_entries);
	     block_id++) {
		struct cache_block *cache_block = NULL;
		enum cache_get_ret ret;
		int do_bt_level_trace = BT_LEVEL_TRACE1;
		int errors = 0;

		M_ASSERT(bc != NULL);
		ASSERT_BITTERN_CACHE(bc);

		bc->bc_cache_block_verifier_scan_last_block = jiffies;
		ret = cache_get_by_id(bc, block_id, &cache_block);
		ASSERT_CACHE_GET_RET(ret);
		BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, cache_block, NULL, NULL,
			 "verify block #%d: %d(%s)", block_id, ret,
			 cache_get_ret_to_str(ret));
		switch (ret) {
		case CACHE_GET_RET_HIT_IDLE:
			/*
			 * block is idle and we own it
			 */
			M_ASSERT(cache_block != NULL);
			ASSERT_CACHE_BLOCK(cache_block, bc);
			if (cache_block->bcb_state == CACHE_VALID_CLEAN) {
				unsigned long cache_flags;

				M_ASSERT(cache_block->bcb_transition_path ==
					 CACHE_TRANSITION_PATH_NONE);

				spin_lock_irqsave(&cache_block->bcb_spinlock,
						  cache_flags);
				cache_state_transition_initial(
					bc,
					cache_block,
					CACHE_TRANSITION_PATH_VERIFY_CLEAN_WTWB,
					CACHE_VALID_CLEAN_VERIFY);
				spin_unlock_irqrestore(&cache_block->
						       bcb_spinlock,
						       cache_flags);

				bc->bc_cache_block_verifier_blocks_verified++;
				errors +=
				    cache_block_verify(
					bc, block_id,
					cache_block,
					do_bt_level_trace);
				{
					unsigned long flags;
					struct cache_block *bcb;

					spin_lock_irqsave(&bc->bc_entries_lock,
							  flags);
					bcb =
					    cache_rb_lookup(
						bc,
						cache_block->bcb_sector);
					spin_unlock_irqrestore(&bc->
							       bc_entries_lock,
							       flags);
					if (bcb != NULL) {
						BT_TRACE(do_bt_level_trace, bc,
							 NULL, cache_block,
							 NULL, NULL,
							 "verify: block #%d in red-black tree",
							 block_id);
					} else {
						BT_TRACE(BT_LEVEL_ERROR, bc,
							 NULL, cache_block,
							 NULL, NULL,
							 "verify: block #%d not in red-black tree",
							 block_id);
						printk_err("error: block #%d not in red-black-tree\n",
						     block_id);
						errors += 1;
					}
				}

				spin_lock_irqsave(&cache_block->bcb_spinlock,
						  cache_flags);
				cache_state_transition_final(
					bc,
					cache_block,
					CACHE_TRANSITION_PATH_NONE,
					CACHE_VALID_CLEAN);
				spin_unlock_irqrestore(&cache_block->
						       bcb_spinlock,
						       cache_flags);
				cache_put(bc, cache_block, 1);

				BT_TRACE(do_bt_level_trace, bc, NULL,
					 cache_block, NULL, NULL,
					 "verify: verified block #%d, errors=%d",
					 block_id, errors);
				if (errors != 0) {
					bc->bc_cache_block_verifier_verify_errors++;
					bc->bc_cache_block_verifier_verify_errors_cumulative++;
					BT_TRACE(BT_LEVEL_ERROR, bc, NULL,
						 cache_block, NULL, NULL,
						 "verify: verified block #%d, errors=%d: block has errors",
						 block_id, errors);
					printk_err("verified block #%d, errors=%d: block has errors\n",
					     block_id, errors);
				}
				schedule();
				if ((bc->
				     bc_cache_block_verifier_blocks_verified %
				     10) == 0) {
					M_ASSERT(bc->
						 bc_cache_block_verifier_scan_delay_ms
						 >= 0);
					M_ASSERT(bc->
						 bc_cache_block_verifier_scan_delay_ms
						 >=
						 CACHE_VERIFIER_BLOCK_SCAN_DELAY_MIN_MS);
					M_ASSERT(bc->
						 bc_cache_block_verifier_scan_delay_ms
						 <=
						 CACHE_VERIFIER_BLOCK_SCAN_DELAY_MAX_MS);
					if (bc->
					    bc_cache_block_verifier_scan_delay_ms
					    > 0)
						msleep(bc->
						       bc_cache_block_verifier_scan_delay_ms);
				}
			} else {
				cache_put(bc, cache_block, 1);
				bc->bc_cache_block_verifier_blocks_not_verified_dirty++;
				BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, cache_block,
					 NULL, NULL,
					 "verify: block #%d dirty -- skipping",
					 block_id);
			}
			break;
		case CACHE_GET_RET_HIT_BUSY:
			M_ASSERT(cache_block == NULL);
			bc->bc_cache_block_verifier_blocks_not_verified_busy++;
			BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL,
				 "verify: block #%d busy -- skipping",
				 block_id);
			schedule();
			break;
		case CACHE_GET_RET_INVALID:
			/* FIXME: need to verify metadata */
			bc->bc_cache_block_verifier_blocks_not_verified_invalid++;
			M_ASSERT(cache_block == NULL);
			BT_TRACE(BT_LEVEL_TRACE2, bc, NULL, NULL, NULL, NULL,
				 "verify: block #%d invalid -- skipping",
				 block_id);
			schedule();
			break;
		default:
			printk_err("unexpected return value from cache_by_id #%d: %d(%s)",
			     block_id, ret, cache_get_ret_to_str(ret));
			BUG();
		}

		if (kthread_should_stop()
		    || bc->bc_cache_block_verifier_running == 0) {
			bc->bc_cache_block_verifier_blocks_verified = 0;
			bc->bc_cache_block_verifier_blocks_not_verified_dirty =
			    0;
			bc->bc_cache_block_verifier_blocks_not_verified_busy =
			    0;
			bc->bc_cache_block_verifier_blocks_not_verified_invalid
			    = 0;
			BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL,
				 "kthread_should_stop()=%d, running=%d: stopping verify",
				 kthread_should_stop(),
				 bc->bc_cache_block_verifier_running);
			break;
		}
	}
	bc->bc_cache_block_verifier_scan_completed = jiffies;
	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL,
		 "verify cycle done, verified=%d, not_verified_dirty=%d, not_verified_busy=%d, not_verified_invalid=%d, verifier_verify_errors=%d",
		 bc->bc_cache_block_verifier_blocks_verified,
		 bc->bc_cache_block_verifier_blocks_not_verified_dirty,
		 bc->bc_cache_block_verifier_blocks_not_verified_busy,
		 bc->bc_cache_block_verifier_blocks_not_verified_invalid,
		 bc->bc_cache_block_verifier_verify_errors);
	BT_TRACE(BT_LEVEL_TRACE3, bc, NULL, NULL, NULL, NULL, "exit");
	if (bc->bc_cache_block_verifier_verify_errors > 0) {
		printk_err("error: verify_errors = %d, verify_errors_cumulative = %d\n",
		     bc->bc_cache_block_verifier_verify_errors,
		     bc->bc_cache_block_verifier_verify_errors_cumulative);
		if (bc->bc_cache_block_verifier_bug_on_verify_errors != 0) {
			M_ASSERT(bc->bc_cache_block_verifier_verify_errors ==
				 0);
		}
	}
}

int cache_block_verifier_kthread(void *__bc)
{
	int ret, cycle, m;
	struct bittern_cache *bc = (struct bittern_cache *)__bc;

	set_user_nice(current, CACHE_VERIFIER_THREAD_NICE);

	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL, "enter, nice=%d",
		 CACHE_VERIFIER_THREAD_NICE);
	cycle = 0;
	while (!kthread_should_stop()) {
		ASSERT(bc != NULL);
		ASSERT_BITTERN_CACHE(bc);
		/*
		 * we wake up when we need to start
		 */
		ret = wait_event_interruptible(bc->bc_verifier_wait,
					       (bc->
						bc_cache_block_verifier_running
						!= 0) || kthread_should_stop());
		if (signal_pending(current))
			flush_signals(current);
		if (bc->bc_cache_block_verifier_running == 0)
			continue;
		cycle++;
		ASSERT(bc != NULL);
		ASSERT_BITTERN_CACHE(bc);
		BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL,
			 "starting verify cycle #%d", cycle);
		ASSERT(current == bc->bc_cache_block_verifier_task);
		cache_block_verifier(bc);
		BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL,
			 "completed verify cycle #%d", cycle);
		bc->bc_cache_block_verifier_scans++;
		if (bc->bc_cache_block_verifier_one_shot) {
			BT_TRACE(BT_LEVEL_TRACE1, bc, NULL, NULL, NULL, NULL,
				 "one shot");
			bc->bc_cache_block_verifier_running = 0;
		}
		for (m = CACHE_VERIFIER_PAUSE_DELAY_MS; m > 0; m -= 20) {
			if (kthread_should_stop()) {
				BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL,
					 NULL, "module being removed, exiting");
				goto done;
			}
			msleep(20);
		}
	}
 done:
	BT_TRACE(BT_LEVEL_TRACE0, bc, NULL, NULL, NULL, NULL, "exit");
	bc->bc_cache_block_verifier_task = NULL;
	bc->bc_cache_block_verifier_running = 0;
	return 0;
}
