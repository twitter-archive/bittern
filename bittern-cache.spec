%define LKAVer 2.6.54-t12.el%{?rhel}
%if 0%{?rhel} >= 06
%define ModDir %{LKAVer}.x86_64
%define KernelSrcDir %{ModDir}
%else
%define ModDir %{LKAVer}
%define KernelSrcDir %{ModDir}-x86_64
%endif

Name: AmericanBittern
Version: 0.27.1
Release: t2%{?dist}
Summary: Bittern Cache uses Persistent Memory to speed up block IO operations.

Group: Core
License: GPL
URL: https://git.twitter.biz/AmericanBittern
Source0: AmericanBittern-%{version}.tar.gz

BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}
BuildRequires: kernel-devel = %{LKAVer}
# List the packages used during the kernel build.
BuildRequires: module-init-tools, patch >= 2.5.4, bash >= 2.03, sh-utils, tar
BuildRequires: bzip2, findutils, gzip, m4, perl, make >= 3.78, diffutils
## BuildRequires: gcc >= 3.4.2, binutils >= 2.12, redhat-rpm-config
BuildRequires: python
BuildRequires: git
BuildRequires: devtoolset-2-toolchain, devtoolset-2-binutils >= 2.22
Requires: kernel = %{LKAVer}

%description
go/bittern

%prep
%setup -q

%build
# Enable devtoolset2 that is compiling our kernel to compile the module.
. /opt/rh/devtoolset-2/enable
(cd bittern-cache/src && ./mk.configure --production)
cd bittern-cache && make KERNEL_SOURCE_VERSION=%{ModDir} KERNEL_TREE=/usr/src/kernels/%{KernelSrcDir}

%install
# Enable devtoolset2 that is compiling our kernel to compile the module.
. /opt/rh/devtoolset-2/enable

cd bittern-cache
make install DESTDIR=%{buildroot} KERNEL_SOURCE_VERSION=%{ModDir} KERNEL_TREE=/usr/src/kernels/%{KernelSrcDir}
/bin/cp -f %{buildroot}/usr/bin/bc_tool %{buildroot}/sbin/bittern_cache/scripts/bc_tool

%files
/lib/modules/%{ModDir}/extra/bittern_cache/bittern_cache.ko
/usr/bin/bc_hash
/usr/bin/bc_tool
%config /etc/bittern.conf.d/bitcache0.conf.example
%doc /usr/share/bittern/addenum/rc.sysinit.patch
/etc/init.d/bittern_cache
/etc/init.d/bittern_cache_prestop
/etc/init.d/bittern_cache_stop
# Scripts
/sbin/bittern_cache/scripts/*

%changelog
* Mon June 29 2015 Fio Cattaneo <fio@twitter.com> 0.27.1-t2
- More cleanup and code refactoring.
- State machine now correctly uses dirty-write-cloning for all write hits.
- REQ_FLUSH is now issued to the cached device.
- Other miscellaneous changes.

* Wed Apr 15 2015 Matt Mullins <mmullins@twitter.com> 0.24.13-t2
- pmem_provider.ko and pmem_ram.ko are no longer built.

* Wed Apr 1 2015 Masoud Sharbiani <msharbiani@twitter.com> 0.24.13
- Interleaved Data/metadata cache layout support.
- All runtime configurable params and control messages are now done via
  "dmsetup message".
- fix race condition - need to wait for devices to pop-up under /dev
  otherwise we'll try to unload a module that has not finished populating
  /dev and hit an error.
- With lots of other changes...

* Mon Mar 2 2015 Masoud Sharbiani <msharbiani@twitter.com> 0.22.23
- Major major cleanup (Merger of bittern-next with the master)
- Restore: remove the old cache block from the rb tree when restoring the
  same sector with a newer xid.
- Bypass sequential writes.
- Update the background writeback policy in a safer manner.

* Mon Feb 2 2015 Masoud Sharbiani <msharbiani@twitter.com> 0.20.2
- Fix pending i/o count.
- Enable multiple cache configs and parallel restore/teardown for caches.

* Mon Jan 12 2015 Masoud Sharbiani <msharbiani@twitter.com> 0.19.4
- Lots of changes (for full history, check the git log)
- Support for multiple buffer pools.
- Fixed cache device size checking and alignment issues.
- Always TRIM the device before use.
- Add more tunables to the code: max_pending_requests, bgwriter_greedyness and
                        bgwriter_max_queue_depth_pct
                        (commitid: b98195f4ae99dae3e058f281a16e19acbc016a41)

- Cleaner bc_stats.pl tool output (commit 0e796eca5).
- Autoflush dirty blocks on device teardown.
- Add multivolume support for pmem_ram and pmem_blockdev.

* Tue Nov 11 2014 Masoud Sharbiani <msharbiani@twitter.com> 0.17.0
- OS-2954: Fix for larger sizes of cache devices on ssd.
- OS-2972: Fix for a couple of 32bit overflows.
- OS-2953: Reduce the stack size used.
- OS-2885: Fix a race condition that causes crash while using with Manhattan.

* Wed Oct 08 2014 Masoud Sharbiani <msharbiani@twitter.com> 0.0.1
- Initial version of the RPM.
