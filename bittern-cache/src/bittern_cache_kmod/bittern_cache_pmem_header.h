/*
 * Bittern Cache.
 *
 * Copyright(c) 2013, 2014, 2015, Twitter, Inc., All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef BITTERN_CACHE_PMEM_HEADER_H
#define BITTERN_CACHE_PMEM_HEADER_H

#define LM_MAGIC	0xf10c5704
#define LM_VERSION	11

/*! size of cache name and device paths - needs to be multiple of 8 */
#define LM_NAME_SIZE 128

#ifndef __KERNEL__
#ifndef __aligned
/*! userland code does not have this macro */
#define __aligned(__sz)		__attribute__((__aligned__(__sz)))
#endif /*__aligned*/
#endif /*__KERNEL__*/

/*!
 * Cache Layout:
 * 'S': Sequential.
 *      Metadata and data are arranged as two
 *      sequential regions.
 *      The metadata region is the first
 *      this layout is optimized for NVDIMM devices.
 * 'I': Interleaved.
 *      Metadata and data are interleaved thru the cache.
 *      Data is layed out before the metadata.
 *      This layout is optimized for NVMe and SSD devices.
 * For convenience, the enum values are all printable.
 */
enum cache_layout {
	CACHE_LAYOUT_SEQUENTIAL = 'S',
	CACHE_LAYOUT_INTERLEAVED = 'I',
};

/*!
 * PMEM header.
 * this is the cache "superblock" and contains all the configuration
 * parameters such as cache size, cached device path, metadata size
 * information, and so on.....
 * Two copies of this header are stored in the cache store both for redundancy
 * and to avoid data corruption in the case of NVDIMM hardware (in the
 * case of NVDIMM, we update the header with simple memory copies. If the
 * system crashes in the middle of a memory copy, the whole data structure
 * will be lost as the crc32c checksum will not match. hence the need of
 * keeping double copies.
 *
 * We enforce the size of this structure to be 4096, the same as a NAND-flash
 * page (also conveniently the same as PAGE_SIZE).
 *
 * All fields used for size and offset computation are now 64 bits, in order
 * to avoid 32 bits overflow issues which creep up in some integer expressions.
 *
 * The order and the type of the first three fields (lm_magic1, lm_version,
 * lm_header_size_bytes) should never be changed as it will be used to support
 * forward binary compatibility when we decide to do so.
 *
 *
 * Cache layout: there are two layout types,
 *
 * 'S' (sequential):
 * * Metadata and data are arranged as two sequential regions.
 * * The metadata region is the first
 * * This layout is optimized for NVDIMM devices.
 *
 * 'I' (interleaved):
 * * Metadata and data are interleaved thru the cache.
 * * Data is layed out before the metadata.
 * * This layout is optimized for NVMe and SSD devices.
 *
 *
 *
 * Sequential Layout:
 *
 * +---------------------------------------------+
 * | hdr0 hdr1   metadata_blocks    data_blocks  |
 * +---------------------------------------------+
 *
 * Sequential layout is used for byte addressable devices,
 * e.g. NVDIMM and/or PCIe-NVRAM devices. in such case we pack metadata blocks
 * (64 bytes sized) together to minimize memory use.
 *
 *
 *
 * Interleaved Layout:
 *
 * +---------------------------------------------+
 * | hdr0 hdr1   (data_metadata_pair)_blocks     |
 * +---------------------------------------------+
 *
 * Interleaved layout is used for block addressable devices.
 * We want to pack data and metadata together to hopefully reduce the amount
 * of full NAND flash page reallocation and garbage collection, and,
 * most importantly, to allow data/metadata updating in one single I/O
 * operation.
 * Hopefully the NAND flash device is also very good at merging together
 * a data write followed by a contigous data write.
 *
 */
struct pmem_header {
	/*!
	 * Magic number, version and header size.
	 * Do not change the order of the first three fields.
	 * We rely on their order to detect version and sizes.
	 */
	uint32_t lm_magic;

	/*!
	 * cache version
	 */
	uint32_t lm_version;

	/*!
	 * Actual size of this structure in bytes.
	 * For alignement purposes the struct is always padded to 4k bytes.
	 */
	uint32_t lm_header_size_bytes;

	/*!
	 * Cache layout:
	 *
	 * 'S': Sequential.
	 *      Metadata and data are arranged as two
	 *      sequential regions.
	 *      The metadata region is the first
	 *      this layout is optimized for NVDIMM devices.
	 *
	 * 'I': Interleaved.
	 *      Metadata and data are interleaved thru the cache.
	 *      Data is layed out before the metadata.
	 *      This layout is optimized for NVMe and SSD devices.
	 */
	uint8_t lm_cache_layout;
	/*! explicitly align to 64 bits offset */
	uint8_t lm_alignment_pad[7];

	/*! cache blocks - how many blocks we have in cache */
	uint64_t lm_cache_blocks;

	/*! cache block size - currently hardcoded to PAGE_SIZE */
	uint64_t lm_cache_block_size;

	/*!
	 * Cache block metadata size in bytes.
	 * If the PMEM hardware allows direct memory access, size is 64,
	 * otherwise PAGE_SIZE.
	 * It'd be great if some future NAND flash technology would give us
	 * an efficient way to write in 512 or 1K chunks.
	 */
	uint64_t lm_mcb_size_bytes;

	/*!
	 * Cache size in bytes
	 * This is the configured cache size, and can be less than the actual
	 * cache device size.
	 */
	uint64_t lm_cache_size_bytes;

	/*!
	 * First metadata or data offset.
	 * for 'S' layout, this is the first mcb offset.
	 * for 'I' layout, this is the first data offset.
	 * \todo convert to uint64_t
	 */
	uint64_t lm_first_offset_bytes;

	/*!
	 * First data block offset (bytes).
	 * For 'I' layout, this is identical to lm_first_offset_bytes.
	 */
	uint64_t lm_first_data_block_offset_bytes;

	/*! cache UUID */
	uint8_t lm_uuid[16];
	/*! cache device path name (ascii, zero terminated) */
	uint8_t lm_name[LM_NAME_SIZE];

	/*! cached device UUID */
	uint8_t lm_device_uuid[16];
	/*! cached device path name (zero terminated) */
	uint8_t lm_device_name[LM_NAME_SIZE];

	/*! first xid used for this cache */
	uint64_t lm_xid_first;
	/*! current xid (last xid used for this cache) */
	uint64_t lm_xid_current;

	/*!
	 * This is spare space which we'll use to add
	 * "forward" compatible changes.
	 * in other words, now:
	 *      lm_spare[64];
	 * after we add tunable for max_pending:
	 *      lm_max_pending_conf;
	 *      lm_spare[63];
	 * after we add dynamically enabling of tracking of
	 * hashes:
	 *      lm_max_pending_conf;
	 *      lm_track_hashes_enabled_conf;
	 *      lm_spare[62];
	 *
	 */
	uint64_t lm_spare[64];

	/*!
	 * Hash of this struct.
	 * Must be last - correct operation depends on this.
	 */
	uint128_t lm_hash;
} __aligned(4096);

/*! we hash the whole struct except for the hash itself */
#define PMEM_HEADER_HASHING_SIZE	offsetof(struct pmem_header, lm_hash)

#ifdef __KERNEL__
#if PAGE_SIZE != 4096
/*! \todo see above comment on size alignment */
#error "this code requires PAGE_SIZE to be 4096 bytes."
#endif /*PAGE_SIZE != 4096 */
#endif /*__KERNEL__*/

/*! seed for crc32c computation - this value has been randomly chosen */
#define BITTERN_CRC32C_SEED			     0xf10cf10c

/*!
 * This should be ideally be the size of a NAND-flash erase block,
 * or at the very least large enough to make sure that no two blocks
 * share the same MLC data.
 */
#define CACHE_NAND_FLASH_ERASE_BLOCK_SIZE	(off_t)(128 * 1024)

/*!
 * This should be ideally be the size of a NAND-flash erase block,
 * or at the very least large enough to make sure that no two blocks
 * share the same MLC data.
 */
#define CACHE_NAND_FLASH_ERASE_BLOCK_SIZE	(off_t)(128 * 1024)

/*! byte offset of copy #0 of pmem_header structure */
#define CACHE_MEM_HEADER_0_OFFSET_BYTES		\
			(off_t)0

/*! byte offset of copy #1 of pmem_header structure */
#define CACHE_MEM_HEADER_1_OFFSET_BYTES		\
			CACHE_NAND_FLASH_ERASE_BLOCK_SIZE

/*! byte offset of first data or metadata block */
#define CACHE_MEM_FIRST_OFFSET_BYTES		\
			(CACHE_NAND_FLASH_ERASE_BLOCK_SIZE * 2)

#define MCBM_MAGIC	0xf10c8a0f

/*!
 * PMEM version of cache block metadata.
 * actual size is currently 40 bytes,
 * padded to 64 bytes for cache line alignment.
 */
struct pmem_block_metadata {
	/*! offset 0: magic */
	uint32_t pmbm_magic;
	/*!
	 * offset 4: block id -- used to index into mem cache data.
	 * first index value is 1. non-zero value helps detecting uninitialized
	 * blocks.
	 */
	uint32_t pmbm_block_id;
	/*! offset 8: cached device sector number */
	uint64_t pmbm_device_sector;
	/*! offset 16: matches in memory xid */
	uint64_t pmbm_xid;
	/*! offset 24: cache status @ref pmem_cache_state */
	uint32_t pmbm_status;
	/*! offset 32: crc32c of the data cache block */
	uint128_t pmbm_hash_data;
	/*!
	 * offset 48:
	 * Hash of this struct.
	 * Must be last - correct operation depends on this.
	 */
	uint128_t pmbm_hash_metadata;
} __aligned(64);

/*! we hash the whole struct except for the hash itself */
#define PMEM_BLOCK_METADATA_HASHING_SIZE	\
		offsetof(struct pmem_block_metadata, pmbm_hash_metadata)

/*!
 * Valid persistent cache states.
 * Every other state is considered transient and rolled back on recovery.
 */
enum pmem_cache_state {
	P_S_INVALID = 0,
	P_S_CLEAN = 1,
	P_S_DIRTY = 2,
};

#endif /* BITTERN_CACHE_PMEM_HEADER_H */
