/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef _KERNEL_ARCH_AARCH64_ARCH_KERNEL_ARGS_H_
#define _KERNEL_ARCH_AARCH64_ARCH_KERNEL_ARGS_H_

#ifndef KERNEL_BOOT_KERNEL_ARGS_H
#	error This file is included from <boot/kernel_args.h> only
#endif

typedef struct {
	phys_addr_t				pgdir_phys;
	void *					pgdir_vir;
	void *					dtb_base;
	size_t					dtb_size;
	uint64					mpidr_map[SMP_MAX_CPUS];
} arch_kernel_args;

#endif /* _KERNEL_ARCH_AARCH64_ARCH_KERNEL_ARGS_H_ */
