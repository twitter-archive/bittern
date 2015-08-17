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
COMMIT_REV ?= $(shell git describe  --always --abbrev=12)
KERNEL_SOURCE_VERSION ?= $(shell uname -r)
KERNEL_TREE ?= /lib/modules/$(KERNEL_SOURCE_VERSION)/build

EXTRA_CFLAGS += -I$(KERNEL_TREE)/drivers/md -I./ -DCOMMIT_REV="\"$(COMMIT_REV)\""
EXTRA_CFLAGS += -I$(KERNEL_TREE)/include/ \
		-I$(KERNEL_TREE)/include/linux \
		-I$(src)/../murmurhash3 \
		$(NULL)
#
# Turn on this flag when you become worried about stack usage
#
#EXTRA_CFLAGS += -Wframe-larger-than=100

LINUX_VERSION_CODE := $(shell grep LINUX_VERSION_CODE $(KERNEL_TREE)/include/generated/uapi/linux/version.h | awk '{ print $$3 }')
# 262144 = KERNEL_VERSION(4, 0, 0)
LINUX_CONTAINS_DAX := $(shell test $(LINUX_VERSION_CODE) -ge 262144 && echo true)

M_SOURCE_FILES = bittern_cache_module.c \
			bittern_cache_module_ctr.c \
			bittern_cache_module_dtr.c \
			bittern_cache_main.c \
			bittern_cache_main_subr.c \
			bittern_cache_cached_devio.c \
			bittern_cache_sm_read.c \
			bittern_cache_sm_write.c \
			bittern_cache_sm_pwrite.c \
			bittern_cache_sm_writeback.c \
			bittern_cache_sm_invalidate.c \
			bittern_cache_getput.c \
			bittern_cache_bgwriter_policy.c \
			bittern_cache_bgwriter_kt.c \
			bittern_cache_invalidator_kt.c \
			bittern_cache_pmem_api.c \
			bittern_cache_pmem_api_block.c \
			bittern_cache_verifier_kt.c \
			bittern_cache_sequential.c \
			bittern_cache_redblack.c \
			bittern_cache_subr.c \
			bittern_cache_debug.c \
			bittern_cache_list_debug.c \
			memcpy_nt.c \
			$(shell echo *.h) \
			mk_git_info.sh \
			ck_git_info.sh \
			Makefile \
			$(NULL)

obj-m += bittern_cache.o
bittern_cache-objs := bittern_cache_module.o \
			bittern_cache_module_ctr.o \
			bittern_cache_module_dtr.o \
			bittern_cache_main.o \
			bittern_cache_main_subr.o \
			bittern_cache_cached_devio.o \
			bittern_cache_sm_read.o \
			bittern_cache_sm_write.o \
			bittern_cache_sm_pwrite.o \
			bittern_cache_sm_writeback.o \
			bittern_cache_sm_invalidate.o \
			bittern_cache_getput.o \
			bittern_cache_bgwriter_policy.o \
			bittern_cache_bgwriter_kt.o \
			bittern_cache_invalidator_kt.o \
			bittern_cache_pmem_api.o \
			bittern_cache_pmem_api_block.o \
			bittern_cache_sequential.o \
			bittern_cache_redblack.o \
			bittern_cache_verifier_kt.o \
			bittern_cache_subr.o \
			bittern_cache_debug.o \
			bittern_cache_list_debug.o \
			bittern_cache_git_info.o \
			memcpy_nt.o \
			../murmurhash3/murmurhash3.o \
			$(NULL)

ifeq ($(LINUX_CONTAINS_DAX),true)
M_SOURCE_FILES += bittern_cache_pmem_api_mem.c
bittern_cache-objs += bittern_cache_pmem_api_mem.o
endif

.PHONY: all
all: modules

.PHONY: modules
modules: bittern_cache_git_info.c $(RHEL5_SETUP)
	$(MAKE) -C $(KERNEL_TREE) M=$(CURDIR) modules V=0

doxygen.docs: doxygen.conf \
		doxygenlayout.xml \
		$(M_SOURCE_FILES) \
		$(shell echo docs/*.md) \
		$(NULL)
	rm -rf doxygen.d doxygen.docs
	doxygen doxygen.conf
	mv doxygen.d doxygen.docs

.FORCE:
bittern_cache_git_info.c: .FORCE
	@echo '  CK [M]  ck_git_info'
	@./ck_git_info.sh $(M_SOURCE_FILES)

.PHONY: modules_install
modules_install: modules
	install -m 0755 -d $(DESTDIR)/lib/modules/$(KERNEL_SOURCE_VERSION)/extra/bittern_cache/
	install -m 0755 bittern_cache.ko $(DESTDIR)/lib/modules/$(KERNEL_SOURCE_VERSION)/extra/bittern_cache/

.PHONY: install
install: modules_install

.PHONY: clean
clean:
	$(MAKE) -C $(KERNEL_TREE) M=$(CURDIR) clean
	rm -f bittern_cache_git_info.c .bittern_cache_git_info.current.c .bittern_cache_git_info.last.c
	rm -rf doxygen.d doxygen.docs

.PHONY: distclean
distclean: clean
	rm -f bittern_cache_config.h
