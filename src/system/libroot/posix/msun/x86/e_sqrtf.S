/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <arch/x86/asm.h>

__FBSDID("$FreeBSD$");
/* RCSID("$NetBSD: e_sqrtf.S,v 1.2 1995/05/08 23:50:14 jtc Exp $") */

ENTRY(sqrtf)
	flds	4(%esp)
	fsqrt
	ret
END(sqrtf)

.weak __sqrtf
.equ __sqrtf, sqrtf

.weak __ieee754_sqrtf
.equ __ieee754_sqrtf, sqrtf

	.section .note.GNU-stack,"",%progbits
