/*
 * Copyright 2017 Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef _ARCH_SIGNAL_H_
#define _ARCH_SIGNAL_H_


/*
 * Architecture-specific structure passed to signal handlers
 */

#if __AARCH64__
struct vregs
{
	ulong		x[30];
	ulong		lr;
	ulong		sp;
	ulong		elr;
	ulong		spsr;
	__uint128_t	fp_q[32];
	u_int		fpsr;
	u_int		fpcr;
};
#endif /* __ARM__ */


#endif /* _ARCH_SIGNAL_H_ */
