/*
 * Copyright 2008-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2003-2012, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2002, Manuel J. Petit. All rights reserved.
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

#include "elf_load_image.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <syscalls.h>

#include "add_ons.h"
#include "elf_haiku_version.h"
#include "elf_symbol_lookup.h"
#include "elf_tls.h"
#include "elf_versioning.h"
#include "images.h"
#include "runtime_loader_private.h"


static const char* sSearchPathSubDir = NULL;


static const char*
get_program_path()
{
	return gProgramImage != NULL ? gProgramImage->path : NULL;
}


static int32
count_regions(const char* imagePath, char const* buff, int phnum, unsigned int phentsize)
{
	Elf_Phdr* pheaders;
	int32 count = 0;
	int i;

	for (i = 0; i < phnum; i++) {
		pheaders = (Elf_Phdr*)(buff + i * phentsize);

		switch (pheaders->p_type) {
			case PT_NULL:
				// NOP header
				break;
			case PT_LOAD:
				++count;

				if (pheaders->p_align & PAGE_MASK) {
					FATAL("%s: PT_LOAD segment is not page aligned\n", imagePath);
					return -1;
				}

				if (pheaders->p_filesz != pheaders->p_memsz) {
					Elf_Addr bss_vaddr = TO_PAGE_SIZE(pheaders->p_vaddr + pheaders->p_filesz);
					Elf_Addr bss_vlimit = TO_PAGE_SIZE(pheaders->p_vaddr + pheaders->p_memsz);
					if(bss_vlimit > bss_vaddr) {
						++count;
					}
				}

				break;
			case PT_DYNAMIC:
				// will be handled at some other place
				break;
			case PT_INTERP:
				// should check here for appropriate interpreter
				break;
			case PT_NOTE:
				// unsupported
				break;
			case PT_SHLIB:
				// undefined semantics
				break;
			case PT_PHDR:
				// we don't use it
				break;
			case PT_GNU_RELRO:
				// not implemented yet, but can be ignored
				break;
			case PT_GNU_STACK:
				// we don't use it
				break;
			case PT_TLS:
				// will be handled at some other place
				break;
			case PT_GNU_EH_FRAME:
				// don't care
				break;
			default:
				FATAL("%s: Unhandled pheader type in count 0x%lx\n",
					imagePath, (unsigned long)pheaders->p_type);
				break;
		}
	}

	return count;
}


static status_t
parse_program_headers(image_t* image, char* buff, int phnum, int phentsize)
{
	Elf_Phdr* pheader;
	int regcount;
	int i;

	image->dso_tls_id = unsigned(-1);

	regcount = 0;
	for (i = 0; i < phnum; i++) {
		pheader = (Elf_Phdr*)(buff + i * phentsize);

		switch (pheader->p_type) {
			case PT_NULL:
				/* NOP header */
				break;
			case PT_LOAD:
				assert(regcount < (int)image->num_regions);

				image->regions[regcount].start = pheader->p_vaddr;
				image->regions[regcount].size = pheader->p_filesz;
				image->regions[regcount].vmstart = PAGE_BASE(pheader->p_vaddr);
				image->regions[regcount].vmsize	= TO_PAGE_SIZE(pheader->p_vaddr + pheader->p_filesz) - PAGE_BASE(pheader->p_vaddr);
				image->regions[regcount].fdstart = pheader->p_offset;
				image->regions[regcount].fdsize = pheader->p_filesz;
				image->regions[regcount].delta = 0;
				image->regions[regcount].flags = 0;

				TRACE(("%s: vmstart=%p vmsize=%zd\n",
						image->path,
						(void *)image->regions[regcount].vmstart,
						(size_t)image->regions[regcount].vmsize));

				if(pheader->p_flags & PF_W) {
					image->regions[regcount].flags |= RFLAG_RW;
				}

				if(pheader->p_flags & PF_X) {
					image->regions[regcount].flags |= RFLAG_EXECUTABLE;
				}

				++regcount;

				if(pheader->p_filesz != pheader->p_memsz) {
					image->regions[regcount - 1].flags |= RFLAG_CLEAR_TRAILER;

					Elf_Addr bss_vaddr = TO_PAGE_SIZE(pheader->p_vaddr + pheader->p_filesz);
					Elf_Addr bss_vlimit = TO_PAGE_SIZE(pheader->p_vaddr + pheader->p_memsz);

					if(bss_vlimit > bss_vaddr) {
						assert(regcount < (int)image->num_regions);

						image->regions[regcount].start = pheader->p_vaddr + pheader->p_filesz;
						image->regions[regcount].size = pheader->p_memsz - pheader->p_filesz;
						image->regions[regcount].vmstart = bss_vaddr;
						image->regions[regcount].vmsize	= bss_vlimit - bss_vaddr;
						image->regions[regcount].fdstart = 0;
						image->regions[regcount].fdsize = 0;
						image->regions[regcount].delta = 0;
						image->regions[regcount].flags = RFLAG_ANON;

						TRACE(("%s: bss - vmstart=%p vmsize=%zd\n",
								image->path,
								(void *)image->regions[regcount].vmstart,
								(size_t)image->regions[regcount].vmsize));

						if(pheader->p_flags & PF_W) {
							image->regions[regcount].flags |= RFLAG_RW;
						}

						if(pheader->p_flags & PF_X) {
							image->regions[regcount].flags |= RFLAG_EXECUTABLE;
						}

						++regcount;
					}
				}

				break;
			case PT_DYNAMIC:
				image->dynamic_ptr = pheader->p_vaddr;
				break;
			case PT_INTERP:
				// should check here for appropiate interpreter
				break;
			case PT_NOTE:
				// unsupported
				break;
			case PT_SHLIB:
				// undefined semantics
				break;
			case PT_PHDR:
				// we don't use it
				break;
			case PT_GNU_RELRO:
				image->relro_page = PAGE_BASE(pheader->p_vaddr);
				image->relro_size = TO_PAGE_SIZE(pheader->p_memsz);
				break;
			case PT_GNU_STACK:
				// we don't use it
				break;
			case PT_GNU_EH_FRAME:
				// don't care
				break;
			case PT_TLS:
				image->dso_tls_id
					= TLSBlockTemplates::Get().Register(
						TLSBlockTemplate((void*)pheader->p_vaddr,
							pheader->p_filesz, pheader->p_memsz));
				break;
			default:
				FATAL("%s: Unhandled pheader type in parse 0x%lx\n",
					image->path, (unsigned long)pheader->p_type);
				return B_BAD_DATA;
		}
	}

	return B_OK;
}


static bool
assert_dynamic_loadable(image_t* image)
{
	uint32 i;

	if (!image->dynamic_ptr)
		return true;

	for (i = 0; i < image->num_regions; i++) {
		if (image->dynamic_ptr >= image->regions[i].start
			&& image->dynamic_ptr
				< image->regions[i].start + image->regions[i].size) {
			return true;
		}
	}

	return false;
}


static bool
parse_dynamic_segment(image_t* image)
{
	Elf_Dyn* d;
	int i;
	int sonameOffset = -1;
	const Elf_Hashelt * hashtab;
    Elf32_Word bkt, nmaskwords;
    int bloom_size32;

	image->valid_hash_sysv = false;
	image->valid_hash_gnu = false;
	image->syms = 0;
	image->strtab = 0;

	d = (Elf_Dyn*)image->dynamic_ptr;
	if (!d)
		return true;

	for (i = 0; d[i].d_tag != DT_NULL; i++) {
		switch (d[i].d_tag) {
			case DT_NEEDED:
				image->num_needed += 1;
				break;
			case DT_HASH:
				hashtab = (const Elf_Hashelt *)(d[i].d_un.d_ptr	+ image->regions[0].delta);
				image->nbuckets = hashtab[0];
				image->nchains = hashtab[1];
				image->buckets = hashtab + 2;
				image->chains = image->buckets + image->nbuckets;
				image->valid_hash_sysv = image->nbuckets > 0 && image->nchains > 0 && image->buckets != NULL;
				break;
			case DT_GNU_HASH:
				hashtab = (const Elf_Hashelt *)(d[i].d_un.d_ptr	+ image->regions[0].delta);
				image->nbuckets_gnu = hashtab[0];
				image->symndx_gnu = hashtab[1];
				nmaskwords = hashtab[2];
				bloom_size32 = (__ELF_WORD_SIZE / 32) * nmaskwords;
				image->maskwords_bm_gnu = nmaskwords - 1;
				image->shift2_gnu = hashtab[3];
				image->bloom_gnu = (Elf_Addr *) (hashtab + 4);
				image->buckets_gnu = hashtab + 4 + bloom_size32;
				image->chain_zero_gnu = image->buckets_gnu + image->nbuckets_gnu - image->symndx_gnu;
				/* Number of bitmask words is required to be power of 2 */
				image->valid_hash_gnu = powerof2(nmaskwords) && image->nbuckets_gnu > 0 && image->buckets_gnu != NULL;
				break;
			case DT_STRTAB:
				image->strtab
					= (char*)(d[i].d_un.d_ptr + image->regions[0].delta);
				break;
			case DT_SYMTAB:
				image->syms = (Elf_Sym*)
					(d[i].d_un.d_ptr + image->regions[0].delta);
				break;
			case DT_REL:
				image->rel = (Elf_Rel*)
					(d[i].d_un.d_ptr + image->regions[0].delta);
				break;
			case DT_RELSZ:
				image->rel_len = d[i].d_un.d_val;
				break;
			case DT_RELA:
				image->rela = (Elf_Rela*)
					(d[i].d_un.d_ptr + image->regions[0].delta);
				break;
			case DT_RELASZ:
				image->rela_len = d[i].d_un.d_val;
				break;
			case DT_JMPREL:
				// procedure linkage table relocations
				image->pltrel = (Elf_Rel*)
					(d[i].d_un.d_ptr + image->regions[0].delta);
				break;
			case DT_PLTRELSZ:
				image->pltrel_len = d[i].d_un.d_val;
				break;
			case DT_INIT:
				image->init_routine
					= (d[i].d_un.d_ptr + image->regions[0].delta);
				break;
			case DT_FINI:
				image->term_routine
					= (d[i].d_un.d_ptr + image->regions[0].delta);
				break;
			case DT_SONAME:
				sonameOffset = d[i].d_un.d_val;
				break;
			case DT_VERSYM:
				image->symbol_versions = (elf_versym*)
					(d[i].d_un.d_ptr + image->regions[0].delta);
				break;
			case DT_VERDEF:
				image->version_definitions = (Elf_Verdef*)
					(d[i].d_un.d_ptr + image->regions[0].delta);
				break;
			case DT_VERDEFNUM:
				image->num_version_definitions = d[i].d_un.d_val;
				break;
			case DT_VERNEED:
				image->needed_versions = (Elf_Verneed*)
					(d[i].d_un.d_ptr + image->regions[0].delta);
				break;
			case DT_VERNEEDNUM:
				image->num_needed_versions = d[i].d_un.d_val;
				break;
			case DT_SYMBOLIC:
				image->flags |= RFLAG_SYMBOLIC;
				break;
			case DT_FLAGS:
			{
				uint32 flags = d[i].d_un.d_val;
				if ((flags & DF_SYMBOLIC) != 0)
					image->flags |= RFLAG_SYMBOLIC;
				if ((flags & DF_STATIC_TLS) != 0) {
					FATAL("Static TLS model is not supported.\n");
					return false;
				}
				if((flags & DT_TEXTREL) != 0) {
					image->flags |= RFLAG_TEXTREL;
				}
				break;
			}
			case DT_INIT_ARRAY:
				// array of pointers to initialization functions
				image->init_array = (addr_t*)
					(d[i].d_un.d_ptr + image->regions[0].delta);
				break;
			case DT_INIT_ARRAYSZ:
				// size in bytes of the array of initialization functions
				image->init_array_len = d[i].d_un.d_val;
				break;
			case DT_PREINIT_ARRAY:
				// array of pointers to pre-initialization functions
				image->preinit_array = (addr_t*)
					(d[i].d_un.d_ptr + image->regions[0].delta);
				break;
			case DT_PREINIT_ARRAYSZ:
				// size in bytes of the array of pre-initialization functions
				image->preinit_array_len = d[i].d_un.d_val;
				break;
			case DT_FINI_ARRAY:
				// array of pointers to termination functions
				image->term_array = (addr_t*)
					(d[i].d_un.d_ptr + image->regions[0].delta);
				break;
			case DT_FINI_ARRAYSZ:
				// size in bytes of the array of termination functions
				image->term_array_len = d[i].d_un.d_val;
				break;
			case DT_TEXTREL:
				image->flags |= RFLAG_TEXTREL;
				break;
			default:
				continue;

			// TODO: Implement:
			// DT_RELENT: The size of a DT_REL entry.
			// DT_RELAENT: The size of a DT_RELA entry.
			// DT_SYMENT: The size of a symbol table entry.
			// DT_PLTREL: The type of the PLT relocation entries (DT_JMPREL).
			// DT_BIND_NOW/DF_BIND_NOW: No lazy binding allowed.
			// DT_RUNPATH: Library search path (supersedes DT_RPATH).
			// DT_TEXTREL/DF_TEXTREL: Indicates whether text relocations are
			//		required (for optimization purposes only).
		}
	}

	// lets make sure we found all the required sections
	// lets make sure we found all the required sections
	if (!image->syms || !image->strtab)
		return false;

	if(!image->valid_hash_sysv && !image->valid_hash_gnu)
		return false;

	if (sonameOffset >= 0)
		strlcpy(image->name, image->String(sonameOffset), sizeof(image->name));

	if (image->valid_hash_sysv) {
		image->dynsymcount = image->nchains;
	} else if (image->valid_hash_gnu) {
	    const Elf32_Word *hashval;
		image->dynsymcount = 0;
		for (bkt = 0; bkt < image->nbuckets_gnu; bkt++) {
			if (image->buckets_gnu[bkt] == 0)
				continue;
			hashval = &image->chain_zero_gnu[image->buckets_gnu[bkt]];
			do {
				image->dynsymcount++;
			} while ((*hashval++ & 1u) == 0);
		}
		image->dynsymcount += image->symndx_gnu;
	}

	return true;
}

bool elf_handle_textrel(image_t * image, bool before)
{
	TRACE(("%s: Handle text relocations (%s)\n", image->path, before ? "before" : "after"));

	for(uint32 i = 0 ; i < image->num_regions ; ++i) {
		if(image->regions[i].flags & RFLAG_RW)
			continue;

		uint32 protection = B_READ_AREA
			| ((image->regions[i].flags & RFLAG_EXECUTABLE) ? B_EXECUTE_AREA : 0)
			| (before ? B_WRITE_AREA : 0);

		TRACE(("%s: set protection of %p--%p to %x\n",
				image->path,
				(void *)image->regions[i].vmstart,
				(void *)(image->regions[i].vmstart + image->regions[i].vmsize),
				protection));

		status_t status = _kern_set_memory_protection((void *)image->regions[i].vmstart, image->regions[i].vmsize, protection);

		if(status != B_OK) {
			TRACE(("Can't enforce region protection for %s (%d)\n", image->name, status));
			return false;
		}
	}
	return true;
}

bool elf_handle_relro(image_t * image)
{
	if(image->relro_size > 0){
		TRACE(("%s: set relro protection of %p--%p\n",
				(void *)(image->regions[0].delta + image->relro_page),
				(void *)(image->regions[0].delta + image->relro_page + image->relro_size)));

		status_t status = _kern_set_memory_protection((void *)(image->relro_page +
				image->regions[0].delta),
				image->relro_size, B_READ_AREA);
		if(status != B_OK) {
			TRACE(("Can't enforce relro protection for %s (%d)\n", image->name, status));
			return false;
		}

		image->relro_size = 0;
		image->relro_page = 0;
	}
	return true;
}

// #pragma mark -


status_t
parse_elf_header(Elf_Ehdr* eheader, int32* _pheaderSize,
	int32* _sheaderSize)
{
	if (memcmp(eheader->e_ident, ELFMAG, 4) != 0)
		return B_NOT_AN_EXECUTABLE;

	if (eheader->e_ident[EI_CLASS] != ELF_CLASS)
		return B_NOT_AN_EXECUTABLE;

	if (eheader->e_phoff == 0)
		return B_NOT_AN_EXECUTABLE;

	if (eheader->e_phentsize < sizeof(Elf_Phdr))
		return B_NOT_AN_EXECUTABLE;

	*_pheaderSize = eheader->e_phentsize * eheader->e_phnum;
	*_sheaderSize = eheader->e_shentsize * eheader->e_shnum;

	if (*_pheaderSize <= 0 || *_sheaderSize <= 0)
		return B_NOT_AN_EXECUTABLE;

	return B_OK;
}


status_t
load_image(char const* name, image_type type, const char* rpath,
	const char* requestingObjectPath, image_t** _image)
{
	int32 pheaderSize, sheaderSize;
	char path[PATH_MAX];
	ssize_t length;
	char pheaderBuffer[4096];
	int32 numRegions;
	image_t* found;
	image_t* image;
	status_t status;
	int fd;

	Elf_Ehdr eheader;

	// Have we already loaded that image? Don't check for add-ons -- we always
	// reload them.
	if (type != B_ADD_ON_IMAGE) {
		found = find_loaded_image_by_name(name, APP_OR_LIBRARY_TYPE);

		if (found == NULL && type != B_APP_IMAGE && gProgramImage != NULL) {
			// Special case for add-ons that link against the application
			// executable, with the executable not having a soname set.
			if (const char* lastSlash = strrchr(name, '/')) {
				if (strcmp(gProgramImage->name, lastSlash + 1) == 0)
					found = gProgramImage;
			}
		}

		if (found) {
			atomic_add(&found->ref_count, 1);
			*_image = found;
			KTRACE("rld: load_container(\"%s\", type: %d, rpath: \"%s\") "
				"already loaded", name, type, rpath);
			return B_OK;
		}
	}

	KTRACE("rld: load_container(\"%s\", type: %d, rpath: \"%s\")", name, type,
		rpath);

	strlcpy(path, name, sizeof(path));

	// find and open the file
	fd = open_executable(path, type, rpath, get_program_path(),
		requestingObjectPath, sSearchPathSubDir);
	if (fd < 0) {
		FATAL("Cannot open file %s: %s\n", name, strerror(fd));
		KTRACE("rld: load_container(\"%s\"): failed to open file", name);
		return fd;
	}

	// normalize the image path
	status = _kern_normalize_path(path, true, path);
	if (status != B_OK)
		goto err1;

	// Test again if this image has been registered already - this time,
	// we can check the full path, not just its name as noted.
	// You could end up loading an image twice with symbolic links, else.
	if (type != B_ADD_ON_IMAGE) {
		found = find_loaded_image_by_name(path, APP_OR_LIBRARY_TYPE);
		if (found) {
			atomic_add(&found->ref_count, 1);
			*_image = found;
			_kern_close(fd);
			KTRACE("rld: load_container(\"%s\"): already loaded after all",
				name);
			return B_OK;
		}
	}

	length = _kern_read(fd, 0, &eheader, sizeof(eheader));
	if (length != sizeof(eheader)) {
		status = B_NOT_AN_EXECUTABLE;
		FATAL("%s: Troubles reading ELF header\n", path);
		goto err1;
	}

	status = parse_elf_header(&eheader, &pheaderSize, &sheaderSize);
	if (status < B_OK) {
		FATAL("%s: Incorrect ELF header\n", path);
		goto err1;
	}

	// ToDo: what to do about this restriction??
	if (pheaderSize > (int)sizeof(pheaderBuffer)) {
		FATAL("%s: Cannot handle program headers bigger than %d\n",
			path, (int)sizeof(pheaderBuffer));
		status = B_UNSUPPORTED;
		goto err1;
	}

	length = _kern_read(fd, eheader.e_phoff, pheaderBuffer, pheaderSize);
	if (length != pheaderSize) {
		FATAL("%s: Could not read program headers: %s\n", path,
			strerror(length));
		status = B_BAD_DATA;
		goto err1;
	}

	numRegions = count_regions(path, pheaderBuffer, eheader.e_phnum,
		eheader.e_phentsize);
	if (numRegions <= 0) {
		FATAL("%s: Troubles parsing Program headers, numRegions = %" B_PRId32
			"\n", path, numRegions);
		status = B_BAD_DATA;
		goto err1;
	}

	image = create_image(name, path, numRegions);
	if (image == NULL) {
		FATAL("%s: Failed to allocate image_t object\n", path);
		status = B_NO_MEMORY;
		goto err1;
	}

	status = parse_program_headers(image, pheaderBuffer, eheader.e_phnum,
		eheader.e_phentsize);
	if (status < B_OK)
		goto err2;

	if (!assert_dynamic_loadable(image)) {
		FATAL("%s: Dynamic segment must be loadable (implementation "
			"restriction)\n", image->path);
		status = B_UNSUPPORTED;
		goto err2;
	}

	status = map_image(fd, path, image, eheader, type == B_APP_IMAGE);
	if (status < B_OK) {
		FATAL("%s: Could not map image: %s\n", image->path, strerror(status));
		status = B_ERROR;
		goto err2;
	}

	if (!parse_dynamic_segment(image)) {
		FATAL("%s: Troubles handling dynamic section\n", image->path);
		status = B_BAD_DATA;
		goto err3;
	}

	if ((image->flags & RFLAG_TEXTREL) &&  !elf_handle_textrel(image, true)) {
		FATAL("%s: Can't prepare text relocations\n", image->path);
		status = B_ERROR;
		goto err3;
	}

	if (eheader.e_entry != 0)
		image->entry_point = eheader.e_entry + image->regions[0].delta;

	analyze_image_haiku_version_and_abi(fd, image, eheader, sheaderSize,
		pheaderBuffer, sizeof(pheaderBuffer));

	// If sSearchPathSubDir is unset (meaning, this is the first image we're
	// loading) we init the search path subdir if the compiler version doesn't
	// match ours.
	if (sSearchPathSubDir == NULL) {
		#if __GNUC__ == 2
			if ((image->abi & B_HAIKU_ABI_MAJOR) == B_HAIKU_ABI_GCC_4)
				sSearchPathSubDir = "x86";
		#elif __GNUC__ >= 4
			if ((image->abi & B_HAIKU_ABI_MAJOR) == B_HAIKU_ABI_GCC_2)
				sSearchPathSubDir = "x86_gcc2";
		#endif
	}

	set_abi_version(image->abi);

	// init gcc version dependent image flags
	// symbol resolution strategy
	if (image->abi == B_HAIKU_ABI_GCC_2_ANCIENT)
		image->find_undefined_symbol = find_undefined_symbol_beos;

	// init version infos
	status = init_image_version_infos(image);

	image->type = type;
	register_image(image, fd, path);
	image_event(image, IMAGE_EVENT_LOADED);

	_kern_close(fd);

	enqueue_loaded_image(image);

	*_image = image;

	KTRACE("rld: load_container(\"%s\"): done: id: %" B_PRId32 " (ABI: %#"
		B_PRIx32 ")", name, image->id, image->abi);

	return B_OK;

err3:
	unmap_image(image);
err2:
	delete_image_struct(image);
err1:
	_kern_close(fd);

	KTRACE("rld: load_container(\"%s\"): failed: %s", name,
		strerror(status));

	return status;
}
