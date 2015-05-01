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

enum cache_get_ret {
	/* caller hit a cache block -- caller owned */
	CACHE_GET_RET_HIT_IDLE = 1,
	/* caller hit a cache block -- block busy, caller needs to release */
	CACHE_GET_RET_HIT_BUSY,
	/*
	 * caller missed, and an invalid idle block has been returned
	 * -- caller owned
	 */
	CACHE_GET_RET_MISS_INVALID_IDLE,
	/*
	* caller missed, no block returned.
	* this happens if caller only wanted hits or
	* caller allowed for misses, but all blocks are busy.
	*/
	CACHE_GET_RET_MISS,
	/* cache block is invalid -- only returned by get_by_id() */
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
 * clone a cache block into a new one
 */
extern enum cache_get_ret cache_get_clone(struct bittern_cache  *bc,
					  struct cache_block *cache_block,
					  struct cache_block **o_cache_block);
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
					     enum transition_path p_from,
					     enum cache_state s_from,
					     enum transition_path p_to,
					     enum cache_state  s_to);
#else /*ENABLE_ASSERT */
#define __cache_validate_state_transition(__bc, __cb, __func, __line, __p_from, __s_from, __p_to, __s_to) (0)
#endif /*ENABLE_ASSERT */

#define __cache_state_transition(__bc, __bcb, __p_from, __s_from, __p_to, __s_to) ({                            \
	int __ret;                                                                                                      \
	__bcb = (__bcb); /* this makes sure __bcb is an l-value -- compiler will optimize this out */                   \
	__bc = (__bc); /* this makes sure __bc is an l-value -- compiler will optimize this out */                      \
	ASSERT_BITTERN_CACHE(__bc);                                                                                     \
	ASSERT_CACHE_BLOCK(__bcb, __bc);                                                                        \
	ASSERT(CACHE_TRANSITION_PATH_VALID(__p_from));                                                          \
	ASSERT(CACHE_TRANSITION_PATH_VALID(__p_to));                                                            \
	ASSERT(CACHE_STATE_VALID(__s_from));                                                                    \
	ASSERT(CACHE_STATE_VALID(__s_to));                                                                      \
	__ret = __cache_validate_state_transition((__bc), (__bcb),                                              \
						__func__, __LINE__,                                                     \
						(__p_from), (__s_from),                                                 \
						(__p_to), (__s_to));                                                    \
	ASSERT(__p_from == (__bcb)->bcb_transition_path);                                                               \
	ASSERT(__s_from == (__bcb)->bcb_state);                                                                         \
	(__bcb)->bcb_transition_path = (__p_to);                                                                        \
	(__bcb)->bcb_state = (__s_to);                                                                                  \
	ASSERT(__ret == 0);                                                                                             \
	atomic_inc(&(__bc)->bc_transition_paths_counters[__p_to]);                                                \
	atomic_inc(&(__bc)->bc_cache_states_counters[__s_to]);                                                          \
})
#define cache_state_transition_initial(__bc, __bcb, __p_to, __s_to) ({                                          \
	__bcb = (__bcb); /* this makes sure __bcb is an l-value -- compiler will optimize this out */                   \
	__bc = (__bc); /* this makes sure __bc is an l-value -- compiler will optimize this out */                      \
	ASSERT(__bcb->bcb_state == CACHE_VALID_CLEAN_NO_DATA ||                                                 \
		__bcb->bcb_state == CACHE_VALID_DIRTY_NO_DATA ||                                                \
		__bcb->bcb_state == CACHE_VALID_CLEAN ||                                                        \
		__bcb->bcb_state == CACHE_VALID_DIRTY);                                                         \
	__cache_state_transition(__bc, __bcb, __bcb->bcb_transition_path, __bcb->bcb_state, __p_to, __s_to);    \
})
#define cache_state_transition3(__bc, __bcb, __p_path, __s_from, __s_to) ({                                     \
	__bcb = (__bcb); /* this makes sure __bcb is an l-value -- compiler will optimize this out */                   \
	__bc = (__bc); /* this makes sure __bc is an l-value -- compiler will optimize this out */                      \
	__cache_state_transition(__bc, __bcb, __p_path, __s_from, __p_path, __s_to);                            \
})
#define cache_state_transition2(__bc, __bcb, __s_from, __s_to) ({                                               \
	__bcb = (__bcb); /* this makes sure __bcb is an l-value -- compiler will optimize this out */                   \
	__bc = (__bc); /* this makes sure __bc is an l-value -- compiler will optimize this out */                      \
	__cache_state_transition(__bc, __bcb,                                                                   \
					__bcb->bcb_transition_path, __s_from,                                           \
					__bcb->bcb_transition_path, __s_to);                                            \
})
#define cache_state_transition(__bc, __bcb, __s_to) ({                                                          \
	__bcb = (__bcb); /* this makes sure __bcb is an l-value -- compiler will optimize this out */                   \
	__bc = (__bc); /* this makes sure __bc is an l-value -- compiler will optimize this out */                      \
	__cache_state_transition(__bc, __bcb,                                                                   \
					__bcb->bcb_transition_path, __bcb->bcb_state,                                   \
					__bcb->bcb_transition_path, __s_to);                                            \
})
#define cache_state_transition_final(__bc, __bcb, __p_to, __s_to) ({                                            \
	__bcb = (__bcb); /* this makes sure __bcb is an l-value -- compiler will optimize this out */                   \
	__bc = (__bc); /* this makes sure __bc is an l-value -- compiler will optimize this out */                      \
	ASSERT(__p_to == CACHE_TRANSITION_PATH_NONE);                                                           \
	ASSERT(__s_to == CACHE_INVALID ||                                                                       \
		__s_to == CACHE_VALID_CLEAN ||                                                                  \
		__s_to == CACHE_VALID_DIRTY);                                                                   \
	__cache_state_transition(__bc, __bcb, __bcb->bcb_transition_path, __bcb->bcb_state, __p_to, __s_to);    \
})

/*! \todo
 * @ref cache_work_item_allocate and
 * @ref cache_work_item_reallocate have a lot in common,
 * can be simplified.
 */
extern struct work_item *cache_work_item_allocate(struct bittern_cache *bc,
						  struct cache_block *cache_block,
						  struct bio *bio,
						  int wi_flags,
						  wi_io_endio_f  wi_io_endio,
						  int gfp_flags);
extern void cache_work_item_reallocate(struct bittern_cache *bc,
				       struct cache_block *cache_block,
				       struct work_item *wi,
				       struct bio *bio, int wi_flags,
				       wi_io_endio_f wi_io_endio);
extern void cache_work_item_free(struct bittern_cache *bc,
				 struct work_item *wi);

static inline void
cache_work_item_add_pending_io(struct bittern_cache *bc, struct work_item *wi,
			       char op_type, sector_t op_sector,
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

static inline void
cache_work_item_del_pending_io(struct bittern_cache *bc,
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
extern void cache_wakeup_deferred(struct bittern_cache *bc);
/*! queue request to deferred queue for execution in a thread context */
extern void cache_queue_to_deferred(struct bittern_cache *bc,
				    struct deferred_queue *queue,
				    struct work_item *wi);
/*! dequeue request from deferred queue -- used by @ref cache_handle_deferred */
extern struct work_item *cache_dequeue_from_deferred(struct bittern_cache *bc,
						struct deferred_queue *queue);

static inline int bio_is_pureflush_request(struct bio *bio)
{
	if ((bio->bi_rw & REQ_FLUSH) != 0 && bio->bi_iter.bi_size == 0)
		return 1;
	return 0;
}

static inline int bio_is_discard_request(struct bio *bio)
{
	if ((bio->bi_rw & REQ_DISCARD) != 0)
		return 1;
	return 0;
}

static inline int bio_is_pureflush_or_discard_request(struct bio *bio)
{
	if ((bio->bi_rw & REQ_DISCARD) != 0)
		return 1;
	if ((bio->bi_rw & REQ_FLUSH) != 0 && bio->bi_iter.bi_size == 0)
		return 1;
	return 0;
}

#define bio_is_data_request(__bio) \
	(bio_is_pureflush_or_discard_request(__bio) == 0)

extern void cache_make_request_worker(struct work_struct *work);
extern void cache_state_machine_endio(struct bio *cloned_bio, int err);
/*! main state machine */
extern void cache_state_machine(struct bittern_cache *bc,
				struct work_item *wi,
				struct bio *bio);
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
					 struct data_buffer_info *dbi_data,
					 void *callback_context, int err);
extern void cache_put_page_write_callback(struct bittern_cache *bc,
					  struct cache_block *cache_block,
					  struct data_buffer_info *dbi_data,
					  void *callback_context, int err);
extern void cache_metadata_write_callback(struct bittern_cache *bc,
					  struct cache_block *cache_block,
					  struct data_buffer_info *dbi_data,
					  void *callback_context,  int err);

/*
 * read path state machine functions
 */
extern void sm_read_hit_copy_from_cache_start(struct bittern_cache *bc,
					      struct work_item *wi,
					      struct bio *bio);
extern void sm_read_hit_copy_from_cache_end(struct bittern_cache *bc,
					    struct work_item *wi,
					    struct bio *bio);
extern void sm_read_miss_copy_from_device_startio(struct bittern_cache *bc,
						  struct work_item *wi,
						  struct bio *bio);
extern void sm_read_miss_copy_from_device_endio(struct bittern_cache *bc,
						struct work_item *wi,
						struct bio *bio);
extern void sm_read_miss_copy_to_cache_end(struct bittern_cache *bc,
					   struct work_item *wi,
					   struct bio *bio);

/*
 * write path state machine functions
 */
extern void sm_dirty_write_miss_copy_to_cache_start(struct bittern_cache *bc,
						    struct work_item *wi,
						    struct bio *bio);
extern void sm_dirty_write_miss_copy_to_cache_end(struct bittern_cache *bc,
						  struct work_item *wi,
						  struct bio *bio);
extern void sm_clean_write_miss_copy_to_device_startio(struct bittern_cache *bc,
						       struct work_item *wi,
						       struct bio *bio);
extern void sm_clean_write_miss_copy_to_device_endio(struct bittern_cache *bc,
						     struct work_item *wi,
						     struct bio *bio);
extern void sm_clean_write_miss_copy_to_cache_end(struct bittern_cache *bc,
						  struct work_item *wi,
						  struct bio *bio);

/*
 * writeback path state machine functions
 */
extern void sm_writeback_copy_from_cache_start(struct bittern_cache *bc,
					       struct work_item *wi,
					       struct bio *bio);
extern void sm_writeback_copy_from_cache_end(struct bittern_cache *bc,
					     struct work_item *wi,
					     struct bio *bio);
extern void sm_writeback_copy_to_device_endio(struct bittern_cache *bc,
					      struct work_item *wi,
					      struct bio *bio);
extern void sm_writeback_update_metadata_end(struct bittern_cache *bc,
					     struct work_item *wi,
					     struct bio *bio);

/*
 * write path - partial write hit
 */
extern void sm_clean_pwrite_hit_copy_from_cache_start(struct bittern_cache *bc,
						      struct work_item *wi,
						      struct bio *bio);

/*
 * write path - dirty write hit state machine functions
 */
extern void
sm_dirty_pwrite_hit_clone_copy_from_cache_start(struct bittern_cache *bc,
						struct work_item *wi,
						struct bio *bio);
extern void
sm_dirty_write_hit_clone_copy_to_cache_start(struct bittern_cache *bc,
					     struct work_item *wi,
					     struct bio *bio);

extern void
sm_dirty_write_hit_clone_copy_to_cache_end(struct bittern_cache *bc,
					   struct work_item *wi,
					   struct bio *bio);

/*
 * write path - partial write miss
 */
extern void
sm_pwrite_miss_copy_from_device_endio(struct bittern_cache *bc,
				    struct work_item *wi,
				    struct bio *bio);
extern void
sm_pwrite_miss_copy_from_device_startio(struct bittern_cache *bc,
				      struct work_item *wi,
				      struct bio *bio);
extern void
sm_pwrite_miss_copy_to_device_endio(struct bittern_cache *bc,
				    struct work_item *wi,
				    struct bio *bio);
extern void
sm_pwrite_miss_copy_to_cache_end(struct bittern_cache *bc,
				 struct work_item *wi,
				 struct bio *bio);

/*
 * invalidate path
 */
extern void
sm_invalidate_start(struct bittern_cache *bc,
		    struct work_item *wi,
		    struct bio *bio);
extern void
sm_invalidate_end(struct bittern_cache *bc,
		  struct work_item *wi,
		  struct bio *bio);

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

/*!
 * \todo
 * should clean up the pagebuf_allocate* functions and let
 * them deal directly with dbi struct. in that case we should be able to do away
 * with most of this code (which is basically a wrapper and a bunch of asserts
 */

/*!
 * Allocate a pagebuf from the specified pool.
 * This is a non-suspensive version, allocation will fail if there are no free
 * buffers in the page pool.
 */
static inline void
pagebuf_allocate_dbi_nowait(struct bittern_cache *bc,
			    int pool,
			    struct data_buffer_info *dbi)
{
	ASSERT(dbi->di_buffer_vmalloc_buffer == NULL);
	ASSERT(dbi->di_buffer_vmalloc_page == NULL);
	ASSERT(dbi->di_buffer_vmalloc_pool == -1);
	dbi->di_buffer_vmalloc_buffer = pagebuf_allocate_nowait(bc,
						pool,
						&dbi->di_buffer_vmalloc_page);
	if (dbi->di_buffer_vmalloc_buffer != NULL) {
		ASSERT(PAGE_ALIGNED(dbi->di_buffer_vmalloc_buffer));
		ASSERT(dbi->di_buffer_vmalloc_page != NULL);
		ASSERT(dbi->di_buffer_vmalloc_page ==
		       vmalloc_to_page(dbi->di_buffer_vmalloc_buffer));
		dbi->di_buffer_vmalloc_pool = pool;
	} else {
		ASSERT(dbi->di_buffer_vmalloc_page == NULL);
	}
}

/*!
 * Allocate a vmalloc buffer from the specified pool.
 */
static inline void
pagebuf_allocate_dbi_wait(struct bittern_cache *bc,
			  int pool,
			  struct data_buffer_info *dbi)
{
	ASSERT(dbi->di_buffer_vmalloc_buffer == NULL);
	ASSERT(dbi->di_buffer_vmalloc_page == NULL);
	ASSERT(dbi->di_buffer_vmalloc_pool == -1);
	dbi->di_buffer_vmalloc_buffer = pagebuf_allocate_wait(bc,
						pool,
						&dbi->di_buffer_vmalloc_page);
	ASSERT(dbi->di_buffer_vmalloc_buffer != NULL);
	ASSERT(PAGE_ALIGNED(dbi->di_buffer_vmalloc_buffer));
	ASSERT(dbi->di_buffer_vmalloc_page != NULL);
	ASSERT(dbi->di_buffer_vmalloc_page ==
	       vmalloc_to_page(dbi->di_buffer_vmalloc_buffer));
	dbi->di_buffer_vmalloc_pool = pool;
}

/*!
 * frees up a vmalloc buffer obtain with either
 * @ref pagebuf_allocate_dbi_wait or
 * @ref pagebuf_allocate_dbi_nowait
 */
static inline void pagebuf_free_dbi(struct bittern_cache *bc,
				    struct data_buffer_info *dbi)
{
	ASSERT(dbi->di_buffer_vmalloc_buffer != NULL);
	ASSERT(PAGE_ALIGNED(dbi->di_buffer_vmalloc_buffer));
	ASSERT(dbi->di_buffer_vmalloc_page != NULL);
	ASSERT(dbi->di_buffer_vmalloc_page ==
	       vmalloc_to_page(dbi->di_buffer_vmalloc_buffer));
	ASSERT(dbi->di_buffer_vmalloc_pool != -1);
	pagebuf_free(bc,
		     dbi->di_buffer_vmalloc_pool,
		     dbi->di_buffer_vmalloc_buffer);
	dbi->di_buffer_vmalloc_buffer = NULL;
	dbi->di_buffer_vmalloc_page = NULL;
	dbi->di_buffer_vmalloc_pool = -1;
}

/*! \todo right now we always allocate the double buffer even when not needed */
#define __ASSERT_PAGE_DBI(__dbi, __flags) ({				     \
	/* make sure it's an l-value. compiler will optimize this away */    \
	__dbi = (__dbi);						     \
	ASSERT(__dbi->di_buffer_vmalloc_buffer != NULL);		     \
	ASSERT(PAGE_ALIGNED(__dbi->di_buffer_vmalloc_buffer));		     \
	ASSERT(__dbi->di_buffer_vmalloc_page != NULL);			     \
	ASSERT(__dbi->di_buffer_vmalloc_page ==				     \
	       vmalloc_to_page(__dbi->di_buffer_vmalloc_buffer));	     \
	ASSERT((__dbi->di_flags & (__flags)) == (__flags));		     \
	if ((__dbi->di_flags & CACHE_DI_FLAGS_DOUBLE_BUFFERING) != 0) {	     \
		ASSERT(__dbi->di_buffer == __dbi->di_buffer_vmalloc_buffer); \
		ASSERT(__dbi->di_page == __dbi->di_buffer_vmalloc_page);     \
	} else {							     \
		ASSERT(__dbi->di_buffer != NULL);			     \
		ASSERT(__dbi->di_page != NULL);				     \
	}								     \
	ASSERT(__dbi->di_flags != 0x0);					     \
	ASSERT(atomic_read(&__dbi->di_busy) == 1);			     \
})
#define ASSERT_PAGE_DBI(__dbi)				\
		__ASSERT_PAGE_DBI(__dbi, 0x0)
#define ASSERT_PAGE_DBI_DOUBLE_BUFFERING(__dbi)		\
		__ASSERT_PAGE_DBI(__dbi, CACHE_DI_FLAGS_DOUBLE_BUFFERING)

static inline void cache_set_page_dbi(struct data_buffer_info *dbi,
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

static inline void
cache_set_page_dbi_double_buffering(struct data_buffer_info *dbi, int flags)
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
	       vmalloc_to_page(dbi->di_buffer_vmalloc_buffer));
	dbi->di_buffer = dbi->di_buffer_vmalloc_buffer;
	dbi->di_page = dbi->di_buffer_vmalloc_page;
	dbi->di_flags = flags;
	atomic_inc(&dbi->di_busy);
}

static inline void
__cache_clear_page_dbi(struct data_buffer_info *dbi, int flags)
{
	__ASSERT_PAGE_DBI(dbi, flags);
	dbi->di_buffer = NULL;
	dbi->di_page = NULL;
	dbi->di_flags = 0x0;
	atomic_dec(&dbi->di_busy);
	ASSERT(atomic_read(&dbi->di_busy) == 0);
}

/*
 * when we know we are using double buffering, use this macro instead of the
 * next
 * */
#define cache_clear_page_dbi_double_buffering(__dbi)                    \
		__cache_clear_page_dbi(__dbi, CACHE_DI_FLAGS_DOUBLE_BUFFERING)
#define cache_clear_page_dbi(__dbi)                                     \
		__cache_clear_page_dbi(__dbi, 0)

#endif /* BITTERN_CACHE_MAIN_H */
