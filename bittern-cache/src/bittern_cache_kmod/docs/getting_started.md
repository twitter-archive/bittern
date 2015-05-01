# Getting Started {#getting_started}

Even though there are very few hardware samples of Persistent Memory devices
(NVDIMM and PCIe-NVRAM), you can use and test all aspects of Bittern
(except restore from power failure of course) on simulated NVDIMM using the BRD
driver with DAX support. Performance will be identical to real NVDIMM hardware.

The recommended kernel is 4.0 or later, which will allow you to test
block block-based NAND-flash devices (NVMe, SSD) as well as NVDIMM.
The 3.14.8 kernel is also supported and tested for legacy reasons for
NAND-flash devices only.

## Kernel Configuration

No specific configuration is required for 3.14.8 kernel.
For 4.0 and later kernels, the following configuration is required:


        CONFIG_BLK_DEV_RAM=m
        CONFIG_BLK_DEV_RAM_DAX=y
        CONFIG_FS_DAX=y

## Building Bittern

Configure, build, install and reboot with the desired kernel. To build:

        # cd $BITTERN_REPO/bittern-cache/src/
        # make distclean
        # make devconfig
        # make
        # cd bittern_cache_kmod
        # make doxygen.docs

### Build Types

Bittern has two build types, development and production. The development
build contains a lot of extra runtime checks and assertion checks used for
development and some testing --  do not use this build for performance testing.
The production build is the one intended to be used for production as well as
performance testig.

To build a debug version:

        # cd $BITTERN_REPO/bittern-cache/src/
        # make distclean
        # make devconfig
        # make

To build a production version:

        # cd $BITTERN_REPO/bittern-cache/src/
        # make distclean
        # make prodconfig
        # make

## Using Bittern with scripts

This section describes how to setup a bittern cache using the provided scripts.
Note that the preferred method (outside of testing and development)
is with LVM2.

You will need two logical volumes, one for the cache device
(NAND-flash or NVDIMM) hardware and one for the cached device (LVM calls this
the origin device).
Ideally they should both be on separate drives, but it's not really needed
for functionality and stress testing.
If you do not have a NAND-flash device (SSD or NVMe), you can use the BRD device
to simulate NVDIMM hardware.

First, load Bittern and the BRD driver so we can use the ramdisk as the cache
device (in this example we use a 1 Gbyte size for BRD):

         # cd $BITTERN_REPO/src/bittern_cache_kmod
         # ../../scripts/bc_insmod_devel.sh --insmod-brd --insmod-brd-size $((1024*1024))

Now Setup a bittern cache on top of the volume to be cached:

         # ../../scripts/bc_setup.sh --cache-operation create --cache-name bitcache0 --cache-device /dev/ram0 --device /dev/mapper/vg_hdd-lvol0

The above command will create a bittern cache on /dev/ram0,
caching the device /dev/mapper/vg_hdd-lvol0 and expose the resulting volume
as a device mapper entry /dev/mapper/bitcache0

To list loaded bittern caches:

         # ../../scripts/bc_control.sh --list

To show information about a cache status, operating mode and configuration:

         # ../../scripts/bc_control.sh --get bitcache0

To unload the cache:

         # ../../scripts/bc_remove.sh bitcache0

Please note that you cannot modify the content of the cached device, otherwise
data corruption will result when the cache is restored. Using LVM removes this
danger, but with manual scripts there is no such protection.
To restore the cache:

         # ../../scripts/bc_setup.sh --cache-operation restore --cache-name bitcache0 --cache-device /dev/ram0 --device /dev/mapper/vg_hdd-lvol0

To remove the kernel modules once all caches are unloaded:

         # ../../scripts/bc_rmmod.sh --rmmod-brd

To show bittern cache statistics every second:

         # ../../scripts/bc_stats.pl

Do "bc_stats.pl --help" for an explanation of the various columns.

## Using bc_control.sh

The bc_control.sh script allows you modify things like cache operating mode
(writeback or writethrough), replacement algorithms, and various other
runtime configurable parameters.
Do display general help, do "bc_control.sh --help".

Miscellaneous examples:

         # ../scripts/bc_control.sh --set flush <cachename> # force flushing out of all dirty buffers

         # ../scripts/bc_control.sh --set writethrough <cachename> # set writethrough mode

         # ../scripts/bc_control.sh --set writeback <cachename> # set writeback mode (default)

Command sequence to force flushing out of all dirty buffers when in writeback mode

         # ../scripts/bc_control.sh --set writethrough <cachename> # set writethrough mode, initiate flush out

         # ../scripts/bc_control.sh --set flush <cachename> # wait for flushing out of all dirty buffers

         # ../scripts/bc_control.sh --set writeback <cachename> # set writeback mode (optional)

Buffer flushing example:

         # ../../scripts/bc_control.sh --set writethrough bitcache0
         ../../scripts/bc_control.sh: bitcache0: setting cache mode to writethrough
         # ../../scripts/bc_control.sh --set flush bitcache0
         ../../scripts/bc_control.sh: bitcache0: flushing dirty blocks
         ../../scripts/bc_control.sh: bitcache0 has 215638 dirty blocks ...
         ../../scripts/bc_control.sh: bitcache0 has 174766 dirty blocks ...
         ../../scripts/bc_control.sh: bitcache0 has 169450 dirty blocks ...
         ../../scripts/bc_control.sh: bitcache0 has 76161 dirty blocks ...
         ../../scripts/bc_control.sh: bitcache0 has 66613 dirty blocks ...
         ../../scripts/bc_control.sh: bitcache0 has 12047 dirty blocks ...
         ../../scripts/bc_control.sh: bitcache0 has 3245 dirty blocks ...
         # ../../scripts/bc_control.sh --set writeback bitcache0
         ../../scripts/bc_control.sh: bitcache0: setting cache mode to writeback
