/*-
 * Copyright (c) 2001 David E. O'Brien
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
 * $NetBSD: endian.h,v 1.7 1999/08/21 05:53:51 simonb Exp $
 * $FreeBSD$
 */

#ifndef _ENDIAN_H_
#define	_ENDIAN_H_

#include <sys/types.h>

#ifdef __ARMEB__
#define _QUAD_HIGHWORD 0
#define _QUAD_LOWWORD 1
#define __ntohl(x)	((__haiku_std_uint32)(x))
#define __ntohs(x)	((__haiku_std_uint16)(x))
#define __htonl(x)	((__haiku_std_uint32)(x))
#define __htons(x)	((__haiku_std_uint16)(x))
#else
#define _QUAD_HIGHWORD  1
#define _QUAD_LOWWORD 0
#define __ntohl(x)        (__bswap32(x))
#define __ntohs(x)        (__bswap16(x))
#define __htonl(x)        (__bswap32(x))
#define __htons(x)        (__bswap16(x))
#endif /* __ARMEB__ */

static __inline __haiku_std_uint64
__bswap64(__haiku_std_uint64 _x)
{

	return ((_x >> 56) | ((_x >> 40) & 0xff00) | ((_x >> 24) & 0xff0000) |
	    ((_x >> 8) & 0xff000000) | ((_x << 8) & ((__haiku_std_uint64)0xff << 32)) |
	    ((_x << 24) & ((__haiku_std_uint64)0xff << 40)) |
	    ((_x << 40) & ((__haiku_std_uint64)0xff << 48)) | ((_x << 56)));
}

static __inline __haiku_std_uint32
__bswap32_var(__haiku_std_uint32 v)
{
	__haiku_std_uint32 t1;

	__asm__ __volatile__("eor %1, %0, %0, ror #16\n"
	    		"bic %1, %1, #0x00ff0000\n"
			"mov %0, %0, ror #8\n"
			"eor %0, %0, %1, lsr #8\n"
			 : "+r" (v), "=r" (t1));

	return (v);
}

static __inline __haiku_std_uint16
__bswap16_var(__haiku_std_uint16 v)
{
	__haiku_std_uint32 ret = v & 0xffff;

	__asm__ __volatile__(
	    "mov    %0, %0, ror #8\n"
	    "orr    %0, %0, %0, lsr #16\n"
	    "bic    %0, %0, %0, lsl #16"
	    : "+r" (ret));

	return ((__haiku_std_uint16)ret);
}

#ifdef __OPTIMIZE__

#define __bswap32_constant(x)	\
    ((((x) & 0xff000000U) >> 24) |	\
     (((x) & 0x00ff0000U) >>  8) |	\
     (((x) & 0x0000ff00U) <<  8) |	\
     (((x) & 0x000000ffU) << 24))

#define __bswap16_constant(x)	\
    ((((x) & 0xff00) >> 8) |		\
     (((x) & 0x00ff) << 8))

#define __bswap16(x)	\
    ((__haiku_std_uint16)(__builtin_constant_p(x) ?	\
     __bswap16_constant(x) :			\
     __bswap16_var(x)))

#define __bswap32(x)	\
    ((__haiku_std_uint32)(__builtin_constant_p(x) ? 	\
     __bswap32_constant(x) :			\
     __bswap32_var(x)))

#else
#define __bswap16(x)	__bswap16_var(x)
#define __bswap32(x)	__bswap32_var(x)

#endif /* __OPTIMIZE__ */
#endif /* !_ENDIAN_H_ */
