# Design Overview {#design_overview}

This section is meant to provide a very brief overview of the most important Bittern subsystems. Each subsystem is explored in detail in separate documents.
This simplified diagram shows the most important Bittern modules and how they related to each other. If you are reading this on a text terminal, please resize it to 120 columns.


               +-----------+                                 +---------------+
               |   cache   |    +----------------------+     |  dirty block  |
           +---*  manager  *----*  metadata core copy  *-----*    bgwriter   |
           |   +-----------+    |   rb tree index      |     +-*-------------+
           |                    |   free list,         |       |
           |   +-----------+    |   dirty/clean lists  |       |
           |   | deferred  *----*                      |       |
           |   |  queues   |    +-------*--------------+       |
           |   +-----------+            |                      |
           |                            |                      |
           |          +-----------------*----------------------*--------+
           +----------*              PMEM_API                           |
                      +-----------------*-------------------------------+
                                        |
                                        *
                                 (PMEM hardware)


## Data Structures
One large data structure @ref bittern_cache holds all other data structures and references. The most important structures are:
* the array of metadata blocks @ref bittern_cache::bc_cache_blocks described by @ref cache_block struct.
* a red-black tree used for fast lookups @ref bittern_cache::bc_rb_node.
* invalid/valid, clean/dirty linked lists for easy access to cache blocks
@ref bittern_cache::bc_invalid_entries_list, @ref bittern_cache::bc_valid_entries_list,  @ref bittern_cache::bc_valid_entries_clean_list, @ref bittern_cache::bc_valid_entries_dirty_list.

## Cache Manager
The cache manager is the core of Bittern. Its main entry point is via the DM map callback function, where block IO requests are queued from the block io layer. The general flow is as follows:
* @ref cache_map callback function checks if there are resources to handle the IO request and if the current number of pending requests is below the maximum allowed threshold.
* If insufficient resources are available, or if there too many currently pending IO requests, the request is deferred for later execution via the deferred queue thread subsystem.
* A cache lookup is made. If a cache block is found (cache hit), or if there is no cache block, but there is an available invalid block for a cache fill, the request is queued for immediate execution thru the main state machine. Otherwise the request is queued for later execution.
The bulk of the "Cache Manager" is essentially the main state machine, which handles all the possible state transitions, such as read-miss (invalid -> read-miss-in-progress -> valid-busy -> copy-result -> valid), read-hit (valid -> copy-result -> valid), and so on.
The cache manager handles both *writeback* (default) and write-through cache opertional modes.
In order to minimize the wearout on SSD media and to maximize cache hits, every IO is internally rounded and aligned to the cache block size (currently hardcoded to PAGE_SIZE). Requests which are multiple of PAGE_SIZE are split in independent requests (this is actually done by DM on behalf of Bittern). Requests which less than PAGE_SIZE are translated into full cache block reads, followed by partial cache updates in the case of partial writes.

## Deferred Queues
There are two deferred queues and two corresponding threads which handle request deferrals. Requests can be deferred either because the pending queue (current queue of requests being handled by the state machine) is full, the desired cache block is busy due to another request, or because there are not enough memory resources. Whatever the reason, requests are queued onto one of the two deferred queues. Each of the two threads handling the deferred queue gets woken up either when a request completes, a resource becomes available, or both. The deferred queue thread will then requeue requests to the main state machine. For more information on this topic, please refer to [Deferred Queues](doxy_deferredqueues.md).

## Dirty Block Bgwriter
When operating in writeback mode, dirty blocks are never explicitly written to the cached device. This task is accomplied by the background writer thread.
This thread writes back dirty blocks taking into account the block age and the dirty percentage of the cache. The dirtier the cache, the lower the block age threshold and the higher the writeback rate.

## Other Components
The following important subsystems are also present (not shown above for the sake of clarity):
* *sequential access bypass*: very large sequential scans have the potential of polluting the cache. Bittern detects such scans and automatically bypasses them. The bypass behavior is tunable.
* *performance counters and timers*: Bittern keeps a very extensive set of performance variables, which can be queried via /sys/fs/<cache_name>/. These entries have been formatted to allow for easy parsing and use by user level tools such as bc_stats.pl, which periodically displays statistical information ala iostat.
* *dynamic configuration*: entries in /sys/fs/ allow for dynamic configuration (*writeback* or writethrough, cache replacement modes *random*, LRU, FIFO). configuration changes are made via bc_control.sh.
* dynamic tuning: there are several tunable variables (controlled via bc_control.sh)  which allow to change default behaiors, such as writeback aggressivness, maximum number of pending requests, etc., etc.
* *tracing and debugging*: tracing is dynamically configurable. debug builds also make extensive use of BUG_ON code. it's also possible to "dump" lists of pending requests, clean/dirty blocks into the kernel log.
* *verification*: the amount of checksumming is dynamically tunable - some checks can be disabled for instance in the case of NVDIMM cache hardware. it's also possible do enable/disable a verifier background thread, which keeps verifies the consistency of clean cache blocks against the storage being cached.
* *setup and teardown code*.
