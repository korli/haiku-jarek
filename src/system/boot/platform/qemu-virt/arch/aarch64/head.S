/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

.section .text.head, "ax", @progbits

.globl _start
_start:
	b	real_start

.section .text

real_start:
	b	.
