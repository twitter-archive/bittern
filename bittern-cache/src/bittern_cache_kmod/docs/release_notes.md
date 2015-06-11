# Release Notes {#release_notes}

[TOC]

## Current stable release

The current stable release is version 0.25.10, nicknamed "El Bosque del Apache".
(Bitten releases are nicknamed after U.S. National Wildlife Refuge
wetlands where the Bittern bird can be sometimes found).

The release under development is version 0.26, nickanamed "Klamath".

Because of known missing features like error handling, known state
machine issues with write handling, this is not intended to be a production
release.
It is however suitable for use in stress testing, performance evaluation
and non-production workloads such as write-only mirroring of live traffic.
The pending list of issues below refers to the most recent development version.

## Current dev branch

The current dev branch is version 0.26.x, nicknamed "Klamath".

## Issues to be fixed by Production

The following major issues must all be fixed in order for Bittern to become
production ready:
* *Error handling* currently unimplemented. The code will currently
  sane crash the machine whenever any kind of IO error or during some
  memory allocation in critical sections.
* *Target Device Flushing* The target device (i.e. the device being cached,
  typically a hard disk drive) needs to be properly flushed, that is, SEQ_FLUSH
  must be implemented.

An explicit non-goal for the first release is the flushing of the cache device,
which is assumed to be power-fail safe.

## Issues to be fixed "as soon as possible"

The following issues need to be addressed with a certain urgency but are not
critical for production use. Most of these issues should be fixed before the
Release 2. Some of these issues might be fixed by Release 1 if time allows and
if the fix is deemed to be very low-risk.

* *Cache Device Flushing* This will allow using NAND-flash devices which are
  not power-fail-safe as cache device.
* WRITE_SAME support ?
* REQ_FLUSH support ? Probably yes if it makes sense performance wise
  by the time we implement HDD flushing (and in any case let user tune it).
* TRIM support. Performance wise, it only really makes sense for
  NVDIMM, as we would need to erase the whole cache.
* *Deep Architecture Dive* Bittern has so far received internal scrutiny by
  "insiders" and as such it has not yes reviewed the impartial scrutiny it
  needs.
* *Replacement Algorithms* The current default replacement algorithm (random)
  is only applied during cache evictions,
  whereas the background writer follows a FIFO approach. The fix is actually
  fairly straightforward, and the amount of time is mostly bound by performance
  testing.
  Another currenty missing feature is the lack of a better algorithm than
  FIFO and LRU. The possible targets are ARC, a modified version of LRU2,
  or possibly even the same algorithm used by the virtual memory (the "clock"
  hand algorithm, formally known as "last chance").
  ARC is quite possibly the best replacement strategy overall, but it's
  distributed with the MIT license. Having a good-enough GPL version would
  probably be a good choice for customers who do not wish to deal with the
  hassle of dual licensing.
* *Statistics Collection* We need to have a reliable way to automatically
  collect all Bittern statistics on a periodic basis.
  This will be very important for troubleshooting and performance analysis.
  Bittern currently has a Performance Copilot plugin, but its reliability is
  and ease of use is unknown.
* *Statistics* Although not essential, it would be nice to be able to
  zero the cumulative statistics. The stats counters also need to become
  64 bits instead of the current 32 for most counters.
* *Memory Allocation* Right now a double buffer is always allocated for
  every request. In most cases it is actually not needed, and this needs
  to be optimized. It should not be allocated at all when using DAX.
* *Coding Style* For yet another set of historical reasons Bittern code has
  a lot of warts which need to be addressed, including coding style and
  excessive reinvention of the wheel.
  This is probably best done in a safe and slow incremental fashion.
* *XID Rollover Catastrophe* This is akin the 32 bits time overflow
  except that the XID is a 64 bit quantity. XID numbering is used to determine
  which version of a dirty block is the most recent one, and hence it is
  critical that rollover be handled correctly (ala TCP sequence numbers).
  At a constant rate of about 10 million IOPS per cache, this needs to be
  implemented by the year 3315. YMMV once you account for Moore's Law.

## Future major features of Bittern

The following major features are in various stages of design/planning for the
next version of Bittern. This list is obviously subject to changes based
on the experience that will be gathered in production environment as well as by
what kind of hardware becomes available.

* *Performance Tuning* There is a lot of room for improvement in the writeback,
  invalidation and cache replacement algorithms.
* *Optimize Cache Fills* Right now data and metadata are updated in a serialized
  fashion, in two separate IO operations. Provided that some rules are followed,
  it is possible to optimize this into one single IO operation.
  This should optimize latency as well as being a foundation upon to build
  support for larger than 4k sector sizes or their functional equivalent.
* *Support for 4160 sector size* With 4160 bytes sector size data and metadata
  can be very efficiently transferred in one single IO operation, and most
  importantly this would make the metadata overhead the smallest possible.
* *Support for user-writeable DIF* The equivalent goal of a 4160 bytes sector
  can be achieved with hardware which would support user-writeable metadata
  instead the standard DIF checksum.
* *Configurable Cache Size* The current cache block size is hardcoded to
  PAGE_SIZE. This is highly inefficient on block devices as it results in 50%
  metadata overhead. One approach to address this issue and boost performance
  is to allow to configurable cache block sizes. For instance with 4k pages and
  32k bytes block size the metadata overhead drops to about 10%.
* *RAID Write Hole* This is a priority effort that requires more scoping.
  On a first glance, it appears possible to
  avoid the RAID write hole by matching the cache block size with the RAID
  stripe size. Bittern provides a "at-least-once" writeback semantics, so as
  such a crash recovery scenario would eventually force the replay of all
  previously pending writeback operations, thus forcing RAID "parity"
  recalculation. A trivial solution like this would
  be ideal as it would not require any interdependency with the RAID layer.
  The other possible advantage is that in this scenario the RAID layer would
  no longer need to keep a dirty chunk bitmap, as all writes are guaranteed
  to be eventually replayed.
  An alternative to the above is letting the RAID layer solve this problem.
  A Mixed alternative is also probably possible but also needs to be scoped.
  Note that all types of RAID devices needs to be addressed: the write hole
  issues applies to all raid levels except for RAID0 (most notably, it applies
  to RAID1 and not just to RAID5).
