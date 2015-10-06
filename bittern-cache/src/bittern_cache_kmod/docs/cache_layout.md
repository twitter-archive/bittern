# Cache Layout {#cache_layout}

## Index-Free

Unlike other caching solutions, Bittern does not use any kind of
centralized index.
The advantage of this approach is that metadata synchronization is not
serialized and there is no need to cluster multiple updates in one single
operation. Also, there is no single point of failure, so the loss of any
block (data or metadata) only impacts one block -- whereas with an indexed
approach you would possibly lose all the blocks described by the metadata in
the lost block.
This approach is designed to work its best with NVDIMM type hardware, that is,
byte addressable caches. It also works very well if the NAND flash devices
allows storing both data and metadata in the same block
(that is made possible by formatting the NAND flash device to 520x8 = 4160 bytes
pages).
In the case of traditional NAND flash devices, e.g. most current SSDs, a steep
penalty is paid in that half of the pages of the SSD are devoted to
store metadata information.
Somewhat surprisingly, this has not proven to be a performance bottleneck yet.
Some rethinking will be needed in this area if the use of traditional
SSDs will prove to be the dominant case.

## Cache Layout

The cache layout is two header "superblocks", followed by
metadata and data blocks, as follows:

        +----+ +-----+  +--------------------+  +---------------------------+
        |hdr0| | hdr1|  |  metadata array    |  |  data array               |
        +----+ +-----+  +--------------------+  +---------------------------+

With the need to use byte-addressable PMEM such as NVDIMM,
Bittern can make no assumptions on update atomicity (a memory copy or any other
non-atomic multi-byte update may terminate
in the middle in the event of a crash). This
also helps in the case of NAND-flash devices which are not power-fail safe and
do not necessarily provide atomic page updates.

Atomicity is achieved by the use of crc32c checksums for headers,
metadata and data.
An update is made by first writing the "payload" portion, and then
updating its crc32c checksum. The update is not considered complete unless there
is a crc32c checksum match. During restore, data and metadata are checked
for crc32c
integrity. Anything that does not have a correct crc32c is considered as a
"transaction in progress" and thus aborted.

This is the reason there are two copies of the superblock.
Only one copy at a time
is updated, and if the crc32c has a mismatch, the other copy is used.
The superblock, described by @ref bittern_cache_pmem_header,
only contains static information (number of cache blocks, layout
type, metadata block size, .....). The only non static information is
the transaction identifier @ref bittern_cache_pmem_header::bc_xid, which is
lazily updated: its updated value upon recovery is simply the highest
transaction identifier found among all valid metadata blocks.

The transaction identifier, XID, is a monotonically increasing logical
timestamp, incremented for each "cache transaction", i.e. any operation which
modifies the cache state.

The metadata block is described by @ref bittern_cache_pmem_block_metadata,
and can be either 64 bytes or PAGE_SIZE size. The former size is used for memory
addressable caches (NVDIMM), whereas the latter is used for block addressable
devices.

## Cache States

The metadata information for each block, described by
@ref bittern_cache_pmem_block_metadata, contains information about the actual
block being cached and its state. There are several cache states, most of them
used by the state machine for state transitions ("transitional states").
The "stable states" are:
* INVALID @ref BITTERN_CACHE_STATE_INVALID
* CLEAN @ref BITTERN_CACHE_STATE_VALID_CLEAN
* DIRTY @ref BITTERN_CACHE_STATE_VALID_DIRTY
Currently, only stable states are updated to the PMEM store, but there is
nothing in the architecture which prevents intermediate updates in case of
byte-addressable devices which can be updated at memory speeds (NVDIMM). In fact
state updates for this case have been done in previous versions of Bittern
and can potentially help troubleshooting in the event of a crash.
The recovery code simply ignores transitional states and simply aborts the
transaction by marking the cache block as invalid.

## Transactional Integrity

All-or-nothing semantics is achieved by ordering data and metadata updates in
such a way that the new update is not considered valid until all the checksums
are updated. Data is first written, followed by a metadata update. The metadata
contains two crc32c checksums,
@ref bittern_cache_pmem_block_metadata::pmbm_crc32c for the metadata itself, and
@ref bittern_cache_pmem_block_metadata::pmbm_crc32c_data for the data block.

To see how transactional recovery works, consider this update sequence:


       update1_data
       update1_metadata
       update2_data
       update2_metadata

Suppose the system crashes in the middle of update2_data. In such case the
metadata block has not been written yet, so the cache block is still considered
invalid (this is because every block is always invalidated before use).
So in this case only update1 transaction is valid. Update2 transaction is
aborted.
Similarly, if a crash occurs during update2_metadata, the metadata information
itself will either be not present at all, be corrupt (potentially with an
invalid state) or have a crc32c mismatch. In all of these cases the transaction
is aborted, and the cache block is marked invalid.

## Dirty Write-hit Handling

The most difficult case for crash consistency is a dirty-write-hit.
Suppose we have two consecutive writes on the same block, DATA_T0 and DATA_T1.
In order to maintain WAW (write-after-write) and RAW (read-after-write)
semantics, we cannot simply overwrite the data block because in the event of
a crash we could end up with a partially written update, and thus with data
corruption. Therefore, we first *clone* the metadata and data blocks
(essentially a copy-on-write, called cloning to avoid confusion with VM COW),
then we update the new block with the new data. Each step is by itself
atomic as described above.
Once the newer copy is safely committed to the cache,
the old copy is invalidated. In the worst case we'll crash right after
committing the newer update, but before we invalidate the old copy.
The restore code detects this by comparing the XID logical timestamps.
If there are multiple copies of the same block with different timestamps,
only the newest copy is restored. Assume for instance we have the following

              +------------+     +------------+
              | block X    |     | block X'   |
              | xid = 12   |     | xid = 2718 |
              +------------+     +------------+

The recovery code looks at the XID, and determines that the X' copy is the most
recent one. The X copy is simply invalidated.

## Pitfalls

* Metadata size for NAND-flash devices is wasteful. If this becomes the dominant
  use case it's probably worth considering optimizations, the most obvious
  being a log-structured approach. Other approaches also include hoping that
  future NAND-flash devices will support efficient updating of sector-sized IO
  requests, in which case the metadata overhead will be minimal.
* Restore is fairly slow because it needs to scan all the metadata.
  For NAND flash devices this implies scanning the full cache.
  It's possible to fairly easily boost restore performance by multi-threading
  the restore process. A log-structured approach would also help.


## Known Bugs
The dirty write-hit case also applies to the clean-hit case,
for the very same reasons. The fix is fairly trivial
(just do write cloning for write-hit case as well in both modes,
writeback and writetrhough).

