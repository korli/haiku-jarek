/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <arch/debug.h>

#include <stdio.h>
#include <stdlib.h>

#include <ByteOrder.h>
#include <TypeConstants.h>

#include <cpu.h>
#include <debug.h>
#include <debug_heap.h>
#include <elf.h>
#include <kernel.h>
#include <kimage.h>
#include <thread.h>
#include <vm/vm.h>
#include <vm/vm_types.h>
#include <vm/VMAddressSpace.h>
#include <vm/VMArea.h>

void
arch_debug_save_registers(arch_debug_registers* registers)
{
}


void
arch_debug_stack_trace(void)
{
}


bool
arch_debug_contains_call(Thread* thread, const char* symbol, addr_t start,
	addr_t end)
{
	return false;
}

void*
arch_debug_get_caller(void)
{
	return nullptr;
}

int32
arch_debug_get_stack_trace(addr_t* returnAddresses, int32 maxCount,
	int32 skipIframes, int32 skipFrames, uint32 flags)
{
	return 0;
}


/*!	Returns the program counter of the currently debugged (respectively this)
	thread where the innermost interrupts happened. \a _isSyscall, if specified,
	is set to whether this interrupt frame was created by a syscall. Returns
	\c NULL, if there's no such frame or a problem occurred retrieving it;
	\a _isSyscall won't be set in this case.
*/
void*
arch_debug_get_interrupt_pc(bool* _isSyscall)
{
	return NULL;
}


/*!	Sets the current thread to \c NULL.
	Invoked in the kernel debugger only.
*/
void
arch_debug_unset_current_thread(void)
{
}


bool
arch_is_debug_variable_defined(const char* variableName)
{
	return false;
}


status_t
arch_set_debug_variable(const char* variableName, uint64 value)
{
	return B_ENTRY_NOT_FOUND;
}


status_t
arch_get_debug_variable(const char* variableName, uint64* value)
{
	return B_ENTRY_NOT_FOUND;
}

ssize_t
arch_debug_gdb_get_registers(char* buffer, size_t bufferSize)
{
	return B_NOT_SUPPORTED;
}


status_t
arch_debug_init(kernel_args* args)
{
	return B_NO_ERROR;
}
