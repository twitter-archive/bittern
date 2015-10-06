# Debugging, Tracing and Performance {#debugging_tracing_and_performance}

## Debugging

## Tracing

## Performance Counters

## Sys FS

### How to parse sysfs entries output

The general format of all readable /sys/fs/entries is as follows:
~~~~~~~~~~
        <cachename>: <sysfsentry>: <var1> <var2> ...... <varN>
        <cachename>: <sysfsentry>: <var1> <var2> ...... <varN>
        <cachename>: <sysfsentry>: <var1> <var2> ...... <varN>
        ......
        ......
~~~~~~~~~~

the format is multiline and var is almost always further subdivided as:

        varname=varvalue

Both variable names and values cannot have any tab, blank or other white spaces.
As a general rule, variables never get renamed. However, the order in which
they appear can change, along with the number of lines.
So never hardcode for a specific position or line number.

### SysFs Examples

/sys/fs/bittern/cache-name/page_buffer:
~~~~~~~~~~
        bitcache0: page_buffer: pages=66 max_pages=66
        bitcache0: page_buffer: in_use_pages_0=0 stat_alloc_wait_c=79028
~~~~~~~~~~

/sys/fs/bittern/cache-name/stats:
~~~~~~~~~~
        bitcache0: stats: io_xid=339264 read_requests=97 write_requests=278159
        bitcache0: stats: read+write_requests=278256 deferred_requests=0
        bitcache0: stats: pending_writeback_requests=0 total_entries=252057
        ......
        ......
~~~~~~~~~~

/sys/fs/bittern/cache-name/info:
~~~~~~~~~~
        bitcache0: info: version=0.20.2a.graysharbor build_timestamp=2015-02-11-12:09:23 max_io_len_pages=1 memcpy_nt_type=intel-sse-nti-x64
        bitcache0: info: cache_name=bitcache0 cache_device_name=/dev/pmem_ram0 cached_device_name=/dev/mapper/vg_hdd-lvol0
        bitcache0: info: pmem_provider_version=0.9.1 cache_device_type=pmem_ram cached_device_size_bytes=20401094656 cached_device_size_mbytes=19456 cache_entries=16127 mcb_size_bytes=64 cache_kaddr=ffffc90011f41000 cache_paddr=0x0 requested_cache_size=67108864 allocated_cache_size=67108864 in_use_cache_size=67100672
        bitcache0: info: replacement_mode=repl-random cache_mode=writeback
~~~~~~~~~~
