/*
 * Based on code written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <arch/x86/asm.h>
__FBSDID("$FreeBSD$")

ENTRY(copysignl)
	movl	24(%esp),%edx
	andl	$0x8000,%edx
	movl	12(%esp),%eax
	andl	$0x7fff,%eax
	orl	%edx,%eax
	movl	%eax,12(%esp)
	fldt	4(%esp)
	ret
END(copysignl)

.weak __copysignl
.equ __copysignl, copysignl

	.section .note.GNU-stack,"",%progbits
