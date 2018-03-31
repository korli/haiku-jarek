/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifdef _BOOT_MODE
#	include <boot/arch.h>
#endif

#include <KernelExport.h>

#include <elf_priv.h>
#include <arch/elf.h>


//#define TRACE_ARCH_ELF
#ifdef TRACE_ARCH_ELF
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


#ifndef _BOOT_MODE
static bool
is_in_image(struct elf_image_info *image, addr_t address)
{
	for(unsigned int i = 0 ; i < image->num_regions ; ++i) {
		if(address >= image->regions[i].start &&
				address < (image->regions[i].start + image->regions[i].size))
		{
			return true;
		}
	}
	return false;
}
#endif	// !_BOOT_MODE


#ifdef _BOOT_MODE
status_t
boot_arch_elf_relocate_rel(preloaded_elf64_image* image, Elf64_Rel* rel,
	int relLength)
#else
int
arch_elf_relocate_rel(struct elf_image_info *image,
	struct elf_image_info *resolveImage, Elf64_Rel *rel, int relLength)
#endif
{
	return B_ERROR;
}


#ifdef _BOOT_MODE
status_t
boot_arch_elf_relocate_rela(preloaded_elf64_image* image, Elf64_Rela* rel,
	int relLength)
#else
int
arch_elf_relocate_rela(struct elf_image_info *image,
	struct elf_image_info *resolveImage, Elf64_Rela *rel, int relLength)
#endif
{
	for (int i = 0; i < relLength / (int)sizeof(Elf64_Rela); i++) {
		int type = ELF64_R_TYPE(rel[i].r_info);
		int symIndex = ELF64_R_SYM(rel[i].r_info);
		Elf64_Addr symAddr = 0;

		// Resolve the symbol, if any.
		if (symIndex != 0) {
			const Elf64_Sym* symbol = image->Symbol(symIndex);

			status_t status;
#ifdef _BOOT_MODE
			status = boot_elf_resolve_symbol(image, symbol, &symAddr);
#else
			status = elf_resolve_symbol(image, symbol, resolveImage, &symAddr);
#endif
			if (status < B_OK)
				return status;
		}

		// Address of the relocation.
		Elf64_Addr relocAddr = image->regions[0].delta + rel[i].r_offset;

		// Calculate the relocation value.
		Elf64_Addr relocValue;
		switch(type) {
		case R_AARCH64_ABS64:
		case R_AARCH64_GLOB_DAT:
			relocValue = symAddr + rel[i].r_addend;
			break;
		case R_AARCH64_COPY:
			dprintf("arch_elf_relocate_rela: copy relocations found in image\n");
			return B_BAD_DATA;
		case R_AARCH64_TLSDESC:
		case R_AARCH64_TLS_TPREL64:
			dprintf("arch_elf_relocate_rela: TLS relocations found in kernel image\n");
			return B_BAD_DATA;
		case R_AARCH64_RELATIVE:
			relocValue = image->regions[0].delta + rel[i].r_addend;
			break;
		case R_AARCH64_JUMP_SLOT:
			relocValue = symAddr;
			break;
		default:
			dprintf("arch_elf_relocate_rela: unhandled relocation type %d\n", type);
			return B_BAD_DATA;
		}

#ifdef _BOOT_MODE
		boot_elf64_set_relocation(relocAddr, relocValue);
#else
		if (!is_in_image(image, relocAddr)) {
			dprintf("arch_elf_relocate_rela: invalid offset %#lx\n", rel[i].r_offset);
			return B_BAD_ADDRESS;
		}

		*(Elf64_Addr *)relocAddr = relocValue;
#endif
	}

	return B_OK;
}
