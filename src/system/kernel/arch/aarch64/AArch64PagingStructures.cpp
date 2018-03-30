/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include "AArch64PagingStructures.h"
#include <kernel/util/OpenHashTable.h>
#include <kernel/util/AutoLock.h>
#include <kernel/arch/aarch64/armreg.h>
#include <drivers/KernelExport.h>

struct AArch64PagingStructuresHashDefinition {
	typedef int32_t						KeyType;
	typedef	AArch64PagingStructures		ValueType;

	size_t HashKey(KeyType key) const
	{
		return key >> 1;
	}

	size_t Hash(ValueType* value) const
	{
		return HashKey(value->asid);
	}

	bool Compare(KeyType key, ValueType* value) const
	{
		return value->asid == key;
	}

	ValueType*& GetLink(ValueType* value) const
	{
		return value->asid_hash_next;
	}
};

static BOpenHashTable<AArch64PagingStructuresHashDefinition> sASIDHashTable;
static spinlock sASIDHashTableLock = B_SPINLOCK_INITIALIZER;
static uint32_t sNextASID = 1;
uint32 AArch64PagingStructures::sMaximumASID;
uint64 AArch64PagingStructures::sPhysicalAddressMask;
bool AArch64PagingStructures::sPrivilegedAccessNeverSupported;

AArch64PagingStructures::AArch64PagingStructures()
{
}

AArch64PagingStructures::~AArch64PagingStructures() {
	if(asid != -1) {
		InterruptsSpinLocker locker(sASIDHashTableLock);
		sASIDHashTable.RemoveUnchecked(this);
		sNextASID = asid;
		// Invalidate TLB
		__asm__ __volatile__("tlbi aside1, %0" :: "r"(uint64(asid) << 48));
	}
	free(virtual_pgdir);
}

bool AArch64PagingStructures::AllocateASID() {
	InterruptsSpinLocker locker(sASIDHashTableLock);
	if(sASIDHashTable.CountElements() >= (sMaximumASID + 1)) {
		return false;
	}
	while(sASIDHashTable.Lookup(sNextASID)) {
		++sNextASID;
		if(sNextASID > sMaximumASID) {
			sNextASID = 1;
		}
	}
	this->asid = sNextASID;
	sASIDHashTable.InsertUnchecked(this);
	return true;
}

void AArch64PagingStructures::InitKernelASID() {
	sASIDHashTable.Init(128);
	InterruptsSpinLocker locker(sASIDHashTableLock);
	this->asid = READ_SPECIALREG(ttbr0_el1) >> 48;
	sASIDHashTable.InsertUnchecked(this);
}

void AArch64PagingStructures::RemoveReference() {
	if(ref_count.fetch_sub(1, std::memory_order_release) == 1) {
		if(are_interrupts_enabled()) {
			delete this;
		} else {
			deferred_delete(this);
		}
	}
}

void AArch64PagingStructures::Init(uint64 * virtualPgdir, phys_addr_t physicalPgdir)
{
	virtual_pgdir = virtualPgdir;
	pgdir_phys = physicalPgdir;
}
