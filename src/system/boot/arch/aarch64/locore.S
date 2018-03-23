/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

/*-
 * Copyright (c) 2012-2014 Andrew Turner
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <kernel/arch/aarch64/armreg.h>
#include <kernel/arch/aarch64/hypervisor.h>
#include <system/arch/aarch64/asm_defs.h>

#define	VIRT_BITS	48

.section .text

/*
 * This function will perform basic MMU init.
 * It will return to the caller after basic machine
 * context is set up. Caller must provide initial stack
 * and pointer to the Device Tree Blob in x0.
 */
ENTRY(aarch64_locore_init)
	/* Save return address and DTB address */
	stp		x0, x30, [sp, #-16]!

	/* Drop to EL1 from HVC mode if needed */
	bl	drop_to_el1

	/*
	 * Disable the MMU
	 */
	dsb	sy
	mrs	x2, sctlr_el1
	bic	x2, x2, SCTLR_M
	msr	sctlr_el1, x2
	isb

	/*
	 * Setup TCR according to PARange bits from ID_AA64MMFR0_EL1.
	 */
	ldr	x2, tcr
	mrs	x3, id_aa64mmfr0_el1
	bfi	x2, x3, #32, #3
	msr	tcr_el1, x2

	/* Initialize the MMU */
	/* x0 - pointer to DTB */
	/* x1 - executable start (physical load address) */
	/* x2 - executable end (physical) */
	/* x3 - start of data */
	/* x4 - DTB allocator */
	/* x5 - virtual address of loader */
	/* x6 - physical address of .rodata */
	adrp	x1, __executable_start
	adrp	x2, _end
	add		x2, x2, :lo12:_end
	adrp	x3, __data_start__
	adrp	x4, gAArch64DTBAllocatorStore
	add		x4, x4, :lo12:gAArch64DTBAllocatorStore
	ldr		x5, =__executable_start
	adrp	x6, __etext
	bl		aarch64_initialize_mmu

	/* Load TTBR0 and TTBR1 */
	adrp 	x0, gAArch64PageDirectoryPhysicalTTBR0
	add		x0, x0, :lo12:gAArch64PageDirectoryPhysicalTTBR0
	ldr		x0, [x0]
	msr		ttbr0_el1, x0

	adrp 	x0, gAArch64PageDirectoryPhysicalTTBR1
	add		x0, x0, :lo12:gAArch64PageDirectoryPhysicalTTBR1
	ldr		x0, [x0]
	msr		ttbr1_el1, x0

	/* Set the context id */
	msr	contextidr_el1, xzr

	/* Clear the Monitor Debug System control register */
	msr	mdscr_el1, xzr

	/* Invalidate the TLB */
	tlbi	vmalle1is

	ldr	x2, mair
	msr	mair_el1, x2

	/* Setup SCTLR */
	ldr	x2, sctlr_set
	ldr	x3, sctlr_clear
	mrs	x1, sctlr_el1
	bic	x1, x1, x3	/* Clear the required bits */
	orr	x1, x1, x2	/* Set the required bits */
	msr	sctlr_el1, x1

	isb

	ldp		x0, x30, [sp], #16

	/* Wipe the .bss segment */
	ldr		x10, =__bss_start__
	ldr		x11, =__bss_end__
1:
	stp		xzr, xzr, [x10], #16
	stp		xzr, xzr, [x10], #16
	stp		xzr, xzr, [x10], #16
	stp		xzr, xzr, [x10], #16
	cmp		x10, x11
	b.lo	1b

	/* Enable VFP */
	mrs		x10, cpacr_el1
	bic		x10, x10, #CPACR_FPEN_MASK
	orr		x10, x10, #CPACR_FPEN_TRAP_NONE
	msr		cpacr_el1, x10

	ret
END(aarch64_locore_init)

/*
 * If we are started in EL2, configure the required hypervisor
 * registers and drop to EL1.
 */
drop_to_el1:
	mrs	x1, CurrentEL
	lsr	x1, x1, #2
	cmp	x1, #0x2
	b.eq	1f
	ret
1:
	/* Configure the Hypervisor */
	mov	x2, #(HCR_RW)
	msr	hcr_el2, x2

	/* Load the Virtualization Process ID Register */
	mrs	x2, midr_el1
	msr	vpidr_el2, x2

	/* Load the Virtualization Multiprocess ID Register */
	mrs	x2, mpidr_el1
	msr	vmpidr_el2, x2

	/* Set the bits that need to be 1 in sctlr_el1 */
	ldr	x2, .Lsctlr_res1
	msr	sctlr_el1, x2

	/* Don't trap to EL2 for exceptions */
	mov	x2, #CPTR_RES1
	msr	cptr_el2, x2

	/* Don't trap to EL2 for CP15 traps */
	msr	hstr_el2, xzr

	/* Enable access to the physical timers at EL1 */
	mrs	x2, cnthctl_el2
	orr	x2, x2, #(CNTHCTL_EL1PCTEN | CNTHCTL_EL1PCEN)
	msr	cnthctl_el2, x2

	/* Set the counter offset to a known value */
	msr	cntvoff_el2, xzr

	/* Hypervisor trap functions */
	adr	x2, hyp_vectors
	msr	vbar_el2, x2

	mov	x2, #(PSR_F | PSR_I | PSR_A | PSR_D | PSR_M_EL1h)
	msr	spsr_el2, x2

	/* Configure GICv3 CPU interface */
	mrs	x2, id_aa64pfr0_el1
	/* Extract GIC bits from the register */
	ubfx	x2, x2, #ID_AA64PFR0_GIC_SHIFT, #ID_AA64PFR0_GIC_BITS
	/* GIC[3:0] == 0001 - GIC CPU interface via special regs. supported */
	cmp	x2, #(ID_AA64PFR0_GIC_CPUIF_EN >> ID_AA64PFR0_GIC_SHIFT)
	b.ne	2f

	mrs	x2, S3_4_C12_C9_5
	orr	x2, x2, #ICC_SRE_EL2_EN	/* Enable access from insecure EL1 */
	orr	x2, x2, #ICC_SRE_EL2_SRE	/* Enable system registers */
	msr	S3_4_C12_C9_5, x2
2:

	/* Set the address to return to our return address */
	msr	elr_el2, x30
	isb

	eret

	.align 3
.Lsctlr_res1:
	.quad SCTLR_RES1

#define	VECT_EMPTY	\
	.align 7;	\
	1:	b	1b

	.align 11
hyp_vectors:
	VECT_EMPTY	/* Synchronous EL2t */
	VECT_EMPTY	/* IRQ EL2t */
	VECT_EMPTY	/* FIQ EL2t */
	VECT_EMPTY	/* Error EL2t */

	VECT_EMPTY	/* Synchronous EL2h */
	VECT_EMPTY	/* IRQ EL2h */
	VECT_EMPTY	/* FIQ EL2h */
	VECT_EMPTY	/* Error EL2h */

	VECT_EMPTY	/* Synchronous 64-bit EL1 */
	VECT_EMPTY	/* IRQ 64-bit EL1 */
	VECT_EMPTY	/* FIQ 64-bit EL1 */
	VECT_EMPTY	/* Error 64-bit EL1 */

	VECT_EMPTY	/* Synchronous 32-bit EL1 */
	VECT_EMPTY	/* IRQ 32-bit EL1 */
	VECT_EMPTY	/* FIQ 32-bit EL1 */
	VECT_EMPTY	/* Error 32-bit EL1 */

	.align 3
mair:
	.quad	MAIR_ATTR(MAIR_DEVICE_nGnRnE, 0) |	\
		MAIR_ATTR(MAIR_NORMAL_NC, 1) |		\
		MAIR_ATTR(MAIR_NORMAL_WB, 2) |		\
		MAIR_ATTR(MAIR_NORMAL_WT, 3)
tcr:
	.quad (TCR_TxSZ(64 - VIRT_BITS) | TCR_ASID_16 | TCR_TG1_4K | \
	    TCR_CACHE_ATTRS | TCR_SMP_ATTRS)
sctlr_set:
	/* Bits to set */
	.quad (SCTLR_LSMAOE | SCTLR_nTLSMD | SCTLR_UCI | SCTLR_SPAN | \
	    SCTLR_nTWE | SCTLR_nTWI | SCTLR_UCT | SCTLR_DZE | \
	    SCTLR_I | SCTLR_SED | SCTLR_SA0 | SCTLR_SA | SCTLR_C | \
	    SCTLR_M | SCTLR_CP15BEN | SCTLR_WXN)
sctlr_clear:
	/* Bits to clear */
	.quad (SCTLR_EE | SCTLR_EOE | SCTLR_IESB | SCTLR_UMA | \
	    SCTLR_ITD | SCTLR_THEE | SCTLR_A)
