/*
 * Copyright 2003-2010, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */

#include <drivers/KernelExport.h>
#include <kernel/boot/platform.h>
#include <kernel/boot/heap.h>
#include <kernel/boot/stage2.h>
#include <kernel/arch/cpu.h>
#include <kernel/boot/memory.h>

#include <string.h>
#include <assert.h>

#include "cpu.h"
#include "serial.h"
#include "console.h"

static uint32 sBootOptions = BOOT_OPTION_MENU | BOOT_OPTION_DEBUG_OUTPUT;

extern "C" void
platform_start_kernel(void)
{
}


extern "C" void
platform_exit(void)
{
}

extern "C" uint32
platform_boot_options(void)
{
	return sBootOptions;
}

extern void (*__preinit_array_start[])();
extern void (*__preinit_array_end[])();
extern void (*__init_array_start[])();
extern void (*__init_array_end[])();

#define HEAP_SIZE ((1024 + 256) * 1024)

static void call_ctors(void) {
	void (*fn)(void);
	size_t array_size, n;

	array_size = __preinit_array_end - __preinit_array_start;
	for (n = 0; n < array_size; n++) {
		fn = __preinit_array_start[n];
		if ((uintptr_t) fn != 0 && (uintptr_t) fn != 1)
			fn();
	}
	array_size = __init_array_end - __init_array_start;
	for (n = 0; n < array_size; n++) {
		fn = __init_array_start[n];
		if ((uintptr_t) fn != 0 && (uintptr_t) fn != 1)
			fn();
	}
}

void arch_init_mmu(stage2_args * args);

extern "C" void _plat_start(const void * dtb_phys)
{
	stage2_args args;

	call_ctors();

	args.heap_size = HEAP_SIZE;
	args.arguments = NULL;

	arch_init_mmu(&args);

	{
		void * dtb_header = gBootVirtualMemoryMapper->MapPhysicalLoaderMemory((addr_t)dtb_phys,
			4096,
			true);

		assert(dtb_header);

		fdt::Node dtb_header_phys(dtb_header);
		assert(dtb_header_phys.IsDTBValid());
		size_t dtb_size = dtb_header_phys.DeviceTreeSize();
		args.platform.fdt_phys = (addr_t)dtb_phys;

		addr_t remap_base = args.platform.fdt_phys & ~(B_PAGE_SIZE - 1);
		addr_t remap_end = (args.platform.fdt_phys + dtb_size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);

		gBootVirtualMemoryMapper->UnmapPhysicalLoaderMemory(dtb_header, 4096);

		void * fdt_ldr_base;

		gBootLoaderVirtualRegionAllocator->AllocateVirtualMemoryRegion(&fdt_ldr_base,
				remap_end - remap_base,
				B_PAGE_SIZE,
				false,
				false);

		gBootVirtualMemoryMapper->MapVirtualMemoryRegion((addr_t)fdt_ldr_base,
				remap_base,
				remap_end - remap_base,
				B_KERNEL_READ_AREA);

		args.platform.fdt_virt = (void *)((addr_t)fdt_ldr_base + (args.platform.fdt_phys & (B_PAGE_SIZE - 1)));

	}

	cpu_init_via_device_tree(fdt::Node(args.platform.fdt_virt));

	serial_init(fdt::Node(args.platform.fdt_virt));
	console_init();

	main(&args);

	for(;;);
}

extern "C" void spin(bigtime_t timeout)
{

}
