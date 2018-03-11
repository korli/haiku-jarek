/*
 * Based on the i387 version written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <system/arch/x86_64/asm_defs.h>

FUNCTION(remainderf):
	movss	%xmm0,-4(%rsp)
	movss	%xmm1,-8(%rsp)
	flds	-8(%rsp)
	flds	-4(%rsp)
1:	fprem1
	fstsw	%ax
	testw	$0x400,%ax
	jne	1b
	fstps	-4(%rsp)
	movss	-4(%rsp),%xmm0
	fstp	%st
	ret

FUNCTION_END(remainderf)

.weak __remainderf
.equ __remainderf, remainderf

.weak __ieee754_remainderf
.equ __ieee754_remainderf, remainderf

	.section .note.GNU-stack,"",%progbits
