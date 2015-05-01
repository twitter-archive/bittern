#ifndef _LINUX_MATH128_H
#define _LINUX_MATH128_H

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

/*! \file */

#ifdef __KERNEL__
#include <linux/kernel.h>
#ifdef __BIG_ENDIAN
#define __IS_BIG_ENDIAN
#else
#define __IS_LITTLE_ENDIAN
#endif
#else /*__KERNEL__*/
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#if __BYTE_ORDER == __BIG_ENDIAN
#define __IS_BIG_ENDIAN
#else
#define __IS_LITTLE_ENDIAN
#endif
#endif /*__KERNEL__*/

/*!
 * 128 bits arithmetic.
 * GCC does not support 128 bits types on all implementations,
 * so relying on __int128_t is not necessarily a good idea.
 *
 * This file currenty only implements assignment and comparison
 * for equality/inequality.
 * The data type can be directly assigned and passed
 * as function argument and/or return value.
 */
typedef struct {
#ifdef __IS_BIG_ENDIAN
	uint64_t hi64;
	uint64_t lo64;
#else /*__IS_BIG_ENDIAN*/
	uint64_t lo64;
	uint64_t hi64;
#endif /*__IS_BIG_ENDIAN*/
} uint128_t;

#define UINT128_FROM_UINT2(__hi, __lo)	\
		(uint128_t){ .hi64 = (__hi), .lo64 = (__lo) }
#define UINT128_FROM_UINT(__lo)		\
		UINT128_FROM_UINT2(0, __lo)
#define UINT128_ZERO			\
		UINT128_FROM_UINT2(0, 0)

/*! returns true if a equals b */
static inline int uint128_eq(uint128_t a, uint128_t b)
{
	return a.hi64 == b.hi64 && a.lo64 == b.lo64;
}

/*! returns true if a equals 0 */
static inline int uint128_z(uint128_t a)
{
	return a.hi64 == 0ULL && a.lo64 == 0ULL;
}

/*! returns true if a does not equal b */
static inline int uint128_ne(uint128_t a, uint128_t b)
{
	return a.hi64 != b.hi64 || a.lo64 != b.lo64;
}

/*! returns true if a does not equal 0 */
static inline int uint128_nz(uint128_t a)
{
	return a.hi64 != 0ULL || a.lo64 != 0ULL;
}

/*! printf format helper */
#ifdef __KERNEL__
#define	UINT128_FMT			"%016llx%016llx"
#else
#define	UINT128_FMT			"%016" PRIx64 "%016" PRIx64
#endif
/*! printf arg helper -- note this is not side effect free */
#define UINT128_ARG(__u128)		(__u128).hi64, (__u128).lo64

#endif /* _LINUX_MATH128_H */
