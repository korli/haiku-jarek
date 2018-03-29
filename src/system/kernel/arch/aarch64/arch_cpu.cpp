/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <cpu.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <algorithm>

#include <boot_device.h>
#include <commpage.h>
#include <debug.h>
#include <elf.h>
#include <safemode.h>
#include <smp.h>
#include <util/BitUtils.h>
#include <vm/vm.h>
#include <vm/vm_types.h>
#include <vm/VMAddressSpace.h>

status_t
arch_cpu_preboot_init_percpu(kernel_args* args, int cpu)
{
	return B_OK;
}

status_t
arch_cpu_init_percpu(kernel_args* args, int cpu)
{
	return B_OK;
}

status_t
arch_cpu_init(kernel_args* args)
{
	return B_OK;
}

status_t
arch_cpu_init_post_vm(kernel_args* args)
{
	return B_OK;
}

status_t
arch_cpu_init_post_modules(kernel_args* args)
{
	return B_OK;
}

void
arch_cpu_user_TLB_invalidate(void)
{
}

void
arch_cpu_global_TLB_invalidate(void)
{
}

void
arch_cpu_invalidate_TLB_range(addr_t start, addr_t end)
{
}

void
arch_cpu_invalidate_TLB_list(addr_t pages[], int num_pages)
{
}

status_t
arch_cpu_shutdown(bool rebootSystem)
{
	return B_OK;
}

void
arch_cpu_sync_icache(void* address, size_t length)
{
}
