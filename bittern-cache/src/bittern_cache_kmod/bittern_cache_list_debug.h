/*
 * This code has been copied from list.h and modified to have a debug
 * version of list_del(), list_add(), list_add_tail() and list_del_init()
 * which also print the function name and line number of the offending code
 * if a list corruption occurs.
 *
 * The original header file has neither a license header nor author attribution.
 */
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

#ifndef BITTERN_CACHE_LIST_DEBUG_H
#define BITTERN_CACHE_LIST_DEBUG_H

extern void __list_add_debug__(struct list_head *new, struct list_head *prev,
			       struct list_head *next, const char *func,
			       int line);
extern void __list_del_entry_debug__(struct list_head *entry, const char *func,
				     int line);

/**
 * list_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: list_empty on entry does not return true after this, the entry is
 * in an undefined state.
 */
#define list_del(__entry) ({						\
	struct list_head *_entry = (__entry);				\
	__list_del_entry_debug__(_entry, __func__, __LINE__);		\
	_entry->next = LIST_POISON1;					\
	_entry->prev = LIST_POISON2;					\
})

#define list_add(__new, __head) ({					   \
	struct list_head *_new = (__new);				   \
	struct list_head *_head = (__head);				   \
	__list_add_debug__(_new, _head, _head->next, __func__, __LINE__);  \
})

/**
 * list_add_tail - add a new entry
 * @new: new entry to be added
 * @head: list head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
#define list_add_tail(__new, __head) ({					  \
	struct list_head *_new = (__new);				  \
	struct list_head *_head = (__head);				  \
	__list_add_debug__(_new, _head->prev, _head, __func__, __LINE__); \
})

/**
 * list_del_init - deletes entry from list and reinitialize it.
 * @entry: the element to delete from the list.
 */
#define list_del_init(__entry)  ({					\
	struct list_head *_entry = (__entry);				\
	__list_del_entry_debug__(_entry, __func__, __LINE__);		\
	INIT_LIST_HEAD(_entry);						\
})

#endif /* BITTERN_CACHE_LIST_DEBUG_H */
