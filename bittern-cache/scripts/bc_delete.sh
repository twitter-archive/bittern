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

# set -xe

#
# usage: bc_delete.sh memory_type
#

__dirpath=$(dirname $0)
# search for production location
__bctool="$__dirpath/bc_tool"
if [ ! -x "$__bctool" ]
then
	# search for developer location
        __bctool="$__dirpath/../src/tools/bc_tool"
fi
if [ ! -x "$__bctool" ]
then
        echo $0: ERROR: cannot find bc_tool executable
        exit 1
fi

# arguments processing
CACHE_DEVICE=""
FORCE_DELETE="no"
ARGS=$(getopt -o "hf" -l "help,force-delete" -n "bc_delete.sh" -- "$@");
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
                echo bc_delete.sh: usage: bc_delete.sh '[--force-delete]' cache_device
                exit 0
                ;;
        -f|--force-delete)
                shift
                FORCE_DELETE="yes"
                ;;
        --)
                shift
                CACHE_DEVICE="$1"
                shift
                break
                ;;
        esac
done
if [ "$CACHE_DEVICE" = "" ]
then
        echo $0: ERROR: cache device needs to be specified
        exit 21
fi

# extra variables and arg processing
echo $0: NOTE: cache device $CACHE_DEVICE

# privileges check
if [ $(id -u) != 0 ]
then
        echo $0: you must be root to run this
        exit 15
fi

# main
main() {
        __data_loss="no"
        if [ ! -b $CACHE_DEVICE ]
        then
                echo $0: ERROR: $CACHE_DEVICE is not a block device
                exit 3
        fi
        #
        # see if cache exists
        #
        $__bctool --silent --read --cache-device $CACHE_DEVICE
        __bctool_status=$?
        if [ $__bctool_status -eq 0 ]
        then
                echo $0: NOTE: found existing cache in $CACHE_DEVICE
                #
                # check for dirty blocks
                #
                __dirty_blocks=$($__bctool --read --check-data-blocks --cache-device $CACHE_DEVICE | grep valid_dirty= | sed -e 's/^.*valid_dirty=//' -e 's/,.*$//')
                if [ $__dirty_blocks -ne 0 ]
                then
                        __data_loss="yes"
                        echo $0: WARNING: there are $__dirty_blocks dirty blocks in cache
                        echo $0: WARNING: deleting the cache will result in data loss
                        if [ "$FORCE_DELETE" = "yes" ]
                        then
                                echo $0: NOTE: '--force-delete' has been specified, deleting anyway
                        else
                                echo $0: NOTE: to delete a cache with valid dirty blocks, specify '--force-delete' option
                                exit 10
                        fi
                else
                        echo $0: NOTE: cache is clean, deletion is safe
                fi
        else
                echo $0: WARNING: there is either no existing cache in $CACHE_DEVICE, or cache is corrupt
                if [ "$FORCE_DELETE" = "yes" ]
                then
                        echo $0: WARNING: '--force-delete' has been specified, deleting anyway
                else
                        echo $0: NOTE: specify '--force-delete' option if you want to delete it anyway '(this will wipe out data from the partition)'
                        exit 10
                fi
        fi
        #
        #
        #
        __seconds=10
        while [ $__seconds -ne 0 ]
        do
                if [ $__seconds -eq 1 ]
                then
                        __seconds_str="second"
                else
                        __seconds_str="seconds"
                fi
                if [ "$__data_loss" = "yes" ]
                then
                        echo $0: WARNING: deleting DIRTY cache in $__seconds $__seconds_str '(DATA LOSS IS GUARANTEED)', hit INTR to quit
                else
                        echo $0: NOTE: deleting cache in $__seconds $__seconds_str, hit INTR to quit
                fi
                sleep 1
                __seconds=$(($__seconds - 1))
        done
        #
        # zero out the first 2 megabytes, thus wiping out the superblocks.
        # this takes care of both layout types, interleaved and sequential.
        #
        dd if=/dev/zero oflag=direct of=$CACHE_DEVICE bs=256k count=8 > /dev/null 2>&1
        __status=$?
        if [ $__status -eq 0 ]
        then
                echo $0: NOTE: deleted cache
        else
                echo $0: ERROR: deletion of cache failed
                exit 7
        fi
        #
        # issue blkdiscard
        # note we always need to explicitly wipe out the superblocks,
        # for two reasons:
        # 1) some devices don't support DISCARD -- although you really ought to
        #    upgrade your device in such case
        # 2) DISCARD is advisory in nature: NAND flash devices's only
        #    requirement is to mark the blocks as available for reallocation,
        #    but they are not necessarily erased. the latter is
        #    implementation-dependent.
        #
        __blkdiscard_output=$(blkdiscard -v $CACHE_DEVICE 2>&1)
        __status=$?
        if [ $__status != 0 ]
        then
                echo $0: WARNING: cache discard failed: $__blkdiscard_output
        else
                echo $0: NOTE: cache discard succeeded: $__blkdiscard_output
        fi
        echo $0: NOTE: cache deletion succeeded
}

#
# execute main
#
main

exit 0
