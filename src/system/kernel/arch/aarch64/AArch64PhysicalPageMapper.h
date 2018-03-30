/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef SYSTEM_KERNEL_ARCH_AARCH64_AARCH64PHYSICALPAGEMAPPER_H_
#define SYSTEM_KERNEL_ARCH_AARCH64_AARCH64PHYSICALPAGEMAPPER_H_

#include <kernel/vm/VMTranslationMap.h>

class TranslationMapPhysicalPageMapper {
public:
	void Delete();
	void* GetPageTableAt(phys_addr_t physicalAddress);
};

class AArch64PhysicalPageMapper final : public VMPhysicalPageMapper {
public:
	AArch64PhysicalPageMapper();
	virtual ~AArch64PhysicalPageMapper();

	virtual status_t GetPage(phys_addr_t physicalAddress,
			addr_t* _virtualAddress, void** _handle) override;
	virtual status_t PutPage(addr_t virtualAddress, void* handle) override;

	// get/put virtual address for physical page -- thread must be pinned the
	// whole time
	virtual status_t GetPageCurrentCPU(phys_addr_t physicalAddress,
			addr_t* _virtualAddress, void** _handle) override;
	virtual status_t PutPageCurrentCPU(addr_t virtualAddress,
			void* _handle) override;

	// get/put virtual address for physical in KDL
	virtual status_t GetPageDebug(phys_addr_t physicalAddress,
			addr_t* _virtualAddress, void** _handle) override;
	virtual status_t PutPageDebug(addr_t virtualAddress, void* handle) override;

	// memory operations on pages
	virtual status_t MemsetPhysical(phys_addr_t address, int value,
			phys_size_t length) override;
	virtual status_t MemcpyFromPhysical(void* to, phys_addr_t from,
			size_t length, bool user) override;
	virtual status_t MemcpyToPhysical(phys_addr_t to, const void* from,
			size_t length, bool user) override;
	virtual void MemcpyPhysicalPage(phys_addr_t to,	phys_addr_t from) override;
};

#endif /* SYSTEM_KERNEL_ARCH_AARCH64_AARCH64PHYSICALPAGEMAPPER_H_ */
