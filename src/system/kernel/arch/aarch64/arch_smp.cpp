/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <boot/kernel_args.h>
#include <vm/vm.h>
#include <cpu.h>
#include <int.h>
#include <smp.h>

#include <arch/atomic.h>
#include <arch/cpu.h>
#include <arch/vm.h>
#include <arch/smp.h>

#include <string.h>
#include <stdio.h>

#include <algorithm>

status_t
arch_smp_init(kernel_args *args)
{
	return B_OK;
}


status_t
arch_smp_per_cpu_init(kernel_args *args, int32 cpu)
{
	return B_OK;
}


void
arch_smp_send_multicast_ici(CPUSet& cpuSet)
{
}


void
arch_smp_send_broadcast_ici(void)
{
}


void
arch_smp_send_ici(int32 target_cpu)
{
}
