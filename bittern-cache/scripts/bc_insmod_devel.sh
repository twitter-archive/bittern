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

# we don't really care how many devices we have given memory is lazily allocated
INSMOD_BRD_DEVICES=8

# arguments processing
IGNORE_ALREADY_LOADED="no"
INSMOD_BRD="no"
INSMOD_BRD_SIZE=0
# default provider
GETOPT_O="hibs:"
GETOPT_L="help,ignore-already-loaded,insmod-brd,insmod-brd-size:"
ARGS=$(getopt -o $GETOPT_O -l $GETOPT_L -n "bc_insmod.sh" -- "$@");
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
                echo 'bc_insmod.h: usage: bc_insmod.sh'
		echo '                        [-i|--ignore-already-loaded]'
		echo '                        [-b|--insmod-brd]'
		echo '                        [-s|--insmod-brd-size brd_size]'
                exit 0
                ;;
        -i|--ignore-already-loaded)
                shift
                IGNORE_ALREADY_LOADED="yes"
                ;;
	-b|--insmod-brd)
                shift
                INSMOD_BRD="yes"
                ;;
	-s|--insmod-brd-size)
                shift
                INSMOD_BRD_SIZE="$1"
		shift
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
        __module_path=$2
        if [ ! -f "$__module_path" ]
        then
                echo $0: ERROR: cannot find kernel module at "$__module_path"
                exit 3
        fi
        # space at the end is intentional
        MODULE_LOADED=$(/sbin/lsmod | /bin/grep "^$__module_name ")
        if [ ! -z "$MODULE_LOADED" ]
        then
                if [ $IGNORE_ALREADY_LOADED = "yes" ]
                then
                        echo $0: NOTE: $__module_name kernel module is already loaded
                else
                        echo $0: ERROR: $__module_name kernel module is already loaded
                        exit 1
                fi
        else
                /sbin/insmod "$__module_path"
                __status=$?
                if [ $__status != 0 ]
                then
                        echo $0: ERROR: /sbin/insmod "$__module_path" failed with status $__status
                        exit 4
                fi
                # space at the end is intentional
                MODULE_LOADED=$(/sbin/lsmod | /bin/grep "^$__module_name ")
                if [ -z "$MODULE_LOADED" ]
                then
                        echo $0: ERROR: /sbin/insmod "$__module_path" succeeded but the module is not loaded
                        exit 5
                fi
                echo $0: /sbin/insmod "$__module_path" succeeded
	fi
}

modprobe_brd() {
	local __mod_params="rd_nr=$INSMOD_BRD_DEVICES"
	local __max_minor=$(($INSMOD_BRD_DEVICES - 1))

        # space at the end is intentional
        local __module_loaded=$(/sbin/lsmod | /bin/grep "^brd ")
        if [ ! -z "$__module_loaded" ]
        then
                if [ $IGNORE_ALREADY_LOADED = "yes" ]
                then
                        echo $0: NOTE: "brd" kernel module is already loaded
                else
                        echo $0: ERROR: "brd" kernel module is already loaded
                        exit 1
                fi
	else

		if [ $INSMOD_BRD_SIZE -ne 0 ]
		then
			__mod_params="$__mod_params rd_size=$INSMOD_BRD_SIZE"
		fi

		/sbin/modprobe "brd" $__mod_params
		__status=$?
		if [ $__status != 0 ]
		then
			echo $0: ERROR: /sbin/modprobe "brd" failed with status $__status
			exit 4
		else
			echo $0: /sbin/modprobe "brd" $__mod_params succeeded
		fi

		for __minor in $(seq 0 $__max_minor)
		do
			while [ ! -b /dev/ram$__minor ]
			do
				echo $0: waiting for /dev/ram$__minor
				usleep 100000
			done
		done

		echo $0: "brd" device is ready
        fi
}

# main
main() {
	local __path="$(dirname $0)/../src/bittern_cache_kmod/bittern_cache.ko"

        load_module "bittern_cache" $__path

	if [ "$INSMOD_BRD" == "yes" ]
	then
		modprobe_brd "brd"
	fi
}


# execute main
main
exit 0
