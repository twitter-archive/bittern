#!/bin/bash
#
# This is used as a convenient template for name changing.
# It will be deleted
#

replace_in_file() {
	local __old=$1
	local __new=$2
	local __file=$3
	sed -i -e "s/$__old/$__new/g" $__file
}

for f in *.h *.c docs/*.md
do
	echo $0: applying sed expressions to $f ......
	#replace_in_file transition_path cache_transition $f
	#replace_in_file CACHE_TRANSITION_PATH_VALID CACHE_TRANSITION_VALID $f
	#replace_in_file __CACHE_TRANSITION_PATHS_NUM __CACHE_TRANSITION_NUM $f
	#replace_in_file CACHE_TRANSITION_PATH_ PATH_ $f
	#replace_in_file PATH_ TS_ $f
	#replace_in_file __CACHE_TRANSITION_NUM __TS_NUM $f
	#replace_in_file CACHE_INVALID S_INVALID $f
	#replace_in_file CACHE_VALID_CLEAN S_CLEAN $f
	#replace_in_file CACHE_VALID_DIRTY S_DIRTY $f
	#replace_in_file _PARTIAL_WRITE_ _P_WRITE_ $f
	replace_in_file _COPY_TO_ _CPT_ $f
	replace_in_file _COPY_FROM_ _CPF_ $f
done
