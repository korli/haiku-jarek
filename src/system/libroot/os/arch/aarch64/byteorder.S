/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <asm_defs.h>

/* uint16 __swap_int16(uint16 value) */
ENTRY(__swap_int16)
	rev16	%w0, %w0
	ret
END(__swap_int16)

/* uint32 __swap_int32(uint32 value) */
ENTRY(__swap_int32)
	rev		%w0, %w0
	ret
END(__swap_int32)

/* uint64 __swap_int64(uint64 value) */
ENTRY(__swap_int64)
	rev		%x0, %x0
	ret
END(__swap_int64)

/* float __swap_float(float value) */
FUNCTION(__swap_float)
	vrev32	d0, d0
	ret
FUNCTION_END(__swap_float)

/* double __swap_double(double value) */
FUNCTION(__swap_double):
	vrev64	q0, q0
	ret
FUNCTION_END(__swap_double)
