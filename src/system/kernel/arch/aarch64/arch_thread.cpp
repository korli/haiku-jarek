/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <arch/thread.h>

#include <string.h>

#include <arch_cpu.h>
#include <cpu.h>
#include <kernel.h>
#include <ksignal.h>
#include <int.h>
#include <team.h>
#include <thread.h>
#include <tls.h>
#include <vm/vm_types.h>
#include <vm/VMAddressSpace.h>

status_t
arch_team_init_team_struct(Team* p, bool kernel)
{
	return B_OK;
}


/*!	Initializes the user-space TLS local storage pointer in
	the thread structure, and the reserved TLS slots.
	
	Is called from _create_user_thread_kentry().
*/
status_t
arch_thread_init_tls(Thread* thread)
{
	return B_OK;
}


void
arch_thread_context_switch(Thread* from, Thread* to)
{
}


bool
arch_on_signal_stack(Thread *thread)
{
	return false;
}


/*!	Saves everything needed to restore the frame in the child fork in the
	arch_fork_arg structure to be passed to arch_restore_fork_frame().
	Also makes sure to return the right value.
*/
void
arch_store_fork_frame(struct arch_fork_arg* arg)
{
}


/*!	Restores the frame from a forked team as specified by the provided
	arch_fork_arg structure.
	Needs to be called from within the child team, i.e. instead of
	arch_thread_enter_userspace() as thread "starter".
	This function does not return to the caller, but will enter userland
	in the child team at the same position where the parent team left of.

	\param arg The architecture specific fork arguments including the
		environment to restore. Must point to a location somewhere on the
		caller's stack.
*/
void
arch_restore_fork_frame(struct arch_fork_arg* arg)
{
}

status_t
arch_thread_init(kernel_args* args)
{
	return B_OK;
}

status_t
arch_thread_init_thread_struct(Thread* thread)
{
	return B_OK;
}

void
arch_thread_init_kthread_stack(Thread* thread, void* _stack, void* _stackTop,
	void (*function)(void*), const void* data)
{

}

void
arch_thread_dump_info(void* info)
{

}

status_t
arch_thread_enter_userspace(Thread* thread, addr_t entry, void* args1,
	void* args2)
{
	return B_OK;
}

status_t
arch_setup_signal_frame(Thread* thread, struct sigaction* action,
	struct signal_frame_data* signalFrameData)
{
	return B_OK;
}

int64
arch_restore_signal_frame(struct signal_frame_data* signalFrameData)
{
	return 0;
}
