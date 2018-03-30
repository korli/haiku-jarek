/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <stdlib.h>
#include <string.h>

#include <boot/kernel_args.h>
#include <util/AutoLock.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/VMAddressSpace.h>
#include <kernel/arch/aarch64/pte.h>
#include <kernel/arch/aarch64/armreg.h>

#include "AArch64PagingMethod.h"
#include "AArch64PagingStructures.h"
#include "AArch64PhysicalPageMapper.h"
#include "AArch64VMTranslationMap.h"

#include <new>
#include <memory>

phys_addr_t AArch64PagingMethod::fKernelPhysicalPgDir;
uint64 * AArch64PagingMethod::fKernelVirtualPgDir;
AArch64PhysicalPageMapper * AArch64PagingMethod::fPhysicalPageMapper;
TranslationMapPhysicalPageMapper * AArch64PagingMethod::fKernelPhysicalPageMapper;

static std::aligned_storage<sizeof(TranslationMapPhysicalPageMapper),
		alignof(TranslationMapPhysicalPageMapper)>::type sKernelPageMapper;
static std::aligned_storage<sizeof(AArch64PhysicalPageMapper),
		alignof(AArch64PhysicalPageMapper)>::type sPhysicalPageMapper;

status_t AArch64PagingMethod::Init(kernel_args * args, VMPhysicalPageMapper ** _mapper)
{
	fKernelPhysicalPgDir = args->arch_args.pgdir_phys;
	fKernelVirtualPgDir = (uint64 *)args->arch_args.pgdir_vir;
	fPhysicalPageMapper = new(&sPhysicalPageMapper) AArch64PhysicalPageMapper();
	fKernelPhysicalPageMapper = new(&sKernelPageMapper) TranslationMapPhysicalPageMapper();

	*_mapper = fPhysicalPageMapper;
	return B_OK;
}

status_t AArch64PagingMethod::InitPostArea(kernel_args * args)
{
//	void * address = (void *)DMAP_BASE;
//	area_id area = vm_create_null_area(VMAddressSpace::KernelID(),
//			"physical cached map area",
//			&address,
//			B_EXACT_ADDRESS,
//			DMAP_END - DMAP_BASE,
//			0);
//	if(area < B_OK)
//		return area;
//
//	address = (void *)UMAP_BASE;
//	area = vm_create_null_area(VMAddressSpace::KernelID(),
//			"physical device map area",
//			&address,
//			B_EXACT_ADDRESS,
//			UMAP_END - UMAP_BASE,
//			0);
//	if(area < B_OK)
//		return area;

	void * address = (void *)fKernelVirtualPgDir;
	area_id area = create_area("kernel ttb",
			&address,
			B_EXACT_ADDRESS,
			B_PAGE_SIZE,
			B_ALREADY_WIRED,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if(area < B_OK)
		return area;


	return B_OK;
}

status_t AArch64PagingMethod::CreateTranslationMap(bool kernel, VMTranslationMap ** _map)
{
	AArch64VMTranslationMap * map = new(std::nothrow) AArch64VMTranslationMap();

	if(!map)
		return B_NO_MEMORY;

	status_t error = map->Init(kernel);

	if(error != B_OK) {
		delete map;
		return error;
	}

	*_map = map;
	return B_OK;
}

static void aarch64_promote_mapping(
		uint64 * table_entry,
		page_num_t (*get_free_page)(kernel_args*),
		kernel_args * args,
		size_t child_size,
		uint8 descr_type)
{
	uint64 phys;

	page_num_t page_index = get_free_page(args);
	assert(page_index > 0);

	phys = page_index * B_PAGE_SIZE;
	uint64 * virtual_block = (uint64 *)(DMAP_BASE + phys);
	uint64 base_entry = (table_entry[0] & ~uint64(ATTR_DESCR_MASK)) | descr_type;

	for(uint32 i = 0 ; i < Ln_ENTRIES ; ++i) {
		virtual_block[i] = base_entry;
		base_entry += child_size;
	}

	table_entry[0] = phys | L1_TABLE;
}

status_t AArch64PagingMethod::MapEarly(kernel_args* args, addr_t virtualAddress,
		phys_addr_t physicalAddress, uint8 attributes,
		page_num_t (*get_free_page)(kernel_args*))
{
	assert(virtualAddress >= KERNEL_BASE);

	int l0_index = pmap_l0_index(virtualAddress);
	int l1_index = pmap_l1_index(virtualAddress);
	int l2_index = pmap_l2_index(virtualAddress);
	int l3_index = pmap_l3_index(virtualAddress);

	uint64 * l0 = fKernelVirtualPgDir;
	uint64 * l1;
	uint64 * l2;
	uint64 * l3;

	if(!l0[l0_index]) {
		page_num_t page_index = get_free_page(args);
		assert(page_index > 0);
		phys_addr_t phys = page_index * B_PAGE_SIZE;

		l1 = (uint64 *)(phys + DMAP_BASE);

		memset(l1, 0, B_PAGE_SIZE);

		l0[l0_index] = phys | L0_TABLE;
	} else {
		l1 = (uint64 *)((l0[l0_index] & ~ATTR_MASK) + DMAP_BASE);
	}

	if((l1[l1_index] & ATTR_DESCR_MASK) == L1_BLOCK) {
		aarch64_promote_mapping(l1 + l1_index,
				get_free_page,
				args,
				L2_SIZE,
				L2_BLOCK);
	}

	if(!l1[l1_index]) {
		page_num_t page_index = get_free_page(args);
		assert(page_index > 0);
		phys_addr_t phys = page_index * B_PAGE_SIZE;

		l2 = (uint64 *)(phys + DMAP_BASE);

		memset(l2, 0, B_PAGE_SIZE);

		l1[l1_index] = phys | L1_TABLE;
	} else {
		l2 = (uint64 *)((l1[l1_index] & ~ATTR_MASK) + DMAP_BASE);
	}

	if((l2[l2_index] & ATTR_DESCR_MASK) == L2_BLOCK) {
		aarch64_promote_mapping(l2 + l2_index,
				get_free_page,
				args,
				L3_SIZE,
				L3_PAGE);
	}

	if(!l2[l2_index]) {
		page_num_t page_index = get_free_page(args);
		assert(page_index > 0);
		phys_addr_t phys = page_index * B_PAGE_SIZE;

		l3 = (uint64 *)(phys + DMAP_BASE);

		memset(l3, 0, B_PAGE_SIZE);

		l2[l2_index] = phys | L1_TABLE;
	} else {
		l3 = (uint64 *)((l2[l2_index] & ~ATTR_MASK) + DMAP_BASE);
	}

	l3[l3_index] = physicalAddress | L3_PAGE | AttributesForMemoryFlags(attributes, 0);

	__asm__ __volatile__(
		"dsb ishst\n\t"
		"tlbi vaae1is, %0\n\t"
		"dsb ish\n\t"
		"isb"
		::"r"(virtualAddress / B_PAGE_SIZE));

	return B_OK;
}

bool AArch64PagingMethod::IsKernelPageAccessible(addr_t virtualAddress, uint32 protection)
{
	if(protection & B_KERNEL_WRITE_AREA) {
		__asm__ __volatile__("AT S1E1W, %0" :: "r"(virtualAddress));
	} else {
		__asm__ __volatile__("AT S1E1R, %0" :: "r"(virtualAddress));
	}

	__asm__ __volatile__("ISB");

	uint64 par = READ_SPECIALREG(par_el1);

	if(par & 1)
		return false;

	return true;
}

uint64 AArch64PagingMethod::AttributesForMemoryFlags(uint32 prot, uint32 memoryType)
{
	uint64 result = ATTR_AF | ATTR_SH(ATTR_SH_IS);

	switch(memoryType)
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
