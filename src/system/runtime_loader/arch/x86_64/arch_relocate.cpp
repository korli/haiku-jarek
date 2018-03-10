/*
 * Copyright 2012, Alex Smith, alex@alex-smith.me.uk.
 * Distributed under the terms of the MIT License.
 */


#include "runtime_loader_private.h"
#include "elf_tls.h"

#include <runtime_loader.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>


static status_t
relocate_rela(image_t* rootImage, image_t* image, Elf64_Rela* rel,
	size_t relLength, SymbolLookupCache* cache)
{
	for (size_t i = 0; i < relLength / sizeof(Elf64_Rela); i++) {
		int type = ELF64_R_TYPE(rel[i].r_info);
		int symIndex = ELF64_R_SYM(rel[i].r_info);
		addr_t symAddr = 0;
		image_t* symbolImage = NULL;

		// Resolve the symbol, if any.
		if (symIndex != 0) {
			const Elf_Sym* sym = image->Symbol(symIndex);

			status_t status = resolve_symbol(rootImage, image, sym, cache,
				&symAddr, &symbolImage);
			if (status != B_OK) {
				TRACE(("resolve symbol \"%s\" returned: %" B_PRId32 "\n",
					image->SymbolName(sym), status));
				printf("resolve symbol \"%s\" returned: %" B_PRId32 "\n",
					image->SymbolName(sym), status);
				return status;
			}
		}

		// Address of the relocation.
		Elf64_Addr relocAddr = image->regions[0].delta + rel[i].r_offset;

		// Calculate the relocation value.
		Elf64_Addr relocValue;
		switch(type) {
			case R_X86_64_NONE:
				continue;
			case R_X86_64_64:
				relocValue = symAddr + rel[i].r_addend;
				break;
			case R_X86_64_GLOB_DAT:
				relocValue = symAddr;
				break;
			case R_X86_64_JMP_SLOT:
				relocValue = symAddr + rel[i].r_addend;
				break;
			case R_X86_64_PC32:
				*(Elf32_Addr *)relocAddr = (Elf32_Addr)(symAddr + rel[i].r_addend - relocAddr);
				continue;
			case R_X86_64_RELATIVE:
				relocValue = image->regions[0].delta + rel[i].r_addend;
				break;
			case R_X86_64_DTPMOD64:
				relocValue = symbolImage == NULL
							? image->dso_tls_id : symbolImage->dso_tls_id;
				break;
			case R_X86_64_DTPOFF32:
			case R_X86_64_DTPOFF64:
				relocValue = symAddr;
				break;
			default:
				TRACE(("unhandled relocation type %d\n", type));
				return B_BAD_DATA;
		}

		*(Elf64_Addr *)relocAddr = relocValue;
	}

	return B_OK;
}


status_t
arch_relocate_image(image_t* rootImage, image_t* image,
	SymbolLookupCache* cache)
{
	status_t status;

	// No REL on x86_64.

	// Perform RELA relocations.
	if (image->rela) {
		status = relocate_rela(rootImage, image, image->rela, image->rela_len,
			cache);
		if (status != B_OK)
			return status;
	}

	// PLT relocations (they are RELA on x86_64).
	if (image->pltrel) {
		status = relocate_rela(rootImage, image, (Elf64_Rela*)image->pltrel,
			image->pltrel_len, cache);
		if (status != B_OK)
			return status;
	}

	return B_OK;
}

void allocate_initial_tls()
{
    /*
     * Fix the size of the static TLS block by using the maximum
     * offset allocated so far and adding a bit for dynamic modules to
     * use.
     */
	TLSState::tls_static_space = TLSState::tls_last_offset + RTLD_STATIC_TLS_EXTRA;

	void * tls = allocate_tls(0, 3*sizeof(Elf_Addr), sizeof(Elf_Addr));
	assert(tls);

	// Update pointer set by kernel
	__asm__ __volatile__("movq %0, %%fs:0" :: "r"(segbase));
}

void *__tls_get_addr(tls_index *ti)
{
    Elf_Addr** segbase;

    __asm __volatile("movq %%fs:0, %0" : "=r" (segbase));

    return tls_get_addr_common(&segbase[1], ti->ti_module, ti->ti_offset);
}
