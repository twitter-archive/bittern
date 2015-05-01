# Background Writer {#background_writer}

The Background Writer, "bgwriter", flushes dirty cache blocks from the cache
to the cached device. After flush completes the state of the cache
block changes from DIRTY to CLEAN, indicating that the cached device
content is up-to-date with the cache content.

The writeback rate and the choice of which dirty cache block should
be invalidated is critical for the performance of a writeback cache.
If invalidation is too aggressive we risk having to writeback the same
dirty block once for each "write" on the block.
If invalidation is too slow we risk running out of clean or invalid cache
blocks which are needed to do new cache fills (handling of cache misses).

There are thus several variables that are used to determine when to write
out a dirty block and how many blocks to write out.

These are the variables which are currently used:
* Cache dirtiness: The dirtier the cache, the faster the writeback rate is.
* Age: Every cache block has an associated minimum age. Dirty blocks are only
  flushed out when the minimum age is reached. The age is also adjusted based
  of the cache dirtiness.
The bgwriter code takes cache dirtyness into account and generates three
parameters which control the writeback rate:
* writeback queue depth, @ref bittern_cache::bc_bgwriter_curr_queue_depth
* writeback rate per second, @ref bittern_cache::bc_bgwriter_curr_rate_per_sec
* cache block minimum age, @ref bittern_cache::bc_bgwriter_curr_min_age_secs

Currently these parameters are computed based on a completely empirical formula.
Also, not all possible input parameters are considered. For instance,
the current queue depth on the cached device should be taken into account in
order to reduce the impact of the writeback rate over explicit cache device
requests (in this case, sequential bypasses and read misses).
The current bgwriter code attempts to address these limitations by allowing
runtime selectable writeback policies.

What appears to be a far better approach would be to put the cache policy
calculation in a tabular format (for instance one table could give the writeback
rate based of the dirty ratio) which could be computed by user code and then
inserted via /sys/fs/ or dmsetup messages.
The advantage of such approach over the current hard coded approach is that it
would allow far greater flexibility in experimenting with different writeback
policies safely at runtime while also being able to measure the policy change
results immmediately.

## Pitfalls

Currently the bgwriter selects the first dirty block from the dirty block
list head. The dirty block selection should be based on which replacement
strategy has been selected.


## Invalidator Interaction

Under heavy cache block pressure a flushed clean block will almost immediately
be invalidated. In order to avoid such case in which the same cache block
metadata is needlessly updated twice, the Background Writer will flush the block
and then mark it immediately as invalid. This avoids the double metadata update
as well as having to awaken the invalidator thread.
