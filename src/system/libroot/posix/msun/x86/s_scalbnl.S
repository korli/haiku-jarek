/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <arch/x86/asm.h>

__FBSDID("$FreeBSD$");
/* RCSID("$NetBSD: s_scalbnf.S,v 1.4 1999/01/02 05:15:40 kristerw Exp $") */

ENTRY(scalbnl)
	fildl	16(%esp)
	fldt	4(%esp)
	fscale
	fstp	%st(1)
	ret
END(scalbnl)

.globl ldexpl
.set	ldexpl,scalbnl

.weak __scalbnl
.equ __scalbnl, scalbnl

.weak __ldexpl
.equ __ldexpl, scalbnl

	.section .note.GNU-stack,"",%progbits
