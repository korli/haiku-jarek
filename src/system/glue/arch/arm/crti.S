/*
 * Copyright 2017 Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#define FUNCTION(x) .global x; .type x,%function; x

.section .init
FUNCTION(_init):
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, r11, r12, r14}
	bl	__haiku_init_before

.section .fini
FUNCTION(_fini):
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, r11, r12, r14}
	bl	__haiku_term_before
