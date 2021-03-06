/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <arch/x86/asm.h>

__FBSDID("$FreeBSD$");
/* RCSID("$NetBSD: s_rintf.S,v 1.3 1995/05/09 00:17:22 jtc Exp $") */

ENTRY(rintf)
	flds	4(%esp)
	frndint
	ret
END(rintf)

.weak __rintf
.equ __rintf, rintf

	.section .note.GNU-stack,"",%progbits
