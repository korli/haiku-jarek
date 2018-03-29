/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <arch/user_debugger.h>

#include <string.h>

#include <debugger.h>
#include <driver_settings.h>
#include <int.h>
#include <team.h>
#include <thread.h>
#include <util/AutoLock.h>

void
arch_clear_team_debug_info(arch_team_debug_info* info)
{
}


void
arch_destroy_team_debug_info(arch_team_debug_info* info)
{
}


void
arch_clear_thread_debug_info(arch_thread_debug_info* info)
{
}


void
arch_destroy_thread_debug_info(arch_thread_debug_info* info)
{
}

void
arch_update_thread_single_step()
{
}

void
arch_set_debug_cpu_state(const debug_cpu_state* cpuState)
{
}


void
arch_get_debug_cpu_state(debug_cpu_state* cpuState)
{
}

status_t
arch_get_thread_debug_cpu_state(Thread* thread, debug_cpu_state* cpuState)
{
	return B_BAD_VALUE;
}


status_t
arch_set_breakpoint(void* address)
{
	return B_BAD_VALUE;
}


status_t
arch_clear_breakpoint(void* address)
{
	return B_BAD_VALUE;
}


status_t
arch_set_watchpoint(void* address, uint32 type, int32 length)
{
	return B_BAD_VALUE;
}


status_t
arch_clear_watchpoint(void* address)
{
	return B_BAD_VALUE;
}


bool
arch_has_breakpoints(arch_team_debug_info* info)
{
	return false;
}


#if KERNEL_BREAKPOINTS

status_t
arch_set_kernel_breakpoint(void* address)
{
	return B_BAD_VALUE;
}


status_t
arch_clear_kernel_breakpoint(void* address)
{
	return B_BAD_VALUE;
}


status_t
arch_set_kernel_watchpoint(void* address, uint32 type, int32 length)
{
	return B_BAD_VALUE;
}


status_t
arch_clear_kernel_watchpoint(void* address)
{
	return B_BAD_VALUE;
}

#endif	// KERNEL_BREAKPOINTS
