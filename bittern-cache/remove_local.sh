#!/bin/bash

set -e
set -x

if [ `id -u` -ne 0 ] ; then exit 10; fi

chkconfig --del bittern_cache_prestop
chkconfig --del bittern_cache_stop

# remove entries under rc.d/
/bin/rm -f /etc/sysconfig/modules/bittern.modules
/bin/rm -f /etc/rc.d/*/*bittern_cache*
/bin/rm -f /etc/init.d/bittern_cache
/bin/rm -f /etc/init.d/bittern_cache_prestop
/bin/rm -f /etc/init.d/bittern_cache_stop
# restore backup file
if [ -r /etc/rc.d/rc.sysinit.bak ]
then
        mv /etc/rc.d/rc.sysinit.bak /etc/rc.d/rc.sysinit
        sync
fi
# do not remove bittern.conf
# remove all scripts, binaries and modules
/bin/rm -rf /sbin/bittern_cache/
