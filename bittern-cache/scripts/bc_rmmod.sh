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

# arguments processing
IGNORE_ALREADY_REMOVED="no"
RMMOD_BRD="no"
GETOPT_O="hib"
GETOPT_L="help,ignore-already-removed,rmmod-brd"
ARGS=$(getopt -o $GETOPT_O -l $GETOPT_L -n "bc_rmmod.sh" -- "$@");
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
		# do not document --rmmod-brd option as it is for development
                echo 'bc_rmmod.h: usage: bc_rmmod.sh '
		echo '                       [-i|--ignore-already-removed]'
                exit 0
                ;;
        -i|--ignore-already-removed)
                shift
                IGNORE_ALREADY_REMOVED="yes"
                ;;
	-b|--rmmod-brd)
                shift
                RMMOD_BRD="yes"
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

# remove_module function
remove_module() {
        __module_name=$1
        # space at the end of grep is intentional
        MODULE_LOADED=$(/sbin/lsmod | /bin/grep "^$__module_name ")
        if [ -z "$MODULE_LOADED" ]
        then
                if [ $IGNORE_ALREADY_REMOVED = "yes" ]
                then
                        echo $0: NOTE: $__module_name kernel module is not loaded
                else
                        echo $0: ERROR: $__module_name kernel module is not loaded
                        exit 1
                fi
        else
                echo $0: removing kernel module $__module_name
                /sbin/rmmod $__module_name
                __status=$?
                if [ $__status != 0 ]
                then
                        echo $0: ERROR: /sbin/rmmod $__module_name kernel module failed with status $__status
                        exit 2
                fi
                # space at the end of grep is intentional
                MODULE_LOADED=$(/sbin/lsmod | /bin/grep "^$__module_name ")
                if [ "$MODULE_LOADED" != "" ]
                then
                        echo $0: ERROR: /sbin/rmmod $__module_name kernel module succeded, but module is still loaded
                        exit 3
                fi
        fi
}

# main
main() {
        remove_module "bittern_cache"

	if [ "$RMMOD_BRD" == "yes" ]
	then
		remove_module "brd"
	fi
}


# execute main
main
exit 0
