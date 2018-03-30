/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef SYSTEM_KERNEL_ARCH_AARCH64_AARCH64PAGINGSTRUCTURES_H_
#define SYSTEM_KERNEL_ARCH_AARCH64_AARCH64PAGINGSTRUCTURES_H_

#include <SupportDefs.h>
#include <heap.h>
#include <smp.h>
#include <atomic>

struct AArch64PagingStructures : public DeferredDeletable {
	phys_addr_t						pgdir_phys = 0;
	int32_t							asid = -1;
	AArch64PagingStructures *		asid_hash_next = nullptr;
	std::atomic<int>				ref_count{1};
	uint64 *						virtual_pgdir = nullptr;

	AArch64PagingStructures();
	virtual ~AArch64PagingStructures();

	inline void AddReference() {
		ref_count.fetch_add(1, std::memory_order_relaxed);
	}

	void RemoveReference();

	static uint32 sMaximumASID;
	static uint64 sPhysicalAddressMask;
	static bool sPrivilegedAccessNeverSupported;

	bool AllocateASID();
	void InitKernelASID();

	void Init(uint64 * virtualPgdir, phys_addr_t physicalPgdir);
};

#endif /* SYSTEM_KERNEL_ARCH_AARCH64_AARCH64PAGINGSTRUCTURES_H_ */
