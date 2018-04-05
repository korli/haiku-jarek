/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef PRIVATE_KERNEL_ARCH_AARCH64_IFRAME_H_
#define PRIVATE_KERNEL_ARCH_AARCH64_IFRAME_H_

/* raw exception frames */
struct iframe {
	uint64			sp;
	uint64			lr;
	uint64			elr;
	uint32			spsr;
	uint32			esr;
	uint64			x[30];
};

#endif /* PRIVATE_KERNEL_ARCH_AARCH64_IFRAME_H_ */
