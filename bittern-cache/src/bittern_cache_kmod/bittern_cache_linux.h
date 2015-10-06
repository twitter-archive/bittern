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

#ifndef BITTERN_CACHE_LINUX_H
#define BITTERN_CACHE_LINUX_H

#include <linux/version.h>
#include <linux/kernel.h>

#define TRUE 1
#define FALSE 0

#define list_tail(__list, __type, __member) \
	list_entry((__list)->prev, __type, __member)
#define list_next(__element, __type, __member) \
	list_entry((__element)->__member.next, __type, __member)
#define list_non_empty(__list) (!list_empty(__list))

#define bio_list_non_empty(__list)	(!bio_list_empty(__list))

#define jiffies_to_secs(__jiffies) \
	((unsigned long)(jiffies_to_msecs(__jiffies)) / 1000UL)

/* percent macro: this calculates _a % of _b (result undefined if _b is zero) */
#define PERCENT_OF(_a, _b) ({ ((_a) * 100) / (_b); })

#define RB_NON_EMPTY_NODE(__node) (!RB_EMPTY_NODE(__node))
#define RB_NON_EMPTY_ROOT(__root) (!RB_EMPTY_ROOT(__root))

/*
 * not using constants totally sucks, but this is how bio_data_dir works
 */
#define bio_set_data_dir_write(__bio) ((__bio)->bi_rw |= 1)
#define bio_set_data_dir_read(__bio) ((__bio)->bi_rw &= ~1)
#define data_dir_read(__bi_rw) (((__bi_rw) & 1) == READ)
#define data_dir_write(__bi_rw) (((__bi_rw) & 1) == WRITE)
#define bio_data_dir_read(__bio) (((__bio)->bi_rw & 1) == READ)
#define bio_data_dir_write(__bio) (((__bio)->bi_rw & 1) == WRITE)

/*!
 * Atomically compares "new" with "v".
 * If "new" is higher than "v", "v" is set to "new".
 */
static inline void atomic_set_if_higher(atomic_t *v, int new)
{
	for (;;) {
		int curr, old;

		curr = atomic_read(v);
		if (new <= curr)
			break;
		old = atomic_cmpxchg(v, curr, new);
		if (old == curr)
			break;
	}
}

/*!
 * Atomically compares "new" with "v".
 * If "new" is higher than "v", "v" is set to "new".
 */
static inline void atomic64_set_if_higher(atomic64_t *v, long long new)
{
	for (;;) {
		long long curr, old;

		curr = atomic64_read(v);
		if (new <= curr)
			break;
		old = atomic64_cmpxchg(v, curr, new);
		if (old == curr)
			break;
	}
}

/*!
 * why linux does not define sector size and yet it uses 512 and
 * its corresponding 9 shift everywhere?
 */
#define SECTOR_SIZE 512

/*! converts kbytes to sectors */
#define KBYTES_TO_SECTORS(__kb) (((__kb) * 1024) / SECTOR_SIZE)
/*! converts mbytes to sectors */
#define MBYTES_TO_SECTORS(__mb) (((__mb) * 1024 * 1024) / SECTOR_SIZE)

/*!
 * ASSERT, M_ASSERT and M_ASSERT_FIXME.
 * these macros are used to assert code invariants.
 *
 * M_ASSERT cannot be compiled out (Mandatory_ASSERT)
 *
 * M_ASSERT_FIXME is just like M_ASSERT, except it denotes places where we
 * really ought to do error handling instead.
 */
#define M_ASSERT(__assert_expr__)        do {                               \
	if (!(__assert_expr__)) {                                           \
		printk(KERN_ERR "%s@%d(): ERROR: ASSERTION FAILED: '%s'\n", \
				__func__,                                   \
				__LINE__,                                   \
				__stringify(__assert_expr__));              \
		BUG();                                                      \
		/* should never reach this */                               \
		for (;;)                                                    \
			;                                                   \
	}                                                                   \
} while (0)

/*! see M_ASSERT */
#define M_ASSERT_FIXME(__assert_expr__) M_ASSERT(__assert_expr__)

#ifdef ENABLE_ASSERT
/*! see M_ASSERT */
#define ASSERT(__assert_expr__) M_ASSERT(__assert_expr__)
#else /*ENABLE_ASSERT */
/*! see M_ASSERT */
#define ASSERT(__assert_expr__) (void)0
#endif /*DISABLE_ASSERT */

/*! convenient wrapper for printk */
#define __printk_kern(__kern, __func, __line, __kern_str, __fmt, ...)	({ \
	printk(__kern "%s@%d: [%d]: " __kern_str ": " __fmt,               \
		__func, __line,                                            \
		current->pid,                                              \
		##__VA_ARGS__);                                            \
})

/*! convenient wrapper for printk_ratelimited */
#define __printk_kern_ratelimited(__kern, __func, __line, __kern_str,      \
				  __fmt, ...) ({                           \
	printk_ratelimited(__kern "%s@%d: [%d]: " __kern_str ": " __fmt,   \
			   __func, __line,                                 \
			   current->pid,                                   \
			   ##__VA_ARGS__);                                 \
})

#if !defined(PRINTK_DEBUG_DEFAULT)
#define PRINTK_DEBUG_DEFAULT            KERN_DEBUG
#endif /* !defined(PRINTK_DEBUG_DEFAULT) */

#define __printk_debug(__func, __line, __fmt, ...)			\
		__printk_kern(PRINTK_DEBUG_DEFAULT, __func, __line,	\
			      "DEBUG", __fmt, ##__VA_ARGS__)
#define __printk_info(__func, __line, __fmt, ...)			\
		__printk_kern(KERN_INFO, __func, __line,		\
			      "INFO", __fmt, ##__VA_ARGS__)
#define __printk_warning(__func, __line, __fmt, ...)			\
		__printk_kern(KERN_WARNING, __func, __line,		\
			      "WARNING", __fmt, ##__VA_ARGS__)
#define __printk_err(__func, __line, __fmt, ...)			\
		__printk_kern(KERN_ERR, __func, __line,			\
			      "ERR", __fmt, ##__VA_ARGS__)
#define printk_debug(fmt, ...)						\
		__printk_debug(__func__, __LINE__, fmt, ##__VA_ARGS__)
#define printk_info(fmt, ...)						\
		__printk_info(__func__, __LINE__, fmt, ##__VA_ARGS__)
#define printk_warning(fmt, ...)					\
		__printk_warning(__func__, __LINE__, fmt, ##__VA_ARGS__)
#define printk_err(fmt, ...)						\
		__printk_err(__func__, __LINE__, fmt, ##__VA_ARGS__)

#define __printk_debug_ratelimited(__func, __line, __fmt, ...)		\
		__printk_kern_ratelimited(PRINTK_DEBUG_DEFAULT,		\
					  __func, __line,		\
					  "DEBUG", __fmt, ##__VA_ARGS__)
#define __printk_info_ratelimited(__func, __line, __fmt, ...)		\
		__printk_kern_ratelimited(KERN_INFO,			\
					  __func, __line,		\
					  "INFO", __fmt, ##__VA_ARGS__)
#define __printk_warning_ratelimited(__func, __line, __fmt, ...)	\
		__printk_kern_ratelimited(KERN_WARNING, __func, __line,	\
					  "WARNING", __fmt, ##__VA_ARGS__)
#define __printk_err_ratelimited(__func, __line, __fmt, ...)		\
		__printk_kern_ratelimited(KERN_ERR, __func, __line,	\
					  "ERR", __fmt, ##__VA_ARGS__)
#define printk_debug_ratelimited(fmt, ...)				\
		__printk_debug_ratelimited(__func__, __LINE__,		\
					   fmt, ##__VA_ARGS__)
#define printk_info_ratelimited(fmt, ...)				\
		__printk_info_ratelimited(__func__, __LINE__,		\
					   fmt, ##__VA_ARGS__)
#define printk_warning_ratelimited(fmt, ...)				\
		__printk_warning_ratelimited(__func__, __LINE__,	\
					     fmt, ##__VA_ARGS__)
#define printk_err_ratelimited(fmt, ...)				\
		__printk_err_ratelimited(__func__, __LINE__,		\
					 fmt, ##__VA_ARGS__)

/*!
 * Converts a virtual address to a page.
 * Works with both vmalloc buffers and "regular" addresses.
 */
static inline struct page *virtual_to_page(void *buf)
{
	struct page *pg;

	if (is_vmalloc_addr(buf))
		pg = vmalloc_to_page(buf);
	else
		pg = virt_to_page(buf);
	M_ASSERT(pg != NULL);
	return pg;
}

#endif /* BITTERN_CACHE_LINUX_H */
