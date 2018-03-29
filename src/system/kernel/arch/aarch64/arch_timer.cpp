/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <boot/stage2.h>
#include <kernel.h>

#include <arch/int.h>
#include <arch/cpu.h>

#include <console.h>
#include <debug.h>
#include <timer.h>
#include <int.h>
#include <safemode.h>

#include <arch/timer.h>

void
arch_timer_set_hardware_timer(bigtime_t timeout)
{
}


void
arch_timer_clear_hardware_timer(void)
{
}


int
arch_init_timer(kernel_args *args)
{
	return 0;
}
