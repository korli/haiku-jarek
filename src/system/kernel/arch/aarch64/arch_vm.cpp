/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <new>

#include <KernelExport.h>

#include <boot/kernel_args.h>
#include <smp.h>
#include <util/AutoLock.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_priv.h>
#include <vm/VMAddressSpace.h>
#include <vm/VMArea.h>

#include <arch/vm.h>
#include <arch/int.h>
#include <arch/cpu.h>

#include "AArch64PagingMethod.h"

status_t
arch_vm_init(kernel_args *args)
{
	return 0;
}


/*!	Marks DMA region as in-use, and maps it into the kernel space */
status_t
arch_vm_init_post_area(kernel_args *args)
{
	return B_OK;
}


/*!	Gets rid of all yet unmapped (and therefore now unused) page tables */
status_t
arch_vm_init_end(kernel_args *args)
{
	return B_OK;
}


status_t
arch_vm_init_post_modules(kernel_args *args)
{
	return B_OK;
}


void
arch_vm_aspace_swap(struct VMAddressSpace *from, struct VMAddressSpace *to)
{
	// This functions is only invoked when a userland thread is in the process
	// of dying. It switches to the kernel team and does whatever cleanup is
	// necessary (in case it is the team's main thread, it will delete the
	// team).
	// It is however not necessary to change the page directory. Userland team's
	// page directories include all kernel mappings as well. Furthermore our
	// arch specific translation map data objects are ref-counted, so they won't
	// go away as long as they are still used on any CPU.
}


bool
arch_vm_supports_protection(uint32 protection)
{
	return true;
}


void
arch_vm_unset_memory_type(struct VMArea *area)
{
}


status_t
arch_vm_set_memory_type(struct VMArea *area, phys_addr_t physicalBase,
	uint32 type)
{
	return B_OK;
}
