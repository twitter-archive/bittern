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

# usage: bc_remove.sh [--ignore-already-removed] cache_name

# arguments processing
IGNORE_ALREADY_REMOVED="no"
CACHE_NAME=""
DEV_MAPPER_NAME=""
ARGS=$(getopt -o "hi" -l "help,ignore-already-removed" -n "bc_remove.sh" -- "$@");
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
                echo $0: usage: bc_remove.sh '[-i|--ignore-already-removed]' cache_name
                exit 0
                ;;
        -i|--ignore-already-removed)
                shift
                IGNORE_ALREADY_REMOVED="yes"
                ;;
        --)
                # after this shift we'll have all the arguments (should be one)
                shift
                case $# in
                1)
                        CACHE_NAME="$1"
                        DEV_MAPPER_NAME="/dev/mapper/$CACHE_NAME"
                        shift
                        ;;
                *)
                        echo $0: usage: bc_remove.sh '[-i|--ignore-already-removed]' cache_name
                        exit 10
                        ;;
                esac
                break
                ;;
        esac
done
if [ "$CACHE_NAME" = "" ]
then
        echo $0: ERROR: internal error, no cache name after argument parsing
        exit 21
fi

# privileges check
if [ $(id -u) != 0 ]
then
        echo $0: you must be root to run this
        exit 15
fi

# main
main() {
        if [ ! -b "$DEV_MAPPER_NAME" ]
        then
                if [ $IGNORE_ALREADY_REMOVED = "yes" ]
                then
                        echo $0: NOTE: cache "$DEV_MAPPER_NAME" does not exist, or is not a block device
                        exit 0
                fi
                echo $0: ERROR: cache "$DEV_MAPPER_NAME" does not exist, or is not a block device
                exit 2
        fi
        echo $0: removing cache "$DEV_MAPPER_NAME"
        /sbin/dmsetup remove "$CACHE_NAME"
        __status=$?
        if [ $__status != 0 ]
        then
                echo $0: ERROR: /sbin/dmsetup remove "$DEV_MAPPER_NAME" failed with status $__status
                exit 3
        fi
        if [ -b "$DEV_MAPPER_NAME" ]
        then
                echo $0: ERROR: /sbin/dmsetup remove succeded, but "$DEV_MAPPER_NAME" still exists
                exit 4
        fi
}


# execute main
main
exit 0
