/*
 * Based on code written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <system/arch/x86_64/asm_defs.h>

FUNCTION(scalbnl):
	movl	%edi,-4(%rsp)
	fildl	-4(%rsp)
	fldt	8(%rsp)
	fscale
	fstp	%st(1)
	ret
FUNCTION_END(scalbnl)

.globl	ldexpl
.set	ldexpl,scalbnl

.weak __scalbnl
.equ __scalbnl, scalbnl

.weak __ldexpl
.equ __ldexpl, scalbnl

	.section .note.GNU-stack,"",%progbits
