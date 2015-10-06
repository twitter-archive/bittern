Bittern Beta Release 0.27.1 Release Notes
-----------------------------------------

To get started with Bittern, you will need the following:

* Operating System
	* CentOS 6
	* CentOS 7
* Linux kernel
	* 3.14.8 or later
	* 4.0 or later (for testing DAX support)
* Doxygen
	* 1.8.9.1 or later
* Graphviz

Use Linux Kernel 3.14.8 or later if you want to test block device only.
There are no specific configuration requirements for this kernel.

Use Linux Kernel 4.0 or later if you want to test block device and DAX.
For this kernel you will to compile BRD driver (aka ramdisk) as a module and
with DAX support enabled. BRD allows to simulate NVDIMM hardware (minus crash
recovery) and needs to be a loadable module.

Once all the above is setup and the OS is running the desired kernel,
do the following:

	pwd $                                   cd bittern-cache
	bittern-cache $                         cd src/
	bittern-cache/src $                     make distclean
	bittern-cache/src $                     make devconfig
	bittern-cache/src $                     make
	bittern-cache/src $                     cd bittern_cache_kmod
	bittern-cache/src/bittern_cache_kmod $  make doxygen.docs
	bittern-cache/src/bittern_cache_kmod $  firefox doxygen.docs/html/index.html

At this point you will have all the binaries built and doxygen documentation
available.

The doxygen documentation contains both Release Notes as well as the
Getting Started guide. The use of doxygen 1.8.9.1 or later is required
in order to generate documentation correctly.
