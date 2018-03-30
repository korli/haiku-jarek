/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <arch/vm_translation_map.h>

#include <boot/kernel_args.h>
#include <safemode.h>

#include "AArch64PagingMethod.h"

status_t
arch_vm_translation_map_create_map(bool kernel, VMTranslationMap** _map)
{
	return AArch64PagingMethod::CreateTranslationMap(kernel, _map);
}


status_t
arch_vm_translation_map_init(kernel_args *args,
	VMPhysicalPageMapper** _physicalPageMapper)
{
	return AArch64PagingMethod::Init(args, _physicalPageMapper);
}


status_t
arch_vm_translation_map_init_post_sem(kernel_args *args)
{
	return B_OK;
}


status_t
arch_vm_translation_map_init_post_area(kernel_args *args)
{
	return AArch64PagingMethod::InitPostArea(args);
}


status_t
arch_vm_translation_map_early_map(kernel_args *args, addr_t va, phys_addr_t pa,
	uint8 attributes, phys_addr_t (*get_free_page)(kernel_args *))
{
	return AArch64PagingMethod::MapEarly(args, va, pa, attributes, get_free_page);
}


/*!	Verifies that the page at the given virtual address can be accessed in the
	current context.

	This function is invoked in the kernel debugger. Paranoid checking is in
	order.

	\param virtualAddress The virtual address to be checked.
	\param protection The area protection for which to check. Valid is a bitwise
		or of one or more of \c B_KERNEL_READ_AREA or \c B_KERNEL_WRITE_AREA.
	\return \c true, if the address can be accessed in all ways specified by
		\a protection, \c false otherwise.
*/
bool
arch_vm_translation_map_is_kernel_page_accessible(addr_t virtualAddress,
	uint32 protection)
{
	return AArch64PagingMethod::IsKernelPageAccessible(virtualAddress, protection);
}
