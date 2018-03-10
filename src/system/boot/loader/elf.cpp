/*
 * Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Copyright 2012, Alex Smith, alex@alex-smith.me.uk.
 * Distributed under the terms of the MIT License.
 */


#include "elf.h"

#include <kernel/boot/arch.h>
#include <kernel/boot/platform.h>
#include <kernel/boot/stage2.h>
#include <drivers/driver_settings.h>
#include <system/elf_private.h>
#include <kernel/kernel.h>
#include <system/vm_defs.h>
#include <support/SupportDefs.h>

#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

//#define TRACE_ELF
#ifdef TRACE_ELF
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


static bool sLoadElfSymbols = true;


// #pragma mark - Generic ELF loader


template<typename Class>
class ELFLoader {
private:
	typedef typename Class::ImageType	ImageType;
	typedef typename Class::RegionType	RegionType;
	typedef typename Class::AddrType	AddrType;
	typedef typename Class::EhdrType	EhdrType;
	typedef typename Class::PhdrType	PhdrType;
	typedef typename Class::ShdrType	ShdrType;
	typedef typename Class::DynType		DynType;
	typedef typename Class::SymType		SymType;
	typedef typename Class::RelType		RelType;
	typedef typename Class::RelaType	RelaType;

public:
	static	status_t	Create(int fd, preloaded_image** _image);
	static	status_t	Load(int fd, preloaded_image* image);
	static	status_t	Relocate(preloaded_image* image);
	static	status_t	Resolve(ImageType* image, const SymType* symbol,
							AddrType* symbolAddress);

private:
	static	status_t	_LoadSymbolTable(int fd, ImageType* image);
	static	status_t	_ParseDynamicSection(ImageType* image);
};


#ifdef BOOT_SUPPORT_ELF32
struct ELF32Class {
	static const uint8 kIdentClass = ELFCLASS32;

	typedef preloaded_elf32_image	ImageType;
	typedef elf32_region			RegionType;
	typedef Elf32_Addr				AddrType;
	typedef Elf32_Ehdr				EhdrType;
	typedef Elf32_Phdr				PhdrType;
	typedef Elf32_Shdr				ShdrType;
	typedef Elf32_Dyn				DynType;
	typedef Elf32_Sym				SymType;
	typedef Elf32_Rel				RelType;
	typedef Elf32_Rela				RelaType;

	static inline status_t
	AllocateRegion(AddrType* _address, AddrType size, uint8 protection,
		void** _mappedAddress)
	{
		status_t status = platform_allocate_region((void**)_address, size,
			protection, false);
		if (status != B_OK)
			return status;

		*_mappedAddress = (void*)*_address;
		return B_OK;
	}

	static inline void*
	Map(AddrType address)
	{
		return (void*)address;
	}
};

typedef ELFLoader<ELF32Class> ELF32Loader;
#endif


#ifdef BOOT_SUPPORT_ELF64
struct ELF64Class {
	static const uint8 kIdentClass = ELFCLASS64;

	typedef preloaded_elf64_image	ImageType;
	typedef elf64_region			RegionType;
	typedef Elf64_Addr				AddrType;
	typedef Elf64_Ehdr				EhdrType;
	typedef Elf64_Phdr				PhdrType;
	typedef Elf64_Shdr				ShdrType;
	typedef Elf64_Dyn				DynType;
	typedef Elf64_Sym				SymType;
	typedef Elf64_Rel				RelType;
	typedef Elf64_Rela				RelaType;

	static inline status_t
	AllocateRegion(AddrType* _address, AddrType size, uint8 protection,
		void **_mappedAddress)
	{
#ifdef _BOOT_PLATFORM_EFI
		void* address = (void*)*_address;

		status_t status = platform_allocate_region(&address, size, protection,
			false);
		if (status != B_OK)
			return status;

		*_mappedAddress = address;
		platform_bootloader_address_to_kernel_address(address, _address);
#else
		// Assume the real 64-bit base address is KERNEL_LOAD_BASE_64_BIT and
		// the mappings in the loader address space are at KERNEL_LOAD_BASE.

		void* address = (void*)(addr_t)(*_address & 0xffffffff);

		status_t status = platform_allocate_region(&address, size, protection,
			false);
		if (status != B_OK)
			return status;

		*_mappedAddress = address;
		*_address = (AddrType)(addr_t)address + KERNEL_LOAD_BASE_64_BIT
			- KERNEL_LOAD_BASE;
#endif
		return B_OK;
	}

	static inline void*
	Map(AddrType address)
	{
#ifdef _BOOT_PLATFORM_EFI
		void *result;
		if (platform_kernel_address_to_bootloader_address(address, &result) != B_OK) {
			panic("Couldn't convert address %#lx", address);
		}
		return result;
#else
		return (void*)(addr_t)(address - KERNEL_LOAD_BASE_64_BIT
			+ KERNEL_LOAD_BASE);
#endif
	}
};

typedef ELFLoader<ELF64Class> ELF64Loader;
#endif


template<typename Class>
/*static*/ status_t
ELFLoader<Class>::Create(int fd, preloaded_image** _image)
{
	ImageType* image = (ImageType*)kernel_args_malloc(sizeof(ImageType));
	if (image == NULL)
		return B_NO_MEMORY;

	ssize_t length = read_pos(fd, 0, &image->elf_header, sizeof(EhdrType));
	if (length < (ssize_t)sizeof(EhdrType)) {
		kernel_args_free(image);
		return B_BAD_TYPE;
	}

	const EhdrType& elfHeader = image->elf_header;

	if (memcmp(elfHeader.e_ident, ELFMAG, 4) != 0
		|| elfHeader.e_ident[4] != Class::kIdentClass
		|| elfHeader.e_phoff == 0
		|| elfHeader.e_ident[EI_DATA] != ELF_TARG_DATA
		|| elfHeader.e_phentsize != sizeof(PhdrType)) {
		kernel_args_free(image);
		return B_BAD_TYPE;
	}

	image->elf_class = elfHeader.e_ident[EI_CLASS];

	*_image = image;
	return B_OK;
}


template<typename Class>
/*static*/ status_t
ELFLoader<Class>::Load(int fd, preloaded_image* _image)
{
	ssize_t length;
	status_t status;
	void* mappedRegion = NULL;
	typename Class::AddrType base_vaddr, base_vlimit, mapsize;
	int regionIndex = 0;

	ImageType* image = static_cast<ImageType*>(_image);
	const EhdrType& elfHeader = image->elf_header;

	ssize_t size = elfHeader.e_phnum * elfHeader.e_phentsize;
	PhdrType* programHeaders = (PhdrType*)malloc(size);
	if (programHeaders == NULL) {
		dprintf("error allocating space for program headers\n");
		status = B_NO_MEMORY;
		goto error1;
	}

	length = read_pos(fd, elfHeader.e_phoff, programHeaders, size);
	if (length < size) {
		TRACE(("error reading in program headers\n"));
		status = B_ERROR;
		goto error1;
	}

	image->count_regions = 0;

	for (int32 i = 0; i < elfHeader.e_phnum; i++) {
		PhdrType& header = programHeaders[i];
		if(header.p_type == PT_LOAD) {
			if(image->count_regions >= BOOT_ELF_MAX_REGIONS) {
				dprintf("Too many regions in ELF file. Increase BOOT_ELF_MAX_REGIONS\n");
				status = B_NO_MEMORY;
				goto error1;
			}
			++image->count_regions;
		}
	}

	for (int32 i = 0; i < elfHeader.e_phnum; i++) {
		PhdrType& header = programHeaders[i];

		switch (header.p_type) {
			case PT_LOAD:
				break;
			case PT_DYNAMIC:
				image->dynamic_section.start = header.p_vaddr;
				image->dynamic_section.size = header.p_memsz;
				continue;
			case PT_INTERP:
			case PT_PHDR:
			case PT_GNU_EH_FRAME: // Don't care
			case PT_GNU_STACK: // Don't care
			case PT_GNU_RELRO:
			case PT_NOTE: // Don't care
				// known but unused type
				continue;
			case PT_ARM_EXIDX: // Don't care
#if defined(__ARM__)
				image->exidx_section.start = header.p_vaddr;
				image->exidx_section.size = header.p_memsz;
#endif
				continue;
			default:
				dprintf("unhandled pheader type 0x%" B_PRIx64 "\n", (uint64_t)header.p_type);
				continue;
		}

		RegionType* region = &image->regions[regionIndex];
		++regionIndex;

		region->start = header.p_vaddr & ~AddrType(B_PAGE_SIZE - 1);
		region->size = ((header.p_memsz + header.p_vaddr + B_PAGE_SIZE - 1) & ~AddrType(B_PAGE_SIZE - 1)) - region->start;
		region->delta = 0;
		region->protection = B_KERNEL_READ_AREA |
					(header.p_flags & PF_W ? B_KERNEL_WRITE_AREA : 0) |
					(header.p_flags & PF_X ? B_KERNEL_EXECUTE_AREA : 0);

		TRACE(("segment %ld: start = 0x%llx, size = %llu, delta = %llx, prot = %x\n", i,
			(uint64)region->start, (uint64)region->size,
			(int64)(AddrType)region->delta,
			region->protection));
	}

	base_vaddr = image->regions[0].start;
	base_vlimit = image->regions[image->count_regions - 1].start + image->regions[image->count_regions - 1].size;
	mapsize = base_vlimit - base_vaddr;

	if (Class::AllocateRegion(&base_vaddr, mapsize, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, &mappedRegion) != B_OK) {
		status = B_NO_MEMORY;
		goto error1;
	}

	image->regions[0].delta = (AddrType)mappedRegion - image->regions[0].start;

	for (uint32 i = 0 ; i < image->count_regions ; ++i) {
		image->regions[i].delta = image->regions[0].delta;
		image->regions[i].start += image->regions[0].delta;

		TRACE(("#%02d: start 0x%llx, size 0x%llx, delta 0x%llx\n",
			i,
			(uint64)image->regions[i].start, (uint64)image->regions[i].size,
			(int64)(AddrType)image->regions[i].delta));

	}

	// load program data

	regionIndex = 0;

	for (uint32 i = 0; i < elfHeader.e_phnum; i++) {
		PhdrType& header = programHeaders[i];

		if (header.p_type != PT_LOAD)
			continue;

		RegionType* region = &image->regions[regionIndex];
		++regionIndex;

		TRACE(("load segment %ld (%llu bytes) mapped at %p...\n", i,
			(uint64)header.p_filesz, Class::Map(region->start)));

		length = read_pos(fd, header.p_offset,
			Class::Map(region->start + (header.p_vaddr % B_PAGE_SIZE)),
			header.p_filesz);

		if (length < (ssize_t)header.p_filesz) {
			status = B_BAD_DATA;
			dprintf("error reading in seg %" B_PRId32 "\n", i);
			goto error2;
		}

		// Clear anything above the file size (that may also contain the BSS
		// area)

		uint32 offset = (header.p_vaddr % B_PAGE_SIZE) + header.p_filesz;

		if (offset < region->size) {
			memset(Class::Map(region->start + offset), 0, region->size - offset);
		}
	}

	// offset dynamic section, and program entry addresses by the delta of the
	// regions
	image->dynamic_section.start += image->regions[0].delta;
	image->elf_header.e_entry += image->regions[0].delta;

#if defined(__ARM__)
	if(image->exidx_section.size) {
		image->exidx_section.start += image->regions[0].delta;
	}
#endif

	image->num_debug_symbols = 0;
	image->debug_symbols = NULL;
	image->debug_string_table = NULL;

	if (sLoadElfSymbols)
		_LoadSymbolTable(fd, image);

	free(programHeaders);

	return B_OK;

error2:
	if (mappedRegion != NULL)
		platform_free_region(mappedRegion, mapsize);
error1:
	free(programHeaders);
	kernel_args_free(image);

	return status;
}


template<typename Class>
/*static*/ status_t
ELFLoader<Class>::Relocate(preloaded_image* _image)
{
	ImageType* image = static_cast<ImageType*>(_image);

	status_t status = _ParseDynamicSection(image);
	if (status != B_OK)
		return status;

	// deal with the rels first
	if (image->rel) {
		TRACE(("total %i relocs\n",
			(int)image->rel_len / (int)sizeof(RelType)));

		status = boot_arch_elf_relocate_rel(image, image->rel, image->rel_len);
		if (status != B_OK)
			return status;
	}

	if (image->pltrel) {
		RelType* pltrel = image->pltrel;
		if (image->pltrel_type == DT_REL) {
			TRACE(("total %i plt-relocs\n",
				(int)image->pltrel_len / (int)sizeof(RelType)));

			status = boot_arch_elf_relocate_rel(image, pltrel,
				image->pltrel_len);
		} else {
			TRACE(("total %i plt-relocs\n",
				(int)image->pltrel_len / (int)sizeof(RelaType)));

			status = boot_arch_elf_relocate_rela(image, (RelaType*)pltrel,
				image->pltrel_len);
		}
		if (status != B_OK)
			return status;
	}

	if (image->rela) {
		TRACE(("total %i rela relocs\n",
			(int)image->rela_len / (int)sizeof(RelaType)));
		status = boot_arch_elf_relocate_rela(image, image->rela,
			image->rela_len);
		if (status != B_OK)
			return status;
	}

	for (uint32 i = 0 ; i < image->count_regions ; ++i) {
		if(image->regions[i].protection != (B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA)) {
			platform_protect_region((void *)image->regions[i].start,
					image->regions[i].size,
					image->regions[i].protection);
		}
	}

	return B_OK;
}

template<typename Class>
/*static*/ status_t
ELFLoader<Class>::Resolve(ImageType* image, const SymType* symbol,
	AddrType* symbolAddress)
{
	switch (symbol->st_shndx) {
		case SHN_UNDEF:
			// Since we do that only for the kernel, there shouldn't be
			// undefined symbols.
			TRACE(("elf_resolve_symbol: undefined symbol\n"));
			return B_MISSING_SYMBOL;
		case SHN_ABS:
			*symbolAddress = symbol->st_value;
			return B_NO_ERROR;
		case SHN_COMMON:
			// ToDo: finish this
			TRACE(("elf_resolve_symbol: COMMON symbol, finish me!\n"));
			return B_ERROR;
		default:
			// standard symbol
			*symbolAddress = symbol->st_value + image->regions[0].delta;
			return B_OK;
	}
}


template<typename Class>
/*static*/ status_t
ELFLoader<Class>::_LoadSymbolTable(int fd, ImageType* image)
{
	const EhdrType& elfHeader = image->elf_header;
	SymType* symbolTable = NULL;
	ShdrType* stringHeader = NULL;
	uint32 numSymbols = 0;
	char* stringTable;
	status_t status;

	// get section headers

	ssize_t size = elfHeader.e_shnum * elfHeader.e_shentsize;
	ShdrType* sectionHeaders = (ShdrType*)malloc(size);
	if (sectionHeaders == NULL) {
		dprintf("error allocating space for section headers\n");
		return B_NO_MEMORY;
	}

	ssize_t length = read_pos(fd, elfHeader.e_shoff, sectionHeaders, size);
	if (length < size) {
		TRACE(("error reading in program headers\n"));
		status = B_ERROR;
		goto error1;
	}

	// find symbol table in section headers

	for (int32 i = 0; i < elfHeader.e_shnum; i++) {
		if (sectionHeaders[i].sh_type == SHT_SYMTAB) {
			stringHeader = &sectionHeaders[sectionHeaders[i].sh_link];

			if (stringHeader->sh_type != SHT_STRTAB) {
				TRACE(("doesn't link to string table\n"));
				status = B_BAD_DATA;
				goto error1;
			}

			// read in symbol table
			size = sectionHeaders[i].sh_size;
			symbolTable = (SymType*)kernel_args_malloc(size);
			if (symbolTable == NULL) {
				status = B_NO_MEMORY;
				goto error1;
			}

			length = read_pos(fd, sectionHeaders[i].sh_offset, symbolTable,
				size);
			if (length < size) {
				TRACE(("error reading in symbol table\n"));
				status = B_ERROR;
				goto error1;
			}

			numSymbols = size / sizeof(SymType);
			break;
		}
	}

	if (symbolTable == NULL) {
		TRACE(("no symbol table\n"));
		status = B_BAD_VALUE;
		goto error1;
	}

	// read in string table

	size = stringHeader->sh_size;
	stringTable = (char*)kernel_args_malloc(size);
	if (stringTable == NULL) {
		status = B_NO_MEMORY;
		goto error2;
	}

	length = read_pos(fd, stringHeader->sh_offset, stringTable, size);
	if (length < size) {
		TRACE(("error reading in string table\n"));
		status = B_ERROR;
		goto error3;
	}

	TRACE(("loaded %ld debug symbols\n", numSymbols));

	// insert tables into image
	image->debug_symbols = symbolTable;
	image->num_debug_symbols = numSymbols;
	image->debug_string_table = stringTable;
	image->debug_string_table_size = size;

	free(sectionHeaders);
	return B_OK;

error3:
	kernel_args_free(stringTable);
error2:
	kernel_args_free(symbolTable);
error1:
	free(sectionHeaders);

	return status;
}


template<typename Class>
/*static*/ status_t
ELFLoader<Class>::_ParseDynamicSection(ImageType* image)
{
	image->syms = 0;
	image->rel = 0;
	image->rel_len = 0;
	image->rela = 0;
	image->rela_len = 0;
	image->pltrel = 0;
	image->pltrel_len = 0;
	image->pltrel_type = 0;

	if(image->dynamic_section.start == 0)
		return B_ERROR;

	DynType* d = (DynType*)Class::Map(image->dynamic_section.start);

	for (int i = 0; d[i].d_tag != DT_NULL; i++) {
		switch (d[i].d_tag) {
			case DT_HASH:
			case DT_STRTAB:
				break;
			case DT_SYMTAB:
				image->syms = (SymType*)Class::Map(d[i].d_un.d_ptr
					+ image->regions[0].delta);
				break;
			case DT_REL:
				image->rel = (RelType*)Class::Map(d[i].d_un.d_ptr
					+ image->regions[0].delta);
				break;
			case DT_RELSZ:
				image->rel_len = d[i].d_un.d_val;
				break;
			case DT_RELA:
				image->rela = (RelaType*)Class::Map(d[i].d_un.d_ptr
					+ image->regions[0].delta);
				break;
			case DT_RELASZ:
				image->rela_len = d[i].d_un.d_val;
				break;
			case DT_JMPREL:
				image->pltrel = (RelType*)Class::Map(d[i].d_un.d_ptr
					+ image->regions[0].delta);
				break;
			case DT_PLTRELSZ:
				image->pltrel_len = d[i].d_un.d_val;
				break;
			case DT_PLTREL:
				image->pltrel_type = d[i].d_un.d_val;
				break;
			case DT_RELENT:
				if(d[i].d_un.d_val != sizeof(typename Class::RelType)) {
					dprintf("Invalid relocation entry size\n");
					return B_ERROR;
				}
				break;
			case DT_RELAENT:
				if(d[i].d_un.d_val != sizeof(typename Class::RelaType)) {
					dprintf("Invalid addend relocation entry size\n");
					return B_ERROR;
				}
				break;
			default:
				continue;
		}
	}

	// lets make sure we found all the required sections
	if (image->syms == NULL)
		return B_ERROR;

	return B_OK;
}


// #pragma mark -


void
elf_init()
{
// TODO: This cannot work, since the driver settings are loaded *after* the
// kernel has been loaded successfully.
#if 0
	void *settings = load_driver_settings("kernel");
	if (settings == NULL)
		return;

	sLoadElfSymbols = !get_driver_boolean_parameter(settings, "load_symbols",
		false, false);
	unload_driver_settings(settings);
#endif
}


status_t
elf_load_image(int fd, preloaded_image** _image)
{
	status_t status = B_ERROR;

	TRACE(("elf_load_image(fd = %d, _image = %p)\n", fd, _image));

#if BOOT_SUPPORT_ELF64
	if (gKernelArgs.kernel_image == NULL
		|| gKernelArgs.kernel_image->elf_class == ELFCLASS64) {
		status = ELF64Loader::Create(fd, _image);
		if (status == B_OK)
			return ELF64Loader::Load(fd, *_image);
		else if (status != B_BAD_TYPE)
			return status;
	}
#endif
#if BOOT_SUPPORT_ELF32
	if (gKernelArgs.kernel_image == NULL
		|| gKernelArgs.kernel_image->elf_class == ELFCLASS32) {
		status = ELF32Loader::Create(fd, _image);
		if (status == B_OK)
			return ELF32Loader::Load(fd, *_image);
	}
#endif

	return status;
}


status_t
elf_load_image(Directory* directory, const char* path)
{
	preloaded_image* image;

	TRACE(("elf_load_image(directory = %p, \"%s\")\n", directory, path));

	int fd = open_from(directory, path, O_RDONLY);
	if (fd < 0)
		return fd;

	// check if this file has already been loaded

	struct stat stat;
	if (fstat(fd, &stat) < 0)
		return errno;

	image = gKernelArgs.preloaded_images;
	for (; image != NULL; image = image->next) {
		if (image->inode == stat.st_ino) {
			// file has already been loaded, no need to load it twice!
			close(fd);
			return B_OK;
		}
	}

	// we still need to load it, so do it

	status_t status = elf_load_image(fd, &image);
	if (status == B_OK) {
		image->name = kernel_args_strdup(path);
		image->inode = stat.st_ino;

		// insert to kernel args
		image->next = gKernelArgs.preloaded_images;
		gKernelArgs.preloaded_images = image;
	} else
		kernel_args_free(image);

	close(fd);
	return status;
}


status_t
elf_relocate_image(preloaded_image* image)
{
#ifdef BOOT_SUPPORT_ELF64
	if (image->elf_class == ELFCLASS64)
		return ELF64Loader::Relocate(image);
	else
#endif
#ifdef BOOT_SUPPORT_ELF32
		return ELF32Loader::Relocate(image);
#else
		return B_ERROR;
#endif
}


#ifdef BOOT_SUPPORT_ELF32
status_t
boot_elf_resolve_symbol(preloaded_elf32_image* image, const Elf32_Sym* symbol,
	Elf32_Addr* symbolAddress)
{
	return ELF32Loader::Resolve(image, symbol, symbolAddress);
}
#endif


#ifdef BOOT_SUPPORT_ELF64
status_t
boot_elf_resolve_symbol(preloaded_elf64_image* image, const Elf64_Sym* symbol,
	Elf64_Addr* symbolAddress)
{
	return ELF64Loader::Resolve(image, symbol, symbolAddress);
}

void
boot_elf64_set_relocation(Elf64_Addr resolveAddress, Elf64_Addr finalAddress)
{
	Elf64_Addr* dest = (Elf64_Addr*)ELF64Class::Map(resolveAddress);
	*dest = finalAddress;
}

void
boot_elf32_set_relocation(Elf64_Addr resolveAddress, Elf32_Addr finalAddress)
{
	Elf32_Addr* dest = (Elf32_Addr*)ELF64Class::Map(resolveAddress);
	*dest = finalAddress;
}
#endif
