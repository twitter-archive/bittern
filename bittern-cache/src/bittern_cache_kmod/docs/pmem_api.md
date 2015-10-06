# PMEM_API {#pmem_api}

PMEM_API provides an abstraction to access cache hardware to Bittern cache.
This simplifies Bittern code and allows supporting many types of hardware.
Currently the supported types fall into two classes:
* Memory-addressable hardware (NVDIMM and PCIe-NVRAM with some limitations).
* Block-addressable hardware (NVMe, SSD and other types of NAND-Flash harware).
PMEM_API consists in a generic high level "base class", and derived classes for
each supported class of hardware.

The general architecture is as follows:

         +----------------------------------------+
         |  bittern cache (hardware independent)  |
         +------------------+---------------------+
                            |
         +------------------+---------------------+
         |        generic pmem_api abstraction    |
         +-------+--------------------+-----------+
                 |                    |
         +-------+---------+   +------+-----------+
         |  pmem_api_mem   |   | pmem_api_block   |
         +-----------------+   +------------------+

* pmem_api_mem allows access to byte-addressable devices via the use
  of Intel's DAX interface.
* pmem_api_block allows access to block devices via the standard block IO
  interface.

PMEM_API is designed to allow efficient access to both memory-based and
block-based hardware with minimal overhead.

## PMEM_API features

PMEM_API provides the following set of functionality:
* Synchronous initialization/deinitialization APIs.
* Synchronous header restore and data/metadata restore APIs.
* Synchronous header update API.
* Asynchronous accessors to read, write, and read-write access
  to the underlying cache. The write and read-write accessors
  also implicitly update the metadata state.
* Cache layout abstraction.

Also it's important to know what is not provided:
* data and metadata accesses are not serialized. caller is responsible for that.

## Cache Layout Abstraction

The cache layout for block and memory are sligthly different, in particular
the size of the metadata header is 64 bytes for memory, and PAGE_SIZE for
block devices. This detail is hidden away by PMEM_API. Access to metadata and
data blocks is done via metadata/block "index" and not offsets. Also the
computation of the offsets and sizes, including initialization, is hidden
by PMEM_API.

## Synchronous Accessors

Synchronous accessors are only used during initialization and therefore are
not as performance critical as the asynchronous ones. They are described in
detail in the @ref bittern_pmem_api.h header file.

## Asynchronous Accessors

Asynchronous accessors constitute the core of PMEM_API. Three possible
access patterns are supported:
* read
* write (with metadata update)
* read-modify-write (with metadata update)

The asynchronous nature of these accessors is mandated by the asynchronous
nature of NAND-flash block devices, and possibly other devices such as
PCIe-NVRAM.

### Page Read Accessors

The read access pattern is as follows:

* Caller calls @ref pmem_data_get_page_read to start an
  asynchronous cache read transfer. When the asynchronous transfer is done and
  the data is available, the specified callback_function is called.
  Caller must not make any assumption as to when the callback function
  is called.
  In particular, the callback function could be called even before this function
  returns (this is guaranteed to be true for memory-based cache).
* The callback code is then free to access data in read-only mode using the
  provided kernel address pointer. This pointer is guaranteed to stay valid
  until released.
* When data access is complete, the function
  @ref pmem_data_put_page_read is called to release the
  associated resources.

Data flow for block-based caches
(implemented by @ref pmem_api_block.c):
~~~~~~~~~~~~~~~
        pmem_data_get_page_read(callback)
        [ start async block io operation ]
        ....
        ....
        [ io operation complete ]
        callback()
        [ data access ]
        pmem_data_put_page_read()
~~~~~~~~~~~~~~~

Data flow for mem-based caches
(implemented by @ref pmem_api_mem.c):
~~~~~~~~~~~~~~~
        pmem_data_get_page_read(callback)
        callback()
        [ data access ]
        pmem_data_put_page_read()
~~~~~~~~~~~~~~~

### Page Write Accessors

The write access pattern is as follows:

* Caller calls @ref pmem_data_get_page_write
  to setup the necessary context to start an asynchronous cache write transfer.
  The returned kernel memory address is guaranteed to be valid until
  put_page_write() is called. Caller can then write to such buffer.
* Caller calls @ref pmem_data_put_page_write
  to actually start the asynchronous cache write
  transfer. The specified callback_function is called when the data and
  corresponding metadata have been updated.
  Caller must not make any assumption as to when the callback function
  is called.
  In particular, the callback function could be called even before this function
  returns (this is guaranteed to be true for memory-based cache).

### Page Read-Modify-Write Accessors

A read-modify-write cycle is implemented using a combination of read and write
accessors. First a page is obtained for read, and then converted to write mode
before starting the write operation.

There APIs are as follows:

* @ref pmem_data_convert_read_to_write
  This API is used to "convert" a page from read mode to write mode, and is
  used to handle partial writes, i.e. write operations which are not a full
  cache block size (and hence require reading a block, updating a data portion
  and writing the updated block back).
* @ref pmem_data_clone_read_to_write
  This API is used to clone a page into another page for writing. It implements
  the PMEM_API portion of write cloning.
