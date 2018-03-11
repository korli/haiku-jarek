/*
 * Copyright 2003-2012 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ENDIAN_H_
#define _ENDIAN_H_

#include <sys/cdefs.h>
#include <config/HaikuConfig.h>
#include <sys/types.h>

/* Defines architecture dependent endian constants.
 * The constant reflects the byte order, "4" is the most
 * significant byte, "1" the least significant one.
 */

#if defined(__HAIKU_LITTLE_ENDIAN)
#	define LITTLE_ENDIAN	1234
#	define BIG_ENDIAN		0
#	define BYTE_ORDER		LITTLE_ENDIAN
#elif defined(__HAIKU_BIG_ENDIAN)
#	define BIG_ENDIAN		4321
#	define LITTLE_ENDIAN	0
#	define BYTE_ORDER		BIG_ENDIAN
#endif

#define __BIG_ENDIAN		BIG_ENDIAN
#define __LITTLE_ENDIAN		LITTLE_ENDIAN
#define __BYTE_ORDER		BYTE_ORDER

#include __HAIKU_ARCH_HEADER(arch_endian.h)


/*
 * General byte order swapping functions.
 */
#define	bswap16(x)	__bswap16(x)
#define	bswap32(x)	__bswap32(x)
#define	bswap64(x)	__bswap64(x)

/*
 * Host to big endian, host to little endian, big endian to host, and little
 * endian to host byte order functions as detailed in byteorder(9).
 */
#if defined(__HAIKU_LITTLE_ENDIAN)
#define	htobe16(x)	bswap16((x))
#define	htobe32(x)	bswap32((x))
#define	htobe64(x)	bswap64((x))
#define	htole16(x)	((__haiku_std_uint16)(x))
#define	htole32(x)	((__haiku_std_uint32)(x))
#define	htole64(x)	((__haiku_std_uint64)(x))

#define	be16toh(x)	bswap16((x))
#define	be32toh(x)	bswap32((x))
#define	be64toh(x)	bswap64((x))
#define	le16toh(x)	((__haiku_std_uint16)(x))
#define	le32toh(x)	((__haiku_std_uint32)(x))
#define	le64toh(x)	((__haiku_std_uint64)(x))
#else /* __HAIKU_BIG_ENDIAN */
#define	htobe16(x)	((__haiku_std_uint16)(x))
#define	htobe32(x)	((__haiku_std_uint32)(x))
#define	htobe64(x)	((__haiku_std_uint64)(x))
#define	htole16(x)	bswap16((x))
#define	htole32(x)	bswap32((x))
#define	htole64(x)	bswap64((x))

#define	be16toh(x)	((__haiku_std_uint16)(x))
#define	be32toh(x)	((__haiku_std_uint32)(x))
#define	be64toh(x)	((__haiku_std_uint64)(x))
#define	le16toh(x)	bswap16((x))
#define	le32toh(x)	bswap32((x))
#define	le64toh(x)	bswap64((x))
#endif /* __HAIKU_LITTLE_ENDIAN */


/* Alignment-agnostic encode/decode bytestream to/from little/big endian. */

static __inline __haiku_std_uint16
be16dec(const void *pp)
{
	__haiku_std_uint8 const *p = (__haiku_std_uint8 const *)pp;

	return ((p[0] << 8) | p[1]);
}

static __inline __haiku_std_uint32
be32dec(const void *pp)
{
	__haiku_std_uint8 const *p = (__haiku_std_uint8 const *)pp;

	return (((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

static __inline __haiku_std_uint64
be64dec(const void *pp)
{
	__haiku_std_uint8 const *p = (__haiku_std_uint8 const *)pp;

	return (((__haiku_std_uint64)be32dec(p) << 32) | be32dec(p + 4));
}

static __inline __haiku_std_uint16
le16dec(const void *pp)
{
	__haiku_std_uint8 const *p = (__haiku_std_uint8 const *)pp;

	return ((p[1] << 8) | p[0]);
}

static __inline __haiku_std_uint32
le32dec(const void *pp)
{
	__haiku_std_uint8 const *p = (__haiku_std_uint8 const *)pp;

	return (((unsigned)p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0]);
}

static __inline __haiku_std_uint64
le64dec(const void *pp)
{
	__haiku_std_uint8 const *p = (__haiku_std_uint8 const *)pp;

	return (((__haiku_std_uint64)le32dec(p + 4) << 32) | le32dec(p));
}

static __inline void
be16enc(void *pp, __haiku_std_uint16 u)
{
	__haiku_std_uint8 *p = (__haiku_std_uint8 *)pp;

	p[0] = (u >> 8) & 0xff;
	p[1] = u & 0xff;
}

static __inline void
be32enc(void *pp, __haiku_std_uint32 u)
{
	__haiku_std_uint8 *p = (__haiku_std_uint8 *)pp;

	p[0] = (u >> 24) & 0xff;
	p[1] = (u >> 16) & 0xff;
	p[2] = (u >> 8) & 0xff;
	p[3] = u & 0xff;
}

static __inline void
be64enc(void *pp, __haiku_std_uint64 u)
{
	__haiku_std_uint8 *p = (__haiku_std_uint8 *)pp;

	be32enc(p, (__haiku_std_uint32)(u >> 32));
	be32enc(p + 4, (__haiku_std_uint32)(u & 0xffffffffU));
}

static __inline void
le16enc(void *pp, __haiku_std_uint16 u)
{
	__haiku_std_uint8 *p = (__haiku_std_uint8 *)pp;

	p[0] = u & 0xff;
	p[1] = (u >> 8) & 0xff;
}

static __inline void
le32enc(void *pp, __haiku_std_uint32 u)
{
	__haiku_std_uint8 *p = (__haiku_std_uint8 *)pp;

	p[0] = u & 0xff;
	p[1] = (u >> 8) & 0xff;
	p[2] = (u >> 16) & 0xff;
	p[3] = (u >> 24) & 0xff;
}

static __inline void
le64enc(void *pp, __haiku_std_uint64 u)
{
	__haiku_std_uint8 *p = (__haiku_std_uint8 *)pp;

	le32enc(p, (__haiku_std_uint32)(u & 0xffffffffU));
	le32enc(p + 4, (__haiku_std_uint32)(u >> 32));
}

#endif	/* _ENDIAN_H_ */
