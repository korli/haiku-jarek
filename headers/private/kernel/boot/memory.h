/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef KERNEL_BOOT_MEMORY_H_
#define KERNEL_BOOT_MEMORY_H_

#include <kernel/boot/kernel_args.h>

class PhysicalMemoryAllocator
{
public:
	virtual ~PhysicalMemoryAllocator() {}

	/*!
	 * Allocate physical memory block from the physical memory pool.
	 * This function must fulfill the complete memory allocation.
	 * Alignment must be preserved.
	 */
	virtual status_t AllocatePhysicalMemory(uint64 size, uint64 alignment, uint64& physicalAddress) = 0;

	/*!
	 * Free physical memory chunk which has been previously allocated.
	 * The memory must be freed using the same allocation size it
	 * has been allocated before.
	 */
	virtual status_t FreePhysicalMemory(uint64 physicalAddress, uint64 size) = 0;

	/*!
	 * Generate kernel arguments for the gKernelArgs.
	 */
	virtual void GenerateKernelArguments() = 0;
};

class VirtualRegionAllocator
{
public:
	virtual ~VirtualRegionAllocator() {}

	/*!
	 * Allocate virtual memory region with specified size
	 * and alignment. This allocator doesn't create any memory
	 * mappings. It only creates the raw allocation. If specified
	 * address is not-NULL, the address is starting point, otherwise the
	 * allocator shall search any free region.
	 */
	virtual status_t AllocateVirtualMemoryRegion(void ** _address,
				size_t size,
				size_t alignment,
				bool exactAddress,
				bool preferHighVirtualRanges) = 0;

	virtual status_t ReleaseVirtualMemoryRegion(void * address, size_t size) = 0;

	virtual void GenerateKernelArguments() = 0;
};

class BlockPhysicalMemoryAllocator : public PhysicalMemoryAllocator
{
public:
	BlockPhysicalMemoryAllocator();

	void AddRegion(uint64 physicalAddress, uint64 size);
	void ReservePlatformRegion(uint64 physicalAddress, uint64 size);
	void GenerateKernelArguments();

	status_t AllocatePhysicalMemory(uint64 size, uint64 alignment, uint64& physicalAddress);
	status_t FreePhysicalMemory(uint64 physicalAddress, uint64 size);

	void Sort();

public:
	addr_range		fRAMRegions[MAX_PHYSICAL_MEMORY_RANGE];
	addr_range		fFreeRanges[MAX_PHYSICAL_FREE_RANGE];
	addr_range		fUsedRanges[MAX_PHYSICAL_ALLOCATED_RANGE];
	uint32			fRAMRegionCount;
	uint32			fFreeRangeCount;
	uint32			fUsedRangeCount;
};

class BlockVirtualRegionAllocator : public VirtualRegionAllocator
{
public:
	BlockVirtualRegionAllocator();
	
	void Init(uint64 virtualStart, uint64 virtualEnd);

	virtual status_t AllocateVirtualMemoryRegion(void ** _address,
			size_t size,
			size_t alignment,
			bool exactAddress,
			bool preferHighVirtualRanges);

	virtual status_t ReleaseVirtualMemoryRegion(void * address, size_t size);
	virtual void GenerateKernelArguments();

	void Sort();

public:
	addr_range		fFreeRanges[MAX_VIRTUAL_FREE_RANGE];
	addr_range		fUsedRanges[MAX_VIRTUAL_ALLOCATED_RANGE];
	uint32			fFreeRangeCount;
	uint32			fUsedRangeCount;
};	

class VirtualMemoryMapper
{
public:
	virtual ~VirtualMemoryMapper() {}

	/*!
	 * Map virtual memory to physical memory directly using page tables.
	 */
	virtual status_t MapVirtualMemoryRegion(uint64 virtualAddress, uint64 physicalAddress, uint64 size, uint32 protection) = 0;

	/*!
	 * Change virtual memory protection. It is not mandatory in all platforms to
	 * have an actual implementation.
	 */
	virtual status_t ProtectVirtualMemoryRegion(uint64 virtualAddress, uint64 size, uint32 protection) = 0;

	/*!
	 * Unmap virtual memory address. This function doesn't free any memory.
	 * The memory must be released by another layer.
	 */
	virtual status_t UnmapVirtualMemoryRegion(uint64 virtualAddress, uint64 size) = 0;

	/*!
	 * Query virtual address and give physical address.
	 * On platform such as ARM we can use sections, so return
	 * region size. On non-section mappings this just would be
	 * B_PAGE_SIZE.
	 */
	virtual uint64 QueryVirtualAddress(uint64 virtualAddress, uint32& regionSize) = 0;

	/*!
	 * Create mapping of temporary physical region into loader's
	 * virtual address space
	 */
	virtual void * MapPhysicalLoaderMemory(uint64 physicalAddress, size_t size, bool allowTemporaryMapping = false);

	/*!
	 * Destroy mapping of temporary physical region from loader's address space
	 */
	virtual void UnmapPhysicalLoaderMemory(void * memory, size_t size);

	/*!
	 * Create mapping of temporary physical region into loader's
	 * virtual address space
	 */
	virtual void * MapPhysicalKernelMemory(uint64 physicalAddress, size_t size);

	/*!
	 * Destroy mapping of temporary physical region from loader's address space
	 */
	virtual void UnmapPhysicalKernelMemory(void * memory, size_t size);
};

class PhysicalMemoryMapper
{
public:
	virtual ~PhysicalMemoryMapper() { }

	virtual void * MapPhysicalPage(uint64 physicalAddress) = 0;
};

class DirectPhysicalMemoryMapper : public PhysicalMemoryMapper
{
public:
	void Init(addr_t virtualBase, uint64 physicalBase, uint64 physicalEnd);

	virtual void * MapPhysicalPage(uint64 physicalAddress);

private:
	addr_t			fVirtualBase;
	uint64			fPhysicalBase;
	uint64			fPhysicalEnd;
};

class PageTablePhysicalMemoryMapper : public PhysicalMemoryMapper
{
public:
	struct Slot {
		uint64				physical_address;
	};

	void Init(addr_t virtualBase, uint32 numberOfSlots, Slot * slots);

	virtual void * MapPhysicalPage(uint64 physicalAddress);

protected:
	virtual void MapOnePage(addr_t virtualAddress, uint64 physicalAddress) = 0;

protected:
	addr_t			fVirtualBase;
	uint32			fNumberOfSlots;
	uint32			fLastSlot;
	Slot *			fSlots;
};

template<int Size> class PageTablePhysicalMemoryMapperWithStorage : public PageTablePhysicalMemoryMapper
{
	PageTablePhysicalMemoryMapper::Slot		fSlots[Size];
public:
	void Init(addr_t virtualBase) {
		PageTablePhysicalMemoryMapper::Init(virtualBase, Size, fSlots);
	}
};

extern PhysicalMemoryAllocator * gBootPhysicalMemoryAllocator;
extern BlockVirtualRegionAllocator gBootKernelVirtualRegionAllocator;
extern VirtualRegionAllocator * gBootLoaderVirtualRegionAllocator;
extern VirtualMemoryMapper * gBootVirtualMemoryMapper;
extern PageTablePhysicalMemoryMapper * gBootPageTableMapper;

#endif /* KERNEL_BOOT_MEMORY_H_ */
