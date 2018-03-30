/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

.section .text.head, "ax", @progbits

.globl _start
_start:
	b	real_start

.section .text

real_start:
	// We enter via linux boot protocol.
	// X0 already contains address of DTB

	// Set up initial stack. Contents of .bss is wiped
	// after call to aarch64_locore_init
	adr		x1, boot_stack_end
	mov		sp, x1

	// Return to virtual base address
	ldr		x30, =1f
	b		aarch64_locore_init
1:

	bl		_plat_start
	b	.

.section .bss

boot_stack_begin:
	.space		16384
boot_stack_end:

.text

.globl __arch_enter_kernel
.type __arch_enter_kernel, #function
__arch_enter_kernel:
	mov	sp, x2
	br x3
