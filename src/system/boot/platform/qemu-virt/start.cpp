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
#include <sys/cdefs.h>
#include <system/vm_defs.h>

#include "cpu.h"
#include "serial.h"
#include "console.h"

static uint32 sBootOptions = BOOT_OPTION_MENU | BOOT_OPTION_DEBUG_OUTPUT;

extern "C" void __arch_enter_kernel(void * args, addr_t sp, addr_t entry) __dead2;

extern "C" void
platform_start_kernel(void)
{
	addr_t stackTop = gKernelArgs.cpu_kstack[0].start + gKernelArgs.cpu_kstack[0].size;
	addr_t entry;

	// 64-bit kernel entry is all handled in long.cpp
	if (gKernelArgs.kernel_image->elf_class == ELFCLASS64) {
		preloaded_elf64_image *image = static_cast<preloaded_elf64_image *>(gKernelArgs.kernel_image.Pointer());
		entry = image->elf_header.e_entry;
	} else {
		preloaded_elf32_image *image = static_cast<preloaded_elf32_image *>(gKernelArgs.kernel_image.Pointer());
		entry = image->elf_header.e_entry;
	}

	gBootPhysicalMemoryAllocator->GenerateKernelArguments();
	gBootKernelVirtualRegionAllocator.GenerateKernelArguments();

	// We're about to enter the kernel -- disable console output.
	stdout = NULL;

	dprintf("kernel entry at %" B_PRIx64 "\n", entry);

	__arch_enter_kernel(&gKernelArgs, stackTop, entry);

	panic("kernel returned!\n");
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

	for(uint32 i = 0 ; i < gKernelArgs.num_cpus ; ++i) {
		void * spBase;
		status_t error = gBootKernelVirtualRegionAllocator.AllocateVirtualMemoryRegion(&spBase,
				KERNEL_STACK_SIZE + KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE,
				KERNEL_STACK_SIZE + KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE,
				false,
				true);

		assert(error == B_OK);

		uint64 cpu_stack_phys = 0;

		error = gBootPhysicalMemoryAllocator->AllocatePhysicalMemory(KERNEL_STACK_SIZE,
				B_PAGE_SIZE,
				cpu_stack_phys);

		assert(error == B_OK);

		error = gBootVirtualMemoryMapper->MapVirtualMemoryRegion(
				(addr_t)spBase + KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE, cpu_stack_phys,
				KERNEL_STACK_SIZE, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_KERNEL_STACK_AREA);

		assert(error == B_OK);

		gKernelArgs.cpu_kstack[i].start = (addr_t)spBase;
		gKernelArgs.cpu_kstack[i].size = KERNEL_STACK_SIZE + KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE;
	}

	main(&args);

	for(;;);
}

extern "C" void spin(bigtime_t timeout)
{

}
