/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <arch/x86/asm.h>

__FBSDID("$FreeBSD$");
/* RCSID("$NetBSD: e_remainderf.S,v 1.2 1995/05/08 23:49:47 jtc Exp $") */

ENTRY(remainderf)
	flds	8(%esp)
	flds	4(%esp)
1:	fprem1
	fstsw	%ax
	sahf
	jp	1b
	fstp	%st(1)
	ret
END(remainderf)

.weak __remainderf
.equ __remainderf, remainderf

.weak __ieee754_remainderf
.equ __ieee754_remainderf, remainderf

	.section .note.GNU-stack,"",%progbits
