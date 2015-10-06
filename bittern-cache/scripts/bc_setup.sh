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

# FIXME: add check for /sys/fs/bittern/<cache_name> entry

#
# see usage function for usage
#

__dirpath=$(dirname $0)
# search for production location
BCTOOL_EXE="$__dirpath/bc_tool"
if [ ! -x "$BCTOOL_EXE" ]
then
	# search for developer location
        BCTOOL_EXE="$__dirpath/../src/tools/bc_tool"
fi
if [ ! -x "$BCTOOL_EXE" ]
then
        echo $0: ERROR: cannot find bc_tool executable
        exit 1
fi

usage() {
        echo $0: usage: $0 arguments
        echo '          [-i|--ignore-already-setup]'
        echo '          [-o|--cache-operation create|restore] (default is restore if unspecified)'
        echo '          -n|--cache-name name'
        echo '          -c|--cache-device cache_device'
        echo '          -d|--device cached_device'
        echo ''
        echo 'examples:'
        echo "          $0 -o create -n bitcache0 -s 128 -c /dev/adrbd0 -t mem -d /dev/mapper/vg-volume-being-cached"
        echo "          $0 -n bitcache0 -c /dev/adrbd0 -t mem -d /dev/mapper/vg-volume-being-cached"
        echo "          $0 -n bitcache0 -c /dev/mvwamb0 -t block -d /dev/mapper/vg-volume-being-cached"
        echo "          $0 -n bitcache0 -c /dev/pmem_ram0 -t block -d /dev/mapper/vg-volume-being-cached"
}

# arguments processing
IGNORE_ALREADY_SETUP="no"
CACHE_OPERATION="restore"       # restore is default for cache operation
CACHE_NAME=""
CACHE_DEVICE=""
CACHED_DEVICE=""
DISCARD_CACHE_DEVICE="no"

__getopt_options_single_letter="hio:n:c:d:"
__getopt_options_full="help"                                            # -h
__getopt_options_full="$__getopt_options_full,ignore-already-setup"     # -i
__getopt_options_full="$__getopt_options_full,cache-operation:"         # -o
__getopt_options_full="$__getopt_options_full,cache-name:"              # -n
__getopt_options_full="$__getopt_options_full,cache-device:"            # -c
__getopt_options_full="$__getopt_options_full,device:"                  # -d
ARGS=$(getopt -o $__getopt_options_single_letter -l $__getopt_options_full -n "bc_setup.sh" -- "$@");
__status=$?
if [ $__status -ne 0 ]
then
        exit 1
fi
eval set -- "$ARGS";
while true
do
        case "$1" in
        -h|--help)
                shift
		usage
                exit 0
                ;;
        -i|--ignore-already-setup)
                shift
                IGNORE_ALREADY_SETUP="yes"
                ;;
        -o|--cache-operation)
                shift
                CACHE_OPERATION="$1"
                shift
                ;;
        -n|--cache-name)
                shift
                CACHE_NAME="$1"
                shift
                ;;
        -c|--cache-device)
                shift
                CACHE_DEVICE="$1"
                shift
                ;;
        -d|--device)
                shift
                CACHED_DEVICE="$1"
                shift
                ;;
        --)
                if [ $# -ne 1 ]
                then
                        echo "$0: ERROR: unexpected arguments $1"
                        usage
                        exit 10
                fi
                break
                ;;
        esac
done
#
if [ "$CACHE_NAME" = "" ]
then
        echo $0: ERROR: --cache-name parameter is mandatory
        usage
        exit 21
fi
if [ "$CACHE_DEVICE" = "" ]
then
        echo $0: ERROR: --cache-device parameter is mandatory
        usage
        exit 21
fi
if [ "$CACHED_DEVICE" = "" ]
then
        echo $0: ERROR: --device parameter is mandatory
        usage
        exit 21
fi
#
#
#
echo $0: NOTE: cache device name is $CACHE_NAME


# extra variables and arg processing
DEV_MAPPER_NAME="/dev/mapper/$CACHE_NAME"
# find cache type.
case "$CACHE_OPERATION" in
        "create"|"restore")
                ;;
        *)
                echo $0: usage: "supported cache operations are 'create', 'restore'"
                exit 2
                ;;
esac

echo $0: NOTE: cache device is $CACHE_DEVICE

check_privileges() {
        #
        # privileges check
        #
        if [ $(id -u) != 0 ]
        then
                echo $0: you must be root to run this
                exit 15
        fi
}

#
# common to both restore and create
#
do_common_preflight() {
        if [ ! -b $CACHED_DEVICE ]
        then
                echo $0: ERROR: $CACHED_DEVICE is not a block device
                exit 3
        fi
        if [ ! -b $CACHE_DEVICE ]
        then
                echo $0: ERROR: $CACHE_DEVICE is not a block device
                exit 3
        fi
        #
        # check cache existence first -- this will save us from a few headaches
        # (like for instance blkdev dying on us because the device is busy)
        #
        if [ -b "$DEV_MAPPER_NAME" ]
        then
                if [ $IGNORE_ALREADY_SETUP = "yes" ]
                then
                        echo $0: NOTE: cache "$DEV_MAPPER_NAME" already exists
                        exit 0
                fi
                echo $0: ERROR: cache "$DEV_MAPPER_NAME" already exists
                exit 5
        fi
        #
        # determine size of cached device
        #
        CACHED_DEVICE_SECTORS=$(blockdev --getsz $CACHED_DEVICE)
        if [ "$CACHED_DEVICE_SECTORS" = "" ]
        then
                echo $0: ERROR: cannot determine device size for $CACHED_DEVICE
                exit 4
        fi
        CACHED_DEVICE_SECTORS_RD=$(($CACHED_DEVICE_SECTORS - $CACHED_DEVICE_SECTORS % 8))
        if [ $CACHED_DEVICE_SECTORS != $CACHED_DEVICE_SECTORS_RD ]
        then
                echo $0: WARNING: cached device size is not 4K multiple, rounding down size from $CACHED_DEVICE_SECTORS to $CACHED_DEVICE_SECTORS_RD
                CACHED_DEVICE_SECTORS=$CACHED_DEVICE_SECTORS_RD
        fi
        CACHED_DEVICE_MBYTES=$(($CACHED_DEVICE_SECTORS / 2048))
        echo $0: NOTE: cached device $CACHED_DEVICE size is $CACHED_DEVICE_SECTORS sectors '('$CACHED_DEVICE_MBYTES mbytes')'
}

#
# restore preflight
#
do_restore_preflight() {
        echo $0: NOTE: cache device $CACHE_DEVICE size is $CACHE_DEVICE_SECTORS sectors '('$CACHE_DEVICE_MBYTES mbytes')'
}

#
# create preflight
#
do_create_preflight() {
        echo $0: NOTE: cache size is $CACHE_DEVICE_MBYTES mbytes
        #
        # trim cache device
        #
        echo $0: NOTE: discarding cache device $CACHE_DEVICE
        __blkdiscard_output=$(blkdiscard -v $CACHE_DEVICE 2>&1)
        __status=$?
        if [ $__status != 0 ]
        then
                echo $0: WARNING: cache discard failed: $__blkdiscard_output
        else
                echo $0: NOTE: cache discard succeeded: $__blkdiscard_output
        fi
}

check_cache_operation() {
        $BCTOOL_EXE --silent --read --cache-device $CACHE_DEVICE
        __bctool_status=$?
        if [ $__bctool_status -eq 0 ]
        then
                __cache_exists="yes"
                if [ $CACHE_OPERATION = "create" ]
                then
                        echo $0: ERROR: $CACHE_OPERATION cache operation cannot be done, a cache already exists in $CACHE_DEVICE
                        exit 11
                fi
        else
                __cache_exists="no"
                if [ $CACHE_OPERATION = "restore" ]
                then
                        echo $0: ERROR: $CACHE_OPERATION cache operation cannot be done, there is no existing cache in $CACHE_DEVICE
                        exit 11
                fi
        fi
}

#
# do_operation does the real work
#
do_operation() {
        #
        # all pre-flight checks ok, now create/restore the cache
        #
        __dmsetup_input="0 $CACHED_DEVICE_SECTORS bittern_cache $CACHE_OPERATION $CACHED_DEVICE $CACHE_DEVICE"
        echo $0: NOTE: /sbin/dmsetup create $CACHE_NAME --table "$__dmsetup_input"
        /sbin/dmsetup create $CACHE_NAME --table "$__dmsetup_input"
        __status=$?
        if [ $__status != 0 ]
        then
                echo $0: ERROR: /sbin/dmsetup create $CACHE_NAME failed with status $__status
                exit 6
        fi
        echo $0: /sbin/dmsetup create $CACHE_NAME succeeded
        if [ ! -b "$DEV_MAPPER_NAME" ]
        then
                echo $0: ERROR: /sbin/dmsetup create $CACHE_NAME succeeded but $DEV_MAPPER_NAME does not exist, or is not a block device
                exit 7
        fi
        #
        # cache create/restore ok, verify results
        #
	CACHE_BLOCK_DEV=$(/sbin/dmsetup info --noheadings -c -o major,minor $CACHE_NAME)
	__sysfs_pmem_api="/sys/fs/bittern/$CACHE_BLOCK_DEV/pmem_api"
        if [ ! -r "$__sysfs_pmem_api" ]
        then
                echo $0: ERROR: /sbin/dmsetup create $CACHE_NAME succeeded sysfs entry $__sysfs_pmem_api does not exist, or is not readable
                exit 7
        fi
	__actual_cache_size=$(cat $__sysfs_pmem_api  | sed -e 's/^.*bdev_actual_size_mbytes=//' -e 's/ .*$//')
	echo $0: NOTE: allocated cache size is $__actual_cache_size mbytes
	__cache_size=$(cat $__sysfs_pmem_api  | sed -e 's/^.*bdev_size_mbytes=//' -e 's/ .*$//')
	echo $0: NOTE: used cache size is $__cache_size mbytes
}

#
# main
#
main() {
        check_privileges
        check_cache_operation
        if [ $CACHE_OPERATION = "create" ]
        then
                do_common_preflight
                do_create_preflight
                do_operation
        else
                do_common_preflight
                do_restore_preflight
                do_operation
        fi
}

#
# execute main
#
main
exit 0
