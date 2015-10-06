#!/bin/bash

if [ `id -u` -ne 0 ]
then
	echo $0: error: need to be root
	exit 1
fi

bash ./remove_pcp.sh

mkdir /var/lib/pcp/pmdas/bittern/
cp pmdabittern/Install /var/lib/pcp/pmdas/bittern/
cp pmdabittern/Remove /var/lib/pcp/pmdas/bittern/
cp pmdabittern/pmdabittern.pl /var/lib/pcp/pmdas/bittern/

cd /var/lib/pcp/pmdas/bittern/

./Install
