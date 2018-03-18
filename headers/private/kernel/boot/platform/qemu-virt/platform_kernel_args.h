/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef PRIVATE_KERNEL_BOOT_PLATFORM_QEMU_VIRT_PLATFORM_KERNEL_ARGS_H_
#define PRIVATE_KERNEL_BOOT_PLATFORM_QEMU_VIRT_PLATFORM_KERNEL_ARGS_H_

#define SMP_MAX_CPUS			64

#define MAX_PHYSICAL_MEMORY_RANGE 		32
#define MAX_PHYSICAL_ALLOCATED_RANGE 	32
#define MAX_VIRTUAL_ALLOCATED_RANGE 	32

typedef struct {
	int dummy;
} platform_kernel_args;

#endif /* PRIVATE_KERNEL_BOOT_PLATFORM_QEMU_VIRT_PLATFORM_KERNEL_ARGS_H_ */
