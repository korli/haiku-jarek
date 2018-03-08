/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef ELF_LOAD_IMAGE_H
#define ELF_LOAD_IMAGE_H

#include "runtime_loader_private.h"


status_t	parse_elf_header(Elf_Ehdr* eheader, int32* _pheaderSize,
				int32* _sheaderSize);
status_t	load_image(char const* name, image_type type, const char* rpath,
				const char* requestingObjectPath, image_t** _image);


#endif	// ELF_LOAD_IMAGE_H
