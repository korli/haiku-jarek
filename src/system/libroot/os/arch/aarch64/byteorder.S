/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <asm_defs.h>

/* uint16 __swap_int16(uint16 value) */
ENTRY(__swap_int16)
	rev16	w0, w0
	ret
END(__swap_int16)

/* uint32 __swap_int32(uint32 value) */
ENTRY(__swap_int32)
	rev		w0, w0
	ret
END(__swap_int32)

/* uint64 __swap_int64(uint64 value) */
ENTRY(__swap_int64)
	rev		x0, x0
	ret
END(__swap_int64)

/* float __swap_float(float value) */
ENTRY(__swap_float)
	rev32	v0.8b, v0.8b
	ret
END(__swap_float)

/* double __swap_double(double value) */
ENTRY(__swap_double)
	rev64	v0.8b, v0.8b
	ret
END(__swap_double)
