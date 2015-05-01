# Incore Layout {#incore_layout}

## bittern_cache struct

Every instance of Bittern Cache is completely self contained and fully
described by @ref bittern_cache structure. Its most important structs
and subfields are as follows:

* @ref bc_papi this struct contains the PMEM_API instance, which is used
  to access the cache hardware.
* @ref bc_xid transaction identifier.
* @ref bc_cache_blocks a linear array of @ref cache_block.
  Each entry fully describes a cache_block.
* @ref bc_rb_root root of a red-black tree used for direct cache block lookup.
* @ref bc_ti device-mapper target for this cache.
* @ref bc_dev device-mapper device of the device being cached.
* @ref bc_invalid_entries_list doubly linked list of invalid entries.
* @ref bc_valid_entries_list doubly linked list of valid entries.
* @ref bc_valid_entries_clean_list doubly linked list of valid clean entries.
* @ref bc_valid_entries_dirty_list doubly linked list of valid dirty entries.

A cache block is in one or more lists and in the red-black tree if valid:
* INVALID blocks are accessed only via the invalid entries list.
* VALID_CLEAN blocks are accessed via the red-black tree, the valid list and
  the clean-valid list.
* VALID_DIRTY blocks are accessed via the red-black tree, the valid list and
  the dirty-valid list.

Each @ref cache_block contains the corresponding data structures used
for linking it to the above mentioned lists and tree. Because a block can either
be invalid or valid, but not both, we use the same list entry to describe that.

## cache_block struct

There is a 1:1 correspondence between each cache block and an instance of
@ref cache_block structure. This is organized as a simple linear array,
which can either be accessed as follows:
* red-black tree for direct block lookup @ref cache_block::bcb_rb_node
* linked list for valid or invalid access @ref cache_block::bcb_entry
* linked list for valid clean or dirty access
  @ref cache_block::bcb_entry_cleandirty
* direct access by block identifier @ref cache_block::bcb_block_id.
  Note that bcb_block_id indexing starts from 1, not zero.
  Direct access is used by auxiliary debug functions and, most importantly,
  for the random replacement algorithm.
* The cache block metadata is fully replicated in this structure. This allows
  efficient use non-memory addressable devices such as NVMe.

## Synchronization

The serialization model is quite simple:
* @ref bittern_cache::bcb_entries_lock serializes access to the above
  described linked lists and red-black tree.
* @ref cache_block::bcb_spinlock serializes access to the
  desired @ref cache_block instance.
Any given cache block is considered "busy" if the cache state is "transient" or
if @ref cache_block::bcb_refcount is greater than zero.

@ref cache_block is acquired as follows:

~~~~~~~~~~
        has_block = false;
        lock(bc_bcb_entries_lock)
        lookup_block
        if (found) {
                lock(bcb_spinlock);
                ret = atomic_inc_return(bcb_refcount);
                if (ret == 1)
                        has_block = true;
                else
                        atomic_dec(bcb_refcount);
                unlock(bcb_spinlock);
        }
        unlock(bc_bcb_entries_lock)
~~~~~~~~~~

Note the acquire lock ordering is (1) global lock (2) per-instance lock.
Relase is done in reverse ordering.

Once a cache block is successfully acquired, the owner is free to
modify its state if necessary. A transient state also indicates that the block
is in use.

## work_item struct

An instance of @ref work_item structure is allocated for each pending
cache transaction, wether it be due to explicit IO request, writeback or
invalidation operation. With the exception of deferred queues and pending
queues, the fields in @ref work_item are implicitly serialized by the associated
@ref cache_block struct. Refer to documentation of @ref work_item
fields for more details.
