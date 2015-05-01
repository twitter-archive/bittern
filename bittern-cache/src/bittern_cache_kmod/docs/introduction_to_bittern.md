# Introduction to Bittern {#introduction_to_bittern}

**Bittern Cache** is a writeback caching technology designed to use persistent
non-volatile memory technologies ("Persistent Memory", or **PMEM**)
such as NVDIMM, NVRAM, NVMe, PCIe-NVRAM and SSD as a disk cache on
slower storage media such as traditional magnetic storage.
If NVDIMM or PCIe-NVRAM is used,
then either SSD or magnetic storage can be cached.

## Primary Design Goals

* Drop-in replacement for hardware RAID using the standard Linux RAID stack.
  Enable storage acceleration using commodity hardware instead of
  hardware RAID proprietary solutions.
* Best-in-class performance and reliability on advanced storage technologies
  such as NVDIMM and NVMe.
* Very good performance and reliability on other NAND-flash
  technologies such as SSD.

## Background

NVDIMM, NVMe and NVRAM technologies are rapidly evolving and we believe
that at this point they are both mature and cheap enough that we will be able
to have a full open-source software stack solution (Bittern + Linux RAID)
which uses PMEM to replace HW RAID.

By taking full control of the RAID and writeback cache stacks we expect to
lower costs of storage technologies while keeping similar
performance characteristics. By producing and using open source,
we can accelerate evolution by accepting bug fixes and new features
from a larger community (in contrast to the current state of hardware RAID
where we are completely dependent on manufacturers for bug fixes).
Another significant advantage of Bittern will be operations and management
in that it allows the use of standard MD/LVM Linux tools to manage and
administer the underlying RAID storage array instead of
tools which come with hardware RAID.

Existing caching solutions such as [Facebook Flashcache][1],
[Bcache][2] and [DM-Cache][3] do a very good job in using SSD media
to cache magnetic media, but are not designed to take advantage of
NVDIMM and NVRAM technologies which are as fast as standard DDR memory.
While SSD provides for fairly large storage (in the order of 100s of Gbytes),
NVDIMM and NVRAM solutions provide storage in the order of few Gbytes
(4 Gbytes to 16-32 Gbytes typically).
Therefore the ratio of the cache size to the storage being cached is very small,
about 1:1000 compared with SSD 1:100.
The implication is that caching algorithms and write-back strategies used for
NVDIMM/NVRAM must be tuned towards being more of a write buffer than
a general cache, due to the much lower hit-ratio.
Another substantial difference is that NVDIMM and NVRAM can be used
to cache SSD storage as well.

## Overview

Bittern is a loadable device-mapper kernel module and does not require
any changes to the kernel source. It currently runs (and is tested on)
Linux 3.14.8, 4.0.RC2 and later versions.
Bittern accesses PMEM hardware either indirectly via standard block IO
functionality or direct memory access via Intel's DAX primitive.
The creation, deletion, and administration of a bittern volume is done via
wrapper scripts which load the proper drivers and invoke dmsetup to create a
virtual volume on top of the volume being cached.
From the point of view of the user, the bittern volume is then exported as a
/dev/mapper/ volume, and it's accessible just like any other block device.

Bittern abstracts access to the underlying cache hardware via an
internal set of APIs called PMEM_API.
This abstraction allows for hardware independent access while maintaining
the maximum efficiency possible for the case of direct memory access.

## Architecture, In Brief

Cache hits are handled directly by accessing the persistent memory via the
internal PMEM_API. Write hits need cloning in order to maintain WAW
(write-after-write) and RAW (read-after-write) crash consistency
(more on this later).
Upon read cache miss, data is loaded into cache from the device being cached,
and then returned to the caller. A bypass mechanism for sequential accesses
avoids cache pollution in the case of large sequential scans
(e.g. reading a large archive file, restoring a database).
All dirty pages are written out asynchronously by a
background-writer kernel thread.

Currently, three cache replacement algorithms are supported,
LRU, FIFO and RANDOM.
At least one more replacement algorithm will be added,
a modified version of either ARC or LRU2.
Both ARC and LRU2 provide significant performance advantages over traditional
LRU and FIFO algorithms in the case where Bittern
is essentially a 2nd level cache which has to serve all the cache misses of
the 1st level cache (OS page cache).

Crash consistency is the most important part of Bittern design.
The goal here is to provide completely transparent support and the same
semantics as durable storage.
In other words, when I/O requests are acknowledged, we guarantee that they are
(for all intents and purposes) on "stable storage". Even stronger,
every single request is guaranteed to be atomic at least at the
cache block size level (PAGE_SIZE).
The cache store keeps both data and the associated metadata which describes it.
Due to the small number of cache objects and cache size,
a separate in-memory copy of the metadata is also kept at all times
for fast lookup operations. Both data and metadata use a crc32c checksum
which is used to verify both the data and metadata integrity upon restoration.

All-or-nothing semantics is achieved by ordering data and metadata updates.
A write miss, for instance, is handled as follows: a cache block is allocated
from the free pool, possibly evicting previous data.
First the data is written into the cache.
Then the metadata block is written out with both the data crc32c and
its own crc32c checksums. Because we rely on checksums for data integrity,
a crash in the middle of data or metadata update simply means
that we will have crc32c checksum errors, in which case the data is not restored
upon crash recovery.
The original I/O request is acknowledged after both data and metadata
have been written to the cache.
This means that the requirements for the cache hardware are minimal:
the only real requirement is that a write, once complete,
is guaranteed to be fully stored in NVDIMM and/or NVRAM,
with all of the CPU caches and write buffers fully flushed.
For NVDIMM, we accomplish this by using Intel SSE non-temporal writes
followed by memory fence instruction.
For PCIe NVRAM, we depend on the hardware card to provide these semantics.

Although we do not require any atomicity semantics from the cache hardware,
Bittern design allows us to efficiently take advantage of such properties
if present. We hope that future hardware will provide such semantics.

The most difficult case for crash consistency is a dirty-write-hit.
Suppose we have two consecutive writes on the same block, DATA_T0 and DATA_T1.
In order to maintain WAW (write-after-write) and
RAW (read-after-write) semantics,
we cannot simply overwrite the data block because in the event of a crash
we could end up with a partially written update, and thus with data corruption.
Therefore, we first *clone* the metadata and data blocks
(essentially a copy-on-write, called cloning to avoid confusion with VM COW),
then we update the new block with the new data.
Each step is by itself atomic as described above.
Once the newer copy is safely committed to the cache,
the old copy is invalidated.
Furthermore, each I/O request has a monotonically increasing logical timestamp,
or transaction id.
In the worst case we'll crash right after committing the newer update,
but before we invalidate the old copy.
The restore code detects this by comparing the logical timestamps.
If there are multiple copies of the same block with different timestamps,
only the newest copy is restored.

[1]: https://github.com/facebook/flashcache/blob/master/doc/flashcache-doc.txt
[2]: http://bcache.evilpiepirate.org/
[3]: http://visa.cs.fiu.edu/tiki/dm-cache

