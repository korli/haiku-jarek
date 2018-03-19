/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include "cpu.h"

#include <kernel/OS.h>
#include <kernel/boot/platform.h>
#include <kernel/boot/stdio.h>
#include <kernel/boot/kernel_args.h>
#include <kernel/boot/stage2.h>
#include <kernel/arch/cpu.h>
#include <arch_kernel.h>
#include <arch_system_info.h>

void cpu_init_via_device_tree(fdt::Node dtb)
{
	// Default to 1 CPU
	gKernelArgs.num_cpus = 1;

	fdt::Node cpus(dtb.GetPath("/cpus"));

	if(cpus.IsNull()) {
		// No node found
		return;
	}

	fdt::Property addressCells(cpus.GetProperty("#address-cells"));

	if(addressCells.IsNull()) {
		// No CPUs found
		return;
	}

	gKernelArgs.num_cpus = 0;

	for(auto child : cpus) {
		fdt::Property deviceType(child.GetProperty("device_type"));

		if(deviceType.IsNull())
			continue;
		if(strcmp((const char *)deviceType.Data(), "cpu") != 0)
			continue;

		fdt::Property status(child.GetProperty("status"));

		if((status.IsNull() ||
			!strcmp((const char *)status.Data(), "okay") ||
			!strcmp((const char *)status.Data(), "ok")) &&
			!child.GetProperty("enable-method").IsNull())
		{
			++gKernelArgs.num_cpus;
		}
	}

	if(!gKernelArgs.num_cpus) {
		gKernelArgs.num_cpus = 1;
	}
}
