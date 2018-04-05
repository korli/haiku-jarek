/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <asm_defs.h>

#include "asm_offsets.h"
#include "syscall_numbers.h"

.text

ENTRY(arch_debug_call_with_fault_handler)
	ret
END(arch_debug_call_with_fault_handler)

ENTRY(_arch_cpu_user_memcpy)
	ret
END(_arch_cpu_user_memcpy)

ENTRY(_arch_cpu_user_memset)
	ret
END(_arch_cpu_user_memset)

ENTRY(_arch_cpu_user_strlcpy)
	ret
END(_arch_cpu_user_strlcpy)

.text

.macro SAVE_REGISTERS el
	sub 	sp,  sp, #(IFRAME_sizeof)
	stp	    x0,  x1, [sp, #(IFRAME_x + 0 * 8)]
	stp	    x2,  x3, [sp, #(IFRAME_x + 2 * 8)]
	stp	    x4,  x5, [sp, #(IFRAME_x + 4 * 8)]
	stp	    x6,  x7, [sp, #(IFRAME_x + 6 * 8)]
	stp	    x8,  x9, [sp, #(IFRAME_x + 8 * 8)]
	stp	   x10, x11, [sp, #(IFRAME_x + 10 * 8)]
	stp	   x12, x13, [sp, #(IFRAME_x + 12 * 8)]
	stp	   x14, x15, [sp, #(IFRAME_x + 14 * 8)]
	stp	   x16, x17, [sp, #(IFRAME_x + 16 * 8)]
	stp	   x18, x19, [sp, #(IFRAME_x + 18 * 8)]
	stp	   x20, x21, [sp, #(IFRAME_x + 20 * 8)]
	stp	   x22, x23, [sp, #(IFRAME_x + 22 * 8)]
	stp	   x24, x25, [sp, #(IFRAME_x + 24 * 8)]
	stp	   x26, x27, [sp, #(IFRAME_x + 26 * 8)]
	stp	   x28, x29, [sp, #(IFRAME_x + 28 * 8)]
	mrs	   x10, elr_el1
	mrs	   x11, spsr_el1
	mrs	   x12, esr_el1
.if \el == 0
	mrs	   x13, sp_el0
.else
	mov	   x13, sp
.endif
	str	   x10, [sp, #(IFRAME_elr)]
	stp	   w11, w12, [sp, #(IFRAME_spsr)]
	stp	   x13, x30, [sp, #(IFRAME_sp)]
.endm

.macro RESTORE_REGISTERS el
.if \el == 1
	msr	   daifset, #2
.endif
	ldp	   x13, x30, [sp, #(IFRAME_sp)]
	ldp	   x10, x11, [sp, #(IFRAME_elr)]
.if \el == 0
	msr	   sp_el0, x13
.endif
	msr	   spsr_el1, x11
	msr	   elr_el1, x10
	ldp	    x0,  x1, [sp, #(IFRAME_x + 0 * 8)]
	ldp	    x2,  x3, [sp, #(IFRAME_x + 2 * 8)]
	ldp	    x4,  x5, [sp, #(IFRAME_x + 4 * 8)]
	ldp	    x6,  x7, [sp, #(IFRAME_x + 6 * 8)]
	ldp	    x8,  x9, [sp, #(IFRAME_x + 8 * 8)]
	ldp	   x10, x11, [sp, #(IFRAME_x + 10 * 8)]
	ldp	   x12, x13, [sp, #(IFRAME_x + 12 * 8)]
	ldp	   x14, x15, [sp, #(IFRAME_x + 14 * 8)]
	ldp	   x16, x17, [sp, #(IFRAME_x + 16 * 8)]
.if \el == 0
	ldp	   x18, x19, [sp, #(IFRAME_x + 18 * 8)]
	ldp	   x20, x21, [sp, #(IFRAME_x + 20 * 8)]
	ldp	   x22, x23, [sp, #(IFRAME_x + 22 * 8)]
	ldp	   x24, x25, [sp, #(IFRAME_x + 24 * 8)]
	ldp	   x26, x27, [sp, #(IFRAME_x + 26 * 8)]
	ldp	   x28, x29, [sp, #(IFRAME_x + 28 * 8)]
.else
	ldr	   x29, [sp, #(IFRAME_x + 29 * 8)]
.endif
.endm

.macro	vector	name
	.align 7
	b	handle_\name
.endm

.macro	vempty
	.align 7
	brk	0xfff
	1: b	1b
.endm

	.text
	.align 11
	.globl exception_vectors
exception_vectors:
	vempty			/* Synchronous EL1t */
	vempty			/* IRQ EL1t */
	vempty			/* FIQ EL1t */
	vempty			/* Error EL1t */

	vector el1h_sync	/* Synchronous EL1h */
	vector el1h_irq		/* IRQ EL1h */
	vempty			/* FIQ EL1h */
	vector el1h_error	/* Error EL1h */

	vector el0_sync		/* Synchronous 64-bit EL0 */
	vector el0_irq		/* IRQ 64-bit EL0 */
	vempty			/* FIQ 64-bit EL0 */
	vector el0_error	/* Error 64-bit EL0 */

	vector el0_sync		/* Synchronous 32-bit EL0 */
	vector el0_irq		/* IRQ 32-bit EL0 */
	vempty			/* FIQ 32-bit EL0 */
	vector el0_error	/* Error 32-bit EL0 */


	.text

ENTRY(handle_el1h_sync)
	SAVE_REGISTERS 1
	mov	x0, sp
	bl	do_el1h_sync
	RESTORE_REGISTERS 1
	eret
END(handle_el1h_sync)

ENTRY(handle_el1h_irq)
	SAVE_REGISTERS 1
	mov	x0, sp
	bl	intr_irq_handler
	RESTORE_REGISTERS 1
	eret
END(handle_el1h_irq)

ENTRY(handle_el1h_error)
	SAVE_REGISTERS 1
	mov	x0, sp
	bl	do_el1h_error
	RESTORE_REGISTERS 1
	eret
END(handle_el1h_error)

ENTRY(handle_el0_sync)
	SAVE_REGISTERS 0
	mov	x0, sp
	bl	do_el0_sync
	RESTORE_REGISTERS 0
	eret
END(handle_el0_sync)

ENTRY(handle_el0_irq)
	SAVE_REGISTERS 0
	mov	x0, sp
	bl	intr_irq_handler
	RESTORE_REGISTERS 0
	eret
END(handle_el0_irq)

ENTRY(handle_el0_error)
	SAVE_REGISTERS 0
	mov	x0, sp
	bl	do_el0_error
	RESTORE_REGISTERS 0
	eret
END(handle_el0_error)

.Lpage_mask:
	.word	PAGE_SIZEOF - 1

/*
 * Macro to handle the cache. This takes the start address in x0, length
 * in x1. It will corrupt x0, x1, x2, and x3.
 */
.macro cache_handle_range dcop = 0, ic = 0, icop = 0
.if \ic == 0
	ldr	x3, =dcache_line_size	/* Load the D cache line size */
.else
	ldr	x3, =idcache_line_size	/* Load the I & D cache line size */
.endif
	ldr	x3, [x3]
	sub	x4, x3, #1		/* Get the address mask */
	and	x2, x0, x4		/* Get the low bits of the address */
	add	x1, x1, x2		/* Add these to the size */
	bic	x0, x0, x4		/* Clear the low bit of the address */
1:
	dc	\dcop, x0
	dsb	ish
.if \ic != 0
	ic	\icop, x0
	dsb	ish
.endif
	add	x0, x0, x3		/* Move to the next line */
	subs	x1, x1, x3		/* Reduce the size */
	b.hi	1b			/* Check if we are done */
.if \ic != 0
	isb
.endif
	ret
.endm

ENTRY(arch_cpu_sync_icache)
	cache_handle_range	dcop = cvau
	ic	ialluis
	dsb	ish
	ret
END(arch_cpu_sync_icache)

ENTRY(aarch64_context_swap)
	mov		x6, sp
	adr		x7, 1f

	stp		x8,  x9, [x0, #(ARCH_THREAD_x + 8 * 8)]
	stp	   x10, x11, [x0, #(ARCH_THREAD_x + 10 * 8)]
	stp	   x12, x13, [x0, #(ARCH_THREAD_x + 12 * 8)]
	stp	   x14, x15, [x0, #(ARCH_THREAD_x + 14 * 8)]
	stp	   x16, x17, [x0, #(ARCH_THREAD_x + 16 * 8)]
	stp	   x18, x19, [x0, #(ARCH_THREAD_x + 18 * 8)]
	stp	   x20, x21, [x0, #(ARCH_THREAD_x + 20 * 8)]
	stp	   x22, x23, [x0, #(ARCH_THREAD_x + 22 * 8)]
	stp	   x24, x25, [x0, #(ARCH_THREAD_x + 24 * 8)]
	stp	   x26, x27, [x0, #(ARCH_THREAD_x + 26 * 8)]
	stp	   x28, x29, [x0, #(ARCH_THREAD_x + 28 * 8)]
	str	   x30, [x0, #(ARCH_THREAD_x + 30 * 8)]
	str	   x6, [x0, #(ARCH_THREAD_sp)]
	str	   x7, [x0, #(ARCH_THREAD_pc)]

1:

	ldp		x8,  x9, [x1, #(ARCH_THREAD_x + 8 * 8)]
	ldp	   x10, x11, [x1, #(ARCH_THREAD_x + 10 * 8)]
	ldp	   x12, x13, [x1, #(ARCH_THREAD_x + 12 * 8)]
	ldp	   x14, x15, [x1, #(ARCH_THREAD_x + 14 * 8)]
	ldp	   x16, x17, [x1, #(ARCH_THREAD_x + 16 * 8)]
	ldp	   x18, x19, [x1, #(ARCH_THREAD_x + 18 * 8)]
	ldp	   x20, x21, [x1, #(ARCH_THREAD_x + 20 * 8)]
	ldp	   x22, x23, [x1, #(ARCH_THREAD_x + 22 * 8)]
	ldp	   x24, x25, [x1, #(ARCH_THREAD_x + 24 * 8)]
	ldp	   x26, x27, [x1, #(ARCH_THREAD_x + 26 * 8)]
	ldp	   x28, x29, [x1, #(ARCH_THREAD_x + 28 * 8)]
	ldr	   x30, [x1, #(ARCH_THREAD_x + 30 * 8)]
	ldr	   x6, [x1, #(ARCH_THREAD_sp)]

	mov	   sp, x6
	ret
END(aarch64_context_swap)
