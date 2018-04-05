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

#include <kernel/arch/aarch64/armreg.h>
#include "AArch64PagingStructures.h"

int64 dcache_line_size;	/* The minimum D cache line size */
int64 icache_line_size;	/* The minimum I cache line size */
int64 idcache_line_size;	/* The minimum cache line size */
int64 dczva_line_size;	/* The size of cache line the dc zva zeroes */

extern "C" void exception_vectors(void);

status_t
arch_cpu_preboot_init_percpu(kernel_args* args, int cpu)
{
	WRITE_SPECIALREG(vbar_el1, exception_vectors);

	return B_OK;
}

status_t
arch_cpu_init_percpu(kernel_args* args, int cpu)
{
	uint64 id_aa64mmfr1_el1 = READ_SPECIALREG(id_aa64mmfr1_el1);

	if (ID_AA64MMFR1_PAN(id_aa64mmfr1_el1) != ID_AA64MMFR1_PAN_NONE) {
		WRITE_SPECIALREG(sctlr_el1, READ_SPECIALREG(sctlr_el1) & ~SCTLR_SPAN);
		__asm__ __volatile__(".inst 0xd500409f | (0x1 << 8)");
	}

	return B_OK;
}

status_t
arch_cpu_init(kernel_args* args)
{
	uint64 id_aa64mmfr0_el1 = READ_SPECIALREG(id_aa64mmfr0_el1);
	uint64 id_aa64mmfr1_el1 = READ_SPECIALREG(id_aa64mmfr1_el1);

	switch(ID_AA64MMFR0_ASID_BITS(id_aa64mmfr0_el1)) {
	case ID_AA64MMFR0_ASID_BITS_8:
		AArch64PagingStructures::sMaximumASID = 0xff;
		break;
	case ID_AA64MMFR0_ASID_BITS_16:
		AArch64PagingStructures::sMaximumASID = 0xffff;
		break;
	default:
		panic("Unknown ASID bits");
	}

	switch(ID_AA64MMFR0_PA_RANGE(id_aa64mmfr0_el1))
	{
	case ID_AA64MMFR0_PA_RANGE_4G:
		AArch64PagingStructures::sPhysicalAddressMask = (1UL << 32) - 1;
		break;
	case ID_AA64MMFR0_PA_RANGE_64G:
		AArch64PagingStructures::sPhysicalAddressMask = (1UL << 36) - 1;
		break;
	case ID_AA64MMFR0_PA_RANGE_1T:
		AArch64PagingStructures::sPhysicalAddressMask = (1UL << 40) - 1;
		break;
	case ID_AA64MMFR0_PA_RANGE_4T:
		AArch64PagingStructures::sPhysicalAddressMask = (1UL << 42) - 1;
		break;
	case ID_AA64MMFR0_PA_RANGE_16T:
		AArch64PagingStructures::sPhysicalAddressMask = (1UL << 44) - 1;
		break;
	case ID_AA64MMFR0_PA_RANGE_256T:
		AArch64PagingStructures::sPhysicalAddressMask = (1UL << 48) - 1;
		break;
	case ID_AA64MMFR0_PA_RANGE_4P:
		AArch64PagingStructures::sPhysicalAddressMask = (1UL << 52) - 1;
		break;
	default:
		panic("Unknown physical address bits");
	}

	if (ID_AA64MMFR1_PAN(id_aa64mmfr1_el1) != ID_AA64MMFR1_PAN_NONE) {
		AArch64PagingStructures::sPrivilegedAccessNeverSupported = true;
	}

	int dcache_line_shift, icache_line_shift, dczva_line_shift;
	uint32 ctr_el0;
	uint32 dczid_el0;

	ctr_el0 = READ_SPECIALREG(ctr_el0);

	/* Read the log2 words in each D cache line */
	dcache_line_shift = CTR_DLINE_SIZE(ctr_el0);
	/* Get the D cache line size */
	dcache_line_size = sizeof(int) << dcache_line_shift;

	/* And the same for the I cache */
	icache_line_shift = CTR_ILINE_SIZE(ctr_el0);
	icache_line_size = sizeof(int) << icache_line_shift;

	idcache_line_size = std::min(dcache_line_size, icache_line_size);

	dczid_el0 = READ_SPECIALREG(dczid_el0);

	/* Check if dc zva is not prohibited */
	if (dczid_el0 & DCZID_DZP) {
		dczva_line_size = 0;
	} else {
		/* Same as with above calculations */
		dczva_line_shift = DCZID_BS_SIZE(dczid_el0);
		dczva_line_size = sizeof(int) << dczva_line_shift;
	}

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
	// No need
}

void
arch_cpu_global_TLB_invalidate(void)
{
	// No need
}

void
arch_cpu_invalidate_TLB_range(addr_t start, addr_t end)
{
	// No need
}

void
arch_cpu_invalidate_TLB_list(addr_t pages[], int num_pages)
{
	// No need
}

status_t
arch_cpu_shutdown(bool rebootSystem)
{
	return B_OK;
}
