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
	return B_ERROR;
}
