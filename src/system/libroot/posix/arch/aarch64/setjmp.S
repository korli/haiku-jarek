#include <system/arch/aarch64/asm_defs.h>

.text

#define	_JB_MAGIC__SETJMP	0xfb5d25837d7ff700
#define	_JB_MAGIC_SETJMP	0xfb5d25837d7ff701

ENTRY(_setjmp)
	/* Store the magic value and stack pointer */
	ldr	x8, .Lmagic_
	mov	x9, sp
	stp	x8, x9, [x0], #16

	/* Store the general purpose registers and lr */
	stp	x19, x20, [x0], #16
	stp	x21, x22, [x0], #16
	stp	x23, x24, [x0], #16
	stp	x25, x26, [x0], #16
	stp	x27, x28, [x0], #16
	stp	x29, lr, [x0], #16

	/* Store the vfp registers */
	stp	d8, d9, [x0], #16
	stp	d10, d11, [x0], #16
	stp	d12, d13, [x0], #16
	stp	d14, d15, [x0]

	/* Return value */
	mov	x0, #0
	ret
	.align	3
.Lmagic_:
	.quad	_JB_MAGIC__SETJMP
END(_setjmp)

ENTRY(_longjmp)
	/* Check the magic value */
	ldr	x8, [x0], #8
	ldr	x9, .Lmagic_
	cmp	x8, x9
	b.ne	botch

	/* Restore the stack pointer */
	ldr	x8, [x0], #8
	mov	sp, x8

	/* Restore the general purpose registers and lr */
	ldp	x19, x20, [x0], #16
	ldp	x21, x22, [x0], #16
	ldp	x23, x24, [x0], #16
	ldp	x25, x26, [x0], #16
	ldp	x27, x28, [x0], #16
	ldp	x29, lr, [x0], #16

	/* Restore the vfp registers */
	ldp	d8, d9, [x0], #16
	ldp	d10, d11, [x0], #16
	ldp	d12, d13, [x0], #16
	ldp	d14, d15, [x0]

	/* Load the return value */
	mov	x0, x1
	ret

botch:
	b	abort
END(_longjmp)

/* int sigsetjmp(jmp_buf buffer, int saveMask) */
ENTRY(setjmp)
ENTRY(sigsetjmp)
ENTRY(__sigsetjmp)
	sub	sp, sp, #16
	stp	x0, lr, [sp]

	bl __setjmp_save_sigs

	ldp	x0, lr, [sp]
	add	sp, sp, #16

	/* Store the magic value and stack pointer */
	ldr	x2, .Lmagic
	mov	x3, sp
	stp	 x2,  x3, [x0], #16

	/* Store the general purpose registers and lr */
	stp	x19, x20, [x0], #16
	stp	x21, x22, [x0], #16
	stp	x23, x24, [x0], #16
	stp	x25, x26, [x0], #16
	stp	x27, x28, [x0], #16
	stp	x29, x30, [x0], #16

	/* Store the vfp registers */
	stp	d8, d9, [x0], #16
	stp	d10, d11, [x0], #16
	stp	d12, d13, [x0], #16
	stp	d14, d15, [x0]

	ret

	.align	3
.Lmagic:
	.quad	_JB_MAGIC_SETJMP
END(setjmp)

ENTRY(siglongjmp)
ENTRY(longjmp)
	mov	x7, x0

	/* Check the magic value */
	ldr	x2, [x0], #8
	ldr	x3, .Lmagic
	cmp	x2, x3
	bne	abort

	/* Restore the stack pointer */
	ldr	x8, [x0], #8
	mov	sp, x8

	/* Restore the general purpose registers and lr */
	ldp	x19, x20, [x0], #16
	ldp	x21, x22, [x0], #16
	ldp	x23, x24, [x0], #16
	ldp	x25, x26, [x0], #16
	ldp	x27, x28, [x0], #16
	ldp	x29, lr, [x0], #16

	/* Restore the vfp registers */
	ldp	d8, d9, [x0], #16
	ldp	d10, d11, [x0], #16
	ldp	d12, d13, [x0], #16
	ldp	d14, d15, [x0]

	mov	x0, x7
	b	__longjmp_return
END(siglongjmp)
