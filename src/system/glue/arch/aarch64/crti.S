/*
 * Copyright 2017 Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#define FUNCTION(x) .global x; .type x,%function; x

.section .init
FUNCTION(_init):
	stp	x29, x30, [sp, #-16]!
	stp	x27, x28, [sp, #-16]!
	stp	x25, x26, [sp, #-16]!
	stp	x23, x24, [sp, #-16]!
	stp	x21, x22, [sp, #-16]!
	stp	x19, x20, [sp, #-16]!
	stp  x0,  x1, [sp, #-16]!
	bl	__haiku_init_before

.section .fini
FUNCTION(_fini):
	stp	x29, x30, [sp, #-16]!
	stp	x27, x28, [sp, #-16]!
	stp	x25, x26, [sp, #-16]!
	stp	x23, x24, [sp, #-16]!
	stp	x21, x22, [sp, #-16]!
	stp	x19, x20, [sp, #-16]!
	stp  x0,  x1, [sp, #-16]!
	bl	__haiku_term_before
