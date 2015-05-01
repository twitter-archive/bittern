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

/bin/rm -f /tmp/.$USER.bittern_cache_git_info.current.txt /tmp/.$USER.bittern_cache_git_info.last.txt

M_SOURCE_FILES="$*"
generate_git_info="no"

./mk_git_info.sh $M_SOURCE_FILES > .bittern_cache_git_info.current.c

if [ \( -r bittern_cache_git_info.c \) -a \( -r .bittern_cache_git_info.last.c \) ]
then
        # echo $0: then
        /bin/grep -v generated .bittern_cache_git_info.current.c > /tmp/.$USER.bittern_cache_git_info.current.txt
        /bin/grep -v generated .bittern_cache_git_info.last.c > /tmp/.$USER.bittern_cache_git_info.last.txt
        /usr/bin/cmp -s /tmp/.$USER.bittern_cache_git_info.current.txt /tmp/.$USER.bittern_cache_git_info.last.txt
        __status=$?
        if [ $__status != 0 ]
        then
                # echo $0: then-then - different - yes
                generate_git_info="yes"
        else
                # echo $0: then-else - same - no
                generate_git_info="no"
        fi
else
        # echo $0: else - yes
        generate_git_info="yes"
fi

for file in $M_SOURCE_FILES
do
        if [ $file -nt bittern_cache_git_info.c ]
        then
                # echo $0: $file -nt bittern_cache_git_info.c
                generate_git_info="yes"
        fi
done

if [ "$generate_git_info" = "yes" ]
then
        echo '  SH [M]  bittern_cache_git_info.c'
        /bin/cp -f .bittern_cache_git_info.current.c bittern_cache_git_info.c
        /bin/cp -f .bittern_cache_git_info.current.c .bittern_cache_git_info.last.c 
        # echo $0: gen git - yes
else
        # echo $0: gen git - no
        generate_git_info="no"
fi

/bin/rm -f /tmp/.$USER.bittern_cache_git_info.current.txt /tmp/.$USER.bittern_cache_git_info.last.txt
