/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <support/SupportDefs.h>

#include <fdt_support.h>
#include <memory>
#include <cassert>
#include <new>
#include <algorithm>

#include <kernel/kernel.h>
#include <kernel/boot/memory.h>
#include <kernel/arch/aarch64/pte.h>
#include <kernel/arch/aarch64/armreg.h>
#include <kernel/arch/aarch64/arch_kernel.h>
#include <kernel/arch/aarch64/arch_vm_translation_map.h>
#include <drivers/KernelExport.h>
#include <system/vm_defs.h>
#include <kernel/boot/stage2.h>

extern "C" {

// We need this allocator before startup. It can't be in .bss section as iw
std::aligned_storage<sizeof(BlockPhysicalMemoryAllocator), alignof(BlockPhysicalMemoryAllocator)>::type __attribute__((section(".data"))) gAArch64DTBAllocatorStore;

// Force allocation in .data segment
uint64 gAArch64PageDirectoryPhysicalTTBR0 = 1;
uint64 gAArch64PageDirectoryPhysicalTTBR1 = 1;

}

struct DirectPhysicalTranslator {
	static unsigned long ToVirtual(phys_addr_t addr) {
		return addr;
	}
};

class PageTablePhysicalMemoryMapperDMAP final : public PhysicalMemoryMapper {
public:
	virtual void * MapPhysicalPage(uint64 physicalAddress) override final {
		return (void *)(DMAP_BASE + physicalAddress);
	}
};

static PageTablePhysicalMemoryMapperDMAP sPageTableTranslatorAArch64;

struct DMAPPhysicalTranslator {
	static unsigned long ToVirtual(phys_addr_t addr) {
		return (unsigned long)sPageTableTranslatorAArch64.MapPhysicalPage(addr);
	}
};

template<typename _Translator> inline int aarch64_promote_mapping(
		uint64 * table_entry,
		BlockPhysicalMemoryAllocator * allocator,
		size_t child_size,
		uint8 descr_type)
{
	uint64 phys;

	int error = allocator->BlockPhysicalMemoryAllocator::AllocatePhysicalMemory(B_PAGE_SIZE,
			B_PAGE_SIZE,
			phys);

	if(error < 0) {
		return error;
	}

	uint64 * virtual_block = (uint64 *)_Translator::ToVirtual(phys);
	uint64 base_entry = (table_entry[0] & ~uint64(ATTR_DESCR_MASK)) | descr_type;

	for(uint32 i = 0 ; i < Ln_ENTRIES ; ++i) {
		virtual_block[i] = base_entry;
		base_entry += child_size;
	}

	table_entry[0] = phys | L1_TABLE;

	return 0;
}

template<typename _Translator> void aarch64_level3_remap(
			uint64 * level3,
			unsigned long& virtualAddress,
			phys_addr_t& physicalAddress,
			size_t& size,
			uint64 protection)
{
	for(int l3_index = pmap_l3_index(virtualAddress) ;
			l3_index < Ln_ENTRIES && size > 0 ;
			++l3_index)
	{
		level3[l3_index] = physicalAddress | protection | L3_PAGE;
		size -= B_PAGE_SIZE;
		virtualAddress += B_PAGE_SIZE;
		physicalAddress += B_PAGE_SIZE;
	}
}

template<typename _Translator> status_t aarch64_level2_remap(
			BlockPhysicalMemoryAllocator * allocator,
			uint64 * level2,
			unsigned long& virtualAddress,
			phys_addr_t& physicalAddress,
			size_t& size,
			uint64 protection)
{
	for(int l2_index = pmap_l2_index(virtualAddress) ;
			l2_index < Ln_ENTRIES && size > 0 ;
			++l2_index)
	{
		uint64 entry = level2[l2_index];
		uint64 * l3;

		if((entry & ATTR_DESCR_MASK) == L2_BLOCK) {
			// Existing block. Check if we need to promote the mapping

			if(!(virtualAddress & L2_OFFSET) &&
			   !(physicalAddress & L2_OFFSET) &&
			   (size >= L2_SIZE))
			{
				// Just update
				level2[l2_index] = physicalAddress | protection | L1_BLOCK;
				size -= L2_SIZE;
				virtualAddress += L2_SIZE;
				physicalAddress += L2_SIZE;
				continue;
			}

			// We need to expand the table
			int error = aarch64_promote_mapping<_Translator>(&level2[l2_index],
					allocator,
					L3_SIZE,
					L3_PAGE);

			if(error < 0) {
				return error;
			}

			 l3 = (uint64 *)_Translator::ToVirtual(level2[l2_index] & ~ATTR_MASK);
		} else if((entry & ATTR_DESCR_MASK) != L2_TABLE) {
			// If we can use the block mapping, use it
			if(!(virtualAddress & L2_OFFSET) &&
			   !(physicalAddress & L2_OFFSET) &&
			   (size >= L2_SIZE))
			{
				// Just update
				level2[l2_index] = physicalAddress | protection | L2_BLOCK;
				size -= L2_SIZE;
				virtualAddress += L2_SIZE;
				physicalAddress += L2_SIZE;
				continue;
			}

			uint64 phys;

			int error = allocator->BlockPhysicalMemoryAllocator::AllocatePhysicalMemory(B_PAGE_SIZE,
					B_PAGE_SIZE,
					phys);

			if(error < 0) {
				return error;
			}

			l3 = (uint64 *)_Translator::ToVirtual(phys);
			memset(l3, 0, B_PAGE_SIZE);

			level2[l2_index] = phys | L2_TABLE;
		} else {
			l3 = (uint64 *)_Translator::ToVirtual(entry & ~ATTR_MASK);
		}

		aarch64_level3_remap<_Translator>(
				l3,
				virtualAddress,
				physicalAddress,
				size,
				protection);
	}

	return 0;
}

template<typename _Translator> status_t aarch64_level1_remap(
			BlockPhysicalMemoryAllocator * allocator,
			uint64 * level1,
			unsigned long& virtualAddress,
			phys_addr_t& physicalAddress,
			size_t& size,
			uint64 protection)
{
	for(int l1_index = pmap_l1_index(virtualAddress) ;
			l1_index < Ln_ENTRIES && size > 0 ;
			++l1_index)
	{
		uint64 entry = level1[l1_index];
		uint64 * l2;

		if((entry & ATTR_DESCR_MASK) == L1_BLOCK) {
			// Existing block. Check if we need to promote the mapping

			if(!(virtualAddress & L1_OFFSET) &&
			   !(physicalAddress & L1_OFFSET) &&
			   (size >= L1_SIZE))
			{
				// Just update
				level1[l1_index] = physicalAddress | protection | L1_BLOCK;
				size -= L1_SIZE;
				virtualAddress += L1_SIZE;
				physicalAddress += L1_SIZE;
				continue;
			}

			// We need to expand the table
			int error = aarch64_promote_mapping<_Translator>(&level1[l1_index],
					allocator,
					L2_SIZE,
					L2_BLOCK);

			if(error < 0) {
				return error;
			}

			l2 = (uint64 *)_Translator::ToVirtual(level1[l1_index] & ~ATTR_MASK);
		} else if((entry & ATTR_DESCR_MASK) != L1_TABLE) {
			// If we can use the block mapping, use it
			if(!(virtualAddress & L1_OFFSET) &&
			   !(physicalAddress & L1_OFFSET) &&
			   (size >= L1_SIZE))
			{
				// Just update
				level1[l1_index] = physicalAddress | protection | L1_BLOCK;
				size -= L1_SIZE;
				virtualAddress += L1_SIZE;
				physicalAddress += L1_SIZE;
				continue;
			}

			uint64 phys;

			int error = allocator->BlockPhysicalMemoryAllocator::AllocatePhysicalMemory(B_PAGE_SIZE,
					B_PAGE_SIZE,
					phys);

			if(error < 0) {
				return error;
			}

			l2 = (uint64 *)_Translator::ToVirtual(phys);
			memset(l2, 0, B_PAGE_SIZE);

			level1[l1_index] = phys | L1_TABLE;
		} else {
			l2 = (uint64 *)_Translator::ToVirtual(entry & ~ATTR_MASK);
		}

		status_t error = aarch64_level2_remap<_Translator>(allocator,
				l2,
				virtualAddress,
				physicalAddress,
				size,
				protection);

		if(error < 0) {
			return error;
		}

	}

	return 0;
}

template<typename _Translator> status_t aarch64_level0_remap(
			BlockPhysicalMemoryAllocator * allocator,
			uint64 * level0,
			unsigned long virtualAddress,
			phys_addr_t physicalAddress,
			size_t size,
			uint64 protection)
{
	if(virtualAddress >= KERNEL_BASE) {
		protection &= ~ATTR_nG;
	} else {
		protection |= ATTR_nG;
	}

	while(size > 0) {
		int l0_index = pmap_l0_index(virtualAddress);
		uint64 * l1;

		if(!level0[l0_index]) {
			uint64 phys;
			int error = allocator->BlockPhysicalMemoryAllocator::AllocatePhysicalMemory(B_PAGE_SIZE,
					B_PAGE_SIZE,
					phys);

			if(error < 0) {
				return error;
			}

			l1 = (uint64 *)_Translator::ToVirtual(phys);
			memset(l1, 0, B_PAGE_SIZE);

			level0[l0_index] = phys | L0_TABLE;
		} else {
			l1 = (uint64 *)_Translator::ToVirtual(level0[l0_index] & ~ATTR_MASK);
		}

		status_t error = aarch64_level1_remap<_Translator>(allocator,
				l1,
				virtualAddress,
				physicalAddress,
				size,
				protection);

		if(error < 0) {
			return error;
		}
	}

	__asm__ __volatile__("tlbi vmalle1is");

	return 0;
}

static void aarch64_level3_unmap(uint64 * level3,
	unsigned long& virtualAddress,
	size_t& size)
{
	for(int l3_index = pmap_l3_index(virtualAddress) ;
			l3_index < Ln_ENTRIES && size > 0 ;
			++l3_index)
	{
		level3[l3_index] = 0;
		size -= B_PAGE_SIZE;
		virtualAddress += B_PAGE_SIZE;
	}
}

static status_t aarch64_level2_unmap(uint64 * level2,
	unsigned long& virtualAddress,
	size_t& size)
{
	auto allocator = static_cast<BlockPhysicalMemoryAllocator *>(gBootPhysicalMemoryAllocator);

	for(int l2_index = pmap_l2_index(virtualAddress) ;
			l2_index < Ln_ENTRIES && size > 0 ;
			++l2_index)
	{
		uint64 entry = level2[l2_index];
		uint64 * l3;

		if((entry & ATTR_DESCR_MASK) == L2_BLOCK) {
			// Existing block. Check if we need to promote the mapping

			if(!(virtualAddress & L2_OFFSET) && (size >= L2_SIZE))
			{
				// Just update
				level2[l2_index] = 0;
				size -= L2_SIZE;
				virtualAddress += L2_SIZE;
				continue;
			}

			// We need to expand the table
			int error = aarch64_promote_mapping<DMAPPhysicalTranslator>(&level2[l2_index],
					allocator,
					L3_SIZE,
					L3_PAGE);

			if(error < 0) {
				return error;
			}

			l3 = (uint64 *)DMAPPhysicalTranslator::ToVirtual(level2[l2_index] & ~ATTR_MASK);
		} else if((entry & ATTR_DESCR_MASK) != L2_TABLE) {
			// If we can use the block mapping, use it
			if(!(virtualAddress & L2_OFFSET) &&
			   (size >= L2_SIZE))
			{
				// Just update
				level2[l2_index] = 0;
				size -= L2_SIZE;
				virtualAddress += L2_SIZE;
				continue;
			}

			size_t sizeLeft = std::min(size, L2_SIZE - (virtualAddress & L2_OFFSET));

			virtualAddress += sizeLeft;
			size -= sizeLeft;
			continue;
		} else {
			l3 = (uint64 *)DMAPPhysicalTranslator::ToVirtual(entry & ~ATTR_MASK);
		}

		aarch64_level3_unmap(l3,
				virtualAddress,
				size);
	}

	return B_OK;
}

static status_t aarch64_level1_unmap(uint64 * level1,
	unsigned long& virtualAddress,
	size_t& size)
{
	auto allocator = static_cast<BlockPhysicalMemoryAllocator *>(gBootPhysicalMemoryAllocator);

	for(int l1_index = pmap_l1_index(virtualAddress) ;
			l1_index < Ln_ENTRIES && size > 0 ;
			++l1_index)
	{
		uint64 entry = level1[l1_index];
		uint64 * l2;

		if((entry & ATTR_DESCR_MASK) == L1_BLOCK) {
			// Existing block. Check if we need to promote the mapping

			if(!(virtualAddress & L1_OFFSET) && (size >= L1_SIZE))
			{
				// Just update
				level1[l1_index] = 0;
				size -= L1_SIZE;
				virtualAddress += L1_SIZE;
				continue;
			}

			// We need to expand the table
			int error = aarch64_promote_mapping<DMAPPhysicalTranslator>(&level1[l1_index],
					allocator,
					L2_SIZE,
					L2_BLOCK);

			if(error < 0) {
				return error;
			}

			l2 = (uint64 *)DMAPPhysicalTranslator::ToVirtual(level1[l1_index] & ~ATTR_MASK);
		} else if((entry & ATTR_DESCR_MASK) != L1_TABLE) {
			// If we can use the block mapping, use it
			if(!(virtualAddress & L1_OFFSET) &&
			   (size >= L1_SIZE))
			{
				// Just update
				level1[l1_index] = 0;
				size -= L1_SIZE;
				virtualAddress += L1_SIZE;
				continue;
			}

			size_t sizeLeft = std::min(size, L1_SIZE - (virtualAddress & L1_OFFSET));

			virtualAddress += sizeLeft;
			size -= sizeLeft;
			continue;
		} else {
			l2 = (uint64 *)DMAPPhysicalTranslator::ToVirtual(entry & ~ATTR_MASK);
		}

		status_t error = aarch64_level2_unmap(l2,
				virtualAddress,
				size);

		if(error < 0) {
			return error;
		}
	}

	return B_OK;
}

static status_t aarch64_level0_unmap(uint64 * level0,
			unsigned long virtualAddress,
			size_t size)
{
	while(size > 0) {
		int l0_index = pmap_l0_index(virtualAddress);
		uint64 * l1;

		if(!level0[l0_index]) {
			size_t blockLeft = L0_SIZE - (virtualAddress & L0_OFFSET);
			if(size <= blockLeft)
				break;
			size -= blockLeft;
			virtualAddress += blockLeft;
		} else {
			l1 = (uint64 *)sPageTableTranslatorAArch64.MapPhysicalPage(level0[l0_index] & ~ATTR_MASK);
		}

		status_t error = aarch64_level1_unmap(l1, virtualAddress, size);

		if(error != B_OK) {
			return error;
		}
	}

	__asm__ __volatile__("tlbi vmalle1is");

	return B_OK;
}

static void aarch64_level3_protect(uint64 * level3,
	unsigned long& virtualAddress,
	size_t& size,
	uint64 protection)
{
	for(int l3_index = pmap_l3_index(virtualAddress) ;
			l3_index < Ln_ENTRIES && size > 0 ;
			++l3_index)
	{
		if(level3[l3_index]) {
			level3[l3_index] = (level3[l3_index] & ~ATTR_MASK) | protection | L3_PAGE;
		}
		size -= B_PAGE_SIZE;
		virtualAddress += B_PAGE_SIZE;
	}
}

static status_t aarch64_level2_protect(uint64 * level2,
	unsigned long& virtualAddress,
	size_t& size,
	uint64 protection)
{
	auto allocator = static_cast<BlockPhysicalMemoryAllocator *>(gBootPhysicalMemoryAllocator);

	for(int l2_index = pmap_l2_index(virtualAddress) ;
			l2_index < Ln_ENTRIES && size > 0 ;
			++l2_index)
	{
		uint64 entry = level2[l2_index];
		uint64 * l3;

		if((entry & ATTR_DESCR_MASK) == L2_BLOCK) {
			// Existing block. Check if we need to promote the mapping

			if(!(virtualAddress & L2_OFFSET) && (size >= L2_SIZE))
			{
				// Just update
				level2[l2_index] = (level2[l2_index] & ~ATTR_MASK) | protection | L2_BLOCK;
				size -= L2_SIZE;
				virtualAddress += L2_SIZE;
				continue;
			}

			// We need to expand the table
			int error = aarch64_promote_mapping<DMAPPhysicalTranslator>(&level2[l2_index],
					allocator,
					L3_SIZE,
					L3_PAGE);

			if(error < 0) {
				return error;
			}

			l3 = (uint64 *)DMAPPhysicalTranslator::ToVirtual(level2[l2_index] & ~ATTR_MASK);
		} else if((entry & ATTR_DESCR_MASK) != L2_TABLE) {
			// If we can use the block mapping, use it
			if(!(virtualAddress & L2_OFFSET) &&
			   (size >= L2_SIZE))
			{
				// Just update
				size -= L2_SIZE;
				virtualAddress += L2_SIZE;
				continue;
			}

			size_t sizeLeft = std::min(size, L2_SIZE - (virtualAddress & L2_OFFSET));

			virtualAddress += sizeLeft;
			size -= sizeLeft;
			continue;
		} else {
			l3 = (uint64 *)DMAPPhysicalTranslator::ToVirtual(entry & ~ATTR_MASK);
		}

		aarch64_level3_protect(l3,
				virtualAddress,
				size,
				protection);
	}

	return B_OK;
}

static status_t aarch64_level1_protect(uint64 * level1,
	unsigned long& virtualAddress,
	size_t& size,
	uint64 protection)
{
	auto allocator = static_cast<BlockPhysicalMemoryAllocator *>(gBootPhysicalMemoryAllocator);

	for(int l1_index = pmap_l1_index(virtualAddress) ;
			l1_index < Ln_ENTRIES && size > 0 ;
			++l1_index)
	{
		uint64 entry = level1[l1_index];
		uint64 * l2;

		if((entry & ATTR_DESCR_MASK) == L1_BLOCK) {
			// Existing block. Check if we need to promote the mapping

			if(!(virtualAddress & L1_OFFSET) && (size >= L1_SIZE))
			{
				// Just update
				level1[l1_index] = (level1[l1_index] & ~ATTR_MASK) | protection | L1_BLOCK;
				size -= L1_SIZE;
				virtualAddress += L1_SIZE;
				continue;
			}

			// We need to expand the table
			int error = aarch64_promote_mapping<DMAPPhysicalTranslator>(&level1[l1_index],
					allocator,
					L2_SIZE,
					L2_BLOCK);

			if(error < 0) {
				return error;
			}

			l2 = (uint64 *)DMAPPhysicalTranslator::ToVirtual(level1[l1_index] & ~ATTR_MASK);
		} else if((entry & ATTR_DESCR_MASK) != L1_TABLE) {
			// If we can use the block mapping, use it
			if(!(virtualAddress & L1_OFFSET) &&
			   (size >= L1_SIZE))
			{
				// Just update
				size -= L1_SIZE;
				virtualAddress += L1_SIZE;
				continue;
			}

			size_t sizeLeft = std::min(size, L1_SIZE - (virtualAddress & L1_OFFSET));

			virtualAddress += sizeLeft;
			size -= sizeLeft;
			continue;
		} else {
			l2 = (uint64 *)DMAPPhysicalTranslator::ToVirtual(entry & ~ATTR_MASK);
		}

		status_t error = aarch64_level2_protect(l2,
				virtualAddress,
				size,
				protection);

		if(error < 0) {
			return error;
		}
	}

	return B_OK;
}

static status_t aarch64_level0_protect(uint64 * level0,
			unsigned long virtualAddress,
			size_t size,
			uint64 protection)
{
	if(virtualAddress >= KERNEL_BASE) {
		protection &= ~ATTR_nG;
	} else {
		protection |= ATTR_nG;
	}

	while(size > 0) {
		int l0_index = pmap_l0_index(virtualAddress);
		uint64 * l1;

		if(!level0[l0_index]) {
			size_t blockLeft = L0_SIZE - (virtualAddress & L0_OFFSET);
			if(size <= blockLeft)
				break;
			size -= blockLeft;
			virtualAddress += blockLeft;
		} else {
			l1 = (uint64 *)sPageTableTranslatorAArch64.MapPhysicalPage(level0[l0_index] & ~ATTR_MASK);
		}

		status_t error = aarch64_level1_protect(l1, virtualAddress, size, protection);

		if(error != B_OK) {
			return error;
		}
	}

	__asm__ __volatile__("tlbi vmalle1is");

	return B_OK;
}
static inline uint64 build_page_protection(uint32 prot, uint32 memory_type)
{
	uint64 result = ATTR_AF | ATTR_SH(ATTR_SH_IS);

	switch(memory_type)
	{
	case B_MTR_UC:
		result |= ATTR_IDX(MAIR_REGION_NC);
		break;
	case B_MTR_WC:
	case B_MTR_WT:
	case B_MTR_WP:
		result |= ATTR_IDX(MAIR_REGION_WT);
		break;
	case B_MTR_DEV:
		result |= ATTR_IDX(MAIR_REGION_nGnRnE);
		break;
	default:
		result |= ATTR_IDX(MAIR_REGION_WBWA);
		break;
	}

	if(prot & (B_READ_AREA | B_WRITE_AREA | B_EXECUTE_AREA | B_STACK_AREA)) {
		result |= ATTR_AP(ATTR_AP_USER);
	}

	if(prot & (B_WRITE_AREA | B_KERNEL_WRITE_AREA)) {
		result |= ATTR_AP(ATTR_AP_RW);
	} else {
		result |= ATTR_AP(ATTR_AP_RO);
	}

	if(!(prot & B_EXECUTE_AREA)) {
		result |= ATTR_UXN;
	}

	if(!(prot & B_KERNEL_EXECUTE_AREA)) {
		result |= ATTR_PXN;
	}

	return result;
}


class AArch64VirtualMemoryMapper final : public VirtualMemoryMapper {
public:
	static inline uint64 * _ChooseTTBR(uint64 virtualAddress) {
		if(virtualAddress >= KERNEL_BASE)
			return (uint64 *)sPageTableTranslatorAArch64.MapPhysicalPage(gAArch64PageDirectoryPhysicalTTBR1);
		return (uint64 *)sPageTableTranslatorAArch64.MapPhysicalPage(gAArch64PageDirectoryPhysicalTTBR0);
	}

	virtual status_t MapVirtualMemoryRegion(uint64 virtualAddress,
			uint64 physicalAddress, uint64 size, uint32 protection)
					override final
	{
		auto allocator = static_cast<BlockPhysicalMemoryAllocator *>(gBootPhysicalMemoryAllocator);
		return aarch64_level0_remap<DMAPPhysicalTranslator>(allocator,
				_ChooseTTBR(virtualAddress),
				virtualAddress,
				physicalAddress,
				size,
				build_page_protection(protection, protection & B_MTR_MASK));
	}

	virtual status_t ProtectVirtualMemoryRegion(uint64 virtualAddress,
			uint64 size, uint32 protection)
	{
		return aarch64_level0_protect(_ChooseTTBR(virtualAddress),
				virtualAddress,
				size,
				build_page_protection(protection, protection & B_MTR_MASK));
	}

	virtual status_t UnmapVirtualMemoryRegion(uint64 virtualAddress,
			uint64 size)
	{
		return aarch64_level0_unmap(_ChooseTTBR(virtualAddress),
				virtualAddress,
				size);
	}

	virtual uint64 QueryVirtualAddress(uint64 virtualAddress,
			uint32& regionSize)
	{
		uint64 * level0 = _ChooseTTBR(virtualAddress);

		int l0_index = pmap_l0_index(virtualAddress);

		if((level0[l0_index] & ATTR_DESCR_MASK) != L0_TABLE) {
			regionSize = 0;
			return 0;
		}

		uint64 * level1 = (uint64 *)sPageTableTranslatorAArch64.MapPhysicalPage(level0[l0_index] & ~ATTR_DESCR_MASK);

		int l1_index = pmap_l1_index(virtualAddress);

		if((level1[l1_index] & ATTR_DESCR_MASK) == L1_BLOCK) {
			regionSize = L1_SIZE;
			return (virtualAddress & L1_OFFSET) | (level1[l1_index] & ~ATTR_MASK);
		}

		if((level1[l1_index] & ATTR_DESCR_MASK) != L1_TABLE) {
			regionSize = 0;
			return 0;
		}

		uint64 * level2 = (uint64 *)sPageTableTranslatorAArch64.MapPhysicalPage(level1[l1_index] & ~ATTR_DESCR_MASK);

		int l2_index = pmap_l2_index(virtualAddress);

		if((level2[l2_index] & ATTR_DESCR_MASK) == L2_BLOCK) {
			regionSize = L2_SIZE;
			return (virtualAddress & L2_OFFSET) | (level2[l2_index] & ~ATTR_MASK);
		}

		if((level2[l2_index] & ATTR_DESCR_MASK) != L2_TABLE) {
			regionSize = 0;
			return 0;
		}

		uint64 * level3 = (uint64 *)sPageTableTranslatorAArch64.MapPhysicalPage(level2[l2_index] & ~ATTR_DESCR_MASK);

		int l3_index = pmap_l3_index(virtualAddress);

		if((level3[l3_index] & ATTR_DESCR_MASK) != L3_PAGE) {
			regionSize = 0;
			return 0;
		}

		regionSize = L3_SIZE;
		return (virtualAddress & L3_OFFSET) | (level3[l3_index] & ~ATTR_MASK);
	}

	virtual void * MapPhysicalLoaderMemory(uint64 physicalAddress, size_t, bool)
	{
		return (void *)(UMAP_BASE + physicalAddress);
	}

	virtual void UnmapPhysicalLoaderMemory(void *, size_t)
	{
	}

	virtual void * MapPhysicalKernelMemory(uint64 physicalAddress, size_t)
	{
		return (void *)(UMAP_BASE + physicalAddress);
	}

	virtual void UnmapPhysicalKernelMemory(void *, size_t)
	{
	}
};

extern "C" void aarch64_initialize_mmu(const void * dtb_phys,
		addr_t executable_start,
		addr_t executable_end,
		addr_t data_start,
		void * theAllocator,
		addr_t executable_virt,
		addr_t rodata_base)
{
	executable_end = ROUNDUP(executable_end, B_PAGE_SIZE);

	// Validate correctness
	fdt::Node device_tree(dtb_phys);

	assert(device_tree.IsDTBValid());

	BlockPhysicalMemoryAllocator * allocator = new(theAllocator) BlockPhysicalMemoryAllocator();

	{
		addr_range excludedRegions[MAX_PHYSICAL_ALLOCATED_RANGE];
		uint32 excludedangeCount = 0;
		addr_range ramRegions[MAX_PHYSICAL_MEMORY_RANGE];
		uint32 ramRangeCount = 0;

		if(!device_tree.GetMemoryRegions(ramRegions,
				ramRangeCount,
				MAX_PHYSICAL_MEMORY_RANGE))
		{
			abort();
		}

		for(uint32 i = 0 ;i < ramRangeCount ; ++i) {
			allocator->AddRegion(ramRegions[i].start, ramRegions[i].size);
		}

		if(device_tree.GetReservedRegions(excludedRegions,
				excludedangeCount,
				MAX_PHYSICAL_ALLOCATED_RANGE))
		{
			for(uint32 i = 0 ; i < excludedangeCount ; ++i) {
				allocator->ReservePlatformRegion(excludedRegions[i].start, excludedRegions[i].size);
			}
		}
	}

	// Exlude bootloader range
	allocator->ReservePlatformRegion(executable_start, ROUNDUP(executable_end - executable_start, B_PAGE_SIZE));

	// Exclude device tree
	{
		addr_t dtb_base = (addr_t)dtb_phys & ~(B_PAGE_SIZE - 1);
		size_t dtb_size = (((addr_t)dtb_phys + device_tree.DeviceTreeSize() + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1)) - dtb_base;
		allocator->ReservePlatformRegion(dtb_base, dtb_size);
	}

	// Exclude initrd
	{
		fdt::Node chosen(device_tree.GetPath("/chosen"));
		if(!chosen.IsNull()) {
			fdt::Property initrd_start(chosen.GetProperty("linux,initrd-start"));
			fdt::Property initrd_end(chosen.GetProperty("linux,initrd-end"));

			if(!initrd_start.IsNull() && !initrd_end.IsNull()) {
				addr_t tgz_base = initrd_start.EncodedGet(0) & ~(B_PAGE_SIZE - 1);
				addr_t tgz_end = ((initrd_end.EncodedGet(0) + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE -1));
				allocator->ReservePlatformRegion(tgz_base, tgz_end - tgz_base);
			}
		}
	}

	// Allocate new page directory
	int error = allocator->BlockPhysicalMemoryAllocator::AllocatePhysicalMemory(B_PAGE_SIZE,
			B_PAGE_SIZE,
			gAArch64PageDirectoryPhysicalTTBR0);
	assert(error == 0);

	memset((void *)gAArch64PageDirectoryPhysicalTTBR0, 0, B_PAGE_SIZE);

	error = allocator->BlockPhysicalMemoryAllocator::AllocatePhysicalMemory(B_PAGE_SIZE,
			B_PAGE_SIZE,
			gAArch64PageDirectoryPhysicalTTBR1);
	assert(error == 0);

	memset((void *)gAArch64PageDirectoryPhysicalTTBR1, 0, B_PAGE_SIZE);

	// Create mapping for DMAP (map physical RAM)
	for(uint32 i = 0 ; i < allocator->fRAMRegionCount ; ++i) {
		aarch64_level0_remap<DirectPhysicalTranslator>(allocator,
			(uint64 *)gAArch64PageDirectoryPhysicalTTBR1,
			DMAP_BASE + allocator->fRAMRegions[i].start,
			allocator->fRAMRegions[i].start,
			allocator->fRAMRegions[i].size,
			build_page_protection(B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, 0));
	}

	// Create mapping for UMAP (uncached device mapping)
	for(uint32 i = 0 ; i < allocator->fRAMRegionCount ; ++i) {
		aarch64_level0_remap<DirectPhysicalTranslator>(allocator,
			(uint64 *)gAArch64PageDirectoryPhysicalTTBR1,
			UMAP_BASE,
			0,
			UMAP_END - UMAP_BASE,
			build_page_protection(B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, B_MTR_DEV));
	}

	// Create virtual mapping for boot loader .text
	aarch64_level0_remap<DirectPhysicalTranslator>(allocator,
		(uint64 *)gAArch64PageDirectoryPhysicalTTBR0,
		executable_virt,
		executable_start,
		rodata_base - executable_start,
		build_page_protection(B_KERNEL_READ_AREA | B_KERNEL_EXECUTE_AREA, 0));

	// Create virtual mapping for boot loader .rodata
	aarch64_level0_remap<DirectPhysicalTranslator>(allocator,
		(uint64 *)gAArch64PageDirectoryPhysicalTTBR0,
		executable_virt + (rodata_base - executable_start),
		rodata_base,
		data_start - rodata_base,
		build_page_protection(B_KERNEL_READ_AREA, 0));

	// Create virtual mapping for boot loader .data and .bss
	aarch64_level0_remap<DirectPhysicalTranslator>(allocator,
		(uint64 *)gAArch64PageDirectoryPhysicalTTBR0,
		executable_virt + (data_start - executable_start),
		data_start,
		executable_end - data_start,
		build_page_protection(B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, 0));

	// Create virtual mapping for boot loader .text (identity map)
	aarch64_level0_remap<DirectPhysicalTranslator>(allocator,
		(uint64 *)gAArch64PageDirectoryPhysicalTTBR0,
		executable_start,
		executable_start,
		rodata_base - executable_start,
		build_page_protection(B_KERNEL_READ_AREA | B_KERNEL_EXECUTE_AREA, 0));

	// Create virtual mapping for boot loader .rodata (identity map)
	aarch64_level0_remap<DirectPhysicalTranslator>(allocator,
		(uint64 *)gAArch64PageDirectoryPhysicalTTBR0,
		rodata_base,
		rodata_base,
		data_start - rodata_base,
		build_page_protection(B_KERNEL_READ_AREA, 0));

	// Create virtual mapping for boot loader .data and .bss (identity map)
	aarch64_level0_remap<DirectPhysicalTranslator>(allocator,
		(uint64 *)gAArch64PageDirectoryPhysicalTTBR0,
		data_start,
		data_start,
		executable_end - data_start,
		build_page_protection(B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, 0));
}

static BlockVirtualRegionAllocator sAArch64LoaderAllocator;
static AArch64VirtualMemoryMapper sAArch64VirtualMemoryMapper;

void arch_init_mmu(stage2_args * args)
{
	gBootPageTableMapper = &sPageTableTranslatorAArch64;
	gBootLoaderVirtualRegionAllocator = &sAArch64LoaderAllocator;

	auto allocator = reinterpret_cast<BlockPhysicalMemoryAllocator *>(&gAArch64DTBAllocatorStore);

	// Initial sort of regions (for RAM map)
	allocator->Sort();

	// This has been initialize by aarch64_initialize_mmu
	gBootPhysicalMemoryAllocator = allocator;

	// Don't bother with low memory ranges
	sAArch64LoaderAllocator.Init(0x200000000, 0x400000000);

	gBootVirtualMemoryMapper = &sAArch64VirtualMemoryMapper;

	gBootKernelVirtualRegionAllocator.Init(KERNEL_BASE, KERNEL_BASE + KERNEL_SIZE);
}
