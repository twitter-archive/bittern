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
SCRIPTS_FILES = bc_insmod.sh \
		bc_insmod_devel.sh \
		bc_rmmod.sh \
		bc_setup.sh \
		bc_remove.sh \
		bc_control.sh \
		bc_delete.sh \
		bc_stats.pl \
		$(NULL)

all:
	@echo nothing to be done here.

install:

	install -d $(DESTDIR)/sbin/bittern_cache/scripts/
	for file in $(SCRIPTS_FILES); do \
		install $$file $(DESTDIR)/sbin/bittern_cache/scripts/$$file; \
	done

clean distclean:
