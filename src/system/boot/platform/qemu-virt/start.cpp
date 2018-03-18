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
