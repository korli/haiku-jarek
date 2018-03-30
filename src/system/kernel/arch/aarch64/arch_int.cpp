/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <cpu.h>
#include <int.h>
#include <kscheduler.h>
#include <team.h>
#include <thread.h>
#include <util/AutoLock.h>
#include <vm/vm.h>
#include <vm/vm_priv.h>

#include <arch/cpu.h>
#include <arch/int.h>

#include <stdio.h>

extern "C" void do_el1h_sync(iframe * frame)
{
	for(;;);
}

extern "C" void intr_irq_handler(iframe * frame)
{
	for(;;);
}

extern "C" void do_el1h_error(iframe * frame)
{
	for(;;);
}

extern "C" void do_el0_sync(iframe * frame)
{
	for(;;);
}

extern "C" void do_el0_error(iframe * frame)
{
	for(;;);
}

void
arch_int_enable_io_interrupt(int irq)
{
}


void
arch_int_disable_io_interrupt(int irq)
{
}


void
arch_int_configure_io_interrupt(int irq, uint32 config)
{
}


#undef arch_int_enable_interrupts
#undef arch_int_disable_interrupts
#undef arch_int_restore_interrupts
#undef arch_int_are_interrupts_enabled


void
arch_int_enable_interrupts(void)
{
	arch_int_enable_interrupts_inline();
}


int
arch_int_disable_interrupts(void)
{
	return arch_int_disable_interrupts_inline();
}


void
arch_int_restore_interrupts(int oldState)
{
	arch_int_restore_interrupts_inline(oldState);
}


bool
arch_int_are_interrupts_enabled(void)
{
	return arch_int_are_interrupts_enabled_inline();
}


void
arch_int_assign_to_cpu(int32 irq, int32 cpu)
{
}


status_t
arch_int_init(kernel_args* args)
{
	return B_OK;
}


status_t
arch_int_init_post_vm(kernel_args* args)
{
	return B_OK;
}


status_t
arch_int_init_io(kernel_args* args)
{
	return B_OK;
}


status_t
arch_int_init_post_device_manager(kernel_args* args)
{
	return B_OK;
}
