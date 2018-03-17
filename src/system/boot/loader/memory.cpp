/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <kernel/boot/platform.h>
#include <kernel/boot/stdio.h>
#include <kernel/boot/memory.h>
#include <kernel/boot/stage2.h>
#include <kernel/kernel.h>
#include <support/SupportDefs.h>
#include <algorithm>
#include <cassert>

PhysicalMemoryAllocator * gBootPhysicalMemoryAllocator;
BlockVirtualRegionAllocator gBootKernelVirtualRegionAllocator;
VirtualMemoryMapper * gBootVirtualMemoryMapper;
VirtualRegionAllocator * gBootLoaderVirtualRegionAllocator;
PageTablePhysicalMemoryMapper * gBootPageTableMapper;

//#define TRACE_MEMORY
#ifdef TRACE_MEMORY
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) ;
#endif

BlockPhysicalMemoryAllocator::BlockPhysicalMemoryAllocator() :
	fRAMRegionCount(0),
	fFreeRangeCount(0),
	fUsedRangeCount(0)
{
}

void BlockPhysicalMemoryAllocator::AddRegion(uint64 physicalAddress, uint64 size)
{
	status_t error = insert_address_range(fFreeRanges,
			&fFreeRangeCount,
			MAX_PHYSICAL_FREE_RANGE,
			physicalAddress,
			size);

	if(error != B_OK) {
		panic("Can't insert region into physical memory areas. Enlarge MAX_PHYSICAL_FREE_RANGE");
	}

	error = insert_address_range(fRAMRegions,
			&fRAMRegionCount,
			MAX_PHYSICAL_MEMORY_RANGE,
			physicalAddress,
			size);

	if(error != B_OK) {
		panic("Can't insert region into physical memory areas. Enlarge MAX_PHYSICAL_MEMORY_RANGE");
	}

	TRACE("Add physical region %" B_PRIx64 " -- %" B_PRIx64 "\n", physicalAddress, physicalAddress + size - 1);
}

void BlockPhysicalMemoryAllocator::ReservePlatformRegion(uint64 physicalAddress, uint64 size)
{
	uint64 reserveBase = physicalAddress;
	uint64 reserveEnd = physicalAddress + size - 1;

	TRACE("Reserve physical region %" B_PRIx64 " -- %" B_PRIx64 "\n", physicalAddress, physicalAddress + size - 1);

	for(uint32 i = 0 ; i < fFreeRangeCount ; ++i) {
		uint64 rangeBase = fFreeRanges[i].start;
		uint64 rangeEnd = fFreeRanges[i].start + fFreeRanges[i].size - 1;

		uint64 intersectionBase = std::max(reserveBase, rangeBase);
		uint64 intersectionEnd = std::min(reserveEnd, rangeEnd);

		if(intersectionBase < intersectionEnd) {
			status_t error = remove_address_range(fFreeRanges,
					&fFreeRangeCount,
					MAX_PHYSICAL_FREE_RANGE,
					intersectionBase,
					intersectionEnd - intersectionBase + 1);

			if(error != B_OK) {
				panic("Can't remove region from physical memory areas. Enlarge MAX_PHYSICAL_FREE_RANGE");
			}

			error = insert_address_range(fUsedRanges,
						&fUsedRangeCount,
						MAX_PHYSICAL_ALLOCATED_RANGE,
						intersectionBase,
						intersectionEnd - intersectionBase + 1);

			if(error != B_OK) {
				panic("Can't insert region into physical memory areas. Enlarge MAX_PHYSICAL_ALLOCATED_RANGE");
			}
		}
	}
}

void BlockPhysicalMemoryAllocator::GenerateKernelArguments()
{
	sort_address_ranges(fRAMRegions, fRAMRegionCount);
	Sort();

	gKernelArgs.num_physical_memory_ranges = fRAMRegionCount;
	for(uint32 i = 0 ; i < fRAMRegionCount ; ++i) {
		gKernelArgs.physical_memory_range[i] = fRAMRegions[i];
	}
	gKernelArgs.num_physical_allocated_ranges = fUsedRangeCount;
	for(uint32 i = 0 ; i < fUsedRangeCount ; ++i) {
		gKernelArgs.physical_allocated_range[i] = fUsedRanges[i];
	}
}

status_t BlockPhysicalMemoryAllocator::AllocatePhysicalMemory(uint64 size, uint64 alignment, uint64& physicalAddress)
{
	TRACE("Allocate physical memory size = %" B_PRIx64 ", alignment = %" B_PRIx64 "\n", size, alignment);

	for(uint32 i = 0 ; i < fFreeRangeCount ; ++i) {
		uint64 alignedBase = (fFreeRanges[i].start + alignment - 1) & ~(alignment - 1);
		uint64 regionEnd = fFreeRanges[i].start + fFreeRanges[i].size - 1;

		if(alignedBase + size - 1 > regionEnd || alignedBase + size <= alignedBase)
			continue;

		status_t status = remove_address_range(fFreeRanges,
					&fFreeRangeCount,
					MAX_PHYSICAL_FREE_RANGE,
					alignedBase,
					size);

		if(status != B_OK) {
			panic("Can't remove region from physical memory areas. Enlarge MAX_PHYSICAL_FREE_RANGE");
		}

		status = insert_address_range(fUsedRanges,
					&fUsedRangeCount,
					MAX_PHYSICAL_ALLOCATED_RANGE,
					alignedBase,
					size);

		if(status != B_OK) {
			panic("Can't insert region into physical memory areas. Enlarge MAX_PHYSICAL_ALLOCATED_RANGE");
		}

		TRACE("Allocate physical memory at %" B_PRIx64 "\n", alignedBase);
		physicalAddress = alignedBase;
		return B_OK;
	}

	return B_NO_MEMORY;
}

status_t BlockPhysicalMemoryAllocator::FreePhysicalMemory(
		uint64 physicalAddress, uint64 size)
{
	TRACE("Free physical memory physicalAddress = %" B_PRIx64 ", size = %" B_PRIx64 "\n", physicalAddress, size);

	status_t status = remove_address_range(fUsedRanges,
			&fUsedRangeCount,
			MAX_PHYSICAL_ALLOCATED_RANGE,
			physicalAddress,
			size);

	if(status != B_OK) {
		panic("Can't remove region from physical memory areas. Enlarge MAX_PHYSICAL_ALLOCATED_RANGE");
	}

	status = insert_address_range(fFreeRanges,
			&fFreeRangeCount,
			MAX_PHYSICAL_FREE_RANGE,
			physicalAddress,
			size);

	if(status != B_OK) {
		panic("Can't insert region into physical memory areas. Enlarge MAX_PHYSICAL_FREE_RANGE");
	}

	return B_OK;
}

void BlockPhysicalMemoryAllocator::Sort()
{
	sort_address_ranges(fFreeRanges, fFreeRangeCount);
	sort_address_ranges(fUsedRanges, fUsedRangeCount);
}

BlockVirtualRegionAllocator::BlockVirtualRegionAllocator() :
	fFreeRangeCount(0),
	fUsedRangeCount(0)
{
}

void BlockVirtualRegionAllocator::Init(uint64 virtualStart, uint64 virtualEnd)
{
	fFreeRangeCount = 1;
	fFreeRanges[0].start = virtualStart;
	fFreeRanges[0].size = virtualEnd - virtualStart;
}

status_t BlockVirtualRegionAllocator::AllocateVirtualMemoryRegion(void ** _address,
			size_t size,
			size_t alignment,
			bool exactAddress,
			bool preferHighVirtualRanges)
{
	TRACE("Allocate virtual memory size = %zx, alignment = %zx from = %p\n", size, alignment, *_address);

	if(exactAddress) {
		uint64 baseAddress = (addr_t)*_address;
		uint64 end = baseAddress + size;
		for(uint32 i = 0 ; i < fFreeRangeCount ; ++i) {
			if(baseAddress >= fFreeRanges[i].start &&
				(end - 1) <= (fFreeRanges[i].start + fFreeRanges[i].size - 1))
			{
				status_t status = remove_address_range(fFreeRanges,
						&fFreeRangeCount,
						MAX_VIRTUAL_FREE_RANGE,
						baseAddress,
						size);

				if(status != B_OK) {
					panic("Can't remove region from virtual memory areas. Enlarge MAX_VIRTUAL_FREE_RANGE");
				}

				status = insert_address_range(fUsedRanges,
						&fUsedRangeCount,
						MAX_VIRTUAL_ALLOCATED_RANGE,
						baseAddress,
						size);

				if(status != B_OK) {
					panic("Can't insert region into virtual memory areas. Enlarge MAX_VIRTUAL_ALLOCATED_RANGE");
				}

				return B_OK;
			}
		}
		return B_BUSY;
	}

	if(preferHighVirtualRanges) {
		for(int32 i = fFreeRangeCount - 1; i >= 0 ; --i) {
			if(size > fFreeRanges[i].size)
				continue;
			uint64 regionEnd = fFreeRanges[i].start + fFreeRanges[i].size;
			uint64 alignedBase = (regionEnd - size) & ~(alignment - 1);
			uint64 alignedEnd = alignedBase + size;

			if(alignedBase < fFreeRanges[i].start || alignedEnd - 1 > regionEnd - 1)
				continue;

			status_t status = remove_address_range(fFreeRanges,
					&fFreeRangeCount,
					MAX_VIRTUAL_FREE_RANGE,
					alignedBase,
					size);

			if(status != B_OK) {
				panic("Can't remove region from virtual memory areas. Enlarge MAX_VIRTUAL_FREE_RANGE");
			}

			status = insert_address_range(fUsedRanges,
					&fUsedRangeCount,
					MAX_VIRTUAL_ALLOCATED_RANGE,
					alignedBase,
					size);

			if(status != B_OK) {
				panic("Warning: Unable to insert memory range. Increase MAX_VIRTUAL_ALLOCATED_RANGE");
			}

			*_address = (void *)(addr_t)alignedBase;
			TRACE("Allocated virtual region at %p\n", *_address);
			return B_OK;
		}
	} else {
		for(uint32 i = 0 ; i < fFreeRangeCount ; ++i) {
			uint64 alignedBase = (fFreeRanges[i].start + alignment - 1) & ~(alignment - 1);
			uint64 regionEnd = fFreeRanges[i].start + fFreeRanges[i].size - 1;

			if(alignedBase + size - 1 >= regionEnd || alignedBase + size <= alignedBase)
				continue;

			status_t status = remove_address_range(fFreeRanges,
					&fFreeRangeCount,
					MAX_VIRTUAL_FREE_RANGE,
					alignedBase,
					size);

			if(status != B_OK) {
				panic("Can't remove region from virtual memory areas. Enlarge MAX_VIRTUAL_FREE_RANGE");
			}

			status = insert_address_range(fUsedRanges,
					&fUsedRangeCount,
					MAX_VIRTUAL_ALLOCATED_RANGE,
					alignedBase,
					size);

			if(status != B_OK) {
				panic("Warning: Unable to insert memory range. Increase MAX_VIRTUAL_ALLOCATED_RANGE");
			}

			*_address = (void *)(addr_t)alignedBase;
			TRACE("Allocated virtual region at %p\n", *_address);
			return B_OK;
		}
	}

	return B_NO_MEMORY;
}

status_t BlockVirtualRegionAllocator::ReleaseVirtualMemoryRegion(void * _address, size_t size)
{
	TRACE("Release virtual memory region %p, size = %zx\n", _address, size);

	uint64 address = (addr_t)_address;

	status_t status = remove_address_range(fUsedRanges,
			&fUsedRangeCount,
			MAX_VIRTUAL_ALLOCATED_RANGE,
			address,
			size);

	if(status != B_OK) {
		panic("Can't remove region from virtual memory areas. Enlarge MAX_VIRTUAL_ALLOCATED_RANGE");
	}

	status = insert_address_range(fFreeRanges,
			&fFreeRangeCount,
			MAX_VIRTUAL_FREE_RANGE,
			address,
			size);

	if(status != B_OK) {
		panic("Warning: Unable to insert memory range. Increase MAX_VIRTUAL_FREE_RANGE");
	}

	return B_OK;
}

void BlockVirtualRegionAllocator::GenerateKernelArguments()
{
	Sort();
	gKernelArgs.num_virtual_allocated_ranges = fUsedRangeCount;
	gKernelArgs.num_virtual_free_ranges = fFreeRangeCount;
	for(uint32 i = 0 ; i < fUsedRangeCount ; ++i)
		gKernelArgs.virtual_allocated_range[i] = fUsedRanges[i];
	for(uint32 i = 0 ; i < fFreeRangeCount ; ++i)
		gKernelArgs.virtual_free_range[i] = fFreeRanges[i];
}

void BlockVirtualRegionAllocator::Sort()
{
	sort_address_ranges(fFreeRanges, fFreeRangeCount);
	sort_address_ranges(fUsedRanges, fUsedRangeCount);
}

void DirectPhysicalMemoryMapper::Init(addr_t virtualBase, uint64 physicalBase, uint64 physicalEnd)
{
	fVirtualBase = virtualBase;
	fPhysicalBase = physicalBase;
	fPhysicalEnd = physicalEnd;
}

void * DirectPhysicalMemoryMapper::MapPhysicalPage(uint64 physicalAddress)
{
	if(physicalAddress < fPhysicalBase || physicalAddress >= fPhysicalEnd)
		panic("Trying to get mapping for physical address out of range");
	physicalAddress -= fPhysicalBase;
	physicalAddress += fVirtualBase;
	return (void *)(addr_t)physicalAddress;
}

void PageTablePhysicalMemoryMapper::Init(addr_t virtualBase, uint32 numberOfSlots, Slot * slots) {
	fVirtualBase = virtualBase;
	fNumberOfSlots = numberOfSlots;
	fLastSlot = 0;
	fSlots = slots;
}

void * PageTablePhysicalMemoryMapper::MapPhysicalPage(uint64 physicalAddress) {
	// First try lookup in existing mappings
	assert(!(physicalAddress & (B_PAGE_SIZE - 1)));

	if(fSlots[fLastSlot].physical_address == physicalAddress) {
		return (void *)(fVirtualBase + fLastSlot * B_PAGE_SIZE);
	}

	for(uint32 i = 0 ; i < fNumberOfSlots ; ++i) {
		if(fSlots[i].physical_address == physicalAddress) {
			return (void *)(fVirtualBase + i * B_PAGE_SIZE);
		}
	}

	fLastSlot = (fLastSlot + 1) % fNumberOfSlots;
	fSlots[fLastSlot].physical_address = physicalAddress;

	MapOnePage(fVirtualBase + fLastSlot * B_PAGE_SIZE, physicalAddress);

	return (void *)(fVirtualBase + fLastSlot * B_PAGE_SIZE);
}

void * VirtualMemoryMapper::MapPhysicalLoaderMemory(uint64 physicalAddress, size_t size, bool)
{
	TRACE("MapPhysicalLoaderMemory: physicalAddress=%" B_PRIx64 ", size = %zd\n",
			physicalAddress,
			size);

	uint32 pageOffset = physicalAddress & (B_PAGE_SIZE - 1);

	size = ROUNDUP(physicalAddress + size, B_PAGE_SIZE) - ROUNDDOWN(physicalAddress, B_PAGE_SIZE);
	physicalAddress = ROUNDDOWN(physicalAddress, B_PAGE_SIZE);

	void * virtualAddress = NULL;

	status_t error = gBootLoaderVirtualRegionAllocator->AllocateVirtualMemoryRegion(&virtualAddress,
			size,
			B_PAGE_SIZE,
			false,
			true);

	if(error != B_OK) {
		panic("Can't allocate virtual mapping");
	}

	TRACE("MapPhysicalLoaderMemory: Allocated virtual region at %p\n", virtualAddress);

	error = MapVirtualMemoryRegion((addr_t)virtualAddress, physicalAddress, size, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);

	if(error != B_OK) {
		panic("Cant map virtual region");
	}

	return (void *)((addr_t)virtualAddress + pageOffset);
}

void VirtualMemoryMapper::UnmapPhysicalLoaderMemory(void * memory, size_t size)
{
	TRACE("UnmapPhysicalLoaderMemory: Address=%p, size=%zx\n", memory, size);
	addr_t virtualAddress = (addr_t)memory;
	size = ROUNDUP(virtualAddress + size, B_PAGE_SIZE) - ROUNDDOWN(virtualAddress, B_PAGE_SIZE);
	virtualAddress = ROUNDDOWN(virtualAddress, B_PAGE_SIZE);
	UnmapVirtualMemoryRegion(virtualAddress, size);
	gBootLoaderVirtualRegionAllocator->ReleaseVirtualMemoryRegion((void *)virtualAddress, size);
}

void * VirtualMemoryMapper::MapPhysicalKernelMemory(uint64 physicalAddress, size_t size)
{
	TRACE("MapPhysicalKernelMemory: physicalAddress=%" B_PRIx64 ", size = %zd\n",
			physicalAddress,
			size);

	uint32 pageOffset = physicalAddress & (B_PAGE_SIZE - 1);

	size = ROUNDUP(physicalAddress + size, B_PAGE_SIZE) - ROUNDDOWN(physicalAddress, B_PAGE_SIZE);
	physicalAddress = ROUNDDOWN(physicalAddress, B_PAGE_SIZE);

	void * virtualAddress = NULL;

	status_t error = gBootKernelVirtualRegionAllocator.AllocateVirtualMemoryRegion(&virtualAddress,
			size,
			B_PAGE_SIZE,
			false,
			true);

	if(error != B_OK) {
		panic("Can't allocate virtual mapping");
	}

	TRACE("MapPhysicalKernelMemory: Allocated virtual region at %p\n", virtualAddress);

	error = MapVirtualMemoryRegion((addr_t)virtualAddress, physicalAddress, size, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);

	if(error != B_OK) {
		panic("Cant map virtual region");
	}

	return (void *)((addr_t)virtualAddress + pageOffset);
}

void VirtualMemoryMapper::UnmapPhysicalKernelMemory(void * memory, size_t size)
{
	TRACE("UnmapPhysicalKernelMemory: Address=%p, size=%zx\n", memory, size);
	addr_t virtualAddress = (addr_t)memory;
	size = ROUNDUP(virtualAddress + size, B_PAGE_SIZE) - ROUNDDOWN(virtualAddress, B_PAGE_SIZE);
	virtualAddress = ROUNDDOWN(virtualAddress, B_PAGE_SIZE);
	UnmapVirtualMemoryRegion(virtualAddress, size);
	gBootKernelVirtualRegionAllocator.ReleaseVirtualMemoryRegion((void *)virtualAddress, size);
}
