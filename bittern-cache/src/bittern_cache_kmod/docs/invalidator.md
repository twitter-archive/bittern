# Invalidator {#invalidator}

The purpose of the Invalidator is to keep a minimum number of invalid blocks
so that they are immediately available for cache fills without having to do
an "inline" invalidation of the cache block allocated to said cache fill.

## Need for Invalidator

Suppose that a cache eviction and a subsequent fill is done without
any intermediate invalidation. The flow in such case would be:

* 1 - cache block X is caching data block A, with content A'
* 2 - evict cache block X and reallocate to B
* 3 - fill X with B's content B'
* 4 - update X metadata to B

If we crash anytime during or after #4, then we are safe, as X has metadata
for B and is correctly filled with B'.
If we crash sometime either before #4 completes, then we'll have cache block X
with metadata for A, but with B' data.
Upon restore if crc32c(A') == crc32c(B'), we'll have data corruption.

By converting a cache eviction into a sequence of independent invalidation
and cache fills, we avoid the problem altogether:

* (invalidator thread) 1 - cache block X is invalidated
* 2 - cache block X is allocated to block B
* 3 - cache block X is filled with B'

## Theory of Operation

The Invalidator thread is awoken every time we are close to the low threshold
of invalid blocks. Once awakened, the thread will invalidate as many blocks as
needed in order to go again above the low threshold.
