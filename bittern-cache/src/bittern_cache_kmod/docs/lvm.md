# LVM Integration {#lvm}

[TOC]


## Existing dm-cache structure {#dmcache}

Each node represents a logical volume in the same volume group.

\dot digraph dm_cache {
    user [label=<user-facing block device<BR />segtype = <FONT FACE='mono' POINT-SIZE='11'>cache</FONT>>];
    pool [label=<cache pool<BR />segtype = <FONT FACE='mono' POINT-SIZE='11'>cache-pool</FONT>>];
    backing [label=<backing store<BR />segtype = <FONT FACE='mono' POINT-SIZE='11'>striped</FONT>>];
    meta [label=<cache metadata<BR />segtype = <FONT FACE='mono' POINT-SIZE='11'>striped</FONT>>];
    data [label=<cache data<BR />segtype = <FONT FACE='mono' POINT-SIZE='11'>striped</FONT>>];
    disk [lable=<raw storage>];

    edge [fontname="mono" fontsize="11"]

    user -> pool [taillabel="cache_pool" labeldistance=5 labelangle=-30];
    pool -> meta [taillabel="metadata" labeldistance=5 labelangle=-30];
    pool -> data [taillabel="data" labeldistance=4 labelangle=30];
    meta -> disk;
    data -> disk;

    user -> backing [taillabel="origin" labeldistance=4 labelangle=30];
    backing -> disk;
}
\enddot

### From the user's perspective {#dmcache_user}

These are displayed to the user via the LVM tools as

    # lvs -a bc
    LV                 VG   Attr       LSize  Pool   Origin             Data%  Move Log Cpy%Sync Convert
    [lvol0_pmspare]    bc   ewi-------  4.01m                                                           
    [pool]             bc   Cwi---C--- 16.00m                                                           
    [pool_cdata]       bc   Cwi---C--- 16.00m                                                           
    [pool_cmeta]       bc   ewi---C---  4.01m                                                           
    userfacing         bc   Cwi-a-C--- 64.00m [pool] [userfacing_corig]                                 
    [userfacing_corig] bc   -wi------- 64.00m          

Additionally, the `_cdata`, `_cmeta`, and `_corig` devices are also loaded at
runtime as device-mapper devices, and the `userfacing` device is set up with a
device-mapper table that references those by block-device major and minor:

    # dmsetup table
    bc-userfacing: 0 131072 cache 253:3 253:2 253:4 128 1 writethrough mq 0
    bc-userfacing_corig: 0 131072 linear 7:0 51248
    bc-pool_cdata: 0 32768 linear 7:0 18480
    bc-pool_cmeta: 0 8216 linear 7:0 10264

These hidden logical volumes are visible as `/dev/dm-*` and as symlinks in
`/dev/mapper/*`, but they are excluded by the existing LVM udev rules from
being symlinked into `/dev/vgname/lvname`, `/dev/disk/by-uuid-*`, etc.  This is
particularly important as the `_corig` volume will have the datastructures that
appear as a normal filesystem but will corrupt data if any cache blocks are
currently dirty.

### Internal runtime data structures {#dmcache_internal}

Internally to LVM, the "cache data" LV is attached as the first (and only) data
segment in the cache pool's `struct logical_volume`; the "metadata" LV is
instead attached by reference on the same segment's `metadata_lv` member.  See
`attach_pool_data_lv()` and `attach_pool_metadata_lv()` in the LVM source for
reference.

However, as stored in the VG metadata, they are both simply referenced by name
in the metadata for a `cache-pool` segment type:

    bc {
    id = "M7QWRW-eLN1-jWZA-4PAs-ZSHU-KxZB-qu0eh1"
    seqno = 137
    format = "lvm2"
    status = ["RESIZEABLE", "READ", "WRITE"]
    flags = []
    extent_size = 8
    max_lv = 0
    max_pv = 0
    metadata_copies = 0
    
    physical_volumes {
    
    pv0 {
    id = "HJiZ17-IdxK-jFxE-RPua-8xrT-dKtm-2ynhTz"
    device = "/dev/loop0"
    
    status = ["ALLOCATABLE"]
    flags = []
    dev_size = 262144
    pe_start = 2048
    pe_count = 32512
    }
    }
    
    logical_volumes {
    
    userfacing {
    id = "Pgbca2-ihc1-e90Y-N9x4-UhJv-F6QG-YiZkwt"
    status = ["READ", "WRITE", "VISIBLE"]
    flags = []
    creation_host = "fedora"
    creation_time = 1425493942
    segment_count = 1
    
    segment1 {
    start_extent = 0
    extent_count = 16384
    
    type = "cache"
    cache_pool = "pool"
    origin = "userfacing_corig"
    }
    }
    
    pool {
    id = "HxWZRy-sNKF-xGZy-3Ivm-LtZb-MfZR-LB2GRt"
    status = ["READ", "WRITE"]
    flags = []
    creation_host = "fedora"
    creation_time = 1425493928
    segment_count = 1
    
    segment1 {
    start_extent = 0
    extent_count = 4096
    
    type = "cache-pool"
    data = "pool_cdata"
    metadata = "pool_cmeta"
    chunk_size = 128
    cache_mode = "writethrough"
    policy = "mq"
    }
    }
    
    lvol0_pmspare {
    id = "5SLhcy-SmWp-9e8B-0Zes-c20A-pEXR-xeF35f"
    status = ["READ", "WRITE"]
    flags = []
    creation_host = "fedora"
    creation_time = 1425493928
    segment_count = 1
    
    segment1 {
    start_extent = 0
    extent_count = 1027
    
    type = "striped"
    stripe_count = 1
    
    stripes = [
    "pv0", 0
    ]
    }
    }
    
    pool_cmeta {
    id = "bRYHOg-MG8id = "bRYHOg-MG8R-D3q1-0sYY-Tf1L-VJQr-67cIfu"
    status = ["READ", "WRITE"]
    flags = []
    creation_host = "fedora"
    creation_time = 1425493928
    segment_count = 1
    
    segment1 {
    start_extent = 0
    extent_count = 1027
    
    type = "striped"
    stripe_count = 1
    
    stripes = [
    "pv0", 1027
    ]
    }
    }
    
    pool_cdata {
    id = "tGcVk6-qJml-7GyH-az29-ASCC-lWX8-zcGaHX"
    status = ["READ", "WRITE"]
    flags = []
    creation_host = "fedora"
    creation_time = 1425493928
    segment_count = 1
    
    segment1 {
    start_extent = 0
    extent_count = 4096
    
    type = "striped"
    stripe_count = 1
    
    stripes = [
    "pv0", 2054
    ]
    }
    }
    
    userfacing_corig {
    id = "6blVQW-gHo5-5W0S-Wuhi-Hwse-uCYn-l0cAIL"
    status = ["READ", "WRITE"]
    flags = []
    creation_host = "fedora"
    creation_time = 1425493944
    segment_count = 1
    
    segment1 {
    start_extent = 0
    extent_count = 16384
    
    type = "striped"
    stripe_count = 1
    
    stripes = [
    "pv0", 6150
    ]
    }
    }
    }
    }
    # Generated by LVM2 version 2.02.117(2)-git (2015-01-30): Wed Mar  4 10:32:24 2015
    
    contents = "Text Format Volume Group"
    version = 1
    
    description = ""
    
    creation_host = "fedora"        # Linux fedora 4.0.0-rc1-00151-ga38ecbbd0be0 #289 SMP Mon Mar 2 14:13:43 PST 2015 x86_64
    creation_time = 1425493944      # Wed Mar  4 10:32:24 2015

## Proposed bittern-cache LV structure {#bitterncache}

\dot digraph bittern_cache {
    user [label=<user-facing block device<BR />segtype = <FONT FACE='mono' POINT-SIZE='11'>cache</FONT>>];
    pool [label=<cache pool<BR />segtype = <FONT FACE='mono' POINT-SIZE='11'>bittern-pool</FONT>>];
    backing [label=<backing store<BR />segtype = <FONT FACE='mono' POINT-SIZE='11'>striped</FONT>>];
    data [label=<cache data<BR />segtype = <FONT FACE='mono' POINT-SIZE='11'>striped</FONT>>];
    disk [lable=<raw storage>];

    edge [fontname="mono" fontsize="11"]

    user -> pool [taillabel="cache_pool" labeldistance=5.25 labelangle=-35];
    pool -> data [taillabel="cache_dev" labeldistance=3.75 labelangle=-60];
    data -> disk;

    user -> backing [taillabel="origin" labeldistance=3.75 labelangle=35];
    backing -> disk;
}
\enddot

We will continue using `attach_pool_data_lv()` as described above, but since
Bittern does not have a separate metadata volume, we will need to skip any of
the code that does operations on the metadata volume --- including attaching it
to the `struct lv_segment` at load time, creating it at lvcreate time,
activating it when the user-facing block device is activated, etc.  Likewise,
existing LVM `cache-pool` LVs have a "chunk size" parameter that do not apply
to Bittern as well.

The integration centers on adding a new segment type in
`lib/metadata/segtype.h` and new LV flags to
`lib/metadata/metadata-exported.h`.  This will add macros
`seg_is_bittern_pool(seg)` and `lv_is_bittern_pool(lv)`, as well as adding the
relevant Bittern case to `*_is_pool(x)`.  To handle the metadata device
differences, as `seg_has_metadata()` will be added and `pool_manip.c` will be
audited for assumptions regarding metadata.

The `cache` segment type will also be modified to create and activate properly
against a `bittern-pool` backend in addition to the existing `cache-pool`.
Most of these changes are in `lib/metadata/cache_manip.c` and
`lib/cache_segtype/cache.c`.

From the user perspective, creating a Bittern cache volume will act very
similarly to creating a LVM cache volume today:

    # lvcreate --type bittern-pool -L 16M -n pool volumegroup fast_pv
    # lvcreate --type cache -L 1G -n filesystem volumegroup/pool slow_pv
    # lvremove volumegroup/filesystem

The block devices visible in `/dev` will be similar to [above](#dmcache_user),
with the exception that the `_cmeta` logical volume will not exist.  The
exclusion from symlink creation is an existing feature of the LVM udev rules,
which check the "hidden" status of the LV (via the device-mapper cookie in the
uevent), and no Bittern-specific rules will be needed.  It is expected that in
normal operations, users will use `/dev/disk/by-uuid` (or `by-label`, etc.) or
`/dev/volumegroup/logicalvolume`, which are always the correct Bittern
device-mapper target; however, in the case that the Bittern cache is unusable,
users will be able to use `/dev/mapper/lv_corig` to recover data if necessary.

To replace the `-o` parameter to `bc_setup.sh`, the `lvcreate` step or first
activation will cause a cache creation event; any subsequent LV activation
(i.e. by `lvchange` or automatic upon boot) will instead restore the cache.

LVM provides other operations via `lvconvert`, particularly inserting a cache
in front of an existing LV and removing a cache.  These will be tested only
after basic create/delete functionality is finished --- the cache removal may
involve work on the Bittern kernel<->userspace interface regarding
synchronously flushing dirty data.
