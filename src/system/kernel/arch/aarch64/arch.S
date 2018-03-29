/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <asm_defs.h>

#include "syscall_numbers.h"


.text

ENTRY(arch_debug_call_with_fault_handler)
	ret
END(arch_debug_call_with_fault_handler)

ENTRY(_arch_cpu_user_memcpy)
	ret
END(_arch_cpu_user_memcpy)

ENTRY(_arch_cpu_user_memset)
	ret
END(_arch_cpu_user_memset)

ENTRY(_arch_cpu_user_strlcpy)
	ret
END(_arch_cpu_user_strlcpy)
