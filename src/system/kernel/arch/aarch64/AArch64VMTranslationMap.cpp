/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include "AArch64VMTranslationMap.h"

#include <int.h>
#include <slab/Slab.h>
#include <thread.h>
#include <util/AutoLock.h>
#include <vm/vm_page.h>
#include <vm/VMAddressSpace.h>
#include <vm/VMCache.h>

#include "AArch64PagingStructures.h"
#include "AArch64PagingMethod.h"
#include "AArch64PhysicalPageMapper.h"

#include <kernel/arch/aarch64/armreg.h>
#include <kernel/arch/aarch64/pte.h>

#include <new>

AArch64VMTranslationMap::AArch64VMTranslationMap() {
}

AArch64VMTranslationMap::~AArch64VMTranslationMap() {
	if(fPageMapper) {
		uint64 * l0 = fPagingStructures->virtual_pgdir;
		phys_addr_t address;
		vm_page* page;
		for(uint32 l0_index = 0 ; l0_index < L0_ENTRIES ; ++l0_index)
		{
			if((l0[l0_index] & ATTR_DESCR_MASK) != L0_TABLE)
				continue;

			uint64 * l1 = (uint64 *)fPageMapper->GetPageTableAt(l0[l0_index] & ~ATTR_MASK);

			for(uint32 l1_index = 0 ; l1_index < Ln_ENTRIES ; ++l1_index) {
				if((l1[l1_index] & ATTR_DESCR_MASK) != L0_TABLE)
					continue;

				uint64 * l2 = (uint64 *)fPageMapper->GetPageTableAt(l1[l1_index] & ~ATTR_MASK);

				for(uint32 l2_index = 0 ; l2_index < Ln_ENTRIES ; ++l2_index) {
					if((l2[l2_index] & ATTR_DESCR_MASK) != L0_TABLE)
						continue;

					address = l2[l2_index] & ~ATTR_MASK;
					page = vm_lookup_page(address / B_PAGE_SIZE);

					if(!page) {
						panic("Incorrect page in page tables at level 2");
					}

					DEBUG_PAGE_ACCESS_START(page);
					vm_page_set_state(page, PAGE_STATE_FREE);
				}

				address = l1[l1_index] & ~ATTR_MASK;
				page = vm_lookup_page(address / B_PAGE_SIZE);

				if(!page) {
					panic("Incorrect page in page tables at level 1");
				}

				DEBUG_PAGE_ACCESS_START(page);
				vm_page_set_state(page, PAGE_STATE_FREE);
			}

			address = l0[l0_index] & ~ATTR_MASK;
			page = vm_lookup_page(address / B_PAGE_SIZE);

			if(!page) {
				panic("Incorrect page in page tables at level 0");
			}

			DEBUG_PAGE_ACCESS_START(page);
			vm_page_set_state(page, PAGE_STATE_FREE);
		}
	}

	if(fPagingStructures) {
		fPagingStructures->RemoveReference();
	}

	if(fPageMapper) {
		fPageMapper->Delete();
	}
}

status_t AArch64VMTranslationMap::Init(bool kernel)
{
	fIsKernelMap = kernel;

	fPagingStructures = new(std::nothrow) AArch64PagingStructures();
	if(!fPagingStructures)
		return B_NO_MEMORY;

	if(kernel) {
		fPageMapper = AArch64PagingMethod::fKernelPhysicalPageMapper;
		fPagingStructures->Init(AArch64PagingMethod::fKernelVirtualPgDir,
				AArch64PagingMethod::fKernelPhysicalPgDir);

		fPagingStructures->InitKernelASID();
	} else {
		fPageMapper = new(std::nothrow) TranslationMapPhysicalPageMapper();
		if(!fPageMapper)
			return B_NO_MEMORY;

		uint64 * level0 = (uint64 *)memalign(B_PAGE_SIZE, B_PAGE_SIZE);
		if(!level0)
			return B_NO_MEMORY;

		memset(level0, 0, B_PAGE_SIZE);

		// Look up the PML4 physical address.
		phys_addr_t physicalPageDir;
		vm_get_page_mapping(VMAddressSpace::KernelID(), (addr_t)level0, &physicalPageDir);

		fPagingStructures->Init(level0, physicalPageDir);

		if(!fPagingStructures->AllocateASID()) {
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}

bool AArch64VMTranslationMap::Lock()
{
	recursive_lock_lock(&fLock);
	return true;
}

void AArch64VMTranslationMap::Unlock()
{
	recursive_lock_unlock(&fLock);
}

addr_t AArch64VMTranslationMap::MappedSize() const
{
	return fMapCount;
}

size_t AArch64VMTranslationMap::MaxPagesNeededToMap(addr_t start, addr_t end) const
{
	if (start == 0) {
		start = L0_SIZE - B_PAGE_SIZE;
		end += start;
	}

	size_t requiredL0 = end / L0_SIZE + 1 - start / L0_SIZE;
	size_t requiredL1 = end / L1_SIZE + 1 - start / L1_SIZE;
	size_t requiredL2 = end / L2_SIZE + 1 - start / L2_SIZE;

	return requiredL0 + requiredL1 + requiredL2;
}

status_t AArch64VMTranslationMap::Map(addr_t virtualAddress, phys_addr_t physicalAddress,
		uint32 attributes, uint32 memoryType,
		vm_page_reservation* reservation)
{
	ThreadCPUPinner pinner(thread_get_current_thread());

	uint64 * entry = AArch64PagingMethod::PageTableEntryForAddress(fPagingStructures->virtual_pgdir,
			virtualAddress,
			fIsKernelMap,
			true,
			reservation,
			fPageMapper,
			fMapCount);

	ASSERT(entry != nullptr);
	ASSERT_PRINT((*entry & ATTR_DESCR_MASK) != L3_PAGE,
			"virtual address: %#" B_PRIxADDR ", existing pte: %#" B_PRIx64,
					virtualAddress, *entry);

	AArch64PagingMethod::SetTableEntry(entry, physicalAddress | L3_PAGE |
			(fIsKernelMap ? ATTR_AF : ATTR_nG) |
			AArch64PagingMethod::AttributesForMemoryFlags(attributes, memoryType));

	__asm__ __volatile__(
			"dsb ishst\n\t"
			"tlbi vae1is, %0\n\t"
			"dsb ish\n\t"
			"isb"
			::"r"(((virtualAddress >> 12) | (uint64(fPagingStructures->asid) << 48))));

	++fMapCount;

	return B_OK;
}

status_t AArch64VMTranslationMap::Unmap(addr_t start, addr_t end)
{
	start = ROUNDDOWN(start, B_PAGE_SIZE);
	if (start >= end)
		return B_OK;

	ThreadCPUPinner pinner(thread_get_current_thread());

	for(uint32 l0_index = pmap_l0_index(start) ; l0_index < L0_ENTRIES && start != 0 && start < end ; ++l0_index) {
		uint64 l0_entry = AArch64PagingMethod::LoadTableEntry(&fPagingStructures->virtual_pgdir[l0_index]);

		if((l0_entry & ATTR_DESCR_MASK) != L0_TABLE) {
			start = ROUNDUP(start + 1, L0_SIZE);
			continue;
		}

		uint64 * l1_table = (uint64 *)fPageMapper->GetPageTableAt(l0_entry & ~ATTR_MASK);

		for(uint32 l1_index = pmap_l1_index(start) ; l1_index < Ln_ENTRIES && start != 0 && start < end ; ++l1_index) {
			uint64 l1_entry = AArch64PagingMethod::LoadTableEntry(&l1_table[l1_index]);

			if((l1_entry & ATTR_DESCR_MASK) == L1_BLOCK) {
				return B_BAD_VALUE;
			}

			if((l1_entry & ATTR_DESCR_MASK) != L1_TABLE) {
				start = ROUNDUP(start + 1, L1_SIZE);
				continue;
			}

			uint64 * l2_table = (uint64 *)fPageMapper->GetPageTableAt(l1_entry & ~ATTR_MASK);

			for(uint32 l2_index = pmap_l2_index(start) ; l2_index < Ln_ENTRIES && start != 0 && start < end ; ++l2_index) {
				uint64 l2_entry = AArch64PagingMethod::LoadTableEntry(&l2_table[l2_index]);

				if((l2_entry & ATTR_DESCR_MASK) == L2_BLOCK) {
					return B_BAD_VALUE;
				}

				if((l2_entry & ATTR_DESCR_MASK) != L2_TABLE) {
					start = ROUNDUP(start + 1, L2_SIZE);
					continue;
				}

				uint64 * l3_table = (uint64 *)fPageMapper->GetPageTableAt(l2_entry & ~ATTR_MASK);

				for(uint32 l3_index = pmap_l3_index(start) ; l3_index < Ln_ENTRIES && start != 0 && start < end ; ++l3_index) {
					uint64 old_entry = AArch64PagingMethod::ClearTableEntry(&l3_table[l3_index]);

					if((old_entry & ATTR_DESCR_MASK) == L3_PAGE) {
						--fMapCount;

						__asm__ __volatile__(
							"dsb ishst\n\t"
							"tlbi vae1is, %0\n\t"
							::"r"((start >> 12) | (uint64(fPagingStructures->asid) << 48)));

					}

					start += L3_SIZE;
				}
			}
		}
	}

	__asm__ __volatile__("dsb ish\n\tisb");

	return B_OK;
}

status_t AArch64VMTranslationMap::DebugMarkRangePresent(addr_t start, addr_t end,
		bool markPresent)
{
	return B_BAD_VALUE;
}

status_t AArch64VMTranslationMap::UnmapPage(VMArea* area, addr_t address,
		bool updatePageQueue)
{
	ASSERT(address % B_PAGE_SIZE == 0);

	RecursiveLocker locker(fLock);

	uint64 * entry = AArch64PagingMethod::PageTableEntryForAddress(fPagingStructures->virtual_pgdir,
			address,
			fIsKernelMap,
			false,
			nullptr,
			fPageMapper,
			fMapCount);

	if(!entry)
		return B_ENTRY_NOT_FOUND;

	uint64 oldEntry = AArch64PagingMethod::ClearTableEntry(entry);

	if((oldEntry & ATTR_DESCR_MASK) != L3_PAGE) {
		return B_ENTRY_NOT_FOUND;
	}

	--fMapCount;

	__asm__ __volatile__(
			"dsb ishst\n\t"
			"tlbi vae1is, %0\n\t"
			"dsb ish\n\t"
			"isb"
			::"r"(((address >> 12) | (uint64(fPagingStructures->asid) << 48))));

	locker.Detach();

	PageUnmapped(area, (oldEntry & ~ATTR_MASK) / B_PAGE_SIZE,
			(oldEntry & ATTR_AF) == ATTR_AF,
			(oldEntry & ATTR_SW_DIRTY) == ATTR_SW_DIRTY,
			updatePageQueue);

	return B_OK;
}

void AArch64VMTranslationMap::UnmapPages(VMArea* area, addr_t base, size_t size,
		bool updatePageQueue)
{
	if (size == 0)
		return;

	addr_t start = base;
	addr_t end = base + size - 1;

	VMAreaMappings queue;

	RecursiveLocker locker(fLock);
	ThreadCPUPinner pinner(thread_get_current_thread());

	for(uint32 l0_index = pmap_l0_index(start) ; l0_index < L0_ENTRIES && start != 0 && start < end ; ++l0_index) {
		uint64 l0_entry = AArch64PagingMethod::LoadTableEntry(&fPagingStructures->virtual_pgdir[l0_index]);

		if((l0_entry & ATTR_DESCR_MASK) != L0_TABLE) {
			start = ROUNDUP(start + 1, L0_SIZE);
			continue;
		}

		uint64 * l1_table = (uint64 *)fPageMapper->GetPageTableAt(l0_entry & ~ATTR_MASK);

		for(uint32 l1_index = pmap_l1_index(start) ; l1_index < Ln_ENTRIES && start != 0 && start < end ; ++l1_index) {
			uint64 l1_entry = AArch64PagingMethod::LoadTableEntry(&l1_table[l1_index]);

			if((l1_entry & ATTR_DESCR_MASK) == L1_BLOCK) {
				panic("Trying to unmap block");
			}

			if((l1_entry & ATTR_DESCR_MASK) != L1_TABLE) {
				start = ROUNDUP(start + 1, L1_SIZE);
				continue;
			}

			uint64 * l2_table = (uint64 *)fPageMapper->GetPageTableAt(l1_entry & ~ATTR_MASK);

			for(uint32 l2_index = pmap_l2_index(start) ; l2_index < Ln_ENTRIES && start != 0 && start < end ; ++l2_index) {
				uint64 l2_entry = AArch64PagingMethod::LoadTableEntry(&l2_table[l2_index]);

				if((l2_entry & ATTR_DESCR_MASK) == L2_BLOCK) {
					panic("Trying to unmap block");
				}

				if((l2_entry & ATTR_DESCR_MASK) != L2_TABLE) {
					start = ROUNDUP(start + 1, L2_SIZE);
					continue;
				}

				uint64 * l3_table = (uint64 *)fPageMapper->GetPageTableAt(l2_entry & ~ATTR_MASK);

				for(uint32 l3_index = pmap_l3_index(start) ; l3_index < Ln_ENTRIES && start != 0 && start < end ; ++l3_index) {
					uint64 old_entry = AArch64PagingMethod::ClearTableEntry(&l3_table[l3_index]);

					if((old_entry & ATTR_DESCR_MASK) == L3_PAGE) {
						--fMapCount;

						__asm__ __volatile__(
							"dsb ishst\n\t"
							"tlbi vae1is, %0\n\t"
							::"r"((start >> 12) | (uint64(fPagingStructures->asid) << 48)));

						if (area->cache_type != CACHE_TYPE_DEVICE) {
							// get the page
							vm_page* page = vm_lookup_page(
									(old_entry & ~ATTR_MASK)
											/ B_PAGE_SIZE);
							ASSERT(page != NULL);

							DEBUG_PAGE_ACCESS_START(page);

							// transfer the accessed/dirty flags to the page
							if ((old_entry & ATTR_AF) != 0)
								page->accessed = true;
							if ((old_entry & ATTR_SW_DIRTY) != 0)
								page->modified = true;

							// remove the mapping object/decrement the wired_count of the
							// page
							if (area->wiring == B_NO_LOCK) {
								vm_page_mapping* mapping = NULL;
								vm_page_mappings::Iterator iterator =
										page->mappings.GetIterator();
								while ((mapping = iterator.Next()) != NULL) {
									if (mapping->area == area)
										break;
								}

								ASSERT(mapping != NULL);

								area->mappings.Remove(mapping);
								page->mappings.Remove(mapping);
								queue.Add(mapping);
							} else
								page->DecrementWiredCount();

							if (!page->IsMapped()) {
								atomic_add(&gMappedPagesCount, -1);

								if (updatePageQueue) {
									if (page->Cache()->temporary)
										vm_page_set_state(page,
												PAGE_STATE_INACTIVE);
									else if (page->modified)
										vm_page_set_state(page,
												PAGE_STATE_MODIFIED);
									else
										vm_page_set_state(page,
												PAGE_STATE_CACHED);
								}
							}

							DEBUG_PAGE_ACCESS_END(page);
						}

					}

					start += L3_SIZE;
				}
			}
		}
	}

	__asm__ __volatile__("dsb ish\n\tisb");


	// TODO: As in UnmapPage() we can lose page dirty flags here. ATM it's not
	// really critical here, as in all cases this method is used, the unmapped
	// area range is unmapped for good (resized/cut) and the pages will likely
	// be freed.

	locker.Unlock();

	// free removed mappings
	bool isKernelSpace = area->address_space == VMAddressSpace::Kernel();
	uint32 freeFlags = CACHE_DONT_WAIT_FOR_MEMORY
		| (isKernelSpace ? CACHE_DONT_LOCK_KERNEL_SPACE : 0);
	while (vm_page_mapping* mapping = queue.RemoveHead())
		object_cache_free(gPageMappingsObjectCache, mapping, freeFlags);
}

void AArch64VMTranslationMap::UnmapArea(VMArea* area, bool deletingAddressSpace,
		bool ignoreTopCachePageFlags)
{
	if (area->cache_type == CACHE_TYPE_DEVICE || area->wiring != B_NO_LOCK) {
		UnmapPages(area, area->Base(), area->Size(), true);
		return;
	}

	bool unmapPages = !deletingAddressSpace || !ignoreTopCachePageFlags;

	RecursiveLocker locker(fLock);
	ThreadCPUPinner pinner(thread_get_current_thread());

	VMAreaMappings mappings;
	mappings.MoveFrom(&area->mappings);

	for (VMAreaMappings::Iterator it = mappings.GetIterator();
			vm_page_mapping* mapping = it.Next();) {
		vm_page* page = mapping->page;
		page->mappings.Remove(mapping);

		VMCache* cache = page->Cache();

		bool pageFullyUnmapped = false;
		if (!page->IsMapped()) {
			atomic_add(&gMappedPagesCount, -1);
			pageFullyUnmapped = true;
		}

		if (unmapPages || cache != area->cache) {
			addr_t address = area->Base()
				+ ((page->cache_offset * B_PAGE_SIZE) - area->cache_offset);

			uint64 * entry = AArch64PagingMethod::PageTableEntryForAddress(fPagingStructures->virtual_pgdir,
					address,
					fIsKernelMap,
					false,
					nullptr,
					fPageMapper,
					fMapCount);

			if (entry == NULL) {
				panic("page %p has mapping for area %p (%#" B_PRIxADDR "), but "
					"has no page table", page, area, address);
				continue;
			}

			uint64 oldEntry = AArch64PagingMethod::ClearTableEntry(entry);

			__asm__ __volatile__(
				"dsb ishst\n\t"
				"tlbi vae1is, %0\n\t"
				::"r"((address >> 12) | (uint64(fPagingStructures->asid) << 48)));

			if ((oldEntry & ATTR_DESCR_MASK) != L3_PAGE) {
				panic("page %p has mapping for area %p (%#" B_PRIxADDR "), but "
					"has no page table entry", page, area, address);
				continue;
			}

			if ((oldEntry & ATTR_AF) != 0) {
				page->accessed = true;
			}

			if ((oldEntry & ATTR_SW_DIRTY) != 0) {
				page->modified = true;
			}

			if (pageFullyUnmapped) {
				DEBUG_PAGE_ACCESS_START(page);

				if (cache->temporary)
					vm_page_set_state(page, PAGE_STATE_INACTIVE);
				else if (page->modified)
					vm_page_set_state(page, PAGE_STATE_MODIFIED);
				else
					vm_page_set_state(page, PAGE_STATE_CACHED);

				DEBUG_PAGE_ACCESS_END(page);
			}
		}

		fMapCount--;
	}

	__asm__ __volatile__("dsb ish\n\tisb");

	Flush();
		// flush explicitely, since we directly use the lock

	locker.Unlock();

	bool isKernelSpace = area->address_space == VMAddressSpace::Kernel();
	uint32 freeFlags = CACHE_DONT_WAIT_FOR_MEMORY
		| (isKernelSpace ? CACHE_DONT_LOCK_KERNEL_SPACE : 0);
	while (vm_page_mapping* mapping = mappings.RemoveHead())
		object_cache_free(gPageMappingsObjectCache, mapping, freeFlags);
}

static uint32 DecodeProtection(uint64 value)
{
	uint32 protection = B_KERNEL_READ_AREA;

	if(value & 1)
		protection |= PAGE_PRESENT;
	if(value & ATTR_AF)
		protection |= PAGE_ACCESSED;
	if(value & ATTR_SW_DIRTY)
		protection |= PAGE_MODIFIED;
	if(!(value & ATTR_AP(ATTR_AP_RO)))
		protection |= B_KERNEL_WRITE_AREA;
	if(value & ATTR_AP(ATTR_AP_USER)) {
		protection |= B_READ_AREA;
		if(!(value & ATTR_AP(ATTR_AP_RO)))
			protection |= B_WRITE_AREA;
	}
	if(!(value & ATTR_UXN))
		protection |= B_EXECUTE_AREA;
	if(!(value & ATTR_PXN))
		protection |= B_KERNEL_EXECUTE_AREA;

	return protection;
}

status_t AArch64VMTranslationMap::Query(addr_t virtualAddress, phys_addr_t* _physicalAddress,
		uint32* _flags)
{
	*_flags = 0;
	*_physicalAddress = 0;

	if(fIsKernelMap) {
		if(virtualAddress < KERNEL_BASE) {
			return B_BAD_ADDRESS;
		}
	} else {
		if(virtualAddress > USER_TOP) {
			return B_BAD_ADDRESS;
		}
	}

	int l0_index = pmap_l0_index(virtualAddress);
	int l1_index = pmap_l1_index(virtualAddress);
	int l2_index = pmap_l2_index(virtualAddress);
	int l3_index = pmap_l3_index(virtualAddress);

	uint64 * l0 = fPagingStructures->virtual_pgdir;
	uint64 * l1;
	uint64 * l2;
	uint64 * l3;

	uint64 l0_value = AArch64PagingMethod::LoadTableEntry(&l0[l0_index]);

	switch(l0_value & ATTR_DESCR_MASK)
	{
	case L0_TABLE:
		l1 = (uint64 *)fPageMapper->GetPageTableAt(l0_value & ~ATTR_MASK);
		break;
	default:
		return B_OK;
	}

	uint64 l1_value = AArch64PagingMethod::LoadTableEntry(&l1[l1_index]);

	switch(l1_value & ATTR_DESCR_MASK)
	{
	case L1_BLOCK:
		*_flags = DecodeProtection(l1_value);
		*_physicalAddress = (l1_value & (~ATTR_MASK) & (~L1_OFFSET)) | (virtualAddress & L1_OFFSET);
		return B_OK;
	case L1_TABLE:
		l2 = (uint64 *)fPageMapper->GetPageTableAt(l1_value & ~ATTR_MASK);
		break;
	default:
		return B_OK;
	}

	uint64 l2_value = AArch64PagingMethod::LoadTableEntry(&l2[l2_index]);

	switch(l2_value & ATTR_DESCR_MASK)
	{
	case L2_BLOCK:
		*_flags = DecodeProtection(l2_value);
		*_physicalAddress = (l2_value & (~ATTR_MASK) & (~L2_OFFSET)) | (virtualAddress & L2_OFFSET);
		return B_OK;
	case L2_TABLE:
		l3 = (uint64 *)fPageMapper->GetPageTableAt(l2_value & ~ATTR_MASK);
		break;
	default:
		return B_OK;
	}

	uint64 l3_value = AArch64PagingMethod::LoadTableEntry(&l3[l3_index]);

	*_flags = DecodeProtection(l3_value);
	*_physicalAddress = (l3_value & (~ATTR_MASK) & (~L3_OFFSET)) | (virtualAddress & L3_OFFSET);
	return B_OK;
}

status_t AArch64VMTranslationMap::QueryInterrupt(addr_t virtualAddress,
		phys_addr_t* _physicalAddress, uint32* _flags)
{
	return Query(virtualAddress, _physicalAddress, _flags);
}

status_t AArch64VMTranslationMap::Protect(addr_t start, addr_t end, uint32 attributes,
		uint32 memoryType)
{
	start = ROUNDDOWN(start, B_PAGE_SIZE);
	if (start >= end)
		return B_OK;

	uint64 newProtectionFlags = L3_PAGE |
			(fIsKernelMap ? ATTR_AF : ATTR_nG) |
			AArch64PagingMethod::AttributesForMemoryFlags(attributes, memoryType);

	ThreadCPUPinner pinner(thread_get_current_thread());

	for(uint32 l0_index = pmap_l0_index(start) ; l0_index < L0_ENTRIES && start != 0 && start < end ; ++l0_index) {
		uint64 l0_entry = AArch64PagingMethod::LoadTableEntry(&fPagingStructures->virtual_pgdir[l0_index]);

		if((l0_entry & ATTR_DESCR_MASK) != L0_TABLE) {
			start = ROUNDUP(start + 1, L0_SIZE);
			continue;
		}

		uint64 * l1_table = (uint64 *)fPageMapper->GetPageTableAt(l0_entry & ~ATTR_MASK);

		for(uint32 l1_index = pmap_l1_index(start) ; l1_index < Ln_ENTRIES && start != 0 && start < end ; ++l1_index) {
			uint64 l1_entry = AArch64PagingMethod::LoadTableEntry(&l1_table[l1_index]);

			if((l1_entry & ATTR_DESCR_MASK) == L1_BLOCK) {
				return B_BAD_VALUE;
			}

			if((l1_entry & ATTR_DESCR_MASK) != L1_TABLE) {
				start = ROUNDUP(start + 1, L1_SIZE);
				continue;
			}

			uint64 * l2_table = (uint64 *)fPageMapper->GetPageTableAt(l1_entry & ~ATTR_MASK);

			for(uint32 l2_index = pmap_l2_index(start) ; l2_index < Ln_ENTRIES && start != 0 && start < end ; ++l2_index) {
				uint64 l2_entry = AArch64PagingMethod::LoadTableEntry(&l2_table[l2_index]);

				if((l2_entry & ATTR_DESCR_MASK) == L2_BLOCK) {
					return B_BAD_VALUE;
				}

				if((l2_entry & ATTR_DESCR_MASK) != L2_TABLE) {
					start = ROUNDUP(start + 1, L2_SIZE);
					continue;
				}

				uint64 * l3_table = (uint64 *)fPageMapper->GetPageTableAt(l2_entry & ~ATTR_MASK);

				for(uint32 l3_index = pmap_l3_index(start) ; l3_index < Ln_ENTRIES && start != 0 && start < end ; ++l3_index) {
					std::atomic<uint64> * pteEntry = reinterpret_cast<std::atomic<uint64> *>(&l3_table[l3_index]);
					uint64 oldEntry = pteEntry->load(std::memory_order_relaxed);

					if((oldEntry & ATTR_DESCR_MASK) == L3_PAGE) {
						while(!pteEntry->compare_exchange_strong(oldEntry,
								(oldEntry & ~ATTR_MASK) |
								(oldEntry & (ATTR_AF | ATTR_SW_DIRTY)) |
								newProtectionFlags,
								std::memory_order_seq_cst));

						__asm__ __volatile__(
							"dsb ishst\n\t"
							"tlbi vae1is, %0\n\t"
							::"r"((start >> 12) | (uint64(fPagingStructures->asid) << 48)));

					}

					start += L3_SIZE;
				}
			}
		}
	}

	__asm__ __volatile__("dsb ish\n\tisb");

	return B_OK;
}

status_t AArch64VMTranslationMap::ClearFlags(addr_t virtualAddress, uint32 flags)
{
	ThreadCPUPinner pinner(thread_get_current_thread());

	uint64 * entry = AArch64PagingMethod::PageTableEntryForAddress(fPagingStructures->virtual_pgdir,
			virtualAddress,
			fIsKernelMap,
			false,
			nullptr,
			fPageMapper,
			fMapCount);

	if(!entry)
		return B_OK;

	uint64 clearFlags = ((flags & PAGE_MODIFIED) ? ATTR_SW_DIRTY : 0) |
			((flags & PAGE_ACCESSED) ? ATTR_AF : 0);

	AArch64PagingMethod::ClearTableEntryFlags(entry, clearFlags);

	__asm__ __volatile__(
		"dsb ishst\n\t"
		"tlbi vae1is, %0\n\t"
		"dsb ish\n\t"
		"isb"
		::"r"((virtualAddress >> 12) | (uint64(fPagingStructures->asid) << 48)));

	return B_OK;
}

bool AArch64VMTranslationMap::ClearAccessedAndModified(VMArea* area, addr_t address,
		bool unmapIfUnaccessed, bool& _modified)
{
	ASSERT(address % B_PAGE_SIZE == 0);

	RecursiveLocker locker(fLock);
	ThreadCPUPinner pinner(thread_get_current_thread());

	uint64 * entry = AArch64PagingMethod::PageTableEntryForAddress(
			fPagingStructures->virtual_pgdir, address, fIsKernelMap,
			false, nullptr, fPageMapper, fMapCount);

	if(!entry)
		return false;

	uint64 oldEntry;

	if (unmapIfUnaccessed) {
		oldEntry = AArch64PagingMethod::LoadTableEntry(entry);

		while (true) {
			if ((oldEntry & ATTR_DESCR_MASK) != L3_PAGE) {
				// page mapping not valid
				return false;
			}

			if (oldEntry & ATTR_AF) {
				// page was accessed -- just clear the flags
				oldEntry = AArch64PagingMethod::ClearTableEntryFlags(entry, ATTR_AF);
				break;
			}

			// page hasn't been accessed -- unmap it
			if (AArch64PagingMethod::TestAndSetTableEntry(entry, 0, oldEntry)) {
				break;
			}

			// something changed -- check again
		}
	} else {
		oldEntry = AArch64PagingMethod::ClearTableEntryFlags(entry, ATTR_AF | ATTR_SW_DIRTY);
	}

	__asm__ __volatile__(
		"dsb ishst\n\t"
		"tlbi vae1is, %0\n\t"
		"dsb ish\n\t"
		"isb"
		::"r"((address >> 12) | (uint64(fPagingStructures->asid) << 48)));

	pinner.Unlock();

	_modified = (oldEntry & ATTR_SW_DIRTY) != 0;

	if ((oldEntry & ATTR_AF) != 0) {
		return true;
	}

	if (!unmapIfUnaccessed) {
		return false;
	}


	fMapCount--;

	locker.Detach();
	// UnaccessedPageUnmapped() will unlock for us

	UnaccessedPageUnmapped(area, (oldEntry & ~ATTR_MASK) / B_PAGE_SIZE);

	return false;
}

void AArch64VMTranslationMap::Flush()
{
}
