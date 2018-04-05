/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef SYSTEM_KERNEL_ARCH_AARCH64_AARCH64VMTRANSLATIONMAP_H_
#define SYSTEM_KERNEL_ARCH_AARCH64_AARCH64VMTRANSLATIONMAP_H_

#include <kernel/vm/VMTranslationMap.h>

struct AArch64PagingStructures;
class TranslationMapPhysicalPageMapper;

class AArch64VMTranslationMap final : public VMTranslationMap {
public:
	AArch64VMTranslationMap();
	virtual ~AArch64VMTranslationMap();

	status_t Init(bool kernel);

	virtual bool Lock() override;
	virtual void Unlock() override;

	virtual addr_t MappedSize() const override;
	virtual size_t MaxPagesNeededToMap(addr_t start, addr_t end) const override;

	virtual status_t Map(addr_t virtualAddress, phys_addr_t physicalAddress,
			uint32 attributes, uint32 memoryType,
			vm_page_reservation* reservation) override;
	virtual status_t Unmap(addr_t start, addr_t end) override;

	virtual status_t DebugMarkRangePresent(addr_t start, addr_t end,
			bool markPresent) override;

	// map not locked
	virtual status_t UnmapPage(VMArea* area, addr_t address,
			bool updatePageQueue) override;
	virtual void UnmapPages(VMArea* area, addr_t base, size_t size,
			bool updatePageQueue) override;
	virtual void UnmapArea(VMArea* area, bool deletingAddressSpace,
			bool ignoreTopCachePageFlags) override;

	virtual status_t Query(addr_t virtualAddress, phys_addr_t* _physicalAddress,
			uint32* _flags) override;
	virtual status_t QueryInterrupt(addr_t virtualAddress,
			phys_addr_t* _physicalAddress, uint32* _flags) override;

	virtual status_t Protect(addr_t base, addr_t top, uint32 attributes,
			uint32 memoryType) override;

	virtual status_t ClearFlags(addr_t virtualAddress, uint32 flags) override;

	virtual bool ClearAccessedAndModified(VMArea* area, addr_t address,
			bool unmapIfUnaccessed, bool& _modified) override;

	virtual void Flush() override;

	inline AArch64PagingStructures * PagingStructures() const {
		return fPagingStructures;
	}

private:
	bool fIsKernelMap = false;
	AArch64PagingStructures * fPagingStructures = nullptr;
	TranslationMapPhysicalPageMapper * fPageMapper = nullptr;
};

#endif /* SYSTEM_KERNEL_ARCH_AARCH64_AARCH64VMTRANSLATIONMAP_H_ */
