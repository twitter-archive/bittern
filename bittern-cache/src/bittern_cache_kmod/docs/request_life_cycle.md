# Request Life Cycle {#request_life_cycle}

There are three types of requests which affects the state of Bittern cache:
* explicit IO requests, initiated by device-mapper,
  as a result of either user block IO requests or file
  system IO requests.
* writeback IO requests, initiated by the Background Writer kernel thread.
  These requests are initiated in order to flush dirty data back to the
  cached device.
* invalidation requests, initiated by the Invalidator kernel thread.
  These requests are initiated in order to keep a sufficient number of invalid
  blocks for new cache fills.

The general flow of an IO request is as shown below:

~~~~~~~~~~~~~~

                       +-----------+
                       | start IO  |
                       +-----+-----+
                             |
                             V
                             +------<------------+
                            /*\                  |
                           /   \                 |
                          /     \                ^
                         /  need \               |
                        /resource \---------->   |
                        \    ?    /          |   |
                         \       /  +--------+---+----+
                          \     /   | wait for        |
                           \   /    | resource        |
                            \*/     +-----------------+
                             |
                             V
                      +------+-------+
                      |   bittern    |
                      |    state     |
                      |   machine    |
                      +------+-------+
                             |
                             V
                       +-----+-----+
                       | IO  done  |
                       +-----------+

~~~~~~~~~~~~~~

## Explicit IO Requests

The main entry point for device-mapper initiated requests is
@ref cache_map:

~~~~~~~~~~~~~~

        cache_map(request)
        {
                allocate work item for request;
                if (current pending requests >= max pending pending requests) {
                        defer request to page queue;
                }
                try allocate page buffer for request;
                if (could not allocate page buffer for request) {
                        defer request to page queue;
                } else {
                        call cache_map_workfunc() to execute request;
                }
        }

~~~~~~~~~~~~~~

@ref cache_map either queues the request for deferred execution if there
are not enough resources (no available buffer pages or
too many pending requests)
or calls @ref cache_map_workfunc to "execute" the request.

One reason the maximum number of pending requests is limited is because there
is no performance benefit having more pending requests than the IO subsystem
can handle. This also puts an upper bound to the latency that a single request
can have (although this won't be seen by the caller because all the caller cares
is the total request latency).
It should be noted here that a @ref work_item struct is always allocated,
which is bad.
A better way to handle this would be to suspend the caller's elevator queue
when we cannot accept new requests (in a shmitt trigger fashion).

The other reason why the maximum number of pending requests is limited is
to avoid having to allocate too many page buffers for intermediate copies.

Either via @ref cache_map, or via deferred thread handling
@ref handle_deferred, @ref cache_map_workfunc
is eventually called:

~~~~~~~~~~~~~~

        cache_map_workfunc(request)
        {
                lookup request in cache;
                if (lookup result == HIT_IDLE) {
                        cache hit,
                        handoff request to state machine;
                } else if (lookup result == HIT_BUSY) {
                        cache hit busy,
                        defer request to busy queue;
                } else if (lookup result == MISS_INVALID_IDLE) {
                        cache miss,
                        handoff request to state machine;
                } else { /* MISS */
                        cache miss, no free invalid blocks,
                        defer request to busy queue;
                }
        }

~~~~~~~~~~~~~~

First the requested cache block is looked up in the cache. There
are 4 possible outcomes:
* HIT_IDLE: cache block has been found and is idle.
  This is a cache hit, handoff request to state machine.
* HIT_BUSY: cache block has been found but is idle.
  Defer request to the busy queue for later execution.
* HIT_MISS: cache block has not been found,
  but an invalid block has been allocated for the requested
  block.
  This is a cache miss, handoff request to state machine.
* HIT_MISS_NO_BLOCK: cache block has not been found,
  and there are no invalid blocks available.
  Defer request to the busy queue for later execution.

Requests deferred to the busy queue will be tried again as cache blocks
become available. This is a fairly rare case.

Requests queued to the state machine has all necesssary resources for
completing the requests, and thus will executed until completion. Once
the request is completed, the original block IO request will be acknowledged
and associated resources released.

## Invalidation and Writeback Requests

These requests are handled by the Invalidator and Background Writer threads,
respectively.
Resource waiting is simpler by virtue of being within a thread context,
so for instance a deferral mechanism is not necessary.
The state machine handling of these requests is virtually identical.
