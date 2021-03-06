/*
 * Based on the i387 version written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <system/arch/x86_64/asm_defs.h>

FUNCTION(fmodf):
	movss	%xmm0,-4(%rsp)
	movss	%xmm1,-8(%rsp)
	flds	-8(%rsp)
	flds	-4(%rsp)
1:	fprem
	fstsw	%ax
	testw	$0x400,%ax
	jne	1b
	fstps	-4(%rsp)
	movss	-4(%rsp),%xmm0
	fstp	%st
	ret
FUNCTION_END(fmodf)

.weak __fmodf
.equ __fmodf, fmodf

.weak __ieee754_fmodf
.equ __ieee754_fmodf, fmodf

	.section .note.GNU-stack,"",%progbits
