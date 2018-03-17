/*
 * Copyright 2004-2008, Axel Dörfler, axeld@pinc-software.de.
 * Based on code written by Travis Geiselbrecht for NewOS.
 *
 * Distributed under the terms of the MIT License.
 */


#include "mmu.h"

#include <string.h>

#include <OS.h>

#include <arch/cpu.h>
#include <arch/x86/descriptors.h>
#include <arch_kernel.h>
#include <boot/platform.h>
#include <boot/stdio.h>
#include <boot/kernel_args.h>
#include <boot/stage2.h>
#include <kernel/boot/memory.h>
#include <kernel.h>
#include <system/vm_defs.h>

#include "bios.h"
#include "interrupts.h"


/*!	The (physical) memory layout of the boot loader is currently as follows:
	  0x0500 - 0x10000	protected mode stack
	  0x0500 - 0x09000	real mode stack
	 0x10000 - ?		code (up to ~500 kB)
	 0x90000			1st temporary page table (identity maps 0-4 MB)
	 0x91000			2nd (4-8 MB)
	 0x92000 - 0x92000	further page tables
	 0x9e000 - 0xa0000	SMP trampoline code
	[0xa0000 - 0x100000	BIOS/ROM/reserved area]
	0x100000			page directory
	     ...			boot loader heap (32 kB)
	     ...			free physical memory

	The first 8 MB are identity mapped (0x0 - 0x0800000); paging is turned
	on. The kernel is mapped at 0x80000000, all other stuff mapped by the
	loader (kernel args, modules, driver settings, ...) comes after
	0x80020000 which means that there is currently only 2 MB reserved for
	the kernel itself (see kMaxKernelSize).

	The layout in PXE mode differs a bit from this, see definitions below.
*/

//#define TRACE_MMU
#ifdef TRACE_MMU
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) ;
#endif


//#define TRACE_MEMORY_MAP
	// Define this to print the memory map to serial debug,
	// You also need to define ENABLE_SERIAL in serial.cpp
	// for output to work.


// memory structure returned by int 0x15, ax 0xe820
struct extended_memory {
	uint64 base_addr;
	uint64 length;
	uint32 type;
};

static BlockPhysicalMemoryAllocator sX86PhysicalAllocator;
static BlockVirtualRegionAllocator sX86BootLoaderVirtualAllocator;

segment_descriptor gBootGDT[BOOT_GDT_SEGMENT_COUNT];

static const uint32 kDefaultPageTableFlags = 0x07;	// present, user, R/W

// working page directory and page table
static uint32 *sPageDirectory = 0;
static bool sMMUActive = false;
static uint32 *sPhysicalMapperPageTable = 0;

#define INITIAL_LOADER_MAPPING_SIZE		0x800000

class X86PhysicalMemoryMapper : public PageTablePhysicalMemoryMapperWithStorage<8>
{
public:
	virtual void * MapPhysicalPage(uint64 physicalAddress) {
		if(!sMMUActive || physicalAddress < INITIAL_LOADER_MAPPING_SIZE)
			return (void *)(addr_t)physicalAddress;
		return PageTablePhysicalMemoryMapperWithStorage<8>::MapPhysicalPage(physicalAddress);
	}

	virtual void MapOnePage(addr_t virtualAddress, uint64 physicalAddress) {
		sPhysicalMapperPageTable[(virtualAddress >> 12) & 1023] = physicalAddress | 3;
		__asm__ __volatile__("invlpg (%0)" :: "r"(virtualAddress));
	}
};

static X86PhysicalMemoryMapper sX86PhysicalMemoryMapper;

class X86VirtualMemoryMapper : public VirtualMemoryMapper
{
public:
	uint64 get_next_page_table()
	{
		uint64 address = 0;
		status_t error = sX86PhysicalAllocator.AllocatePhysicalMemory(B_PAGE_SIZE,
				B_PAGE_SIZE,
				address);

		if(error != B_OK) {
			panic("Failed to allocate memory for page table");
		}

		if(address >= 0x100000000ULL) {
			panic("Allocated page table in PAE region");
		}

		TRACE("Allocated physical page table at %" B_PRIx64 "\n", address);

		void * page = sX86PhysicalMemoryMapper.MapPhysicalPage(address);
		memset(page, 0, B_PAGE_SIZE);

		return address;
	}

	uint32 * add_page_table(addr_t base)
	{
		base = ROUNDDOWN(base, B_PAGE_SIZE * 1024);

		// Get new page table and clear it out
		phys_addr_t pageTablePhys = get_next_page_table();

		TRACE("add_page_table(base = %p), got page: %" B_PRIx64 "\n", (void*)base, pageTablePhys);

		// put the new page table into the page directory
		sPageDirectory[base / (4 * 1024 * 1024)]
			= (uint32)pageTablePhys | kDefaultPageTableFlags;

		return (uint32 *)sX86PhysicalMemoryMapper.MapPhysicalPage(pageTablePhys);
	}

	virtual status_t MapVirtualMemoryRegion(uint64 virtualAddress, uint64 physicalAddress, uint64 size, uint32 protection) {
		const uint32 pteProtection = ((protection & (B_KERNEL_WRITE_AREA | B_KERNEL_EXECUTE_AREA | B_WRITE_AREA | B_EXECUTE_AREA)) ? 2 : 0) |	1;

		TRACE("MapVirtualMemoryRegion: virtualAddress=%" B_PRIx64 ", physicalAddress = %" B_PRIx64 ", size = %" B_PRIx64 ", protection = %" B_PRIx32 "\n",
				virtualAddress,
				physicalAddress,
				size,
				protection);

		size = ROUNDUP(virtualAddress + size, B_PAGE_SIZE) - ROUNDDOWN(virtualAddress, B_PAGE_SIZE);
		virtualAddress = ROUNDDOWN(virtualAddress, B_PAGE_SIZE);
		physicalAddress = ROUNDDOWN(physicalAddress, B_PAGE_SIZE);

		physicalAddress &= ~(B_PAGE_SIZE - 1);

		TRACE("MapVirtualMemoryRegion: Realigned to virtualAddress=%" B_PRIx64 ", physicalAddress = %" B_PRIx64 ", size = %" B_PRIx64 ", protection = %" B_PRIx32 "\n",
				virtualAddress,
				physicalAddress,
				size,
				protection);

		while(size > 0) {
			uint32 pgdIndex = virtualAddress / (4 * 1024 * 1024);
			uint32 * table;

			if(!sPageDirectory[pgdIndex]) {
				table = add_page_table(virtualAddress);
			} else {
				table = (uint32 *)sX86PhysicalMemoryMapper.MapPhysicalPage(sPageDirectory[pgdIndex] & ~(B_PAGE_SIZE - 1));
			}

			uint32 pteIndex = (virtualAddress >> 12) & 1023;

			while(pteIndex < 1024 && size > 0)
			{
				table[pteIndex] = physicalAddress | pteProtection;

				__asm__ __volatile__("invlpg (%0)" :: "r"(virtualAddress));

				++pteIndex;
				size -= B_PAGE_SIZE;
				virtualAddress += B_PAGE_SIZE;
				physicalAddress += B_PAGE_SIZE;
			}
		}

		return B_OK;
	}

	virtual status_t ProtectVirtualMemoryRegion(uint64 virtualAddress, uint64 size, uint32 protection) {
		const uint32 pteProtection = ((protection & (B_KERNEL_WRITE_AREA | B_KERNEL_EXECUTE_AREA | B_WRITE_AREA | B_EXECUTE_AREA)) ? 2 : 0) |	1;

		size = ROUNDUP(virtualAddress + size, B_PAGE_SIZE) - ROUNDDOWN(virtualAddress, B_PAGE_SIZE);
		virtualAddress = ROUNDDOWN(virtualAddress, B_PAGE_SIZE);

		TRACE("ProtectVirtualMemoryRegion: virtualAddress=%" B_PRIx64 ", size = %" B_PRIx64 ", protection = %" B_PRIx32 "\n",
				virtualAddress,
				size,
				protection);

		while(size > 0) {
			uint32 pgdIndex = virtualAddress / (4 * 1024 * 1024);
			uint32 * table;

			if(!sPageDirectory[pgdIndex]) {
				panic("Trying to protect non-existent region");
				return B_ERROR;
			} else {
				table = (uint32 *)sX86PhysicalMemoryMapper.MapPhysicalPage(sPageDirectory[pgdIndex] & ~(B_PAGE_SIZE - 1));
			}

			uint32 pteIndex = (virtualAddress >> 12) & 1023;

			while(pteIndex < 1024 && size > 0)
			{
				table[pteIndex] = (table[pteIndex] & ~0x3) | pteProtection;

				__asm__ __volatile__("invlpg (%0)" :: "r"(virtualAddress));

				++pteIndex;
				size -= B_PAGE_SIZE;
				virtualAddress += B_PAGE_SIZE;
			}
		}

		return B_OK;
	}

	virtual status_t UnmapVirtualMemoryRegion(uint64 virtualAddress, uint64 size) {
		size = ROUNDUP(virtualAddress + size, B_PAGE_SIZE) - ROUNDDOWN(virtualAddress, B_PAGE_SIZE);
		virtualAddress = ROUNDDOWN(virtualAddress, B_PAGE_SIZE);

		TRACE("UnmapVirtualMemoryRegion: virtualAddress=%" B_PRIx64 ", size = %" B_PRIx64 "\n",
				virtualAddress,
				size);

		while(size > 0) {
			uint32 pgdIndex = virtualAddress / (4 * 1024 * 1024);
			uint32 * table;

			if(!sPageDirectory[pgdIndex]) {
				panic("Trying to unmap non-existent region");
				return B_ERROR;
			} else {
				table = (uint32 *)sX86PhysicalMemoryMapper.MapPhysicalPage(sPageDirectory[pgdIndex] & ~(B_PAGE_SIZE - 1));
			}

			uint32 pteIndex = (virtualAddress >> 12) & 1023;

			while(pteIndex < 1024 && size > 0)
			{
				table[pteIndex] = 0;

				__asm__ __volatile__("invlpg (%0)" :: "r"(virtualAddress));

				++pteIndex;
				size -= B_PAGE_SIZE;
				virtualAddress += B_PAGE_SIZE;
			}
		}

		return B_OK;
	}

	virtual uint64 QueryVirtualAddress(uint64 virtualAddress, uint32& regionSize) {
		uint32 pgdIndex = virtualAddress / (4 * 1024 * 1024);
		regionSize = B_PAGE_SIZE;
		if(!sPageDirectory[pgdIndex])
			return 0;
		uint32 * table = (uint32 *)sX86PhysicalMemoryMapper.MapPhysicalPage(sPageDirectory[pgdIndex] & ~(B_PAGE_SIZE - 1));
		uint32 pteIndex = (virtualAddress >> 12) & 1023;
		return table[pteIndex] & ~(B_PAGE_SIZE - 1);
	}

	virtual void * MapPhysicalLoaderMemory(uint64 physicalAddress, size_t size, bool allowTemporaryMapping) {
		if(allowTemporaryMapping && (physicalAddress + size) <= INITIAL_LOADER_MAPPING_SIZE)
		{
			// We have identity mapping. No need to do anything
			TRACE("MapPhysicalLoaderMemory: Use identity mapping\n");
			return (void *)(addr_t)physicalAddress;
		}

		return VirtualMemoryMapper::MapPhysicalLoaderMemory(physicalAddress, size, allowTemporaryMapping);
	}

	virtual void UnmapPhysicalLoaderMemory(void * memory, size_t size) {
		if(((addr_t)memory + size) <= INITIAL_LOADER_MAPPING_SIZE)
			return;

		VirtualMemoryMapper::UnmapPhysicalLoaderMemory(memory, size);
	}
};

static X86VirtualMemoryMapper sX86VirtualMapper;

#ifdef TRACE_MEMORY_MAP
static const char *
e820_memory_type(uint32 type)
{
	switch (type) {
		case 1: return "memory";
		case 2: return "reserved";
		case 3: return "ACPI reclaim";
		case 4: return "ACPI NVS";
		default: return "unknown/reserved";
	}
}
#endif


static uint32
get_memory_map(extended_memory **_extendedMemory)
{
	extended_memory *block = (extended_memory *)kExtraSegmentScratch;
	bios_regs regs = {0, 0, sizeof(extended_memory), 0, 0, (uint32)block, 0, 0};
	uint32 count = 0;

	TRACE("get_memory_map()\n");

	do {
		regs.eax = 0xe820;
		regs.edx = 'SMAP';

		call_bios(0x15, &regs);
		if ((regs.flags & CARRY_FLAG) != 0)
			return 0;

		regs.edi += sizeof(extended_memory);
		count++;
	} while (regs.ebx != 0);

	*_extendedMemory = block;

#ifdef TRACE_MEMORY_MAP
	dprintf("extended memory info (from 0xe820):\n");
	for (uint32 i = 0; i < count; i++) {
		dprintf("    base 0x%08Lx, len 0x%08Lx, type %lu (%s)\n",
			block[i].base_addr, block[i].length,
			block[i].type, e820_memory_type(block[i].type));
	}
#endif

	return count;
}


static void
init_page_directory(void)
{
	TRACE("init_page_directory\n");

	uint64 pageDirectoryPhys = sX86VirtualMapper.get_next_page_table();

	// allocate a new pgdir
	sPageDirectory = (uint32 *)(uint64)pageDirectoryPhys;

	// Create mapping for page table mapper
	uint64 physicalPageTablePhys = 0;
	sX86PhysicalAllocator.AllocatePhysicalMemory(B_PAGE_SIZE, B_PAGE_SIZE, physicalPageTablePhys);

	// Clear table mapper's page table
	memset((void *)(addr_t)physicalPageTablePhys, 0, B_PAGE_SIZE);

	// Create primary mapping. Virtual base address of pointd to sPhysicalMapperPageTable
	sPageDirectory[(addr_t)sPhysicalMapperPageTable / (4 * 1024 * 1024)] = physicalPageTablePhys | kDefaultPageTableFlags;

	// Entry 0 points to itself so we can access the table
	((uint32 *)(addr_t)physicalPageTablePhys)[0] = physicalPageTablePhys | 3;

	// Identity map the first 16 MB of memory so that their
	// physical and virtual address are the same.
	// These page tables won't be taken over into the kernel.
	sX86VirtualMapper.MapVirtualMemoryRegion(0, 0, INITIAL_LOADER_MAPPING_SIZE, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_KERNEL_EXECUTE_AREA);
}

/*!	Sets up the final and kernel accessible GDT and IDT tables.
	BIOS calls won't work any longer after this function has
	been called.
*/
extern "C" void
mmu_init_for_kernel(void)
{
	TRACE("mmu_init_for_kernel\n");

	STATIC_ASSERT(BOOT_GDT_SEGMENT_COUNT > KERNEL_CODE_SEGMENT
		&& BOOT_GDT_SEGMENT_COUNT > KERNEL_DATA_SEGMENT
		&& BOOT_GDT_SEGMENT_COUNT > USER_CODE_SEGMENT
		&& BOOT_GDT_SEGMENT_COUNT > USER_DATA_SEGMENT);

	// set up a new gdt

	// put standard segment descriptors in GDT
	clear_segment_descriptor(&gBootGDT[0]);

	// seg 0x08 - kernel 4GB code
	set_segment_descriptor(&gBootGDT[KERNEL_CODE_SEGMENT], 0, 0xffffffff,
		DT_CODE_READABLE, DPL_KERNEL);

	// seg 0x10 - kernel 4GB data
	set_segment_descriptor(&gBootGDT[KERNEL_DATA_SEGMENT], 0, 0xffffffff,
		DT_DATA_WRITEABLE, DPL_KERNEL);

	// seg 0x1b - ring 3 user 4GB code
	set_segment_descriptor(&gBootGDT[USER_CODE_SEGMENT], 0, 0xffffffff,
		DT_CODE_READABLE, DPL_USER);

	// seg 0x23 - ring 3 user 4GB data
	set_segment_descriptor(&gBootGDT[USER_DATA_SEGMENT], 0, 0xffffffff,
		DT_DATA_WRITEABLE, DPL_USER);

	// load the GDT
	struct gdt_idt_descr gdtDescriptor;
	gdtDescriptor.limit = sizeof(gBootGDT);
	gdtDescriptor.base = gBootGDT;

	asm("lgdt %0" : : "m" (gdtDescriptor));

	TRACE("gdt at virtual address %p\n", gBootGDT);

	// sort the address ranges
	sort_address_ranges(gKernelArgs.physical_memory_range,
		gKernelArgs.num_physical_memory_ranges);
	sort_address_ranges(gKernelArgs.physical_allocated_range,
		gKernelArgs.num_physical_allocated_ranges);
	sort_address_ranges(gKernelArgs.virtual_allocated_range,
		gKernelArgs.num_virtual_allocated_ranges);
	sort_address_ranges(gKernelArgs.virtual_free_range,
		gKernelArgs.num_virtual_free_ranges);

#ifdef TRACE_MEMORY_MAP
	{
		uint32 i;

		dprintf("phys memory ranges:\n");
		for (i = 0; i < gKernelArgs.num_physical_memory_ranges; i++) {
			dprintf("    base %#018" B_PRIx64 ", length %#018" B_PRIx64 "\n",
				gKernelArgs.physical_memory_range[i].start,
				gKernelArgs.physical_memory_range[i].size);
		}

		dprintf("allocated phys memory ranges:\n");
		for (i = 0; i < gKernelArgs.num_physical_allocated_ranges; i++) {
			dprintf("    base %#018" B_PRIx64 ", length %#018" B_PRIx64 "\n",
				gKernelArgs.physical_allocated_range[i].start,
				gKernelArgs.physical_allocated_range[i].size);
		}

		dprintf("allocated virt memory ranges:\n");
		for (i = 0; i < gKernelArgs.num_virtual_allocated_ranges; i++) {
			dprintf("    base %#018" B_PRIx64 ", length %#018" B_PRIx64 "\n",
				gKernelArgs.virtual_allocated_range[i].start,
				gKernelArgs.virtual_allocated_range[i].size);
		}

		dprintf("free virt memory ranges:\n");
		for (i = 0; i < gKernelArgs.num_virtual_free_ranges; i++) {
			dprintf("    base %#018" B_PRIx64 ", length %#018" B_PRIx64 "\n",
				gKernelArgs.virtual_free_range[i].start,
				gKernelArgs.virtual_free_range[i].size);
		}
	}
#endif
}

extern "C" {
	extern char __executable_start[];
	extern char _end[];
}

extern "C" void
mmu_init(void)
{
	TRACE("mmu_init\n");

	gBootPhysicalMemoryAllocator = &sX86PhysicalAllocator;
	gBootVirtualMemoryMapper = &sX86VirtualMapper;
	gBootLoaderVirtualRegionAllocator = &sX86BootLoaderVirtualAllocator;
	gBootPageTableMapper = &sX86PhysicalMemoryMapper;

	// Bootstrap the virtual region allocator. Reserve the last page so we don't overflow
	gBootKernelVirtualRegionAllocator.Init(KERNEL_LOAD_BASE, 0xfffff000);
	sX86BootLoaderVirtualAllocator.Init(INITIAL_LOADER_MAPPING_SIZE, KERNEL_LOAD_BASE);

	extended_memory *extMemoryBlock;
	uint32 extMemoryCount = get_memory_map(&extMemoryBlock);

	// figure out the memory map
	if (extMemoryCount > 0) {
		for (uint32 i = 0; i < extMemoryCount; i++) {
			// Type 1 is available memory
			if (extMemoryBlock[i].type == 1) {
				uint64 base = extMemoryBlock[i].base_addr;
				uint64 length = extMemoryBlock[i].length;
				uint64 end = base + length;

				// round everything up to page boundaries, exclusive of pages
				// it partially occupies
				base = ROUNDUP(base, B_PAGE_SIZE);
				end = ROUNDDOWN(end, B_PAGE_SIZE);

				// We ignore all memory beyond 4 GB, if phys_addr_t is only
				// 32 bit wide.
#if B_HAIKU_PHYSICAL_BITS == 32
				if (end > 0x100000000ULL)
					end = 0x100000000ULL;
#endif

				// Also ignore memory below 1 MB. Apparently some BIOSes fail to
				// provide the correct range type for some ranges (cf. #1925).
				// Later in the kernel we will reserve the range 0x0 - 0xa0000
				// and apparently 0xa0000 - 0x100000 never contain usable
				// memory, so we don't lose anything by doing that.
				if (base < 0x100000)
					base = 0x100000;

				if (end <= base)
					continue;

				sX86PhysicalAllocator.AddRegion(base, end - base);
			}
		}
	} else {
		bios_regs regs;


		regs.eax = 0xe801; // AX
		call_bios(0x15, &regs);
		if ((regs.flags & CARRY_FLAG) != 0) {
			regs.eax = 0x8800; // AH 88h
			call_bios(0x15, &regs);
			if ((regs.flags & CARRY_FLAG) != 0) {
				// TODO: for now!
				dprintf("No memory size - using 64 MB (fix me!)\n");
				uint32 memSize = 64 * 1024 * 1024;

				sX86PhysicalAllocator.AddRegion(0x100000, memSize - 0x100000);
			} else {
				dprintf("Get Extended Memory Size succeeded.\n");
				sX86PhysicalAllocator.AddRegion(0x100000, regs.eax * 1024);
			}
		} else {
			dprintf("Get Memory Size for Large Configurations succeeded.\n");
			sX86PhysicalAllocator.AddRegion(0x100000, regs.ecx * 1024);
			sX86PhysicalAllocator.AddRegion(0x1000000, regs.edx * 64 * 1024);
		}
	}

	// Don't use low addresses for physical allocation as there is BIOS
	// and probably some other stuff we need to don't care about
	sX86PhysicalAllocator.ReservePlatformRegion(0, 0x100000);

	// Exclude bootloader range (if it's loaded above 1MB)
	sX86PhysicalAllocator.ReservePlatformRegion((addr_t)__executable_start, ROUNDUP((addr_t)_end, B_PAGE_SIZE) - (addr_t)__executable_start);

	// Allocate virtual bootloader 4MB region for memory remap - acually we use only part of the region
	sX86BootLoaderVirtualAllocator.AllocateVirtualMemoryRegion((void **)&sPhysicalMapperPageTable,
			B_PAGE_SIZE * 1024, // 1 slot for self-mapping
			B_PAGE_SIZE * 1024, // Align to 4MB
			false,
			true);

	init_page_directory();

	sX86PhysicalMemoryMapper.Init(((addr_t)sPhysicalMapperPageTable) + B_PAGE_SIZE);

	void * pgdir_vir = NULL;
	gBootKernelVirtualRegionAllocator.AllocateVirtualMemoryRegion(&pgdir_vir, B_PAGE_SIZE,
				B_PAGE_SIZE, false, true);

	gKernelArgs.arch_args.vir_pgdir = (addr_t)pgdir_vir;

	sX86VirtualMapper.MapVirtualMemoryRegion((addr_t)pgdir_vir, (uint32) sPageDirectory,
			B_PAGE_SIZE, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);

	// allocate boot CPU kernel stack
	void * boot_cpu_stack = NULL;

	gBootKernelVirtualRegionAllocator.AllocateVirtualMemoryRegion(&boot_cpu_stack,
			KERNEL_STACK_SIZE + KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE,
			B_PAGE_SIZE,
			false,
			true);

	uint64 boot_cpu_stack_phys = 0;

	sX86PhysicalAllocator.AllocatePhysicalMemory(KERNEL_STACK_SIZE,
			B_PAGE_SIZE,
			boot_cpu_stack_phys);

	sX86VirtualMapper.MapVirtualMemoryRegion((addr_t)boot_cpu_stack + KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE, boot_cpu_stack_phys,
			KERNEL_STACK_SIZE, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);

	gKernelArgs.cpu_kstack[0].start = (addr_t)boot_cpu_stack;
	gKernelArgs.cpu_kstack[0].size = KERNEL_STACK_SIZE + KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE;

	TRACE("kernel stack at 0x%" B_PRIx64 " to 0x%" B_PRIx64 "\n",
		gKernelArgs.cpu_kstack[0].start, gKernelArgs.cpu_kstack[0].start
		+ gKernelArgs.cpu_kstack[0].size);

	// switch to the new pgdir and enable paging
	asm("movl %0, %%eax;"
		"movl %%eax, %%cr3;" : : "m" (sPageDirectory) : "eax");
	// Important.  Make sure supervisor threads can fault on read only pages...
	asm("movl %%eax, %%cr0" : : "a" ((1 << 31) | (1 << 16) | (1 << 5) | 1));

	// Remap page directory to new virtual address
	sPageDirectory = (uint32 *)pgdir_vir;
	sMMUActive = true;
}
