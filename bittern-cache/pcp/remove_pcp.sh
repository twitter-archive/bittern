#!/bin/bash

if [ `id -u` -ne 0 ]
then
	echo $0: error: need to be root
	exit 1
fi

if [ ! -d /var/lib/pcp/pmdas/bittern ]
then
	echo $0: error already removed
	exit 1
fi

cd /var/lib/pcp/pmdas/bittern/

./Remove

cd /tmp

rm -rf /var/lib/pcp/pmdas/bittern/
