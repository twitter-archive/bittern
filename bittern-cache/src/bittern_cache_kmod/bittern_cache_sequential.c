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

static void __seq_bypass_initialize(struct seq_io_bypass *bsi,
				    unsigned int bypass_threshold)
{
	int i;

	atomic_set(&bsi->seq_io_count, 0);
	atomic_set(&bsi->non_seq_io_count, 0);
	atomic_set(&bsi->bypass_count, 0);
	atomic_set(&bsi->bypass_hit, 0);
	bsi->bypass_threshold = bypass_threshold;
	bsi->bypass_timeout = SEQ_IO_TIMEOUT_DEFAULT_MS;
	bsi->bypass_enabled = SEQ_IO_BYPASS_ENABLED_DEFAULT;
	spin_lock_init(&bsi->seq_lock);
	bsi->streams_count = 0;
	bsi->streams_count_max = 0;
	bsi->s_streams_len_sum = 0ULL;
	bsi->s_streams_len_count = 0ULL;
	bsi->s_streams_len_max = 0;
	bsi->ns_streams_len_sum = 0ULL;
	bsi->ns_streams_len_count = 0ULL;
	bsi->ns_streams_len_max = 0;
	bsi->lru_hit_depth_sum = 0ULL;
	bsi->lru_hit_depth_count = 0ULL;

	INIT_LIST_HEAD(&bsi->streams_lru);

	for (i = 0; i < SEQ_IO_TRACK_DEPTH; i++) {
		bsi->streams_array[i].magic = BCSIO_MAGIC;
		bsi->streams_array[i].last_sector = -1ULL;
		bsi->streams_array[i].sector_count = 0;
		bsi->streams_array[i].stream_pid = -1;
		list_add(&bsi->streams_array[i].list_entry, &bsi->streams_lru);
	}
}

int seq_bypass_initialize(struct bittern_cache *bc)
{
	M_ASSERT(bc != NULL);
	M_ASSERT(bc->bc_magic1 == BC_MAGIC1);
	M_ASSERT(bc->bc_magic4 == BC_MAGIC4);

	__seq_bypass_initialize(&bc->bc_seq_read,
				SEQ_IO_THRESHOLD_COUNT_READ_DEFAULT);
	__seq_bypass_initialize(&bc->bc_seq_write,
				SEQ_IO_THRESHOLD_COUNT_WRITE_DEFAULT);
	bc->bc_seq_workqueue = alloc_workqueue("b_ws/%s",
					       WQ_MEM_RECLAIM,
					       1,
					       bc->bc_name);
	if (bc->bc_seq_workqueue == NULL) {
		printk_err("%s: cannot allocate seq_io workqueue\n",
			   bc->bc_name);
		return -ENOMEM;
	}

	printk_debug("%s: seq_bypass_initialize done\n", bc->bc_name);

	return 0;
}

void seq_bypass_deinitialize(struct bittern_cache *bc)
{
	printk_debug("%s: seq_bypass_deinitialize\n", bc->bc_name);
	if (bc->bc_seq_workqueue != NULL) {
		printk_info("destroying seq_io workqueue\n");
		destroy_workqueue(bc->bc_seq_workqueue);
	}
}

int set_read_bypass_enabled(struct bittern_cache *bc, int value)
{
	ASSERT(value == 0 || value == 1);
	bc->bc_seq_read.bypass_enabled = value;
	printk_info("bc->bc_name='%s', read_bypass_enabled=%d\n",
		    bc->bc_name,
		    value);
	return 0;
}

int read_bypass_enabled(struct bittern_cache *bc)
{
	return bc->bc_seq_read.bypass_enabled;
}

int set_read_bypass_threshold(struct bittern_cache *bc, int value)
{
	bc->bc_seq_read.bypass_threshold = value;
	return 0;
}

int read_bypass_threshold(struct bittern_cache *bc)
{
	return bc->bc_seq_read.bypass_threshold;
}

int set_read_bypass_timeout(struct bittern_cache *bc, int value)
{
	bc->bc_seq_read.bypass_timeout = value;
	return 0;
}

int read_bypass_timeout(struct bittern_cache *bc)
{

	return bc->bc_seq_read.bypass_timeout;
}

int set_write_bypass_enabled(struct bittern_cache *bc, int value)
{
	ASSERT(value == 0 || value == 1);
	bc->bc_seq_write.bypass_enabled = value;
	printk_info("bc->bc_name='%s', write_bypass_enabled=%d\n",
		    bc->bc_name,
		    value);
	return 0;
}

int write_bypass_enabled(struct bittern_cache *bc)
{
	return bc->bc_seq_write.bypass_enabled;
}

int set_write_bypass_threshold(struct bittern_cache *bc, int value)
{
	bc->bc_seq_write.bypass_threshold = value;
	return 0;
}

int write_bypass_threshold(struct bittern_cache *bc)
{
	return bc->bc_seq_write.bypass_threshold;
}

int set_write_bypass_timeout(struct bittern_cache *bc, int value)
{
	bc->bc_seq_write.bypass_timeout = value;
	return 0;
}

int write_bypass_timeout(struct bittern_cache *bc)
{

	return bc->bc_seq_write.bypass_timeout;
}

/*! update stream len stats - relies on caller to hold the spinlock */
static void seq_stream_len_stats_update(struct seq_io_bypass *bsi,
					struct seq_io_stream *seq_io)
{
	if (seq_io->sector_count >= bsi->bypass_threshold) {
		if (seq_io->sector_count > bsi->s_streams_len_max)
			bsi->s_streams_len_max = seq_io->sector_count;
		bsi->s_streams_len_count++;
		bsi->s_streams_len_sum += seq_io->sector_count;
	} else {
		if (seq_io->sector_count > bsi->ns_streams_len_max)
			bsi->ns_streams_len_max = seq_io->sector_count;
		bsi->ns_streams_len_count++;
		bsi->ns_streams_len_sum += seq_io->sector_count;
	}
}

/*! relies on caller to hold the spinlock */
static void __seq_bypass_timeout_stream(struct bittern_cache *bc,
					struct seq_io_bypass *bsi,
					struct seq_io_stream *seq_io)
{
	BT_TRACE(BT_LEVEL_TRACE3, bc, NULL, NULL, NULL, NULL,
		 "stream_pid=%d, last_sector=%lu, sector_count=%u, threshold=%d: timeout",
		 seq_io->stream_pid,
		 seq_io->last_sector,
		 seq_io->sector_count,
		 bsi->bypass_threshold);
	ASSERT(seq_io->last_sector != -1ULL);

	/* stats */
	ASSERT(bsi->streams_count > 0);
	bsi->streams_count--;
	ASSERT(bsi->streams_count >= 0);
	seq_stream_len_stats_update(bsi, seq_io);
	/* reset */
	seq_io->last_sector = -1ULL;
	seq_io->sector_count = 0;
	seq_io->stream_pid = -1;
	/* move to tail */
	list_del_init(&seq_io->list_entry);
	list_add_tail(&seq_io->list_entry, &bsi->streams_lru);
}

static void __seq_bypass_timeout(struct bittern_cache *bc,
				 struct seq_io_bypass *bsi)
{
	unsigned long flags;
	struct seq_io_stream *seq_io;

	spin_lock_irqsave(&bsi->seq_lock, flags);
	list_for_each_entry(seq_io, &bsi->streams_lru, list_entry) {
		unsigned long age_ms;

		ASSERT(seq_io >= &bsi->streams_array[0] &&
		       seq_io < &bsi->streams_array[SEQ_IO_TRACK_DEPTH]);
		ASSERT(seq_io->magic == BCSIO_MAGIC);
		if (seq_io->last_sector == -1ULL)
			continue;
		age_ms = jiffies_to_msecs(jiffies) - seq_io->timestamp_ms;
		if (age_ms >= bsi->bypass_timeout)
			__seq_bypass_timeout_stream(bc, bsi, seq_io);
	}
	spin_unlock_irqrestore(&bsi->seq_lock, flags);
}

static void seq_bypass_worker(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct bittern_cache *bc;

	bc = container_of(dwork, struct bittern_cache, bc_seq_work);
	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	__seq_bypass_timeout(bc, &bc->bc_seq_write);
	__seq_bypass_timeout(bc, &bc->bc_seq_read);
	schedule_delayed_work(&bc->bc_seq_work, msecs_to_jiffies(5000));
}

void seq_bypass_start_workqueue(struct bittern_cache *bc)
{
	printk_debug("%s: seq_bypass_start_workqueue\n", bc->bc_name);
	M_ASSERT(bc->bc_seq_workqueue != NULL);
	INIT_DELAYED_WORK(&bc->bc_seq_work, seq_bypass_worker);
	schedule_delayed_work(&bc->bc_seq_work, msecs_to_jiffies(5000));
}

void seq_bypass_stop_workqueue(struct bittern_cache *bc)
{
	printk_debug("%s: seq_bypass_stop_workqueue\n", bc->bc_name);
	M_ASSERT(bc->bc_seq_workqueue != NULL);
	printk_debug("%s: cancel_delayed_work\n", bc->bc_name);
	cancel_delayed_work(&bc->bc_seq_work);
	printk_debug("%s: flush_workqueue\n", bc->bc_name);
	flush_workqueue(bc->bc_seq_workqueue);
}

static int seq_bypass_stream_hit(struct bittern_cache *bc,
				 struct seq_io_bypass *bsi,
				 struct seq_io_stream *seq_io,
				 struct bio *bio)
{
	int is_sequential = 0;

	seq_io->last_sector += bio->bi_iter.bi_size / SECTOR_SIZE;
	seq_io->sector_count += bio->bi_iter.bi_size / SECTOR_SIZE;
	seq_io->timestamp_ms = jiffies_to_msecs(jiffies);
	if (seq_io->sector_count >= bsi->bypass_threshold)
		is_sequential = 1;
	/* move to head */
	list_del_init(&seq_io->list_entry);
	list_add(&seq_io->list_entry, &bsi->streams_lru);

	return is_sequential;
}


int seq_bypass_is_sequential(struct bittern_cache *bc,
			     struct bio *bio)
{
	unsigned long flags;
	struct seq_io_bypass *bsi = NULL;
	struct seq_io_stream *seq_io;
	int n = 0;

	ASSERT(bc != NULL);
	ASSERT(bio != NULL);
	ASSERT_BITTERN_CACHE(bc);

	/*
	 * Find the right structure for work_item mode.
	 */

	ASSERT(bio_data_dir(bio) == READ || bio_data_dir(bio) == WRITE);
	if (bio_data_dir(bio) == READ)
		bsi = &bc->bc_seq_read;
	else if (bio_data_dir(bio) == WRITE)
		bsi = &bc->bc_seq_write;

	if (bsi->bypass_enabled == 0)
		return 0;

	BT_TRACE(BT_LEVEL_TRACE4, bc, NULL, NULL, bio, NULL,
		 "enter - current_pid %d",
		 get_current()->pid);

	spin_lock_irqsave(&bsi->seq_lock, flags);

	list_for_each_entry(seq_io, &bsi->streams_lru, list_entry) {
		ASSERT(seq_io >= &bsi->streams_array[0] &&
		       seq_io < &bsi->streams_array[SEQ_IO_TRACK_DEPTH]);
		ASSERT(seq_io->magic == BCSIO_MAGIC);
		n++;
		if (seq_io->last_sector == -1ULL)
			continue;
		if (seq_io->stream_pid != get_current()->pid)
			continue;
		if (bio->bi_iter.bi_sector == seq_io->last_sector) {
			/*
			 * Found maching entry, update stats and figure out
			 * if it's sequential.
			 * Either way, we're done after this.
			 */
			int is_sequential;

			bsi->lru_hit_depth_sum += n;
			bsi->lru_hit_depth_count++;
			is_sequential = seq_bypass_stream_hit(bc,
							      bsi,
							      seq_io,
							      bio);

			spin_unlock_irqrestore(&bsi->seq_lock, flags);

			BT_TRACE(BT_LEVEL_TRACE3, bc, NULL, NULL, bio, NULL,
				 "stream_pid=%d, last_sector=%lu, sector_count=%u, threshold=%d: is_sequential=%d",
				 seq_io->stream_pid,
				 seq_io->last_sector,
				 seq_io->sector_count,
				 bsi->bypass_threshold,
				 is_sequential);

			return is_sequential;
		}
	}

	/*
	 * Couldn't find stream.
	 * Reap the oldest entry and create a new one.
	 */
	seq_io = list_tail(&bsi->streams_lru, struct seq_io_stream, list_entry);
	ASSERT(seq_io >= &bsi->streams_array[0] &&
	       seq_io < &bsi->streams_array[SEQ_IO_TRACK_DEPTH]);
	ASSERT(seq_io->magic == BCSIO_MAGIC);
	/* stats */
	if (seq_io->last_sector == -1ULL) {
		/* first time we allocate this, bump up max */
		ASSERT(bsi->streams_count >= 0);
		bsi->streams_count++;
		if (bsi->streams_count > bsi->streams_count_max)
			bsi->streams_count_max = bsi->streams_count;
	}
	/* setup */
	seq_io->last_sector = bio->bi_iter.bi_sector +
			      bio->bi_iter.bi_size / SECTOR_SIZE;
	seq_io->sector_count = bio->bi_iter.bi_size / SECTOR_SIZE;
	seq_io->stream_pid = get_current()->pid;
	seq_io->timestamp_ms = jiffies_to_msecs(jiffies);
	/* move to head */
	list_del_init(&seq_io->list_entry);
	list_add(&seq_io->list_entry, &bsi->streams_lru);

	spin_unlock_irqrestore(&bsi->seq_lock, flags);

	BT_TRACE(BT_LEVEL_TRACE3, bc, NULL, NULL, bio, NULL,
		 "stream_pid=%d, last_sector=%lu, sector_count=%u, threshold=%d: new stream stream_pid=%d",
		 seq_io->stream_pid,
		 seq_io->last_sector,
		 seq_io->sector_count,
		 bsi->bypass_threshold,
		 seq_io->stream_pid);

	return 0;
}

static int __seq_bypass_stats(struct bittern_cache *bc,
			      struct seq_io_bypass *bsi,
			      const char *subclass,
			      char *result,
			      size_t maxlen)
{
	size_t sz = 0;
	unsigned long long s_avg = 0ULL, ns_avg = 0ULL, d_avg = 0ULL;

	if (bsi->s_streams_len_count > 0)
		s_avg = bsi->s_streams_len_sum / bsi->s_streams_len_count;
	if (bsi->ns_streams_len_count > 0)
		ns_avg = bsi->ns_streams_len_sum / bsi->ns_streams_len_count;
	if (bsi->lru_hit_depth_count > 0)
		d_avg = bsi->lru_hit_depth_sum / bsi->lru_hit_depth_count;

	DMEMIT("%s: sequential: %s_bypass_enabled=%d %s_bypass_threshold=%u %s_bypass_threshold_min=%u %s_bypass_threshold_max=%u %s_bypass_timeout=%u\n",
	       bc->bc_name,
	       subclass, bsi->bypass_enabled,
	       subclass, bsi->bypass_threshold,
	       subclass, SEQ_IO_THRESHOLD_COUNT_MIN,
	       subclass, SEQ_IO_THRESHOLD_COUNT_MAX,
	       subclass, bsi->bypass_timeout);
	DMEMIT("%s: sequential: %s_bypass_streams=%d %s_bypass_streams_max=%d %s_sequential_bypass_count=%u %s_sequential_io_count=%u %s_non_sequential_io_count=%u %s_sequential_bypass_hit=%u %s_sequential_hit_depth_avg=%llu\n",
	       bc->bc_name,
	       subclass, bsi->streams_count,
	       subclass, bsi->streams_count_max,
	       subclass, atomic_read(&bsi->bypass_count),
	       subclass, atomic_read(&bsi->seq_io_count),
	       subclass, atomic_read(&bsi->non_seq_io_count),
	       subclass, atomic_read(&bsi->bypass_hit),
	       subclass, d_avg);
	DMEMIT("%s: sequential: %s_s_streams_len_avg=%llu %s_s_streams_len_max=%u %s_s_streams_len_sum=%llu %s_s_streams_len_count=%llu %s_ns_streams_len_avg=%llu %s_ns_streams_len_max=%u %s_ns_streams_len_sum=%llu %s_ns_streams_len_count=%llu\n",
	       bc->bc_name,
	       subclass, s_avg,
	       subclass, bsi->s_streams_len_max,
	       subclass, bsi->s_streams_len_sum,
	       subclass, bsi->s_streams_len_count,
	       subclass, ns_avg,
	       subclass, bsi->ns_streams_len_max,
	       subclass, bsi->ns_streams_len_sum,
	       subclass, bsi->ns_streams_len_count);

	return sz;
}

int seq_bypass_stats(struct bittern_cache *bc,
		     char *result,
		     size_t maxlen)
{
	size_t sz = 0;

	ASSERT(bc != NULL);
	ASSERT_BITTERN_CACHE(bc);
	sz = __seq_bypass_stats(bc,
				&bc->bc_seq_read,
				"read",
				result,
				maxlen);
	sz += __seq_bypass_stats(bc,
				 &bc->bc_seq_write,
				 "write",
				 result + sz,
				 maxlen - sz);
	return sz;
}
