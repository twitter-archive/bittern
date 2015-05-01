/*
 * Copyright 2006, Red Hat, Inc., Dave Jones
 * Released under the General Public License (GPL).
 *
 * This file contains the linked list implementations for
 * DEBUG_LIST.
 */
/*
 * This code has been copied from list.c and modified to have a debug
 * version of list_del(), list_add(), list_add_tail() and list_del_init()
 * which also print the function name and line number of the offending code
 * if a list corruption occurs.
 *
 * The copyright notice and license header that follows is for the modified
 * portions of the code.
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

/*! \file */

#include "bittern_cache.h"

/*
 * list_add():
 * __list_add_debug__(_new, _head, _head->next, __FILE__, __LINE__, __func__);
 * list_add_tail()
 * __list_add_debug__(_new, _head->prev, _head, __FILE__, __LINE__, __func__);
*/
void __list_add_debug__(struct list_head *new, struct list_head *prev,
			struct list_head *next, const char *func, int line)
{
	int error = 0;

	if (next->prev != prev) {
		__printk_err(func, line,
			     "list_add corruption. new=%p, next->prev should be prev (%p), but was %p. (next=%p).\n",
			     new, prev, next->prev, next);
		error++;
	}
	if (prev->next != next) {
		__printk_err(func, line,
			     "list_add corruption. new=%p, prev->next should be next (%p), but was %p. (prev=%p).\n",
			     new, next, prev->next, prev);
		error++;
	}
	if (new == prev || new == next) {
		__printk_err(func, line,
			     "list_add double add: new=%p, prev=%p, next=%p.\n",
			     new, prev, next);
		error++;
	}
	M_ASSERT(error == 0);
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

void __list_del_entry_debug__(struct list_head *entry, const char *func,
			      int line)
{
	struct list_head *prev, *next;
	int error = 0;

	prev = entry->prev;
	next = entry->next;

	if (next == LIST_POISON1) {
		__printk_err(func, line,
			     "list_del corruption, %p->next is LIST_POISON1 (%p)\n",
			     entry, LIST_POISON1);
		error++;
	}
	if (prev == LIST_POISON2) {
		__printk_err(func, line,
			     "list_del corruption, %p->prev is LIST_POISON2 (%p)\n",
			     entry, LIST_POISON2);
		error++;
	}
	if (prev->next != entry) {
		__printk_err(func, line,
			     "list_del corruption. prev->next should be %p, but was %p\n",
			     entry, prev->next);
		error++;
	}
	if (next->prev != entry) {
		__printk_err(func, line,
			     "list_del corruption. next->prev should be %p, but was %p\n",
			     entry, next->prev);
		error++;
	}
	M_ASSERT(error == 0);
	__list_del(prev, next);
}
