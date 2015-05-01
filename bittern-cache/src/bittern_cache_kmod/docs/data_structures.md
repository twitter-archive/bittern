# Data Structures {#data_structures}

This page is under development.

## bittern_cache

The structure bittern_cache is the "mother of all data structures" in Bittern
and it contains everything else, either as direct members or as pointers to
structures or arrays.
There is one dynamically allocated instance of bittern_cache for each cache
instance.

* `pmem_header bc_pmem_header` : in-core copy of the cache superblock.

* `atomic_t bc_invalid_entries` : number of invalid cache entries.

* `atomic_t bc_valid_entries_clean` : number of valid clean cache entries.

* `atomic_t bc_valid_entries_dirty` : number of valid dirty cache entries.

* `atomic_t bc_total_entries` : total number of cache entries
(invalid + clean + dirty). 

Note that while the total number of cache entries is fixed and it's always
equal to the sum of invalid entries, valid clean entries and valid dirty
entries, the arithmetic sum will only be equal to the number of total entries
if the cache is in a quiesced state. This because the fields
are not updated atomically in respect to each other.

`cache_block *bc_cache_blocks` : this points to a dynamically
allocated array of cache blocks. There is a one-to-one correspondence between
a cache_block id (1 to N) to an element of this array (0 to N-1).

## cache_block

The cache_block data structure contains the runtime state of the
corresponding cache blocks in the persistent memory, a full copy of the
persistent memory values, and various data structures for linked lists
(clean/dirty valid, all blocks LRU/FIFO list) and
red-black tree for direct access.

## work_item

Whenever an I/O request is queued to Bittern, a work_item structure is
allocated to keep track of its intermediate states thru the state machines and
deferred queues.
I/O requests can either be explicit (via user requests
via @ref cache_map function) or implicit
(via bgwriter writeback requests).
This structure contains tracking information, various linked list,
state machine state and other fields.
