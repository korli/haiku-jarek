/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <arch/x86/asm.h>
__FBSDID("$FreeBSD$")

ENTRY(fmodf)
	flds	8(%esp)
	flds	4(%esp)
1:	fprem
	fstsw	%ax
	sahf
	jp	1b
	fstp	%st(1)
	ret
END(fmodf)

.weak __fmodf
.equ __fmodf, fmodf

.weak __ieee754_fmodf
.equ __ieee754_fmodf, fmodf

	.section .note.GNU-stack,"",%progbits
