/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <OS.h>

#include <cpu.h>
#include <kernel.h>
#include <smp.h>
#include <vm/vm.h>
#include <vm/vm_types.h>
#include <vm/VMAddressSpace.h>

#include "AArch64PhysicalPageMapper.h"
#include "AArch64PagingStructures.h"

void TranslationMapPhysicalPageMapper::Delete() {
	delete this;
}

void* TranslationMapPhysicalPageMapper::GetPageTableAt(phys_addr_t physicalAddress) {
	ASSERT(physicalAddress % B_PAGE_SIZE == 0);

	return (void*)(addr_t)(physicalAddress + DMAP_BASE);
}

AArch64PhysicalPageMapper::AArch64PhysicalPageMapper() {
}

AArch64PhysicalPageMapper::~AArch64PhysicalPageMapper() {
}

status_t AArch64PhysicalPageMapper::GetPage(phys_addr_t physicalAddress,
		addr_t* _virtualAddress, void** _handle)
{
	if(physicalAddress > AArch64PagingStructures::sPhysicalAddressMask)
		return B_BAD_ADDRESS;
	*_virtualAddress = physicalAddress + DMAP_BASE;
	return B_OK;
}

status_t AArch64PhysicalPageMapper::PutPage(addr_t, void*)
{
	return B_OK;
}

status_t AArch64PhysicalPageMapper::GetPageCurrentCPU(phys_addr_t physicalAddress,
		addr_t* _virtualAddress, void** _handle)
{
	if(physicalAddress > AArch64PagingStructures::sPhysicalAddressMask)
		return B_BAD_ADDRESS;
	*_virtualAddress = physicalAddress + DMAP_BASE;
	return B_OK;
}

status_t AArch64PhysicalPageMapper::PutPageCurrentCPU(addr_t, void*)
{
	return B_OK;
}

// get/put address for physical in KDL
status_t AArch64PhysicalPageMapper::GetPageDebug(phys_addr_t physicalAddress,
		addr_t* _virtualAddress, void** _handle)
{
	if(physicalAddress > AArch64PagingStructures::sPhysicalAddressMask)
		return B_BAD_ADDRESS;
	*_virtualAddress = physicalAddress + DMAP_BASE;
	return B_OK;
}

status_t AArch64PhysicalPageMapper::PutPageDebug(addr_t, void*)
{
	return B_OK;
}

// memory operations on pages
status_t AArch64PhysicalPageMapper::MemsetPhysical(phys_addr_t address, int value,
		phys_size_t length)
{
	if(address > AArch64PagingStructures::sPhysicalAddressMask || address + length > AArch64PagingStructures::sPhysicalAddressMask)
		return B_BAD_ADDRESS;
	memset((void *)(DMAP_BASE + address), value, length);
	return B_OK;
}

status_t AArch64PhysicalPageMapper::MemcpyFromPhysical(void* to, phys_addr_t from,
		size_t length, bool user)
{
	if(from > AArch64PagingStructures::sPhysicalAddressMask || from + length > AArch64PagingStructures::sPhysicalAddressMask)
		return B_BAD_ADDRESS;
	if(user)
		return user_memcpy(to, (const void *)(DMAP_BASE + from), length);
	memcpy(to, (const void *)(DMAP_BASE + from), length);
	return B_OK;
}

status_t AArch64PhysicalPageMapper::MemcpyToPhysical(phys_addr_t to, const void* from,
		size_t length, bool user)
{
	if(to > AArch64PagingStructures::sPhysicalAddressMask || to + length > AArch64PagingStructures::sPhysicalAddressMask)
		return B_BAD_ADDRESS;
	if(user)
		return user_memcpy((void *)(DMAP_BASE + to), from, length);
	memcpy((void *)(DMAP_BASE + to), from, length);
	return B_OK;
}

void AArch64PhysicalPageMapper::MemcpyPhysicalPage(phys_addr_t to,	phys_addr_t from)
{
	memcpy((void *)(to + DMAP_BASE), (const void *)(from + DMAP_BASE), B_PAGE_SIZE);
}
