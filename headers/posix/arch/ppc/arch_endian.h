/*-
 * Copyright (c) 1987, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)endian.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD$
 */

#ifndef _MACHINE_ENDIAN_H_
#define	_MACHINE_ENDIAN_H_

#include <sys/cdefs.h>
#include <sys/types.h>

/*
 * Define the order of 32-bit words in 64-bit words.
 */
#ifdef __HAIKU_LITTLE_ENDIAN
#define	_QUAD_HIGHWORD 1
#define	_QUAD_LOWWORD 0
#else
#define	_QUAD_HIGHWORD 0
#define	_QUAD_LOWWORD 1
#endif

#if defined(__GNUCLIKE_BUILTIN_CONSTANT_P)
#define	__is_constant(x)	__builtin_constant_p(x)
#else
#define	__is_constant(x)	0
#endif

#define	__bswap16_const(x)	((((__haiku_std_uint16)(x) >> 8) & 0xff) |	\
	(((__haiku_std_uint16)(x) << 8) & 0xff00))
#define	__bswap32_const(x)	((((__haiku_std_uint32)(x) >> 24) & 0xff) |	\
	(((__haiku_std_uint32)(x) >> 8) & 0xff00) |				\
	(((__haiku_std_uint32)(x)<< 8) & 0xff0000) |				\
	(((__haiku_std_uint32)(x) << 24) & 0xff000000))
#define	__bswap64_const(x)	((((__haiku_std_uint64)(x) >> 56) & 0xff) |	\
	(((__haiku_std_uint64)(x) >> 40) & 0xff00) |				\
	(((__haiku_std_uint64)(x) >> 24) & 0xff0000) |				\
	(((__haiku_std_uint64)(x) >> 8) & 0xff000000) |				\
	(((__haiku_std_uint64)(x) << 8) & ((__haiku_std_uint64)0xff << 32)) |		\
	(((__haiku_std_uint64)(x) << 24) & ((__haiku_std_uint64)0xff << 40)) |		\
	(((__haiku_std_uint64)(x) << 40) & ((__haiku_std_uint64)0xff << 48)) |		\
	(((__haiku_std_uint64)(x) << 56) & ((__haiku_std_uint64)0xff << 56)))

static __inline __haiku_std_uint16
__bswap16_var(__haiku_std_uint16 _x)
{

	return ((_x >> 8) | ((_x << 8) & 0xff00));
}

static __inline __haiku_std_uint32
__bswap32_var(__haiku_std_uint32 _x)
{

	return ((_x >> 24) | ((_x >> 8) & 0xff00) | ((_x << 8) & 0xff0000) |
	    ((_x << 24) & 0xff000000));
}

static __inline __haiku_std_uint64
__bswap64_var(__haiku_std_uint64 _x)
{

	return ((_x >> 56) | ((_x >> 40) & 0xff00) | ((_x >> 24) & 0xff0000) |
	    ((_x >> 8) & 0xff000000) | ((_x << 8) & ((__haiku_std_uint64)0xff << 32)) |
	    ((_x << 24) & ((__haiku_std_uint64)0xff << 40)) |
	    ((_x << 40) & ((__haiku_std_uint64)0xff << 48)) | ((_x << 56)));
}

#define	__bswap16(x)	((__haiku_std_uint16)(__is_constant(x) ? __bswap16_const(x) : \
	__bswap16_var(x)))
#define	__bswap32(x)	(__is_constant(x) ? __bswap32_const(x) : \
	__bswap32_var(x))
#define	__bswap64(x)	(__is_constant(x) ? __bswap64_const(x) : \
	__bswap64_var(x))

#ifdef __HAIKU_LITTLE_ENDIAN
#define	__htonl(x)	(__bswap32((__haiku_std_uint32)(x)))
#define	__htons(x)	(__bswap16((__haiku_std_uint16)(x)))
#define	__ntohl(x)	(__bswap32((__haiku_std_uint32)(x)))
#define	__ntohs(x)	(__bswap16((__haiku_std_uint16)(x)))
#else
#define	__htonl(x)	((__haiku_std_uint32)(x))
#define	__htons(x)	((__haiku_std_uint16)(x))
#define	__ntohl(x)	((__haiku_std_uint32)(x))
#define	__ntohs(x)	((__haiku_std_uint16)(x))
#endif

#endif /* !_MACHINE_ENDIAN_H_ */
