/*
 * Copyright 2012, Alex Smith, alex@alex-smith.me.uk.
 * Distributed under the terms of the MIT License.
 */


#include "long.h"

#include <algorithm>

#include <KernelExport.h>

// Include the x86_64 version of descriptors.h
#define __x86_64__
#include <arch/x86/descriptors.h>
#undef __x86_64__

#include <arch_system_info.h>
#include <boot/platform.h>
#include <boot/heap.h>
#include <boot/stage2.h>
#include <boot/stdio.h>
#include <kernel.h>
#include <kernel/boot/memory.h>

#include "debug.h"
#include "mmu.h"
#include "smp.h"

#include <cassert>

#ifdef TRACE_LONG
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) ;
#endif

static const uint64 kTableMappingFlags = 0x7;
static const uint64 kLargePageMappingFlags = 0x183;
static const uint64 kPageMappingFlags = 0x103;
	// Global, R/W, Present

extern "C" void long_enter_kernel(int currentCPU, uint64 stackTop);

extern uint64 gLongGDT;
extern uint32 gLongPhysicalPML4;
extern uint64 gLongKernelEntry;


/*! Convert a 32-bit address to a 64-bit address. */
static inline uint64
fix_address(uint64 address)
{
	if(address >= KERNEL_LOAD_BASE)
		return address - KERNEL_LOAD_BASE + KERNEL_LOAD_BASE_64_BIT;
	else
		return address;
}


template<typename Type>
inline void
fix_address(FixedWidthPointer<Type>& p)
{
	if (p != NULL)
		p.SetTo(fix_address(p.Get()));
}


static void
long_gdt_init()
{
	STATIC_ASSERT(BOOT_GDT_SEGMENT_COUNT > KERNEL_CODE_SEGMENT
		&& BOOT_GDT_SEGMENT_COUNT > KERNEL_DATA_SEGMENT
		&& BOOT_GDT_SEGMENT_COUNT > USER_CODE_SEGMENT
		&& BOOT_GDT_SEGMENT_COUNT > USER_DATA_SEGMENT);

	clear_segment_descriptor(&gBootGDT[0]);

	// Set up code/data segments (TSS segments set up later in the kernel).
	set_segment_descriptor(&gBootGDT[KERNEL_CODE_SEGMENT], DT_CODE_EXECUTE_ONLY,
		DPL_KERNEL);
	set_segment_descriptor(&gBootGDT[KERNEL_DATA_SEGMENT], DT_DATA_WRITEABLE,
		DPL_KERNEL);
	set_segment_descriptor(&gBootGDT[USER_CODE_SEGMENT], DT_CODE_EXECUTE_ONLY,
		DPL_USER);
	set_segment_descriptor(&gBootGDT[USER_DATA_SEGMENT], DT_DATA_WRITEABLE,
		DPL_USER);

	// Used by long_enter_kernel().
	gLongGDT = fix_address((addr_t)gBootGDT);
	dprintf("GDT at 0x%llx\n", gLongGDT);
}

#define	PML4SHIFT		39
#define	PDPSHIFT		30
#define	PDRSHIFT		21
#define	NPTEPGSHIFT		9
#define	NPDEPGSHIFT		9
#define	NPDPEPGSHIFT	9
#define	NPML4EPGSHIFT	9
#define PAGE_SHIFT		12
#define	PG_FRAME		(0x000ffffffffff000ULL)
#define	NBPDR			(1ULL<<PDRSHIFT)   /* bytes/page dir */
#define	NBPDP			(1ULL<<PDPSHIFT)	/* bytes/page dir ptr table */
#define	NBPML4			(1ULL<<PML4SHIFT)/* bytes/page map lev4 table */


static inline uint32 x64_pte_index(uint64 va)
{
	return ((va >> PAGE_SHIFT) & ((1U << NPTEPGSHIFT) - 1));
}

static inline uint32 x64_pde_index(uint64 va)
{
	return ((va >> PDRSHIFT) & ((1U << NPDEPGSHIFT) - 1));
}

static inline uint32 x64_pdpe_index(uint64 va)
{
	return ((va >> PDPSHIFT) & ((1U << NPDPEPGSHIFT) - 1));
}

static inline uint32 x64_pml4e_index(uint64 va)
{
	return ((va >> PML4SHIFT) & ((1ul << NPML4EPGSHIFT) - 1));
}

struct AddressSourceContiguous {
	enum { is_contiguous = 1 };
	enum { has_lookup = 0 };
	uint64		physical_address;

	inline uint64 NextPhysicalAddress(uint64 size) {
		uint64 result = physical_address;
		physical_address += size;
		return result;
	}
};

struct AddressSource32BitMap {
	enum { is_contiguous = 0 };
	enum { has_lookup = 1 };
	uint64		virtual_address;

	inline uint64 NextPhysicalAddress(uint64 size) {
		uint32 regionSize;
		assert(size == B_PAGE_SIZE);
		uint64 result = gBootVirtualMemoryMapper->QueryVirtualAddress(virtual_address, regionSize);
		TRACE("Probe Virtual = %" B_PRIx64 " as Physical: %" B_PRIx64 "\n", virtual_address, result);
		virtual_address += size;
		return result;
	}
};

template<typename _AddressSource> inline void x64_remap_pte_range(uint64 * table,
		uint64 tablePhys,
		uint64& virtualAddress,
		_AddressSource& physicalAddress,
		uint64 pte_attributes,
		uint64& size)
{
	TRACE("pte=%p virtualAddress=%" B_PRIx64 " size=%" B_PRIx64 " index = %" B_PRIx32 "\n",
			table,
			virtualAddress,
			size,
			x64_pte_index(virtualAddress));

	for(uint32 index = x64_pte_index(virtualAddress);
			size > 0 && index < (1U << NPTEPGSHIFT) ; ++index)
	{
		uint64 ptePhys = physicalAddress.NextPhysicalAddress(B_PAGE_SIZE);
		table[index] = ptePhys ? ptePhys | pte_attributes : 0;
		virtualAddress += B_PAGE_SIZE;
		size -= B_PAGE_SIZE;
		if(_AddressSource::has_lookup) {
			table = (uint64 *)gBootPageTableMapper->MapPhysicalPage(tablePhys);
		}
	}
}

template<typename _AddressSource> inline void x64_remap_pde_range(uint64 * table,
		uint64 tablePhys,
		uint64& virtualAddress,
		_AddressSource& physicalAddress,
		uint64 pte_attributes,
		uint64& size)
{
	TRACE("pde=%p virtualAddress=%" B_PRIx64 " size=%" B_PRIx64 "\n",
			table,
			virtualAddress,
			size);

	for (uint32 index = x64_pde_index(virtualAddress);
			size > 0 && index < (1U << NPDEPGSHIFT); ++index) {
		if (_AddressSource::is_contiguous && size >= NBPDR
				&& !(physicalAddress.NextPhysicalAddress(0) & (NBPDR - 1))) {

			TRACE("Put 2MB TLB at index %" B_PRIx32 " for physical address %" B_PRIx64 "\n",
					index,
					physicalAddress.NextPhysicalAddress(0));

			table[index] = physicalAddress.NextPhysicalAddress(NBPDR)
					| pte_attributes | 0x80;
			virtualAddress += NBPDR;
			size -= NBPDR;
			continue;
		}

		uint64 * pte_table;
		uint64 pte_table_physical;

		if (table[index]) {
			// Ensure this is not block region
			if (table[index] & 0x80) {
				panic("Trying to remap 2MB entry: existing value = %" B_PRIx64 " at index = %" B_PRIx32 " tablePtr=%" B_PRIx64 "\n", table[index], index,
					tablePhys);
			}
			pte_table_physical = table[index] & PG_FRAME;
			pte_table = (uint64 *) gBootPageTableMapper->MapPhysicalPage(pte_table_physical);
		} else {
			status_t error =
					gBootPhysicalMemoryAllocator->AllocatePhysicalMemory(
							B_PAGE_SIZE, B_PAGE_SIZE, pte_table_physical);
			assert(error == 0);

			table[index] = pte_table_physical | kTableMappingFlags;

			pte_table = (uint64 *) gBootPageTableMapper->MapPhysicalPage(
					pte_table_physical);

			memset(pte_table, 0, B_PAGE_SIZE);
		}

		TRACE("PDE Index = %" B_PRIx32 " Phys = %" B_PRIx64 " Virt = %p\n",
				index,
				pte_table_physical,
				pte_table);

		x64_remap_pte_range<_AddressSource>(pte_table, pte_table_physical, virtualAddress,
				physicalAddress, pte_attributes, size);

		// Reload table pointer
		table = (uint64 *)gBootPageTableMapper->MapPhysicalPage(tablePhys);
	}
}

template<typename _AddressSource> inline void x64_remap_pdpe_range(uint64 * table,
		uint64 tablePhys,
		uint64& virtualAddress,
		_AddressSource& physicalAddress,
		uint64 pte_attributes,
		uint64& size,
		bool allow_1gpdpe)
{
	TRACE("pdpe=%p virtualAddress=%" B_PRIx64 " size=%" B_PRIx64 " pdep phys = %" B_PRIx64 "\n",
			table,
			virtualAddress,
			size,
			tablePhys);

	for (uint32 index = x64_pdpe_index(virtualAddress);
			size > 0 && index < (1U << NPDPEPGSHIFT); ++index) {
		if (allow_1gpdpe && _AddressSource::is_contiguous && size >= NBPDP
				&& !(physicalAddress.NextPhysicalAddress(0) & (NBPDP - 1))) {

			TRACE("Put 1G TLB at index %" B_PRIx32 " for physical address %" B_PRIx64 "\n",
					index,
					physicalAddress.NextPhysicalAddress(0));

			table[index] = physicalAddress.NextPhysicalAddress(NBPDP)
					| pte_attributes | 0x80;
			virtualAddress += NBPDP;
			size -= NBPDP;
			continue;
		}

		uint64 * pde_table;
		uint64 pde_table_phys;

		if (table[index]) {
			// Ensure this is not block region
			if (table[index] & 0x80) {
				panic("Trying to remap 1GB entry: existing value = %" B_PRIx64 " at index = %" B_PRIx32 " tablePtr=%" B_PRIx64 "\n",
					table[index],
					index,
					tablePhys);
			}

			pde_table_phys = table[index] & PG_FRAME;
			pde_table = (uint64 *) gBootPageTableMapper->MapPhysicalPage(pde_table_phys);
		} else {
			status_t error =
					gBootPhysicalMemoryAllocator->AllocatePhysicalMemory(
							B_PAGE_SIZE, B_PAGE_SIZE, pde_table_phys);
			assert(error == 0);

			table[index] = pde_table_phys | kTableMappingFlags;

			pde_table = (uint64 *) gBootPageTableMapper->MapPhysicalPage(
					pde_table_phys);

			memset(pde_table, 0, B_PAGE_SIZE);
		}

		TRACE("PDPE Index = %" B_PRIx32 " Phys = %" B_PRIx64 " Virt = %p\n",
				index,
				pde_table_phys,
				pde_table);

		x64_remap_pde_range<_AddressSource>(pde_table,
				pde_table_phys,
				virtualAddress,
				physicalAddress,
				pte_attributes,
				size);

		// Reload table pointer as it might have been modified inside
		table = (uint64 *)gBootPageTableMapper->MapPhysicalPage(tablePhys);
	}
}

template<typename _AddressSource> inline void x64_remap_pml4_range(uint64 * pml4,
		uint64 virtualAddress,
		_AddressSource& physicalAddress,
		uint64 pte_attributes,
		uint64 size,
		bool allow_1gpdpe,
		bool allow_nx)
{
	TRACE("pml4p=%p virtualAddress=%" B_PRIx64 " size=%" B_PRIx64 " allow_1gpdpe=%d allow_nx=%d\n",
			pml4,
			virtualAddress,
			size,
			allow_1gpdpe,
			allow_nx);

	for (uint32 index = x64_pml4e_index(virtualAddress);
			size > 0 && index < (1U << NPML4EPGSHIFT); ++index) {
		uint64 * pdpe_table;
		uint64 pdpe_table_physical;

		if (pml4[index]) {
			pdpe_table_physical = pml4[index] & PG_FRAME;
			pdpe_table = (uint64 *) gBootPageTableMapper->MapPhysicalPage(pdpe_table_physical);
		} else {
			status_t error =
					gBootPhysicalMemoryAllocator->AllocatePhysicalMemory(
							B_PAGE_SIZE, B_PAGE_SIZE, pdpe_table_physical);
			assert(error == 0);

			pml4[index] = pdpe_table_physical | kTableMappingFlags;

			pdpe_table = (uint64 *) gBootPageTableMapper->MapPhysicalPage(
					pdpe_table_physical);

			memset(pdpe_table, 0, B_PAGE_SIZE);
		}

		TRACE("Virtual PML4 Index = %" B_PRIx32 " Phys = %" B_PRIx64 " Virt = %p\n",
				index,
				pdpe_table_physical,
				pdpe_table);

		x64_remap_pdpe_range<_AddressSource>(pdpe_table,
				pdpe_table_physical,
				virtualAddress,
				physicalAddress,
				pte_attributes,
				size,
				allow_1gpdpe);
	}
}

void long_mmu_init(bool support_1gpdpe, bool supports_nx)
{
	uint64 * pml4;

	// Find the highest physical memory address. We map all physical memory
	// into the kernel address space, so we want to make sure we map everything
	// we have available.
	uint64 maxAddress = 0;
	for (uint32 i = 0; i < gKernelArgs.num_physical_memory_ranges; i++) {
		maxAddress = std::max(maxAddress,
			gKernelArgs.physical_memory_range[i].start
				+ gKernelArgs.physical_memory_range[i].size);
	}

	// Want to map at least 4GB, there may be stuff other than usable RAM that
	// could be in the first 4GB of physical address space.
	maxAddress = std::max(maxAddress, (uint64)0x100000000ll);

	// Allocate the top level PML4.
	uint64 pml4Phys = 0;
	gBootPhysicalMemoryAllocator->AllocatePhysicalMemory(B_PAGE_SIZE, B_PAGE_SIZE, pml4Phys);
	gKernelArgs.arch_args.phys_pgdir = pml4Phys;

	// Allocate virtual region in kernel space
	gBootKernelVirtualRegionAllocator.AllocateVirtualMemoryRegion((void **)&pml4, B_PAGE_SIZE, B_PAGE_SIZE, false, true);

	// Map PML4 into kernel address space
	gBootVirtualMemoryMapper->MapVirtualMemoryRegion((addr_t)pml4, pml4Phys, B_PAGE_SIZE, B_KERNEL_READ_AREA|B_KERNEL_WRITE_AREA);

	gKernelArgs.arch_args.vir_pgdir = fix_address((addr_t)pml4);

	// Clear the PML4
	pml4 = (uint64 *)gBootVirtualMemoryMapper->MapPhysicalLoaderMemory(pml4Phys, B_PAGE_SIZE, true);
	memset(pml4, 0, B_PAGE_SIZE);


	AddressSourceContiguous ramPhysical;
	ramPhysical.physical_address = 0;

	// Create mapping for kernel pmap
	x64_remap_pml4_range<AddressSourceContiguous>(pml4,
		0xffffff0000000000,
		ramPhysical,
		kPageMappingFlags,
		maxAddress,
		support_1gpdpe,
		supports_nx);

	// Create mirror in PML4[0] for boot time. This is wiped later
	// during the boot process
	pml4[0] = pml4[x64_pml4e_index(0xffffff0000000000)];

	gBootKernelVirtualRegionAllocator.Sort();
	gKernelArgs.num_virtual_allocated_ranges = gBootKernelVirtualRegionAllocator.fUsedRangeCount;
	gKernelArgs.num_virtual_free_ranges = gBootKernelVirtualRegionAllocator.fFreeRangeCount;

	for(uint32 i = 0 ; i < gBootKernelVirtualRegionAllocator.fUsedRangeCount ; ++i) {
		gKernelArgs.virtual_allocated_range[i].start = fix_address(gBootKernelVirtualRegionAllocator.fUsedRanges[i].start);
		gKernelArgs.virtual_allocated_range[i].size = gBootKernelVirtualRegionAllocator.fUsedRanges[i].size;

		AddressSource32BitMap base;
		base.virtual_address = gBootKernelVirtualRegionAllocator.fUsedRanges[i].start;

		x64_remap_pml4_range<AddressSource32BitMap>(pml4,
			gKernelArgs.virtual_allocated_range[i].start,
			base,
			kPageMappingFlags,
			gKernelArgs.virtual_allocated_range[i].size,
			support_1gpdpe,
			supports_nx);
	}

	gBootVirtualMemoryMapper->UnmapPhysicalLoaderMemory(pml4, B_PAGE_SIZE);

	gLongPhysicalPML4 = pml4Phys;
}


static void
convert_preloaded_image(preloaded_elf64_image* image)
{
	fix_address(image->next);
	fix_address(image->name);
	fix_address(image->debug_string_table);
	fix_address(image->syms);
	fix_address(image->rel);
	fix_address(image->rela);
	fix_address(image->pltrel);
	fix_address(image->debug_symbols);
}


/*!	Convert all addresses in kernel_args to 64-bit addresses. */
static void
convert_kernel_args()
{
	fix_address(gKernelArgs.boot_volume);
	fix_address(gKernelArgs.vesa_modes);
	fix_address(gKernelArgs.edid_info);
	fix_address(gKernelArgs.debug_output);
	fix_address(gKernelArgs.previous_debug_output);
	fix_address(gKernelArgs.boot_splash);
	fix_address(gKernelArgs.arch_args.apic);
	fix_address(gKernelArgs.arch_args.hpet);

	convert_preloaded_image(static_cast<preloaded_elf64_image*>(
		gKernelArgs.kernel_image.Pointer()));
	fix_address(gKernelArgs.kernel_image);

	// Iterate over the preloaded images. Must save the next address before
	// converting, as the next pointer will be converted.
	preloaded_image* image = gKernelArgs.preloaded_images;
	fix_address(gKernelArgs.preloaded_images);
	while (image != NULL) {
		preloaded_image* next = image->next;
		convert_preloaded_image(static_cast<preloaded_elf64_image*>(image));
		image = next;
	}

	// Set correct kernel args range addresses.
	dprintf("kernel args ranges:\n");
	for (uint32 i = 0; i < gKernelArgs.num_kernel_args_ranges; i++) {
		gKernelArgs.kernel_args_range[i].start = fix_address(
			gKernelArgs.kernel_args_range[i].start);
		dprintf("    base %#018" B_PRIx64 ", length %#018" B_PRIx64 "\n",
			gKernelArgs.kernel_args_range[i].start,
			gKernelArgs.kernel_args_range[i].size);
	}

	// Fix driver settings files.
	driver_settings_file* file = gKernelArgs.driver_settings;
	fix_address(gKernelArgs.driver_settings);
	while (file != NULL) {
		driver_settings_file* next = file->next;
		fix_address(file->next);
		fix_address(file->buffer);
		file = next;
	}

	gBootPhysicalMemoryAllocator->GenerateKernelArguments();
	gBootKernelVirtualRegionAllocator.GenerateKernelArguments();

	for(uint32 i = 0 ; i < gKernelArgs.num_virtual_free_ranges ; ++i) {
		gKernelArgs.virtual_free_range[i].start = fix_address(gKernelArgs.virtual_free_range[i].start);
	}
	for(uint32 i = 0 ; i < gKernelArgs.num_virtual_allocated_ranges ; ++i) {
		gKernelArgs.virtual_allocated_range[i].start = fix_address(gKernelArgs.virtual_allocated_range[i].start);
	}

	dprintf("phys memory ranges:\n");
	for (uint32 i = 0; i < gKernelArgs.num_physical_memory_ranges; i++) {
		dprintf("    base %#018" B_PRIx64 ", length %#018" B_PRIx64 "\n",
			gKernelArgs.physical_memory_range[i].start,
			gKernelArgs.physical_memory_range[i].size);
	}

	dprintf("allocated phys memory ranges:\n");
	for (uint32 i = 0; i < gKernelArgs.num_physical_allocated_ranges; i++) {
		dprintf("    base %#018" B_PRIx64 ", length %#018" B_PRIx64 "\n",
			gKernelArgs.physical_allocated_range[i].start,
			gKernelArgs.physical_allocated_range[i].size);
	}

	dprintf("allocated virt memory ranges:\n");
	for (uint32 i = 0; i < gKernelArgs.num_virtual_allocated_ranges; i++) {
		dprintf("    base %#018" B_PRIx64 ", length %#018" B_PRIx64 "\n",
			gKernelArgs.virtual_allocated_range[i].start,
			gKernelArgs.virtual_allocated_range[i].size);
	}

	dprintf("free virt memory ranges:\n");
	for (uint32 i = 0; i < gKernelArgs.num_virtual_free_ranges; i++) {
		dprintf("    base %#018" B_PRIx64 ", length %#018" B_PRIx64 "\n",
			gKernelArgs.virtual_free_range[i].start,
			gKernelArgs.virtual_free_range[i].size);
	}
}


static void
enable_sse()
{
	x86_write_cr4(x86_read_cr4() | CR4_OS_FXSR | CR4_OS_XMM_EXCEPTION);
	x86_write_cr0(x86_read_cr0() & ~(CR0_FPU_EMULATION | CR0_MONITOR_FPU));
}


static void
long_smp_start_kernel(void)
{
	uint32 cpu = smp_get_current_cpu();

	// Important.  Make sure supervisor threads can fault on read only pages...
	asm("movl %%eax, %%cr0" : : "a" ((1 << 31) | (1 << 16) | (1 << 5) | 1));
	asm("cld");
	asm("fninit");
	enable_sse();

	// Fix our kernel stack address.
	gKernelArgs.cpu_kstack[cpu].start
		= fix_address(gKernelArgs.cpu_kstack[cpu].start);

	long_enter_kernel(cpu, gKernelArgs.cpu_kstack[cpu].start
		+ gKernelArgs.cpu_kstack[cpu].size);

	panic("Shouldn't get here");
}


void
long_start_kernel()
{
	// Check whether long mode is supported.
	cpuid_info info;
	get_current_cpuid(&info, 0x80000001, 0);
//	if ((info.regs.edx & (1 << 29)) == 0)
//		panic("64-bit kernel requires a 64-bit CPU");

	bool supports_nx = (info.regs.edx & (1 << 20)) == (1 << 20);
	bool support_1gpdpe = (info.regs.edx & (1 << 26)) == (1 << 26);

	enable_sse();

	preloaded_elf64_image *image = static_cast<preloaded_elf64_image *>(
		gKernelArgs.kernel_image.Pointer());

	smp_init_other_cpus();

	long_gdt_init();
	debug_cleanup();
	long_mmu_init(support_1gpdpe, supports_nx);
	convert_kernel_args();

	// Save the kernel entry point address.
	gLongKernelEntry = image->elf_header.e_entry;
	dprintf("kernel entry at %#llx\n", gLongKernelEntry);

	// Fix our kernel stack address.
	gKernelArgs.cpu_kstack[0].start
		= fix_address(gKernelArgs.cpu_kstack[0].start);

	// We're about to enter the kernel -- disable console output.
	stdout = NULL;

	smp_boot_other_cpus(long_smp_start_kernel);

	TRACE("Entering long kernel\n");

	// Enter the kernel!
	long_enter_kernel(0, gKernelArgs.cpu_kstack[0].start
		+ gKernelArgs.cpu_kstack[0].size);

	panic("Shouldn't get here");
}
