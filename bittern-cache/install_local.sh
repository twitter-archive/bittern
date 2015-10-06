#!/bin/bash

set -e

if [ `id -u` -ne 0 ] ; then
  echo $0: You really need to run this as root.
  exit 10;
fi

if [ ! -d src ] ||
   [ ! -d src/tools ]||
   [ ! -d scripts ] ||
   [ ! -d etc ] ; then
   echo $0: Please run this form top level of Bittern directory.
   exit 1
fi

if [ ! -r src/bittern_cache_kmod/bittern_cache.ko ] ||
   [ ! -r src/tools/bc_tool ] ; then
   echo $0: Create your configuration and make the modules first.
   exit 2
fi

set -x

mkdir -p /sbin/bittern_cache/
mkdir -p /sbin/bittern_cache/scripts/

/bin/cp -f -r scripts/* /sbin/bittern_cache/scripts/
/bin/cp -f src/tools/bc_tool /sbin/bittern_cache/scripts/

tar cf - src/*/*.ko | (cd /sbin/bittern_cache/ ; tar xvf -)

cp -f etc/init.d/bittern_cache /etc/init.d/
cp -f etc/init.d/bittern_cache_prestop /etc/init.d/
cp -f etc/init.d/bittern_cache_stop /etc/init.d/

if [ ! -r /etc/bittern.conf ]
then
        cp etc/bittern.conf /etc/bittern.conf
fi

if [ ! -r /etc/rc.d/rc.sysinit.bak ]
then
        cp /etc/rc.d/rc.sysinit /etc/rc.d/rc.sysinit.new
        cp /etc/rc.d/rc.sysinit /etc/rc.d/rc.sysinit.bak
        patch -p0 /etc/rc.d/rc.sysinit.new < etc/rc.sysinit.patch
        sync
        mv /etc/rc.d/rc.sysinit.new /etc/rc.d/rc.sysinit
        sync
fi

chkconfig --add bittern_cache_prestop
chkconfig --level 12345 bittern_cache_prestop on
chkconfig --level 06 bittern_cache_prestop off
chkconfig --add bittern_cache_stop
chkconfig --level 12345 bittern_cache_stop on
chkconfig --level 06 bittern_cache_stop off
