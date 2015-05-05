### MAIN PACKAGE (lvm2)

%post
/sbin/ldconfig
%daemon_reload
%enable -s monitor -t service
%enable -s lvmetad -t socket
%if %{enable_systemd}
%else
  /sbin/chkconfig --add bittern-lvm2
%endif

%preun
if [ "$1" = 0 ]; then
%disable -s monitor -t service
%disable -s lvmetad -t socket
%if %{enable_systemd}
%else
  /sbin/chkconfig --del bittern-lvm2
%endif
fi

%postun
%daemon_reload

if [ $1 -ge 1 ]; then
%try_restart -s monitor -t service
%try_restart -s lvmetad -t service
fi

%triggerun -- %{name} < 2.02.86-2
%{_bindir}/systemd-sysv-convert --save lvm2-monitor >/dev/null 2>&1 || :
/bin/systemctl --no-reload enable lvm2-monitor.service > /dev/null 2>&1 || :
/sbin/chkconfig --del lvm2-monitor > /dev/null 2>&1 || :
/bin/systemctl try-restart lvm2-monitor.service > /dev/null 2>&1 || :
# files in the main package

%files
%defattr(-,root,root,-)
%doc COPYING COPYING.LIB INSTALL README VERSION WHATS_NEW
%doc doc/lvm_fault_handling.txt
%{_sbindir}/bittern-lvchange
%{_sbindir}/bittern-lvconvert
%{_sbindir}/bittern-lvcreate
%{_sbindir}/bittern-lvdisplay
%{_sbindir}/bittern-lvextend
%{_sbindir}/bittern-lvm
%{_sbindir}/bittern-lvmchange
%{_sbindir}/bittern-lvmdiskscan
%{_sbindir}/bittern-lvmdump
%{_sbindir}/bittern-lvmsadc
%{_sbindir}/bittern-lvmsar
%{_sbindir}/bittern-lvreduce
%{_sbindir}/bittern-lvremove
%{_sbindir}/bittern-lvrename
%{_sbindir}/bittern-lvresize
%{_sbindir}/bittern-lvs
%{_sbindir}/bittern-lvscan
%{_sbindir}/bittern-pvchange
%{_sbindir}/bittern-pvck
%{_sbindir}/bittern-pvcreate
%{_sbindir}/bittern-pvdisplay
%{_sbindir}/bittern-pvmove
%{_sbindir}/bittern-pvremove
%{_sbindir}/bittern-pvresize
%{_sbindir}/bittern-pvs
%{_sbindir}/bittern-pvscan
%{_sbindir}/bittern-vgcfgbackup
%{_sbindir}/bittern-vgcfgrestore
%{_sbindir}/bittern-vgchange
%{_sbindir}/bittern-vgck
%{_sbindir}/bittern-vgconvert
%{_sbindir}/bittern-vgcreate
%{_sbindir}/bittern-vgdisplay
%{_sbindir}/bittern-vgexport
%{_sbindir}/bittern-vgextend
%{_sbindir}/bittern-vgimport
%{_sbindir}/bittern-vgimportclone
%{_sbindir}/bittern-vgmerge
%{_sbindir}/bittern-vgmknodes
%{_sbindir}/bittern-vgreduce
%{_sbindir}/bittern-vgremove
%{_sbindir}/bittern-vgrename
%{_sbindir}/bittern-vgs
%{_sbindir}/bittern-vgscan
%{_sbindir}/bittern-vgsplit
%{_sbindir}/bittern-lvmconf
%if %{have_service lvmetad}
 %{_sbindir}/bittern-lvmetad
%endif
%if %{have_with cache}
  %{_mandir}/man7/bittern-lvmcache.7.gz
%endif
%if %{have_with thin}
  %{_mandir}/man7/bittern-lvmthin.7.gz
%endif
%{_mandir}/man7/bittern-lvmsystemid.7.gz
%{_mandir}/man5/bittern-lvm.conf.5.gz
%{_mandir}/man8/bittern-lvchange.8.gz
%{_mandir}/man8/bittern-lvconvert.8.gz
%{_mandir}/man8/bittern-lvcreate.8.gz
%{_mandir}/man8/bittern-lvdisplay.8.gz
%{_mandir}/man8/bittern-lvextend.8.gz
%{_mandir}/man8/bittern-lvm.8.gz
%{_mandir}/man8/bittern-lvmchange.8.gz
%{_mandir}/man8/bittern-lvmconf.8.gz
%{_mandir}/man8/bittern-lvmdiskscan.8.gz
%{_mandir}/man8/bittern-lvmdump.8.gz
%{_mandir}/man8/bittern-lvmsadc.8.gz
%{_mandir}/man8/bittern-lvmsar.8.gz
%{_mandir}/man8/bittern-lvreduce.8.gz
%{_mandir}/man8/bittern-lvremove.8.gz
%{_mandir}/man8/bittern-lvrename.8.gz
%{_mandir}/man8/bittern-lvresize.8.gz
%{_mandir}/man8/bittern-lvs.8.gz
%{_mandir}/man8/bittern-lvscan.8.gz
%{_mandir}/man8/bittern-pvchange.8.gz
%{_mandir}/man8/bittern-pvck.8.gz
%{_mandir}/man8/bittern-pvcreate.8.gz
%{_mandir}/man8/bittern-pvdisplay.8.gz
%{_mandir}/man8/bittern-pvmove.8.gz
%{_mandir}/man8/bittern-pvremove.8.gz
%{_mandir}/man8/bittern-pvresize.8.gz
%{_mandir}/man8/bittern-pvs.8.gz
%{_mandir}/man8/bittern-pvscan.8.gz
%{_mandir}/man8/bittern-vgcfgbackup.8.gz
%{_mandir}/man8/bittern-vgcfgrestore.8.gz
%{_mandir}/man8/bittern-vgchange.8.gz
%{_mandir}/man8/bittern-vgck.8.gz
%{_mandir}/man8/bittern-vgconvert.8.gz
%{_mandir}/man8/bittern-vgcreate.8.gz
%{_mandir}/man8/bittern-vgdisplay.8.gz
%{_mandir}/man8/bittern-vgexport.8.gz
%{_mandir}/man8/bittern-vgextend.8.gz
%{_mandir}/man8/bittern-vgimport.8.gz
%{_mandir}/man8/bittern-vgimportclone.8.gz
%{_mandir}/man8/bittern-vgmerge.8.gz
%{_mandir}/man8/bittern-vgmknodes.8.gz
%{_mandir}/man8/bittern-vgreduce.8.gz
%{_mandir}/man8/bittern-vgremove.8.gz
%{_mandir}/man8/bittern-vgrename.8.gz
%{_mandir}/man8/bittern-vgs.8.gz
%{_mandir}/man8/bittern-vgscan.8.gz
%{_mandir}/man8/bittern-vgsplit.8.gz
%{_mandir}/man8/bittern-lvm-dumpconfig.8.gz
%if %{enable_udev}
 %{_udevdir}/11-dm-lvm.rules
 %if %{have_service lvmetad}
  %{_mandir}/man8/lvmetad.8.gz
  %{_udevdir}/69-dm-lvm-metad.rules
 %endif
%endif
%dir %{lvm_confdir}
%ghost %{lvm_confdir}/cache/.cache
%config(noreplace) %verify(not md5 mtime size) %{lvm_confdir}/lvm.conf
%config(noreplace) %verify(not md5 mtime size) %{lvm_confdir}/lvmlocal.conf
%config(noreplace) %verify(not md5 mtime size) %{lvm_confdir}/profile/command_profile_template.profile
%config(noreplace) %verify(not md5 mtime size) %{lvm_confdir}/profile/metadata_profile_template.profile
%config(noreplace) %verify(not md5 mtime size) %{lvm_confdir}/profile/thin-generic.profile
%config(noreplace) %verify(not md5 mtime size) %{lvm_confdir}/profile/thin-performance.profile
%dir %{lvm_confdir}/backup
%dir %{lvm_confdir}/cache
%dir %{lvm_confdir}/archive
%dir %{_default_locking_dir}
%dir %{_default_run_dir}
%if %{enable_systemd}
 %config(noreplace) %{_prefix}/lib/tmpfiles.d/%{name}.conf
 %{_unitdir}/lvm2-monitor.service
 %if %{have_service lvmetad}
  %{_unitdir}/lvm2-lvmetad.socket
  %{_unitdir}/lvm2-lvmetad.service
  %{_unitdir}/lvm2-pvscan@.service
  %{_unitdir}/blk-availability.service
 %endif
%else
 %{_sysconfdir}/rc.d/init.d/bittern-lvm2
 %if %{have_service lvmetad}
   %{_sysconfdir}/rc.d/init.d/blk-availability
   %{_sysconfdir}/rc.d/init.d/lvm2-lvmetad
 %endif
%endif

##############################################################################
# Library and Development subpackages
##############################################################################

%package libs
Summary: Shared libraries for lvm2
License: LGPLv2
Group: System Environment/Libraries
Requires: device-mapper-event >= %{device_mapper_version}-%{release}

%description libs
This package contains shared lvm2 libraries for applications.

%post libs -p /sbin/ldconfig

%postun libs -p /sbin/ldconfig

%files libs
%defattr(-,root,root,-)

##############################################################################
# Cluster subpackage
##############################################################################
%if %{have_with clvmd}

%package cluster
Summary: Cluster extensions for userland logical volume management tools
License: GPLv2
Group: System Environment/Base
Requires: lvm2 >= %{version}-%{release}
Requires(post): chkconfig
Requires(preun): chkconfig
Requires(preun): device-mapper >= %{device_mapper_version}
Requires(preun): lvm2 >= 2.02
%maybe Requires: %{req_cluster}

%description cluster

Extensions to LVM2 to support clusters.

%post cluster
/sbin/chkconfig --add clvmd

if [ "$1" -gt "1" ] ; then
	/usr/sbin/clvmd -S >/dev/null 2>&1 || :
fi

%preun cluster
if [ "$1" = 0 ]; then
	/sbin/chkconfig --del clvmd
        # lvmconf may no longer exist if lvm2 is being removed in the same rpm run
	if test -x /sbin/lvmconf; then /sbin/lvmconf --disable-cluster; fi
fi

%files cluster
%defattr(-,root,root,-)
%attr(755,root,root) %{_sbindir}/bittern-clvmd
%{_mandir}/man8/bittern-clvmd.8.gz
%{_sysconfdir}/rc.d/init.d/clvmd
%if %{enable_systemd}
  %{_unitdir}/lvm2-clvmd.service
  %{_unitdir}/lvm2-cluster-activation.service
  %{_unitdir}/../lvm2-cluster-activation
%endif

%endif

##############################################################################
# Cluster mirror subpackage
##############################################################################
%if %{enable_cmirror}

%package -n cmirror
Summary: Daemon for device-mapper-based clustered mirrors
Group: System Environment/Base
Requires(post): chkconfig
Requires(preun): chkconfig
Requires: corosync >= %{corosync_version}
Requires: device-mapper >= %{device_mapper_version}-%{release}

%description -n cmirror
Daemon providing device-mapper-based mirrors in a shared-storage cluster.

%post -n cmirror
/sbin/chkconfig --add cmirrord

%preun -n cmirror
if [ "$1" = 0 ]; then
	/sbin/chkconfig --del cmirrord
fi

%files -n cmirror
%defattr(-,root,root,-)
%attr(755,root,root) %{_sbindir}/bittern-cmirrord
%{_mandir}/man8/bittern-cmirrord.8.gz
%{_sysconfdir}/rc.d/init.d/cmirrord
%if %{enable_systemd}
  %{_unitdir}/lvm2-cmirrord.service
%endif

%endif

##############################################################################
# Legacy SysV init subpackage
##############################################################################
%if %{enable_systemd}

%package sysvinit
Summary: SysV style init script for LVM2.
Group: System Environment/Base
Requires: %{name} = %{version}-%{release}
Requires: initscripts

%description sysvinit
SysV style init script for LVM2. It needs to be installed only if systemd
is not used as the system init process.

%files sysvinit
%{_sysconfdir}/rc.d/init.d/lvm2-monitor
%{_sysconfdirr}/rc.d/init.d/blk-availability
%if %{have_service lvmetad}
%{_sysconfdir}/rc.d/init.d/lvm2-lvmetad
%endif

%endif

##############################################################################
# Device-mapper subpackages
##############################################################################
%if 0%{?package_device-mapper}
%package -n device-mapper
Summary: Device mapper utility
Version: %{device_mapper_version}
Release: %{release}
License: GPLv2
Group: System Environment/Base
URL: http://sources.redhat.com/dm
Requires: device-mapper-libs = %{device_mapper_version}-%{release}
Requires: util-linux >= 2.15
%maybe Requires: %{req_udev}
%if %{enable_udev}
# We need dracut to install required udev rules if udev_sync
# feature is turned on so we don't lose required notifications.
Conflicts: dracut < 002-18
%endif

%description -n device-mapper
This package contains the supporting userspace utility, dmsetup,
for the kernel device-mapper.

%files -n device-mapper
%defattr(-,root,root,-)
%doc COPYING COPYING.LIB WHATS_NEW_DM VERSION_DM README INSTALL
%attr(755,root,root) %{_sbindir}/bittern-dmsetup
%{_mandir}/man8/bittern-dmsetup.8.gz
%if %{enable_udev}
%doc udev/12-dm-permissions.rules
%dir %{_udevbasedir}
%dir %{_udevdir}
%{_udevdir}/10-dm.rules
%{_udevdir}/13-dm-disk.rules
%{_udevdir}/95-dm-notify.rules
%endif

%package -n device-mapper-devel
Summary: Development libraries and headers for device-mapper
Version: %{device_mapper_version}
Release: %{release}
License: LGPLv2
Group: Development/Libraries
Requires: device-mapper = %{device_mapper_version}-%{release}
Requires: pkgconfig

%description -n device-mapper-devel
This package contains files needed to develop applications that use
the device-mapper libraries.

%files -n device-mapper-devel
%defattr(-,root,root,-)
%{_libdir}/libdevmapper.so
%{_includedir}/libdevmapper.h

%package -n device-mapper-libs
Summary: Device-mapper shared library
Version: %{device_mapper_version}
Release: %{release}
License: LGPLv2
Group: System Environment/Libraries
Requires: device-mapper = %{device_mapper_version}-%{release}

%description -n device-mapper-libs
This package contains the device-mapper shared library, libdevmapper.

%post -n device-mapper-libs -p /sbin/ldconfig

%postun -n device-mapper-libs -p /sbin/ldconfig

%files -n device-mapper-libs
%attr(755,root,root) %{_libdir}/libdevmapper.so.*

%endif # package_device-mapper

%if x%{with_dmeventd} != x0

%package -n device-mapper-event
Summary: Device-mapper event daemon
Group: System Environment/Base
Version: %{device_mapper_version}
Release: %{release}
Requires: device-mapper = %{device_mapper_version}-%{release}
Requires: device-mapper-event-libs = %{device_mapper_version}-%{release}
%if %{enable_systemd}
Requires(post): systemd-units
Requires(preun): systemd-units
Requires(postun): systemd-units
%endif

%description -n device-mapper-event
This package contains the dmeventd daemon for monitoring the state
of device-mapper devices.

%post -n device-mapper-event
%if %{enable_systemd}
/bin/systemctl daemon-reload > /dev/null 2>&1 || :
/bin/systemctl enable dm-event.socket > /dev/null 2>&1 || :
%endif

%preun -n device-mapper-event
%if %{enable_systemd}
if [ "$1" = 0 ]; then
	/bin/systemctl --no-reload disable dm-event.service dm-event.socket > /dev/null 2>&1 || :
	/bin/systemctl stop dm-event.service dm-event.socket> /dev/null 2>&1 || :
fi
%endif

%postun -n device-mapper-event
%if %{enable_systemd}
/bin/systemctl daemon-reload > /dev/null 2>&1 || :
if [ $1 -ge 1 ]; then
	/bin/systemctl reload dm-event.service > /dev/null 2>&1 || :
fi
%endif

%files -n device-mapper-event
%defattr(-,root,root,-)
%{_sbindir}/dmeventd
%{_mandir}/man8/bittern-dmeventd.8.gz
%if %{enable_systemd}
%{_unitdir}/dm-event.socket
%{_unitdir}/dm-event.service
%endif

%package -n device-mapper-event-libs
Summary: Device-mapper event daemon shared library
Version: %{device_mapper_version}
Release: %{release}
License: LGPLv2
Group: System Environment/Libraries

%description -n device-mapper-event-libs
This package contains the device-mapper event daemon shared library,
libdevmapper-event.

%post -n device-mapper-event-libs -p /sbin/ldconfig

%postun -n device-mapper-event-libs -p /sbin/ldconfig

%files -n device-mapper-event-libs
%attr(755,root,root) %{_libdir}/libdevmapper-event.so.*

%package -n device-mapper-event-devel
Summary: Development libraries and headers for the device-mapper event daemon
Version: %{device_mapper_version}
Release: %{release}
License: LGPLv2
Group: Development/Libraries
Requires: device-mapper-event = %{device_mapper_version}-%{release}
Requires: pkgconfig

%description -n device-mapper-event-devel
This package contains files needed to develop applications that use
the device-mapper event library.

%files -n device-mapper-event-devel
%defattr(-,root,root,-)
%{_libdir}/libdevmapper-event.so
%{_includedir}/libdevmapper-event.h
%{_libdir}/pkgconfig/devmapper-event.pc
%endif

%if %{enable_testsuite}
%package testsuite
Summary: LVM2 Testsuite
License: LGPLv2
Group: Development

%description testsuite
An extensive functional testsuite for LVM2.

%files testsuite
%defattr(-,root,root,-)
%{_datadir}/lvm2-testsuite/
%{_libexecdir}/lvm2-testsuite/
%{_bindir}/lvm2-testsuite
%endif
