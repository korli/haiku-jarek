/*-
 * Copyright (c) 1987, 1991 Regents of the University of California.
 * All rights reserved.
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
 *	@(#)endian.h	7.8 (Berkeley) 4/3/91
 * $FreeBSD$
 */

#ifndef _MACHINE_ENDIAN_H_
#define	_MACHINE_ENDIAN_H_

#include <sys/cdefs.h>
#ifndef	__ASSEMBLER__
#include <sys/types.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __ASSEMBLER__
#if defined(__GNUCLIKE_BUILTIN_CONSTANT_P) && defined(__OPTIMIZE__)
#define	__is_constant(x)	__builtin_constant_p(x)
#else
#define	__is_constant(x)	0
#endif

#define	__bswap16_const(x)	(((x) >> 8) | (((x) << 8) & 0xff00))
#define	__bswap32_const(x)	(((x) >> 24) | (((x) >> 8) & 0xff00) |	\
	(((x) << 8) & 0xff0000) | (((x) << 24) & 0xff000000))
#define	__bswap64_const(x)	(((x) >> 56) | (((x) >> 40) & 0xff00) |	\
	(((x) >> 24) & 0xff0000) | (((x) >> 8) & 0xff000000) |		\
	(((x) << 8) & ((__haiku_std_uint64)0xff << 32)) |			\
	(((x) << 24) & ((__haiku_std_uint64)0xff << 40)) |			\
	(((x) << 40) & ((__haiku_std_uint64)0xff << 48)) | (((x) << 56)))

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

#define	__bswap16(x)	((__haiku_std_uint16)(__is_constant((x)) ?		\
	__bswap16_const((__haiku_std_uint16)(x)) :  __bswap16_var((__haiku_std_uint16)(x))))
#define	__bswap32(x)	((__haiku_std_uint32)(__is_constant((x)) ?		\
	__bswap32_const((__haiku_std_uint32)(x)) :  __bswap32_var((__haiku_std_uint32)(x))))
#define	__bswap64(x)	((__haiku_std_uint64)(__is_constant((x)) ?		\
	__bswap64_const((__haiku_std_uint64)(x)) :  __bswap64_var((__haiku_std_uint64)(x))))

#ifdef __MIPSEB__
#define	__htonl(x)	((__haiku_std_uint32)(x))
#define	__htons(x)	((__haiku_std_uint16)(x))
#define	__ntohl(x)	((__haiku_std_uint32)(x))
#define	__ntohs(x)	((__haiku_std_uint16)(x))
/*
 * Define the order of 32-bit words in 64-bit words.
 */
/*
 * XXXMIPS: Additional parentheses to make gcc more happy.
 */
#define _QUAD_HIGHWORD 0
#define _QUAD_LOWWORD 1
#else
#define _QUAD_HIGHWORD  1
#define _QUAD_LOWWORD 0
#define __ntohl(x)	(__bswap32((x)))
#define __ntohs(x)	(__bswap16((x)))
#define __htonl(x)	(__bswap32((x)))
#define __htons(x)	(__bswap16((x)))
#endif /* _MIPSEB */

#endif /* _ASSEMBLER_ */

#ifdef __cplusplus
}
#endif

#endif /* !_MACHINE_ENDIAN_H_ */
