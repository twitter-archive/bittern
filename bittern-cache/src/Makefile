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
	$(MAKE) all -C murmurhash3
	$(MAKE) all -C bittern_cache_kmod
	$(MAKE) all -C tools

install:
	$(MAKE) install -C bittern_cache_kmod
	$(MAKE) install -C tools

clean:
	$(MAKE) clean -C murmurhash3
	$(MAKE) clean -C bittern_cache_kmod
	$(MAKE) clean -C tools

distclean: clean
	rm -f bittern_cache_kmod/bittern_cache_config.h

devconfig:
	./mk.configure --developer

kdevconfig:
	./mk.configure --developer --extra-kern-info

prodconfig:
	./mk.configure --production

kprodconfig:
	./mk.configure --production --extra-kern-info
