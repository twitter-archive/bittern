#
# Bittern Cache.
#
# Copyright(c) 2013, 2014, 2015, Twitter, Inc., All rights reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
#
#
# CURRENT = $(shell uname -r)
# PWD = $(shell pwd)
# KDIR = /lib/modules/$(CURRENT)/build
#

# CFLAGS += -Wall -static
CFLAGS += -Wall -O2
CC := gcc

INCLUDE_PATH := ../murmurhash3/
MURMURHASH_SOURCE := ../murmurhash3/murmurhash3.c
DEPS := ../murmurhash3/math128.h \
	../murmurhash3/murmurhash3.h \
	$(MURMURHASH_SOURCES) \
	../bittern_cache_kmod/bittern_cache_pmem_header.h \
	$(NULL)

.PHONY: all
all: bc_tool bc_hash

bc_tool: bc_tool.c $(DEPS)
	$(CC) -o bc_tool $(CFLAGS) \
		-I$(INCLUDE_PATH) \
		bc_tool.c \
		$(MURMURHASH_SOURCE)

bc_hash: bc_hash.c $(DEPS)
	$(CC) -o bc_hash $(CFLAGS) \
		-I$(INCLUDE_PATH) \
		bc_hash.c \
		$(MURMURHASH_SOURCE)

.PHONY: install
install: bc_tool
	install -d $(DESTDIR)/usr/bin/
	install -m 0755 bc_tool $(DESTDIR)/usr/bin/bc_tool
	install -m 0755 bc_hash $(DESTDIR)/usr/bin/bc_hash
	install -d $(DESTDIR)/sbin/bittern_cache
	install -d $(DESTDIR)/sbin/bittern_cache/scripts/
	install -m 0755 bc_tool $(DESTDIR)/sbin/bittern_cache/scripts/

.PHONY: clean distclean
clean distclean:
	rm -f murmurhash3_test bc_tool bc_hash *.o core *.log *.out
