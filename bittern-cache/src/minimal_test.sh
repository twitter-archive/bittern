#!/bin/bash
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
# Always run this minimal test before every checkin.
#
# Requirements:
# * Two block devices which can be wiped out and and used for testing.
#   One block device needs be in the 500mb to 1gb range, the other one
#   needs be in the 10-30gb range.
# * Linux kernel source for building.
# * Minimum Linux version is 3.14.8.
# * Strongly recommended Linux version is 4.0.rc2 or later.
# * When using Linux 4.0.rc2 or later, please configure BRD driver as
#   a loadable module, with at least one device and maximum ramdisk size
#   or at least 2 gigabytes.
#
# To run script, simply type (in this directory):
#       bash ./minimal_test.sh
# You will see a bunch of stuff being written. At end if everything runs
# successfully, you will see this message:
#       That's all folks, the bird says thank you.
#
# The script performs the following tests:
# * do a production build
# * do a debug build
# * create a cache on block device cache type and perform a few basic tests,
#   including a make filesystem, mount, copy data, umount, remount, verify data
#   by building it.
# * create a cache on ram device cache type and perform a few basic tests
#
#
# How to setup:
#
# One block device should be in the size range of 500 Mbytes to 1 Gigabyte.
# This block device is used to place the cache. Setup its path as this
# environment variable:
#	export MINIMAL_TEST_CACHE_DEVICE=/dev/mapper/vg_cache-lvol0
#
# The other block device should be in the size range of about 10-30 Gigabytes.
# This block device is used as the device being cached. Setup its path as this
# environment variable:
#	export MINIMAL_TEST_CACHE_DEVICE=/dev/mapper/vg_cache-lvol0
#
#
if [ "$MINIMAL_TEST_CACHE_DEVICE" == "" ]
then
	MINIMAL_TEST_CACHE_DEVICE=/dev/mapper/vg_cache-lvol0
fi
if [ "$MINIMAL_TEST_CACHED_DEVICE" == "" ]
then
	MINIMAL_TEST_CACHED_DEVICE=/dev/mapper/vg_hdd-lvol0
fi
MEM_CACHE_DEVICE=/dev/ram0
BITTERN_DEVICE=bitcache0

echo $0: MINIMAL_TEST_CACHE_DEVICE = $MINIMAL_TEST_CACHE_DEVICE
echo $0: MINIMAL_TEST_CACHED_DEVICE = $MINIMAL_TEST_CACHED_DEVICE

#
#
#
set -e
#
#
#
if [ ! -d bittern_cache_kmod ]
then
	echo $0: sorry, wrong directory
	exit 1
fi

ins_all_mods() {
	echo $0: insmod\'ing stuff from the locally fresh farm-to-fork built spot
	sudo ../scripts/bc_insmod_devel.sh --insmod-brd \
					   --insmod-brd-size 1048576
}

rm_all_mods() {
	sudo ../scripts/bc_rmmod.sh --rmmod-brd $*
}


echo $0: prep steps
#
# try unload currently loaded caches and modules, if any
#
if [ -b /dev/mapper/$BITTERN_DEVICE ]
then
	echo $0: prep - removing cache
	sudo ../scripts/bc_remove.sh $BITTERN_DEVICE
fi
echo $0: prep - removing modules if loaded
rm_all_mods --ignore-already-removed

#
# make sure that both developer and production versions build and load
#
make distclean
make prodconfig
make
ins_all_mods
rm_all_mods

make distclean
make kdevconfig
make
# keep the developer build for the all the tests
ins_all_mods

#
# block
#
# wipe out the beginning of the cached data drive, so we have a consistent
# zeroed-out starting state
echo $0: wiping out start of cached device
sudo dd if=/dev/zero of=$MINIMAL_TEST_CACHED_DEVICE bs=1024k count=100
echo $0: testing block, pass 1
sudo ../scripts/bc_delete.sh $MINIMAL_TEST_CACHE_DEVICE --force
sudo ../scripts/bc_setup.sh --cache-operation create --cache-name $BITTERN_DEVICE --cache-device $MINIMAL_TEST_CACHE_DEVICE --device $MINIMAL_TEST_CACHED_DEVICE
sudo ../scripts/bc_control.sh --set verify_start $BITTERN_DEVICE
sudo mkfs.ext4 /dev/mapper/$BITTERN_DEVICE
sudo fsck -f /dev/mapper/$BITTERN_DEVICE
# not sure why, but there seems to be a need for a bit of a delay here
sleep 2
#
sudo ../scripts/bc_remove.sh $BITTERN_DEVICE
sudo tools/bc_tool -r -c $MINIMAL_TEST_CACHE_DEVICE -b | tail -20
echo $0: testing block, pass 2
#
sudo ../scripts/bc_setup.sh --cache-operation restore --cache-name $BITTERN_DEVICE --cache-device $MINIMAL_TEST_CACHE_DEVICE --device $MINIMAL_TEST_CACHED_DEVICE
sudo ../scripts/bc_control.sh --set verify_start $BITTERN_DEVICE
#
sudo fsck -f /dev/mapper/$BITTERN_DEVICE
sudo dd if=/dev/zero of=/dev/mapper/$BITTERN_DEVICE bs=4k count=1 oflag=direct
sudo dd if=/dev/zero of=/dev/mapper/$BITTERN_DEVICE bs=4k count=1 oflag=direct
sudo dd if=/dev/zero of=/dev/mapper/$BITTERN_DEVICE seek=1000 bs=1k count=1 oflag=direct
sudo dd if=/dev/zero of=/dev/mapper/$BITTERN_DEVICE seek=1000 bs=1k count=1 oflag=direct
# we repeat this three times to hit a bunch of dirty write hits
sudo mkfs.xfs -f /dev/mapper/$BITTERN_DEVICE
sudo mkfs.xfs -f /dev/mapper/$BITTERN_DEVICE
sudo mkfs.xfs -f /dev/mapper/$BITTERN_DEVICE
# not sure why, but there seems to be a need for a bit of a delay here
sleep 2
echo $0: testing block, pass 3
#
sudo ../scripts/bc_remove.sh $BITTERN_DEVICE
sudo ../scripts/bc_setup.sh --cache-operation restore \
				--cache-name $BITTERN_DEVICE \
				--cache-device $MINIMAL_TEST_CACHE_DEVICE \
				--device $MINIMAL_TEST_CACHED_DEVICE
sudo ../scripts/bc_control.sh --set verify_start $BITTERN_DEVICE
sudo xfs_repair /dev/mapper/$BITTERN_DEVICE
sudo rm -rf /tmp/bittern_minimal_test
sudo mkdir /tmp/bittern_minimal_test
sudo mount /dev/mapper/$BITTERN_DEVICE /tmp/bittern_minimal_test
sudo cp -r ../../ /tmp/bittern_minimal_test/bittern.git/
# note: $USER gets expanded before we sudo
sudo chown -R $USER /tmp/bittern_minimal_test/bittern.git/
sudo umount /dev/mapper/$BITTERN_DEVICE
#
echo $0: testing block, pass 4
#
sudo ../scripts/bc_remove.sh $BITTERN_DEVICE
sudo ../scripts/bc_setup.sh --cache-operation restore \
				--cache-name $BITTERN_DEVICE \
				--cache-device $MINIMAL_TEST_CACHE_DEVICE \
				--device $MINIMAL_TEST_CACHED_DEVICE
sudo ../scripts/bc_control.sh --set verify_start $BITTERN_DEVICE
sudo xfs_repair /dev/mapper/$BITTERN_DEVICE
sudo mount /dev/mapper/$BITTERN_DEVICE /tmp/bittern_minimal_test
pushd /tmp/bittern_minimal_test/bittern.git/bittern-cache/src/
git status
make devconfig
make
popd
sudo umount /dev/mapper/$BITTERN_DEVICE
#

#
sudo ../scripts/bc_remove.sh $BITTERN_DEVICE
rm_all_mods
#
sudo rm -rf /tmp/bittern_minimal_test

linux_ver=$(uname -r | sed -e 's/\..*//')
if [ $linux_ver -lt 4 ]
then
	echo $0: That\'s all folks, the bittern bird is partially happy on linux $linux_ver and says thank you.
	exit 0
fi

#
# mem
#
echo $0: testing mem, pass 1
ins_all_mods
sudo ../scripts/bc_delete.sh $MEM_CACHE_DEVICE --force
sudo ../scripts/bc_setup.sh --cache-operation create --cache-name $BITTERN_DEVICE --cache-device $MEM_CACHE_DEVICE --device $MINIMAL_TEST_CACHED_DEVICE
sudo ../scripts/bc_control.sh --set verify_start $BITTERN_DEVICE
sudo mkfs.ext4 /dev/mapper/$BITTERN_DEVICE
sudo fsck -f /dev/mapper/$BITTERN_DEVICE
# we repeat this three times to hit a bunch of dirty write hits
sudo mkfs.xfs -f /dev/mapper/$BITTERN_DEVICE
sudo mkfs.xfs -f /dev/mapper/$BITTERN_DEVICE
sudo mkfs.xfs -f /dev/mapper/$BITTERN_DEVICE
# not sure why, but there seems to be a need for a bit of a delay here
sleep 2
#
sudo ../scripts/bc_remove.sh $BITTERN_DEVICE
sudo tools/bc_tool -r -c $MEM_CACHE_DEVICE -b | tail -20
echo $0: testing mem, pass 2
#
sudo ../scripts/bc_setup.sh --cache-operation restore --cache-name $BITTERN_DEVICE --cache-device $MEM_CACHE_DEVICE --device $MINIMAL_TEST_CACHED_DEVICE
sudo ../scripts/bc_control.sh --set verify_start $BITTERN_DEVICE
#
sudo xfs_repair /dev/mapper/$BITTERN_DEVICE
sudo dd if=/dev/zero of=/dev/mapper/$BITTERN_DEVICE bs=4k count=1 oflag=direct
sudo dd if=/dev/zero of=/dev/mapper/$BITTERN_DEVICE bs=4k count=1 oflag=direct
sudo dd if=/dev/zero of=/dev/mapper/$BITTERN_DEVICE seek=1000 bs=1k count=1 oflag=direct
sudo dd if=/dev/zero of=/dev/mapper/$BITTERN_DEVICE seek=1000 bs=1k count=1 oflag=direct
# we repeat this three times to hit a bunch of dirty write hits
sudo mkfs.xfs -f /dev/mapper/$BITTERN_DEVICE
sudo mkfs.xfs -f /dev/mapper/$BITTERN_DEVICE
sudo mkfs.xfs -f /dev/mapper/$BITTERN_DEVICE
# not sure why, but there seems to be a need for a bit of a delay here
sleep 2
sudo ../scripts/bc_remove.sh $BITTERN_DEVICE
rm_all_mods
#
#
#
make distclean
#
#
#
echo $0: That\'s all folks, the bittern bird says thank you.
