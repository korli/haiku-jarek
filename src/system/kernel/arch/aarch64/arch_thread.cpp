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

#include "AArch64PagingStructures.h"
#include "AArch64VMTranslationMap.h"

#include <kernel/arch/aarch64/armreg.h>

extern "C" void aarch64_context_swap(arch_thread * from, arch_thread * to);
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
	addr_t tls[TLS_FIRST_FREE_SLOT];

	thread->user_local_storage = thread->user_stack_base
		+ thread->user_stack_size;

	// initialize default TLS fields
	memset(tls, 0, sizeof(tls));
	tls[TLS_BASE_ADDRESS_SLOT] = thread->user_local_storage;
	tls[TLS_THREAD_ID_SLOT] = thread->id;
	tls[TLS_USER_THREAD_SLOT] = (addr_t)thread->user_thread;

	return user_memcpy((void*)thread->user_local_storage, tls, sizeof(tls));
}


void
arch_thread_context_switch(Thread* from, Thread* to)
{
	cpu_ent* cpuData = to->cpu;
	from->arch_info.tpidr_el0 = READ_SPECIALREG(tpidr_el0);

	AArch64PagingStructures* activePagingStructures = cpuData->arch.active_paging_structures;
	VMAddressSpace* toAddressSpace = to->team->address_space;

	AArch64PagingStructures* toPagingStructures;
	if (toAddressSpace != NULL
		&& (toPagingStructures = static_cast<AArch64VMTranslationMap *>(
				toAddressSpace->TranslationMap())->PagingStructures())
					!= activePagingStructures)
	{
		toPagingStructures->AddReference();
		cpuData->arch.active_paging_structures = toPagingStructures;

		// set the page directory, if it changes
		phys_addr_t newPageDirectory = toPagingStructures->pgdir_phys;
		uint64 newASID = toPagingStructures->asid;

		if (newPageDirectory != activePagingStructures->pgdir_phys) {
			__asm__ __volatile__(
				"dsb ish\n\t"
				"msr ttbr0_el1, %0\n\t"
				"dsb ish\n\t"
				"isb"
				:: "r"(newPageDirectory | (newASID << 48)));
		}

		// This CPU no longer uses the previous paging structures.
		activePagingStructures->RemoveReference();
	}

	if(from->flags & THREAD_FLAGS_SINGLE_STEP) {
		WRITE_SPECIALREG(mdscr_el1, READ_SPECIALREG(mdscr_el1) & ~DBG_MDSCR_SS);
		aarch64_isb();
	} else if(to->flags & THREAD_FLAGS_SINGLE_STEP) {
		WRITE_SPECIALREG(mdscr_el1, READ_SPECIALREG(mdscr_el1) | DBG_MDSCR_SS);
		aarch64_isb();
	}

	WRITE_SPECIALREG(tpidr_el0, to->arch_info.tpidr_el0);
	WRITE_SPECIALREG(tpidrro_el0, to->arch_info.tpidrro_el0);

	aarch64_context_swap(&from->arch_info, &to->arch_info);
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
