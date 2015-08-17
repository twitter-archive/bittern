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

# doxygen "doxy_sysfs.md" explains how to parse /sus/fs/ entries

# FIXME: add check for /sys/fs/bittern/<cache_name> entry

#
# FIXME: this code is very messy and very soon it'll get too complex
# need to write a common perl (or python) library and convert all scripts
# to use it, starting with this one
#

# set -xe

# usage: bc_control.sh cache_name [command]

do_help() {
	cat << EOF | less
Options Help:
-------------

General Usage:
	bc_control.sh -h|--help
	bc_control.sh -l|--list
	bc_control.sh -g|--get cache_name
	bc_control.sh -s|--set option [-v|--value value] cache_name

$0 --help:
	Display this help message.

$0 --list:
	Lists all currently loaded cache. The external name as well as the
	internal name is displayed.
	The external name is the name of the cache and it is the name by
	which it is exposed in /dev/mapper.
	The internal name is the major:minor used for this cache (note:
	it changes each time a cache is loaded).

	Example:

	fcattaneo@centos66dev.vmware.local # ../../scripts/bc_control.sh  --list
	cache_name  internal_name  dev_mapper_path	sysfs_path
	bitcache0   253:6	  /dev/mapper/bitcache0  /sys/fs/bittern/253:6/


$0 --get cache_name:
	Display main summary information and configuration parameters for
	cache_name.

$0 --set option [--value value] cache_name:
	Perform control or configuration command "option" (with optional
	parameter value value) on cache_name.

Set Option Parameters:
----------------------

Important NOTE: these are maintenance and configuratin operations which can be
very disruptive. While these operations are perfectly safe from data integrity
point of view, they can behave in a counterintuitive manner and/or negatively
impact performance.

$0: --set bgwriter_conf_flush_on_exit --value [0 or 1]
	Tells bgwriter whether it should flush dirty buffers when the cache
	is unloaded.
	If value is set to 0, dirty buffers are not flushed on unload.
	If value is set to 1, dirty buffers are flushed on unload.

$0: --set bgwriter_conf_max_queue_depth_pct --value [20 .. 90] (default 50)
	Sets maximum background writer queue depth. This is expressed as a
	percentage of the maximum requests which can be inflight at any given
	time thru the Bittern state machine (max_pending_requests).
	For the sake of example, say max_pending_requests is 400.
	Setting this value at 50% will tell bgwriter to never queue more than
	200 simultaneous writeback requests.
	The higher this percentage, the higher the rate at which dirty blocks
	are written back by the bgwriter. This lowers the odds of having a cache
	fill request stall due to lack of free cache blocks, but it can
	drastically impact cache performance.

$0: --set bgwriter_conf_cluster_size --value [1 .. 32] (default 1)
	Set cluster size for background write clustering, that is, the number
	of blocks which the bgwriter will try to write out in a sequential
	batch.
	A higher value will decrease the overall seek penalty for writebacks,
	while at the possible expense of the cache block replacement.

$0: --set bgwriter_conf_policy --value [classic|aggressive] (default classic)
	Set bgwriter policy used to determine queue depth and other writeback
	parameters.

$0: --set read_bypass_enabled --value [0,1] (default 0 : enabled)
$0: --set write_bypass_enabled --value [0,1] (default 0 : enabled)
	To enable sequential read_bypass or write_bypass, set value to 1.
	To disable sequential read_bypass or write_bypass, set value to 0.
	Each IO stream is tracked by PID and sector offset, and the
	number of consecutive sectors accessed. When the number of consecutive
	sectors excheeds the valur of the tunable threshold, the IO
	stream is thus considered "sequential", and all the sectors from now
	on will bypass the read cache.
	Please refer to the Doxygen documentation in the Performance Tuning
	section for further information.

$0: --set read_bypass_threshold --value [64 .. 65535 ] (default 128)
$0: --set write_bypass_threshold --value [64 .. 65535 ] (default 128)
	Sector count threshold used to determine if a tracked IO stream
	is sequential.

$0: --set read_bypass_timeout --value [100 .. 180000 ] (default 5000)
$0: --set write_bypass_timeout --value [100 .. 180000 ] (default 5000)
	How long before a tracked sequential stream is considered idle,
	and therefore is no longer tracked.

$0: --set enable-extra-checksum-check
$0: --set disable-extra-checksum-check
	Enable/disable extra cache block checksum checking. Each extra checksum
	operation costs about 2 microseconds on a XEON server. This cost is
	irrelevant for NAND-Flash caches, but imposes a 200% overhead on read
	hits when using NVDIMM caches.
	Just don't use this command unless you know what you are doing.

$0: --set max_pending_requests --value [50 .. 500] (default 400)
	Sets maximum pending requests, that is the maxmimum number of requests
	which can be inflight at any given time thru the Bittern state machine.
	Lowering the maximum number of pending requests will decrease latency
	at the expense of throughput.
	Raising the maximum number of pending requests will increase throughput
	at the expense of latency.
	The ideal value for max_pending requets is of course 42 and is
	unattainable.

$0: --set enable_req_fua (default)
$0: --set disable_req_fua
	Enable or disable issuing of REQ_FUA on all write requests to the cached
	device. Correct cache operation requires that the writeback cache on the
	cached device hardware to be disabled at all times.
	Disabling REQ_FUA can only be done if the cached device has the
	writeback cache turned off or if it guarantees power failure safety.
	Please note that Bittern has no way to know whether this is the case. As
	such the responsability for setting this option correctly is left to the
	user.
	Do not change the default if you are in doubt at what should be done.

$0 --set flush
	Set cache operating mode to writethrough and wait until all dirty blocks
	have been flushed out. The original cache mode is then restored.
	This command can be terminated at any time. However, there is
	no guarantee that dirty blocks will complete flushing in such case.

	This command does not behave intuitively at all: because there is no
	true concept of cache flushing, this command manipulates the cache mode
	to achieve the desired result. As such, it does not behave as a write
	barrier. Command starvation is possible if new writes are issued while
	this command is executed (this command simply waits for the dirty count
	to drop to zero before terminating).

	As such this command is really only truly useful when the cache is idle
	and we want to make sure all dirty blocks make it to the cached device.

$0 --set invalidate
	Invalidate all clean blocks.

$0: --set verify
	Starts a full verify cycle. The contents of all clean blocks are
	compared against the content of the cached device. Verification failure
	indicates a bug in the cache.
	Early termination of this command will simply stop the verification
	cycle.

$0: --set verify_start
	Enable continuous clean block verification.

$0: --set verify_stop
	Disable continuous clean block verification.

$0 --set writeback
	Set cache in writeback mode.

$0 --set writethrough
	Set cache in writethrough mode.

$0 --set trace --value tracemask
	Set tracing options. For debugging purposes.

$0 --set dump_devio_pending --value dump_offset
$0 --set dump_blocks_clean --value dump_offset
$0 --set dump_blocks_dirty --value dump_offset
$0 --set dump_blocks_busy --value dump_offset
$0 --set dump_blocks_pending --value dump_offset
$0 --set dump_blocks_deferred --value dump_offset
$0 --set dump_blocks_deferred_busy --value dump_offset
$0 --set dump_blocks_deferred_page --value dump_offset
	Dump content of various queues. For debugging purposes.

EOF
}

usage() {
	echo $0: usage: bc_control.sh '-h|--help'
	echo $0: usage: bc_control.sh '-l|--list'
	echo $0: usage: bc_control.sh '-g|--get cache_name'
	echo $0: usage: bc_control.sh '-s|--set option [-v|--value value] cache_name'
	echo ''
	echo $0: type 'bc_control.sh --help' for full usage
}

# arguments processing
COMMAND=""
SET_OPTION=""
VALUE_OPTION=""
LIST_OPTION=""
CACHE_NAME=""
SYSFS_PATH=""
FORCE=""
ARGS_O="hgs:v:lf"
ARGS_L="help,get,set:,value:,list,force"
ARGS=$(getopt -o $ARGS_O -l $ARGS_L -n "bc_control.sh" -- "$@");
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
		do_help
		exit 0
		;;
	-f|--force)
		shift
		FORCE="yes"
		;;
	-l|--list)
		shift
		COMMAND="list"
		;;
	-g|--get)
		shift
		COMMAND="get"
		;;
	-s|--set)
		shift
		COMMAND="set"
		SET_OPTION="$1"
		shift
		;;
	-v|--value)
		shift
		VALUE_OPTION="$1"
		shift
		;;
	--)
		# after this shift we'll have all the arguments (should be one)
		shift
		CACHE_NAME="$1"
		shift
		break
		;;
	esac
done

set_paths() {
	# for now we only accept the cache external name
	# internal name:    253:3 --> /sys/fs/bittern/253:3
	# device name: bitcache0 --> /dev/mapper/bitcache0
	if [ ! -b /dev/mapper/$CACHE_NAME ]
	then
		echo $0: ERROR: $CACHE_NAME is not a valid cache name
		exit 33
	fi
	# entry in /dev/mapper means we are using the cache name
	DEV_MAPPER_NAME="/dev/mapper/$CACHE_NAME"
	DEV_INTERNAL_NAME=$(ls -H -o $DEV_MAPPER_NAME | \
					sed -e 's/,//' | \
					awk ' { print $4 ":" $5; } ')
	SYSFS_PATH="/sys/fs/bittern/$DEV_INTERNAL_NAME/"
}

__get_cache_info() {
	local __sysfsname=$1
	local __param=$2
	cat $SYSFS_PATH/$__sysfsname | \
		grep " $__param=" | \
		sed -e "s/.* $__param=//" -e 's/ .*//' -e 's/[ (,].*$//'
}
get_cache_info() {
	local __param=$1
	__get_cache_info "info" $__param
}
get_cache_conf() {
	local __param=$1
	__get_cache_info "conf" $__param
}
get_cache_stats() {
	local __param=$1
	__get_cache_info "stats" $__param
}
get_cache_sequential() {
	local __param=$1
	__get_cache_info "sequential" $__param
}
get_cache_verifier() {
	local __param=$1
	__get_cache_info "verifier" $__param
}
get_cache_mode() {
	local __param=$1
	__get_cache_info "cache_mode" $__param
}
get_replacement() {
	local __param=$1
	__get_cache_info "replacement" $__param
}
get_cache_trace() {
	local __param=$1
	__get_cache_info "trace" $__param
}
#
#
set_cache_conf_silent() {
	local __name=$1
	local __value=$2
	/sbin/dmsetup message $CACHE_NAME 0 $*
}
set_cache_conf() {
	local __name=$1
	local __value=$2
	echo $0: $CACHE_NAME: setting $__name to $__value
	set_cache_conf_silent $__name $__value
}
#
#
do_get() {
	echo "cache info:"
	echo "	 internal_cache_name = $(get_cache_info cache_name)"
	echo "	 external_cache_name = $CACHE_NAME"
	echo "	 dev_mapper_name = $DEV_MAPPER_NAME"
	echo "	 sysfs_path = $SYSFS_PATH"
	echo "	 cache_device_name = $(get_cache_info cache_device_name)"
	echo "	 cached_device_name = $(get_cache_info cached_device_name)"
	__cache_device_size=$(get_cache_info in_use_cache_size)
	__cache_device_size=$(($__cache_device_size / 1024))
	__cache_device_size=$(($__cache_device_size / 1024))
	echo "	 cache_device_size = $__cache_device_size mbytes"
	echo "cache blocks:"
	echo "	 current_clean_blocks = $(get_cache_stats valid_clean_cache_entries)"
	echo "	 current_dirty_blocks = $(get_cache_stats valid_dirty_cache_entries)"
	echo "	 current_invalid_blocks = $(get_cache_stats invalid_cache_entries)"
	echo "cache mode:"
	echo "	 cache_mode = $(get_cache_mode cache_mode)"
	echo "replacement algorithm:"
	echo "	 replacement = $(get_replacement replacement)"
	echo "cache conf parameters:"
	echo "	 devio_worker_delay = $(get_cache_conf devio_worker_delay)"
	echo "	 devio_fua_insert = $(get_cache_conf devio_fua_insert)"
	echo "	 max_pending_requests = $(get_cache_conf max_pending_requests)"
	echo "	 bgwriter_conf_flush_on_exit = $(get_cache_conf bgwriter_conf_flush_on_exit)"
	echo "	 bgwriter_conf_greedyness = $(get_cache_conf bgwriter_conf_greedyness)"
	echo "	 bgwriter_conf_max_queue_depth_pct = $(get_cache_conf bgwriter_conf_max_queue_depth_pct)"
	echo "	 bgwriter_conf_cluster_size = $(get_cache_conf bgwriter_conf_cluster_size)"
	echo "	 bgwriter_policy = $(get_cache_conf bgwriter_conf_policy)"
	echo "	 invalidator_conf_min_invalid_count = $(get_cache_conf invalidator_conf_min_invalid_count)"
	echo "	 enable_extra_checksum_check = $(get_cache_conf enable_extra_checksum_check)"
	echo "debug parameters:"
	echo "	 trace = $(get_cache_trace trace)"
	echo "sequential read bypass:"
	echo "	 read_bypass_enabled = $(get_cache_sequential read_bypass_enabled)"
	echo "	 read_bypass_threshold = $(get_cache_sequential read_bypass_threshold)"
	echo "	 read_bypass_timeout = $(get_cache_sequential read_bypass_timeout)"
	echo "sequential write bypass:"
	echo "	 write_bypass_enabled = $(get_cache_sequential write_bypass_enabled)"
	echo "	 write_bypass_threshold = $(get_cache_sequential write_bypass_threshold)"
	echo "	 write_bypass_timeout = $(get_cache_sequential write_bypass_timeout)"
	echo "verifier thread:"
	__verifier_running=$(get_cache_verifier running)
	echo "	 running = $__verifier_running"
	echo "	 errors = $(get_cache_verifier verify_errors)"
	if [ $__verifier_running -ne 0 ]
	then
		echo "	 verified = $(get_cache_verifier blocks_verified)"
		echo "	 not_verified_invalid = $(get_cache_verifier blocks_not_verified_invalid)"
		echo "	 not_verified_dirty = $(get_cache_verifier blocks_not_verified_dirty)"
		echo "	 not_verified_busy = $(get_cache_verifier blocks_not_verified_busy)"
		echo "	 scans = $(get_cache_verifier scans)"
	fi
}

do_flush() {
	#
	# FIXME (or not)
	#
	# this is fundamentally broken. another thread can change the cache mode
	# between now and the time we reach the end of this function, so the
	# state we restore is potentially out of date.
	#
	# still, it does cover the common case, so it's important to have.
	#
	__do_flush_curr_cache_mode=$(get_cache_mode cache_mode)
	trap "{ \
		echo $0: interrupted; \
		set_cache_conf_silent cache_mode $__do_flush_curr_cache_mode; \
		exit 177;
	} " SIGINT SIGTERM SIGQUIT
	set_cache_conf_silent cache_mode writethrough
	#
	# wait on dirty blocks
	#
	__dirty_blocks=$(get_cache_stats valid_dirty_cache_entries)
	while [ "$__dirty_blocks" -ne 0 ]
	do
		echo $0: $CACHE_NAME has $__dirty_blocks dirty blocks ...
		__dirty_blocks=$(get_cache_stats valid_dirty_cache_entries)
		sleep 1
	done
	#
	# restore original cache_mode, modulo above comments
	#
	set_cache_conf_silent cache_mode $__do_flush_curr_cache_mode
}

do_verify() {
	#
	# tell verifier to stop (if it was running)
	#
	set_cache_conf_silent verifier_running 0
	sleep 1
	#
	#
	#
	trap "{ \
		echo $0: $CACHE_NAME verify interrupted, stopping; \
		set_cache_conf_silent verifier_running 0; \
		exit 177;
	} " SIGINT SIGTERM SIGQUIT
	#
	# set up verifier params and tell verifier to start
	#
	set_cache_conf_silent verifier_scan_delay_ms 0
	set_cache_conf_silent verifier_bugon_on_errors 0
	set_cache_conf_silent verifier_one_shot 1
	set_cache_conf_silent verifier_running 1
	#
	# this only really makes sense after setting writethrough mode
	#
	local __verify_running
	local __verify_errors
	__verify_running=$(get_cache_verifier running)
	__verify_errors=$(get_cache_verifier errors)
	while [ "$__verify_running" -ne 0 ]
	do
		local __blocks_verified
		local __nv_invalid
		local __nv_dirty
		local __nv_busy
		__blocks_verified=$(get_cache_verifier blocks_verified)
		__nv_invalid=$(get_cache_verifier blocks_not_verified_invalid)
		__nv_dirty=$(get_cache_verifier blocks_not_verified_dirty)
		__nv_busy=$(get_cache_verifier blocks_not_verified_busy)
		echo $0: $CACHE_NAME verify still running, \
				$__blocks_verified verified, \
				$__nv_invalid+$__nv_dirty+$__nv_busy \
				not verified
		__verify_running=$(get_cache_verifier running)
		__verify_errors=$(get_cache_verifier verify_errors)
		if [ "$__verify_errors" -ne 0 ]
		then
			echo $0: ERROR: $CACHE_NAME has verify errors
			exit 1
		fi
		sleep 1
	done
	if [ "$__verify_errors" -ne 0 ]
	then
		echo $0: ERROR: $CACHE_NAME has verify errors
		exit 1
	fi
}

do_verify_start(){
	set_cache_conf_silent verifier_running 0
	sleep 1
	set_cache_conf_silent verifier_one_shot 0
	set_cache_conf_silent verifier_running 1
}

do_verify_stop(){
	set_cache_conf_silent verifier_running 0
}

do_set_check_value(){
	if [ "$VALUE_OPTION" = "" ]
	then
		echo $0: ERROR: '--set' $set_option requires '--value'
		exit 97
	fi
}

do_set() {
	local __set_option=$*
	case "$__set_option" in
	"writeback"|"wb")
		set_cache_conf cache_mode writeback
		;;
	"writethrough"|"wt")
		set_cache_conf cache_mode writethrough
		;;
	"replacement")
		do_set_check_value
		set_cache_conf replacement $VALUE_OPTION
		;;
	"flush")
		echo $0: $CACHE_NAME: flushing dirty blocks
		do_flush
		;;
	"enable_req_fua")
		echo $0: $CACHE_NAME: setting enable_req_fua
		set_cache_conf enable_req_fua 1
		;;
	"disable_req_fua")
		echo $0: $CACHE_NAME: setting disable_req_fua
		set_cache_conf enable_req_fua 0
		;;
	"invalidate")
		set_cache_conf invalidate_cache 0
		;;
	"verify")
		echo $0: $CACHE_NAME: verifying clean blocks '(hit ^C to stop)'
		do_verify
		;;
	"verify_start")
		echo $0: $CACHE_NAME: enabling background verifier
		do_verify_start
		;;
	"verify_stop")
		echo $0: $CACHE_NAME: disbling background verifier
		do_verify_stop
		;;
	"bgwriter_conf_flush_on_exit")
		do_set_check_value
		set_cache_conf bgwriter_conf_flush_on_exit $VALUE_OPTION
		;;
	"bgwriter_conf_greedyness")
		do_set_check_value
		set_cache_conf bgwriter_conf_greedyness $VALUE_OPTION
		;;
	"bgwriter_conf_max_queue_depth_pct")
		do_set_check_value
		set_cache_conf bgwriter_conf_max_queue_depth_pct $VALUE_OPTION
		;;
	"bgwriter_conf_cluster_size")
		do_set_check_value
		set_cache_conf bgwriter_conf_cluster_size $VALUE_OPTION
		;;
	"bgwriter_conf_policy")
		do_set_check_value
		set_cache_conf bgwriter_conf_policy $VALUE_OPTION
		;;
	"devio_worker_delay")
		do_set_check_value
		set_cache_conf devio_worker_delay $VALUE_OPTION
		;;
	"devio_fua_insert")
		do_set_check_value
		set_cache_conf devio_fua_insert $VALUE_OPTION
		;;
	"invalidator_conf_min_invalid_count")
		do_set_check_value
		set_cache_conf invalidator_conf_min_invalid_count $VALUE_OPTION
		;;
	"max_pending_requests")
		do_set_check_value
		set_cache_conf max_pending_requests $VALUE_OPTION
		;;
	"disable-extra-checksum-check")
		set_cache_conf enable_extra_checksum_check 0
		;;
	"enable-extra-checksum-check")
		set_cache_conf enable_extra_checksum_check 1
		;;
	"read_bypass_enabled")
		do_set_check_value
		set_cache_conf read_bypass_enabled $VALUE_OPTION
		;;
	"read_bypass_threshold")
		do_set_check_value
		set_cache_conf read_bypass_threshold $VALUE_OPTION
		;;
	"read_bypass_timeout")
		do_set_check_value
		set_cache_conf read_bypass_timeout $VALUE_OPTION
		;;
	"write_bypass_enabled")
		do_set_check_value
		set_cache_conf write_bypass_enabled $VALUE_OPTION
		;;
	"write_bypass_threshold")
		do_set_check_value
		set_cache_conf write_bypass_threshold $VALUE_OPTION
		;;
	"write_bypass_timeout")
		do_set_check_value
		set_cache_conf write_bypass_timeout $VALUE_OPTION
		;;
	"trace")
		do_set_check_value
		set_cache_conf trace $VALUE_OPTION
		;;
	"dump_devio_pending")
		do_set_check_value
		set_cache_conf trace $VALUE_OPTION
                ;;
	dump_blocks_*)
		do_set_check_value
		set_cache_conf $__set_option $VALUE_OPTION
                ;;
	*)
		echo $0: unrecognized "--set" option
		exit 1
		;;
	esac
}

#
# verify that caller has specified --force if required
#
check_set_priv() {
	local __set_option=$*
	case "$__set_option" in
	"disable_req_fua")
		if [ -z "$FORCE" ]
		then
cat<<EOF

$0: $CACHE_NAME:

	Disabling the issue of REQ_FUA is **** VERY DANGEROUS ****.
	It should only be done if you truly understand it. If you are
	in doubt, please do not do it, as it could lead to data
	corruption.
	If you still still want to proceed, use --force

EOF
			exit 1
		fi
		;;
	*)
		;;
	esac
}

do_list() {
	local __count=0
	for bdev in /dev/mapper/*
	do
		CACHE_NAME=$(basename $bdev)
		DEV_MAPPER_NAME=$bdev
		DEV_INTERNAL_NAME=$(ls -H -o $DEV_MAPPER_NAME | \
						sed -e 's/,//' | \
						awk ' { print $4 ":" $5; } ')
		SYSFS_PATH="/sys/fs/bittern/$DEV_INTERNAL_NAME/"
		if [ -d $SYSFS_PATH ]
		then
			echo "CACHE_NAME_"$__count = $CACHE_NAME
			echo "DEV_INTERNAL_NAME_"$__count = $DEV_INTERNAL_NAME
			echo "DEV_MAPPER_NAME_"$__count = $DEV_MAPPER_NAME
			echo "SYSFS_PATH_"$__count = $SYSFS_PATH
			__count=$(($__count + 1))
		fi
	done
	echo CACHES = $__count
}

# main
main() {
	case "$COMMAND" in
	"list")
		do_list
		;;
	"get")
		if [ -z "$CACHE_NAME" ]
		then
			echo $0: "--get" requires a cache name
			exit 21
		fi
		set_paths
		if [ ! -d $SYSFS_PATH ]
		then
			echo $0: $CACHE_NAME is not loaded
			exit 1
		fi
		do_get
		;;
	"set")
		if [ -z "$SET_OPTION" ]
		then
			echo $0: "--set" requires a value
			exit 21
		fi
		if [ -z "$CACHE_NAME" ]
		then
			echo $0: "--set" requires a cache name
			exit 21
		fi
		set_paths
		if [ ! -d $SYSFS_PATH ]
		then
			echo $0: $CACHE_NAME is not loaded
			exit 1
		fi
		if [ $(id -u) != 0 ]
		then
			echo $0: you must be root to run this
			exit 15
		fi
		check_set_priv $SET_OPTION
		do_set $SET_OPTION
		;;
	esac
}


# execute main
main

# that's all folks
exit 0
