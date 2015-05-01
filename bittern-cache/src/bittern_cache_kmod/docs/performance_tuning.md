# Performance Tuning {#performance_tuning}

## Compile Time Tunables

## Runtime Tunables

### Runtime Tuning of Read and Write Sequential Thresholds

Bittern keeps track of a certain number of IO streams
(tunable at compile-time by changing @ref SEQ_IO_TRACK_DEPTH) to determine
whether the access pattern is sequential or random. The main reason is
to avoid "cache pollution" in the event of very large sequential accesses
such as backup, or generally speaking, any kind of bulk file copies.
In such event the cache typically
does not help performance, and in fact because the working set by far
exceeds what the cache can handle, a bulk file write will
underperform the same bulk file write done without any caching.
This situation is clearly undesirable, and to overcome it Bittern keeps
track of @ref SEQ_IO_TRACK_DEPTH simultaneous read and write "streams" by
using the sector offset and the pid of the requester process.

The two most important runtime tunables here are
"Sequential Read Threshold" @ref SEQ_IO_THRESHOLD_COUNT_READ_DEFAULT,
and "Sequential Write Threshold" @ref SEQ_IO_THRESHOLD_COUNT_WRITE_DEFAULT.
If a certain IO stream exceeds the
number of sectors specified by these runtime thresholds, then it will be
considered to be sequential, and no further caching will be done on said
stream (of course in order to maintain consistency Bittern will still serve
possible hits from the cache).

Tuning these thresholds can be non-trivial for complex workloads.

#### Runtime Tuning of Read Sequential Threshold

The page cache already filters out most reads, and so Bittern gets
the 1st level cache misses. Because of this, and also because
Bittern is really tuned towards caching writes, tuning of this parameter
is probably not critical, or at any rate is not as critical as the Write
Threshold.

#### Runtime Tuning of Write Sequential Threshold

Unlike sequential reads, tuning of this parameter is critical because
the writeback cache absorbs write bursts for later asynchronous writeback.
Too low of a threshold will negatively impact performance because it won't
give the cache a chance to do its main job.
Too high of a threshold will negatively impact performance because it can
end up in polluting the cache with a working set much larger than the cache
size.
The other consideration is that database or filesystem journal writes can
look as sequential writes, so it's important that a proper tuning capture
journal writes in order to cache the most heavily used portion of a dataset.

For the sake of example, consider this scenario: we populate a file system
by copying a fairly large SQL database into it (i.e., large sequential writes),
and then run Sysbench (i.e., a transactional workload).
Also assume that in such case we have both read and write thresholds set to
128 Kbytes (256 sectors).

The SysFS entry

	/sys/fs/bittern/<cachename>/sequential

can tell us a lot of very useful information, in particular the average size of
a "sequential stream" compared to the average size of a
"non-sequential stream" (as defined by the above threshold).

During the sequential write phase, the average streams length are as follows
(note: _s_ stands for "sequential" and _ns_ stands for "non-sequential"):

	write_s_streams_len_avg=105315 write_ns_streams_len_avg=1

During the Sysbench phase, the average streams length are as follows:

	write_s_streams_len_avg=3573 write_ns_streams_len_avg=28

The average sequential stream length changes from about 50 Mbytes
during the bulk file copy to about 1.5 Mbytes during the Sysbench phase.

If we want to optimize for this, we would want the Sysbench phase to
avoid doing too many write bypasses, so we should set the sequential
write threshold somewhere between 1.5 Mbytes and 50 Mbytes.
We choose 8 Mbytes for the sequential threshold as a reasonable midpoint.

We show here three different test results:
* <b>&infin;</b> (write bypass disabled)
* 128 Kbytes Write Threshold
* 8 Mbytes Write Threshold

| Write Threshold | Cache Type | Bulk Copy | Sysbench TPS |
| :-------------- | :--------- | --------: | -----------: |
| <b>&infin;</b>  | SSD        | 27:37     | 105          |
| <b>&infin;</b>  | NVMe       | 15:26     | 334          |
| <b>&infin;</b>  | NVDIMM     |  7:41     | 653          |
| 128 Kbytes      | SSD        | 13:15     | 115          |
| 128 Kbytes      | NVMe       | 12:26     | 279          |
| 128 Kbytes      | NVDIMM     |  7:49     | 570          |
| 8 Mbytes        | SSD        | 12:43     | 109          |
| 8 Mbytes        | NVMe       | 12:11     | 342          |
| 8 Mbytes        | NVDIMM     |  7:48     | 664          |

It's worth noting the following:
* The 128 Kbytes Write Threshold setting (same as Read Threshold) was
  clearly non-optimal, as it caused a significant degradation in performance
  in the NVMe and NVDIMM cases.
* Setting Write Threshold to 8 Mbytes has "restored" the original
  performance Sysbench benchmark, essentially back to baseline numbers
  for the NVMe and NVDIMM cases.
  (well within the statistical margin of error for a single 2 hour test run).
* A non-optimal Write Threshold setting affects performance in different ways
  depending on the underlying cache technology.
* SSD: (1) Baseline setting has very poor performance in sequential
  bulk writes, whereas both 128 Kbytes and 8 Mbytes settings show the
  same performance. (2) Sysbench TPS seems to be insensitive to Write Threshold
  setting.
* NVMe and NVDIMM: (1) Sequential bulk writes are almost completely insensitive
  to Write Threshold setting.
  (2) Sysbench TPS are adversely impacted in the case 128 Kbytes setting.

It's difficult to draw any general conclusion from such a narrow test, and more
investigation is needed.
With this caveat, the following speculations can be suggested:
* For NVMe and NVDIMM technologies, the overhead of double copy in the
  sequential bulk writes case is mostly avoided due to NVMe and NVDIMM's
  very low latency and high bandwidth.
* The large performance drop in Sysbench TPS in the NVMe and NVDIMM case is
  quite easily explained by noting that the 128 Kbytes setting is just
  non-optimal, and too many IO requests are not cached.
  Most likely the "sequential" patterns in such case are
  actually sequential writes to the database journal, which must be cached in
  order to obtain good performance.
* Why SSDs are so insensitive to this setting for Sysbench is somewhat puzzling,
  and it's best to wait until more data becomes available before theorizing.
