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

# set -xe

# usage: bc_insmod.sh [-i|--ignore-already-loaded]

# arguments processing
IGNORE_ALREADY_LOADED="no"
# default provider
ARGS_O="hi"
ARGS_L="help,ignore-already-loaded"
ARGS=$(getopt -o $ARGS_O -l $ARGS_L -n "bc_insmod.sh" -- "$@");
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
                echo bc_insmod.h: usage: bc_insmod.sh '[-i|--ignore-already-loaded]'
                exit 0
                ;;
        -i|--ignore-already-loaded)
                shift
                IGNORE_ALREADY_LOADED="yes"
                ;;
        --)
                shift
                break
                ;;
        esac
done

# privileges check
if [ $(id -u) != 0 ]
then
        echo $0: you must be root to run this
        exit 15
fi

# load_module function
load_module() {
	__module_name=$1
	/sbin/modprobe "$__module_name"
	__status=$?
	if [ $__status != 0 ]
	then
		echo $0: ERROR: modprobe "$__module_name" failed with \
		    status $__status
		exit 4
	fi
	MODULE_LOADED=$(/sbin/lsmod | /bin/grep -w "^$__module_name")
	if [ -z "$MODULE_LOADED" ]
	then
		echo $0: ERROR: modprobe "$__module_name" succeeded but \
		    the module is not loaded
		exit 5
	fi
	echo $0: modprobe "$__module_name" succeeded
}

# main
main() {
        load_module "bittern_cache"
}


# execute main
main
exit 0
