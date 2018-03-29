/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <arch/system_info.h>

#include <string.h>

#include <KernelExport.h>
#include <OS.h>

#include <boot/kernel_args.h>
#include <cpu.h>
#include <kernel.h>
#include <smp.h>

status_t
arch_system_info_init(struct kernel_args *args)
{
	return B_OK;
}


void
arch_fill_topology_node(cpu_topology_node_info* node, int32 cpu)
{
}
