/*
 * Copyright 2002-2012 Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H


#include <limits.h>


#define MAXPATHLEN      PATH_MAX
#define MAXSYMLINKS		SYMLOOP_MAX

#define NOFILE          OPEN_MAX

#ifndef MIN
#	define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#	define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

#define _ALIGNBYTES	(sizeof(long) - 1)
#define _ALIGN(p) \
	(((u_long)(p) + _ALIGNBYTES) &~ _ALIGNBYTES)

/* maximum possible length of this machine's hostname */
#ifndef MAXHOSTNAMELEN
#	define MAXHOSTNAMELEN 256
#endif

#ifndef powerof2
#define powerof2(x)     ((((x) - 1) & (x)) == 0)
#endif

#ifndef setbit
#define	setbit(a,i)	(((unsigned char *)(a))[(i)/8] |= 1<<((i)%8))
#endif

#ifndef clrbit
#define	clrbit(a,i)	(((unsigned char *)(a))[(i)/8] &= ~(1<<((i)%8)))
#endif

#ifndef isset
#define	isset(a,i)							\
	(((const unsigned char *)(a))[(i)/8] & (1<<((i)%8)))
#endif

#ifndef isclr
#define	isclr(a,i)							\
	((((const unsigned char *)(a))[(i)/8] & (1<<((i)%8))) == 0)
#endif

#endif	/* _SYS_PARAM_H */
