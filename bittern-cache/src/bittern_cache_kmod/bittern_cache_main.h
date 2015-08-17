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

#ifndef BITTERN_CACHE_MAIN_H
#define BITTERN_CACHE_MAIN_H

/* if set, caller allows hits - mandatory flag */
#define CACHE_FL_HIT	0x1
/* if set, caller allows misses */
#define CACHE_FL_MISS	0x2
/*
 * if set, caller wants clean block on allocate
 * this or CLEAN is mandatory if MISS is set
 * */
#define CACHE_FL_CLEAN	0x4
/* if set, caller wants dirty block on allocate
 * this or DIRTY is mandatory if MISS is set */
#define CACHE_FL_DIRTY	0x8

#define CACHE_FL_MASK  (CACHE_FL_HIT | \
			CACHE_FL_MISS | \
			CACHE_FL_CLEAN | \
			CACHE_FL_DIRTY)

#define CACHE_FL_CLEANDIRTY_MASK         (CACHE_FL_CLEAN | CACHE_FL_DIRTY)

/*! possible return values from cache lookup functions */
enum cache_get_ret {
	/*! caller hit a cache block -- caller owned */
	CACHE_GET_RET_HIT_IDLE = 1,
	/*! caller hit a cache block -- block busy, caller needs to release */
	CACHE_GET_RET_HIT_BUSY,
	/*!
	 * caller missed, and an invalid idle block has been returned
	 * -- caller owned
	 * \todo rename to CACHE_GET_RET_MISS
	 */
	CACHE_GET_RET_MISS_INVALID_IDLE,
	/*!
	* caller missed, no block returned.
	* this happens if caller only wanted hits or
	* caller allowed for misses, but all blocks are busy.
	 * \todo rename to CACHE_GET_RET_MISS_NO_RESOURCES
	*/
	CACHE_GET_RET_MISS,
	/*! cache block is invalid -- only returned by get_by_id() */
	CACHE_GET_RET_INVALID,
};
#define ASSERT_CACHE_GET_RET(__ret) \
	ASSERT((__ret) >= CACHE_GET_RET_HIT_IDLE && (__ret) <= CACHE_GET_RET_INVALID)

extern const char *cache_get_ret_to_str(enum cache_get_ret ret);
/*!
 * try to get a cache block.
 * if block is found, refcount is incremented and cache block is returned.
 * the caller who gets a block with a refcount == 1 becomes the owner and is the
 * only one allowed to change it.
 * other callers need to release the block, or leave it unchanged.
 * when caller is done, it needs to call @ref cache_put to decrement use
 * count.
 */
extern enum cache_get_ret cache_get(struct bittern_cache *bc,
				    sector_t cache_block_sector,
				    const int iflags,
				    struct cache_block **o_cache_block);
/*!
 * used by bgwriter thread to get a dirty block to write out to cached device
 */
extern int cache_get_dirty_from_head(struct bittern_cache *bc,
				     struct cache_block **o_cache_block,
				     int requested_block_age);
/*!
 * used by invalidator thread to get a clean block to invalidate
 */
extern int cache_get_clean(struct bittern_cache *bc,
			   struct cache_block **o_cache_block);
/*!
 * Clone a cache block into a new one.
 * "is_dirty" determines if the cloned block should be S_CLEAN or S_DIRTY.
 */
extern enum cache_get_ret cache_get_clone(struct bittern_cache  *bc,
					  struct cache_block *cache_block,
					  struct cache_block **o_cache_block,
					  bool is_dirty);
/*!
 * get cache_block by id
 */
extern enum cache_get_ret cache_get_by_id(struct bittern_cache *bc,
					  int cache_block_id,
					  struct cache_block **o_cache_block);
/*!
 * invalidate a specific cache block which is in a transitional state
 */
extern void cache_move_to_invalid(struct bittern_cache *bc,
				  struct cache_block *cache_block,
				  int is_dirty);
/*!
 * mark as clean a specific cache block which is in a transitional state
 */
extern void cache_move_to_clean(struct bittern_cache *bc,
				struct cache_block *cache_block);
/*!
 * increment refcount and return its value.
 * if the return value is 1, then the caller now holds (owns) the block.
 */
static inline int cache_block_hold(struct bittern_cache *bc,
				   struct cache_block *cache_block)
{
	int ret;

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ret = atomic_inc_return(&cache_block->bcb_refcount);
	ASSERT(ret > 0);
	return ret;
}

/*!
 * returns true if a cache block is currently being held.
 */
static inline bool cache_block_is_held(struct bittern_cache *bc,
				       struct cache_block *cache_block)
{
	int ret;

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ret = atomic_read(&cache_block->bcb_refcount);
	ASSERT(ret >= 0);
	return ret != 0;
}

/*!
 * decrement refcount and return its value.
 * if the returned value is zero, the block is completely released.
 */
static inline int cache_block_release(struct bittern_cache *bc,
				      struct cache_block *cache_block)
{
	int ret;

	ASSERT_BITTERN_CACHE(bc);
	ASSERT_CACHE_BLOCK(cache_block, bc);
	ret = atomic_dec_return(&cache_block->bcb_refcount);
	ASSERT(ret >= 0);
	return ret;
}

/*
 * release cache blocks obtained with the above APIs
 */
extern void __cache_put(struct bittern_cache *bc,
			struct cache_block *cache_block,
			int is_owner, int update_age);
#define cache_put(__bc, __bcb, __is_owner) \
	__cache_put((__bc), (__bcb), (__is_owner), 0)
#define cache_put_update_age(__bc, __bcb, __is_owner) \
	__cache_put((__bc), (__bcb), (__is_owner), 1)

#ifdef ENABLE_ASSERT
extern int __cache_validate_state_transition(struct bittern_cache *bc,
					     struct cache_block *cache_block,
					     const char *___function,
					     int ___line,
					     enum cache_transition p_from,
					     enum cache_state s_from,
					     enum cache_transition p_to,
					     enum cache_state  s_to);
#else /*ENABLE_ASSERT */
#define __cache_validate_state_transition(__bc, __cb, __func, __line, __p_from, __s_from, __p_to, __s_to) (0)
#endif /*ENABLE_ASSERT */

#define __cache_state_transition(__bc, __bcb,				\
				 __p_from, __s_from, __p_to, __s_to) ({	\
	int __ret;							\
	/* make sure it's l-value -- compiler will optimize it out */	\
	__bcb = (__bcb);						\
	/* make sure it's l-value -- compiler will optimize it out */	\
	__bc = (__bc);							\
	ASSERT_BITTERN_CACHE(__bc);					\
	ASSERT_CACHE_BLOCK(__bcb, __bc);				\
	ASSERT(CACHE_TRANSITION_VALID(__p_from));			\
	ASSERT(CACHE_TRANSITION_VALID(__p_to));				\
	ASSERT(CACHE_STATE_VALID(__s_from));				\
	ASSERT(CACHE_STATE_VALID(__s_to));				\
	__ret = __cache_validate_state_transition((__bc), (__bcb),	\
						  __func__, __LINE__,	\
						  (__p_from),		\
						  (__s_from),		\
						  (__p_to),		\
						  (__s_to));		\
	ASSERT(__p_from == (__bcb)->bcb_cache_transition);		\
	ASSERT(__s_from == (__bcb)->bcb_state);				\
	(__bcb)->bcb_cache_transition = (__p_to);			\
	(__bcb)->bcb_state = (__s_to);					\
	ASSERT(__ret == 0);						\
	atomic_inc(&(__bc)->bc_cache_transitions_counters[__p_to]);	\
	atomic_inc(&(__bc)->bc_cache_states_counters[__s_to]);		\
})
#define cache_state_transition_initial(__bc, __bcb, __p_to, __s_to) ({	\
	/* make sure it's l-value -- compiler will optimize it out */	\
	__bcb = (__bcb);						\
	/* make sure it's l-value -- compiler will optimize it out */	\
	__bc = (__bc);							\
	ASSERT(__bcb->bcb_state == S_CLEAN_NO_DATA ||			\
		__bcb->bcb_state == S_DIRTY_NO_DATA ||			\
		__bcb->bcb_state == S_CLEAN ||				\
		__bcb->bcb_state == S_DIRTY);				\
	__cache_state_transition(__bc,					\
				 __bcb,					\
				 __bcb->bcb_cache_transition,		\
				 __bcb->bcb_state,			\
				 __p_to,				\
				 __s_to);				\
})
#define cache_state_transition3(__bc, __bcb, __p_path, __s_from, __s_to)\
({									\
	/* make sure it's l-value -- compiler will optimize it out */	\
	__bcb = (__bcb);						\
	/* make sure it's l-value -- compiler will optimize it out */	\
	__bc = (__bc);							\
	__cache_state_transition(__bc,					\
				 __bcb,					\
				 __p_path,				\
				 __s_from,				\
				 __p_path,				\
				 __s_to);				\
})
#define cache_state_transition2(__bc, __bcb, __s_from, __s_to) ({	\
	/* make sure it's l-value -- compiler will optimize it out */	\
	__bcb = (__bcb);						\
	/* make sure it's l-value -- compiler will optimize it out */	\
	__bc = (__bc);							\
	__cache_state_transition(__bc,					\
				 __bcb,					\
				 __bcb->bcb_cache_transition,		\
				 __s_from,				\
				 __bcb->bcb_cache_transition,		\
				 __s_to);				\
})
#define cache_state_transition(__bc, __bcb, __s_to) ({			\
	/* make sure it's l-value -- compiler will optimize it out */	\
	__bcb = (__bcb);						\
	/* make sure it's l-value -- compiler will optimize it out */	\
	__bc = (__bc);							\
	__cache_state_transition(__bc,					\
				 __bcb,					\
				 __bcb->bcb_cache_transition,		\
				 __bcb->bcb_state,			\
				 __bcb->bcb_cache_transition,		\
				 __s_to);				\
})
#define cache_state_transition_final(__bc, __bcb, __p_to, __s_to) ({	\
	/* make sure it's l-value -- compiler will optimize it out */	\
	__bcb = (__bcb);						\
	/* make sure it's l-value -- compiler will optimize it out */	\
	__bc = (__bc);							\
	ASSERT(__p_to == TS_NONE);					\
	ASSERT(__s_to == S_INVALID ||					\
		__s_to == S_CLEAN ||					\
		__s_to == S_DIRTY);					\
	__cache_state_transition(__bc,					\
				__bcb,					\
				__bcb->bcb_cache_transition,		\
				__bcb->bcb_state,			\
				__p_to,					\
				__s_to);				\
})

/*! \todo
 * @ref work_item_allocate and
 * @ref work_item_reallocate have a lot in common,
 * can be simplified.
 */
extern struct work_item *work_item_allocate(struct bittern_cache *bc,
					    struct cache_block *cache_block,
					    struct bio *bio,
					    int wi_flags);
extern void work_item_reallocate(struct bittern_cache *bc,
				 struct cache_block *cache_block,
				 struct work_item *wi,
				 struct bio *bio,
				 int wi_flags);
extern void work_item_free(struct bittern_cache *bc,
			   struct work_item *wi);

/*!
 * Adds work_item to list of pending IOs.
 * op_type is assumed to be a valid pointer to a constant string
 * which can be used across time.
 */
static inline void
work_item_add_pending_io(struct bittern_cache *bc,
			 struct work_item *wi,
			 const char *op_type,
			 sector_t op_sector,
			 unsigned long op_rw)
{
	unsigned long flags;

	wi->wi_op_type = op_type;
	wi->wi_op_sector = op_sector;
	wi->wi_op_rw = op_rw;
	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	list_add_tail(&wi->wi_pending_io_list, &bc->bc_pending_requests_list);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);
}

/*!
 * Deletes work_item from list of pending IOs.
 */
static inline void
work_item_del_pending_io(struct bittern_cache *bc,
			 struct work_item *wi)
{
	unsigned long flags;

	spin_lock_irqsave(&bc->bc_entries_lock, flags);
	list_del_init(&wi->wi_pending_io_list);
	spin_unlock_irqrestore(&bc->bc_entries_lock, flags);
}

/*!
 * this is called upon every action which frees up resources which the deferred
 * thread is waiting on, that is completion of a pending i/o request, that is:
 * - cache operation done by _map() block request
 * - dirty cache writeback
 * - cache invalidation
 * - freeing up buffers
 */
extern void wakeup_deferred(struct bittern_cache *bc);
/*! queue request to deferred queue for execution in a thread context */
extern void queue_to_deferred(struct bittern_cache *bc,
				    struct deferred_queue *queue,
				    struct bio *bio,
				    struct deferred_queue *old_queue);
/*! dequeue request from deferred queue -- used by @ref handle_deferred */
extern struct bio *dequeue_from_deferred(struct bittern_cache *bc,
					       struct deferred_queue *queue);

static inline bool bio_is_pureflush_request(struct bio *bio)
{
	if ((bio->bi_rw & REQ_FLUSH) != 0 && bio->bi_iter.bi_size == 0)
		return true;
	return false;
}

static inline bool bio_is_discard_request(struct bio *bio)
{
	if ((bio->bi_rw & REQ_DISCARD) != 0)
		return true;
	return false;
}

static inline bool bio_is_pureflush_or_discard_request(struct bio *bio)
{
	return bio_is_pureflush_request(bio) ||
	       bio_is_discard_request(bio);
}

static inline bool bio_is_data_request(struct bio *bio)
{
	return !bio_is_pureflush_or_discard_request(bio);
}

/*! cached device generic callback */
extern void cached_dev_make_request_endio(struct work_item *wi,
					  struct bio *bio,
					  int err);
/*!
 * Do generic_make_request() immediately.
 * Can only be called in a sleepable context.
 * Once the IO operation is complete, @ref cached_dev_make_request_endio
 * is called.
 */
extern void cached_dev_do_make_request(struct bittern_cache *bc,
				       struct work_item *wi,
				       int datadir,
				       bool set_original_bio);
/*! defer generic_make_request() to thread (workqueue) */
extern void cached_dev_make_request_defer(struct bittern_cache *bc,
					  struct work_item *wi,
					  int datadir,
					  bool set_original_bio);

/*! main state machine */
extern void cache_state_machine(struct bittern_cache *bc,
				struct work_item *wi,
				int err);

/*! copy from cache to bio, aka userland reads */
extern void __bio_copy_from_cache(struct work_item *wi,
				  struct bio *bio,
				  uint128_t *hash_data);
#define bio_copy_from_cache_nohash(__wi, __bio)		\
		__bio_copy_from_cache((__wi), (__bio), NULL)
#define bio_copy_from_cache(__wi, __bio, __hash_data)	\
		__bio_copy_from_cache((__wi), (__bio), (__hash_data))
/*! copy to cache from bio, aka userland writes */
extern void bio_copy_to_cache(struct work_item *wi,
			      struct bio *bio,
			      uint128_t *hash_data);

extern void cache_get_page_read_callback(struct bittern_cache *bc,
					 struct cache_block *cache_block,
					 struct pmem_context *pmem_ctx,
					 void *callback_context,
					 int err);
extern void cache_put_page_write_callback(struct bittern_cache *bc,
					  struct cache_block *cache_block,
					  struct pmem_context *pmem_ctx,
					  void *callback_context,
					  int err);
extern void cache_metadata_write_callback(struct bittern_cache *bc,
					  struct cache_block *cache_block,
					  struct pmem_context *pmem_ctx,
					  void *callback_context,
					  int err);

/*
 * read path state machine functions
 */
extern void sm_read_hit_copy_from_cache_start(struct bittern_cache *bc,
					      struct work_item *wi);
extern void sm_read_hit_copy_from_cache_end(struct bittern_cache *bc,
					    struct work_item *wi,
					    int err);
extern void sm_read_miss_copy_from_device_start(struct bittern_cache *bc,
						struct work_item *wi);
extern void sm_read_miss_copy_from_device_end(struct bittern_cache *bc,
					      struct work_item *wi,
					      int err);
extern void sm_read_miss_copy_to_cache_end(struct bittern_cache *bc,
					   struct work_item *wi,
					   int err);

/*
 * bgwriter callback, called by state machine
 */
extern void cache_bgwriter_io_end(struct bittern_cache *bc,
				  struct work_item *wi,
				  struct cache_block *cache_block);
/*
 * write path state machine functions
 */
extern void sm_dirty_write_miss_copy_to_cache_start(struct bittern_cache *bc,
						    struct work_item *wi);
extern void sm_dirty_write_miss_copy_to_cache_end(struct bittern_cache *bc,
						  struct work_item *wi,
						  int err);
extern void sm_clean_write_miss_copy_to_device_start(struct bittern_cache *bc,
						     struct work_item *wi);
extern void sm_clean_write_miss_copy_to_device_end(struct bittern_cache *bc,
						   struct work_item *wi,
						   int err);
extern void sm_clean_write_miss_copy_to_cache_end(struct bittern_cache *bc,
						  struct work_item *wi,
						  int err);

/*
 * writeback path state machine functions
 */
extern void sm_writeback_copy_from_cache_start(struct bittern_cache *bc,
					       struct work_item *wi);
extern void sm_writeback_copy_from_cache_end(struct bittern_cache *bc,
					     struct work_item *wi,
					     int err);
extern void sm_writeback_copy_to_device_end(struct bittern_cache *bc,
					    struct work_item *wi,
					    int err);
extern void sm_writeback_update_metadata_end(struct bittern_cache *bc,
					     struct work_item *wi,
					     int err);

/*
 * write path - partial write hit
 */
extern void sm_clean_pwrite_hit_copy_from_cache_start(struct bittern_cache *bc,
						      struct work_item *wi);

/*
 * write path - dirty write hit state machine functions
 */
extern void
sm_dirty_pwrite_hit_copy_from_cache_start(struct bittern_cache *bc,
					  struct work_item *wi);
extern void
sm_dirty_write_hit_copy_to_cache_start(struct bittern_cache *bc,
				       struct work_item *wi);
extern void
sm_dirty_pwrite_hit_copy_to_cache_start(struct bittern_cache *bc,
					struct work_item *wi,
					int err);
extern void
sm_dirty_write_hit_copy_to_cache_end(struct bittern_cache *bc,
				     struct work_item *wi,
				     int err);

/*
 * write path - partial write miss
 */
extern void
sm_pwrite_miss_copy_from_device_start(struct bittern_cache *bc,
				      struct work_item *wi);
extern void
sm_pwrite_miss_copy_from_device_end(struct bittern_cache *bc,
				    struct work_item *wi,
				    int err);
extern void
sm_pwrite_miss_copy_to_device_end(struct bittern_cache *bc,
				    struct work_item *wi,
				    int err);
extern void
sm_pwrite_miss_copy_to_cache_end(struct bittern_cache *bc,
				 struct work_item *wi,
				 int err);

/*
 * invalidate callback, called by state machine
 */
extern void cache_invalidate_block_io_end(struct bittern_cache *bc,
					  struct work_item *wi,
					  struct cache_block *cache_block);
/*
 * invalidate path
 */
extern void
sm_invalidate_start(struct bittern_cache *bc,
		    struct work_item *wi);
extern void
sm_invalidate_end(struct bittern_cache *bc,
		  struct work_item *wi,
		  int err);

extern int
__cache_verify_hash_data_ret(struct bittern_cache *bc,
			     struct cache_block *cache_block,
			     uint128_t hash_data,
			     const char *func,
			     int line);
/*!
 * Returns number of errors.
 * This is an "internal" function used directly only by the verifier
 * thread, as it is its decision whether to crash on data corruption.
 */
#define cache_verify_hash_data_ret(__bc, __cache_block, __hash_data)	\
		__cache_verify_hash_data_ret(__bc,			\
					     __cache_block,		\
					     __hash_data,		\
					     __func__,			\
					     __LINE__)


extern int
__cache_verify_hash_data_buffer_ret(struct bittern_cache *bc,
				    struct cache_block *cache_block,
				    void *buffer,
				    const char *func,
				    int line);
/*!
 * Returns number of errors.
 * This is an "internal" function used directly only by the verifier
 * thread, as it is its decision whether to crash on data corruption.
 */
#define cache_verify_hash_data_buffer_ret(__bc,				\
					  __cache_block,		\
					  __buffer)			\
		__cache_verify_hash_data_buffer_ret(__bc,		\
						    __cache_block,	\
						    __buffer,		\
						    __func__,		\
						    __LINE__)

extern void
__cache_verify_hash_data(struct bittern_cache *bc,
			 struct cache_block *cache_block,
			 uint128_t hash_data,
			 const char *func,
			 int line);
#define cache_verify_hash_data(__bc, __cache_block, __hash_data)	\
		__cache_verify_hash_data(__bc,				\
					 __cache_block,			\
					 __hash_data,			\
					 __func__,			\
					 __LINE__)


extern void
__cache_verify_hash_data_buffer(struct bittern_cache *bc,
				struct cache_block *cache_block,
				void *buffer,
				const char *func,
				int line);
#define cache_verify_hash_data_buffer(__bc,				\
					__cache_block,			\
					__buffer)			\
		__cache_verify_hash_data_buffer(__bc,			\
						__cache_block,		\
						__buffer,		\
						__func__,		\
						__LINE__)

#endif /* BITTERN_CACHE_MAIN_H */
