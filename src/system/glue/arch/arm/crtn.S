/*
 * Copyright 2017 Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

.section .init
	pop	{r0, r1, r4, r5, r6, r7, r8, r9, r10, r11, r12, r14}
	b	__haiku_init_after

.section .fini
	pop	{r0, r1, r4, r5, r6, r7, r8, r9, r10, r11, r12, r14}
	b	__haiku_term_after
