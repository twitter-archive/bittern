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

all:
	for dir in docs etc scripts src; do \
		$(MAKE) -C $$dir; \
	done

install:
	for dir in etc scripts src; do \
		$(MAKE) -C $$dir install DESTDIR=$(DESTDIR);\
	done
