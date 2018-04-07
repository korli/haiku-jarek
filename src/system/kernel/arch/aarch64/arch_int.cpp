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

#include <kernel/arch/aarch64/armreg.h>

static void save_vfp_context(struct vfp_context *state)
{
	__uint128_t *vfp_state;
	uint64 fpcr, fpsr;

	vfp_state = state->regs;
	__asm __volatile(
	    "mrs	%0, fpcr		\n"
	    "mrs	%1, fpsr		\n"
	    "stp	q0,  q1,  [%2, #16 *  0]\n"
	    "stp	q2,  q3,  [%2, #16 *  2]\n"
	    "stp	q4,  q5,  [%2, #16 *  4]\n"
	    "stp	q6,  q7,  [%2, #16 *  6]\n"
	    "stp	q8,  q9,  [%2, #16 *  8]\n"
	    "stp	q10, q11, [%2, #16 * 10]\n"
	    "stp	q12, q13, [%2, #16 * 12]\n"
	    "stp	q14, q15, [%2, #16 * 14]\n"
	    "stp	q16, q17, [%2, #16 * 16]\n"
	    "stp	q18, q19, [%2, #16 * 18]\n"
	    "stp	q20, q21, [%2, #16 * 20]\n"
	    "stp	q22, q23, [%2, #16 * 22]\n"
	    "stp	q24, q25, [%2, #16 * 24]\n"
	    "stp	q26, q27, [%2, #16 * 26]\n"
	    "stp	q28, q29, [%2, #16 * 28]\n"
	    "stp	q30, q31, [%2, #16 * 30]\n"
	    : "=&r"(fpcr), "=&r"(fpsr) : "r"(vfp_state));

	state->fpcr = fpcr;
	state->fpsr = fpsr;
}

static void load_vfp_context(struct vfp_context *state)
{
	__uint128_t *vfp_state;
	uint64 fpcr, fpsr;

	vfp_state = state->regs;
	fpcr = state->fpcr;
	fpsr = state->fpsr;

	__asm __volatile(
	    "ldp	q0,  q1,  [%2, #16 *  0]\n"
	    "ldp	q2,  q3,  [%2, #16 *  2]\n"
	    "ldp	q4,  q5,  [%2, #16 *  4]\n"
	    "ldp	q6,  q7,  [%2, #16 *  6]\n"
	    "ldp	q8,  q9,  [%2, #16 *  8]\n"
	    "ldp	q10, q11, [%2, #16 * 10]\n"
	    "ldp	q12, q13, [%2, #16 * 12]\n"
	    "ldp	q14, q15, [%2, #16 * 14]\n"
	    "ldp	q16, q17, [%2, #16 * 16]\n"
	    "ldp	q18, q19, [%2, #16 * 18]\n"
	    "ldp	q20, q21, [%2, #16 * 20]\n"
	    "ldp	q22, q23, [%2, #16 * 22]\n"
	    "ldp	q24, q25, [%2, #16 * 24]\n"
	    "ldp	q26, q27, [%2, #16 * 26]\n"
	    "ldp	q28, q29, [%2, #16 * 28]\n"
	    "ldp	q30, q31, [%2, #16 * 30]\n"
	    "msr	fpcr, %0		\n"
	    "msr	fpsr, %1		\n"
	    : : "r"(fpcr), "r"(fpsr), "r"(vfp_state));
}
extern "C" void do_el1h_sync(iframe * frame)
{
	for(;;);
}

extern "C" void intr_irq_handler_el0(iframe * frame)
{
	panic("IRQs not implemented yet");
	for(;;);
}

extern "C" void intr_irq_handler_el1(iframe * frame)
{
	panic("IRQs not implemented yet");
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
