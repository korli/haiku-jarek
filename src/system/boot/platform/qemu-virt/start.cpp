/*
 * Copyright 2003-2010, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */

#include <KernelExport.h>
#include <boot/platform.h>
#include <boot/heap.h>
#include <boot/stage2.h>
#include <arch/cpu.h>

#include <string.h>

#include "cpu.h"

static uint32 sBootOptions = 0;

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
	args.platform.fdt_phys = dtb_phys;

	arch_init_mmu(&args);

	cpu_init_via_device_tree(fdt::Node(dtb_phys));



	for(;;);
}
