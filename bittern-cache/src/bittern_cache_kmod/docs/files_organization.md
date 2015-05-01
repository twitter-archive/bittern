# Files Organization {#files_organization}

This document explains how functionality is split among the various files.

Include Files
-------------
* bittern_cache.h
  The core Bittern include file.
  Contains all the most important #defines as well as all the major incore
  data structures.
  To simplify inclusion in the C files, this file contains the includes
  which are used almost everywhere.
  @ref bittern_cache is the 'mother of all bittern data structures',
  it contains all of the per-cache configuration, perf counters, and
  references to every other data structure.
  @ref work_item keeps track of all the state and resources necessary
  to handle an IO request - there is one instance of this struct for
  each pending or deferred IO request.
* pmem_header.h
  Core Bittern file for Persistent Memory (PMEM) cache layout
  (the "disk" layout data, if you will).
  @ref pmem_header struct is the cache "superblock" and contains
  the per-cache information such as size, name, number of cache entries.
  @ref pmem_block_metadata struct contains the metadata
  information for any given cache block.
* cache_pmem.h
  The PMEM_API file contains the data structures and the definitions of
  PMEM_API (or legacy PMEM_PROVIDER). Most access to PMEM hardware is done
  via these APIs. The remainder is done via PMEM_PROVIDER APIs.
  The latter will completely deprecated (and merged as necessary into
  other files) with the Ridgefield release.
* cache_states.h
  Cache operations are fairly complex especially in cases such as partial
  writes or partial reads. Every block IO request is handled by Bittern
  via the use of a state machine, and all state transitions are defined here.
* cache_timer.h
  Defines timer structure @ref cache_timer and related funtions.
  Used for statistics collection.
* cache_tunables.h
  Bittern compile-time tunable parameters.
* cache_list_debug.h
  Clone of "linux/list.h". If list corruption is detected, the source file and
  line number will be printed.
* cache_main.h
  Contains definitions pertaining the Cache Manager and the State Machine.
* cache_module.h
  DM module constructor and destructor definitions.

C Files
-------

* cache_bgwriter_kt.c
  Background Writer thread code and auxilliary functions.
* cache_bgwriter_policy.c
  Background Writer policy code.
* cache_bgwriter_policy_experimental.c
  Background Writer experimental policy code. Slated for integration.
* cache_bio_request.c
  Block IO request helper functions.
  Only the verifier thread uses these helper functions.
  Should probably be deprecated at some point.
* cache_daemon_kt.c
  Generic Daemon thread.
  Used for periodic actions such as flushing updated superblock to PMEM.
* cache_debug.c
  Debug functions, most of them called on demand by /sys/fs/ hooks.
  They are used to dump the list of pending/deferred IO operations,
  dirty blocks... Also contains the code which keeps track of all crc32cs
  on the cached device for redundant checking.
* cache_getput.c
  The code code of the Cache Manager which handles redblack tree and
  linked lists insertion/deletion.
  Almost all of the code which manipulates the incore state of the cache
  is here, including the replacement code.
* cache_list_debug.c
  Clone of linux "list.c" which provides extra information in the list
  corruption debug messages, printing the file and line number where
  list_add() and list_del() are called.
* cache_main.c
  Contains the main state machine code @cache_state_machine,
  the DM map entry point @cache_map, and the deferred queue thread.
* cache_main_subr.c
  Contains the code which validates the state transitions as well as
  the @ref work_item allocation/deallocation code.
* cache_module.c
  Contains the code which exports /sys/fs/ entries.
  Most entries are read-only and used to read statistical information,
  operating modes and configuration.
  Some entries are writeable and allow to change some configuration parameters,
  operating mode (WB or WT) as well as dump clean/dirty/pending entries.
* cache_module_ctr.c
  DM constructor code (setup code).
* cache_module_dtr.c
  DM destructor code (teardown code).
* cache_page_buffer.c
  Page cache of buffers used for intermediate copies during IO request handling,
  either user initiated via map entry point or
  initiated by the invalidator thread.
* cache_pmem.c
  PMEM_API interface implementation code.
* cache_redblack.c
  Red-black tree implementation,
  essentially a wrapper for linux red-black tree APIs.
* cache_sequential.c
  Detects and keeps track of sequential access streams.
* sm_pwrite.c
  State Machine code which handles partial cache writes
  (that is, writes which are less than PAGE_SIZE).
* sm_read.c
  State Machine code which handles cache read misses and hits.
* sm_write.c
  State Machine code which handles cache write misses and hits.
* sm_writeback.c
  State Machine code which handles state transitions
  used by the bgwriter thread.
* cache_subr.c
  Debug and tracing code.
* cache_verifier_kt.c
  Verifier thread code.
