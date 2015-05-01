#ifndef _MURMURHASH3_H
#define _MURMURHASH3_H

/*! \file */

/*!
 * MurmurHash3 was written by Austin Appleby, and is placed in the public
 * domain. The author hereby disclaims copyright to this source code.
 *
 * Note - The x86 and x64 versions do _not_ produce the same results, as the
 * algorithms are optimized for their respective platforms. You can still
 * compile and run any of them on any platform, but your performance with the
 * non-native version will be less than optimal.
 *
 */
/*
 * This code has been modified from its original format to be Linux-friendly
 * and to allow compile with standard ANSI C with GNU extensions.
 * This code compiles for both kernel and userland.
 *
 * The copyright notice and license header that follows is for the modified
 * portions of the code.
 */
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

#ifdef __KERNEL__
#include <linux/kernel.h>
#else /*__KERNEL__*/
#include <stdint.h>
#include <sys/types.h>
#endif /*__KERNEL__*/

#include "math128.h"

/*!
 * MurmurHash3 x64 version which generates a 128 bits hash.
 */
extern void MurmurHash3_x86_128(const void *key,
				int len,
				uint32_t seed,
				void *out);
/*!
 * MurmurHash3 x86 version which generates a 128 bits hash.
 */
extern void MurmurHash3_x64_128(const void *key,
				int len,
				uint32_t seed,
				void *out);

/*!
 * A Linux friendly of the above APIs, without the 32 bits seed parameter.
 * This API makes use of either the x86 or the x64 version. As such,
 * there will be different results when ran on x86 or x64 platforms.
 */
extern uint128_t murmurhash3_128(const void *buf, size_t sz);
/*!
 * A Linux friendly of the above APIs, with 32 bits seed parameter.
 * This API makes use of either the x86 or the x64 version. As such,
 * there will be different results when ran on x86 or x64 platforms.
 */
extern uint128_t murmurhash3_128_seed(uint32_t seed,
				      const void *buf,
				      size_t sz);

/*!
 * Hash for a 4096 bytes buffer filled with all zeroes,
 * computed with the x64 version.
 * \todo would any code other than Bittern care about this?
 * if not, it probably belongs to Bittern and not here.
 * \todo need to figure out the above value for 32 bits.
 */
#ifdef __IS_LITTLE_ENDIAN
#define MURMURHASH3_128_4K0	(uint128_t){	\
		.hi64 = 0xfb7003051ad6b21cULL,	\
		.lo64 = 0x6ac4fe9480b3fb7aULL,	\
}
#endif

#endif /*_MURMURHASH3_H*/
