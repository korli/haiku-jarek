/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef OS_ARCH_AARCH64_ARCH_DEBUGGER_H_
#define OS_ARCH_AARCH64_ARCH_DEBUGGER_H_

typedef struct aarch64_extended_registers {
#ifdef __AARCH64__
	__uint128_t		q[32];
#else
	uint64			d[64];
#endif
	uint32			fpsr;
	uint32			fpcr;
} aarch64_extended_registers;

struct aarch64_debug_cpu_state {
	aarch64_extended_registers	extended_registers;

	uint64			x[30];
	uint64			lr;
	uint64			sp;
	uint64			elr;
	uint32			spsr;
} __attribute__((aligned(16)));

#endif /* OS_ARCH_AARCH64_ARCH_DEBUGGER_H_ */
