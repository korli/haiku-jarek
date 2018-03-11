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
#include <sys/types.h>

/*
 * Define the order of 32-bit words in 64-bit words.
 */
#define	_QUAD_HIGHWORD 1
#define	_QUAD_LOWWORD 0

#define	__bswap16_gen(x)	(__haiku_std_uint16)((x) << 8 | (x) >> 8)
#define	__bswap32_gen(x)		\
	(((__haiku_std_uint32)__bswap16((x) & 0xffff) << 16) | __bswap16((x) >> 16))
#define	__bswap64_gen(x)		\
	(((__haiku_std_uint64)__bswap32((x) & 0xffffffff) << 32) | __bswap32((x) >> 32))

#ifdef __GNUCLIKE_BUILTIN_CONSTANT_P
#define	__bswap16(x)				\
	((__haiku_std_uint16)(__builtin_constant_p(x) ?	\
	    __bswap16_gen((__haiku_std_uint16)(x)) : __bswap16_var(x)))
#define	__bswap32(x)			\
	(__builtin_constant_p(x) ?	\
	    __bswap32_gen((__haiku_std_uint32)(x)) : __bswap32_var(x))
#define	__bswap64(x)			\
	(__builtin_constant_p(x) ?	\
	    __bswap64_gen((__haiku_std_uint64)(x)) : __bswap64_var(x))
#else
/* XXX these are broken for use in static initializers. */
#define	__bswap16(x)	__bswap16_var(x)
#define	__bswap32(x)	__bswap32_var(x)
#define	__bswap64(x)	__bswap64_var(x)
#endif

/* These are defined as functions to avoid multiple evaluation of x. */

static __inline __haiku_std_uint16
__bswap16_var(__haiku_std_uint16 _x)
{

	return (__bswap16_gen(_x));
}

static __inline __haiku_std_uint32
__bswap32_var(__haiku_std_uint32 _x)
{

#ifdef __GNUCLIKE_ASM
	__asm__("bswap %0" : "+r" (_x));
	return (_x);
#else
	return (__bswap32_gen(_x));
#endif
}

static __inline __haiku_std_uint64
__bswap64_var(__haiku_std_uint64 _x)
{

#if defined(__HAIKU_ARCH_X86_64) && defined(__GNUCLIKE_ASM)
	__asm__("bswap %0" : "+r" (_x));
	return (_x);
#else
	/*
	 * It is important for the optimizations that the following is not
	 * really generic, but expands to 2 __bswap32_var()'s.
	 */
	return (__bswap64_gen(_x));
#endif
}

#define	__htonl(x)	__bswap32(x)
#define	__htons(x)	__bswap16(x)
#define	__ntohl(x)	__bswap32(x)
#define	__ntohs(x)	__bswap16(x)

#endif /* !_MACHINE_ENDIAN_H_ */
