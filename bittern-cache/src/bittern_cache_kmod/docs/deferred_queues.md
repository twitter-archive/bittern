# Deferred Queues {#deferred_queues}

## Why Deferred Requests

User-level requests (either directly via block layer or indirectly via file
systems) are queued to bittern via the main *device mapper* (DM) map
function @ref cache_map.

The @ref cache_map allocates a work_item structure, page buffers and
then passes the request over to @ref cache_workfunc.
The latter places a hold on a cache block (for cache hits),
and starts the main state machine work to satisfy the request.

There are circumnstances however when this is not possible, and in such case,
both @ref cache_map or @ref cache_workfunc will defer the
request for later execution in a thread context.

There are several reasons why this happens:

        1. i/o requests which are waiting for busy blocks (wait_busy)
           [ waiting on pending request on same cache block to to complete ]
        2. i/o requests which are waiting for free cache blocks (wait_free)
           [ waiting for a block to be invalid ]
               OR
           [ waiting for a block to be clean ]
        3. i/o requests which are waiting for a free buffer page (wait_page)
           [ waiting for any page buffer ]
        4. i/o requests which are held for the pending queue to become un-saturated (wait_pending)
           [ waiting for any pending request to complete ]
                OR
           [ waiting for a writeback to complete ]

Case 1 is essentially the same as case 2 given that in either case
we are waiting for some request to complete (in case 1 it's a
specific request, in case 2 is any request). So they are both
handled by the same thread and by the same queue.
This case is handled by @ref cache_deferred_busy_kthread thread.

Case 3 is essentially the same as case 4 given that page buffers are
freed up either when a pending request completes or when a writeback
completes. Also, case 3 and 4 will need to allocate a page buffer
for the i/o request. So they are both handled by the same thread and
by the same queue.
This case is handled by @ref cache_deferred_page_kthread thread.

Waiting conditions and wakeup strategy for threads
* For cases 1 and 2: We wait for pending requests to be bumped up.
  In this case requests are deferred either because there are no free
  cache blocks, or if a given cache block is busy. In either case these
  requests can be executed when a cache block frees up, that is,
  when a pending request completes.
  So in this case we need to wait for a change in the number of
  completed requests. this condition is signalled by
  calling @ref wakeup_deferred.
* For cases 3 and 4: We wait for the deferred generation number to be bumped up.
  In this case requests are deferred due to being unable to allocate resources
  in the entry point functions, but they can be scheduled immediately if
  resources are available in the thread context (the page buffer for instance).
  So in this case we want the thread to wake up in two cases:
  each time a request is queued, done implicitly when the request is
  queued by @ref queue_to_deferred, and each time a pending
  request completes thus freeing up a resource.
  The latter is signalled by calling @ref wakeup_deferred.
  In either case we bump up the generation number of the thread,
  so the wait condition is simply "has the gen num changed?".

## Generation Numbers

The reasons why a request can be deferred are multiple,
and can also change dynamically.
A request deferred for, say, lack of a buffer, could have be deferred again
if all cache blocks are busy (no free blocks), or if the specified cache block
is busy. Because of this, @ref handle_deferred will potentially
defer requests again.
This implies that the deferred thread needs to wait on conditions other than
just the deferred thread queue being non-empty. If we waited on the latter,
and if a requeue happens, then we would hit an infinite loop.
So to avoid this, we wait on a "generation" number, which simply indicates
that the queue condition has changed since last time it was worked on.

## Other Considerations

All theses events are fairly rare conditions except for case 4, which
can very easily happen during heavy i/o bursts such as creating a
file system with mkfs.
In all cases we can expect a significant delay in the execution of
these requests (due to lack of resources), thus we don't need
to worry about how efficiently they are handled, so long as
starvation is not possbile.

## Data structures and data members

A deferred queue is described by struct deferred_queue.
There are two instances two instances of said structure in bittern_cache:
* defer_busy: this queue handles cases 1 and 2.
* defer_page: this queue handles cases 3 and 4.

## Code Paths

### deferral path

* @ref cache_map --> @ref queue_to_deferred
* @ref cache_map -->
  @ref cache_map_workfunc -->
  @ref queue_to_deferred
* @ref handle_deferred -->
  @ref cache_map_workfunc -->
  @ref queue_to_deferred

### wakeup path

* Request complete --> @ref wakeup_deferred
* Writeback request complete --> @ref wakeup_deferred

### threads path

* @ref cache_deferred_page_kthread -->
  @ref cache_deferred_io_handler

* @ref cache_deferred_busy_kthread -->
  @ref cache_deferred_io_handler
