/*
 * Copyright 2008-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2003-2008, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2002, Manuel J. Petit. All rights reserved.
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

#include "elf_symbol_lookup.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include "add_ons.h"
#include "errors.h"
#include "images.h"
#include "runtime_loader_private.h"


/*!	Checks whether \a name matches the name of \a image.

	It is expected that \a name does not contain directory components. It is
	compared with the base name of \a image's name.

	\param image The image.
	\param name The name to check against. Can be NULL, in which case \c false
		is returned.
	\return \c true, iff \a name is non-NULL and matches the name of \a image.
*/
static bool
equals_image_name(image_t* image, const char* name)
{
	if (name == NULL)
		return false;

	const char* lastSlash = strrchr(name, '/');
	return strcmp(image->name, lastSlash != NULL ? lastSlash + 1 : name) == 0;
}


// #pragma mark -


uint32
elf_hash(const char* _name)
{
	const uint8* name = (const uint8*)_name;

	uint32 hash = 0;
	uint32 temp;

	while (*name) {
		hash = (hash << 4) + *name++;
		if ((temp = hash & 0xf0000000)) {
			hash ^= temp >> 24;
		}
		hash &= ~temp;
	}
	return hash;
}

/*
 * The GNU hash function is the Daniel J. Bernstein hash clipped to 32 bits
 * unsigned in case it's implemented with a wider type.
 */
uint32_t
gnu_hash(const char *s)
{
	uint32_t h;
	unsigned char c;

	h = 5381;
	for (c = *s; c != '\0'; c = *++s)
		h = h * 33 + c;
	return (h & 0xffffffff);
}

void
patch_defined_symbol(image_t* image, const char* name, void** symbol,
	int32* type)
{
	RuntimeLoaderSymbolPatcher* patcher = image->defined_symbol_patchers;
	while (patcher != NULL && *symbol != 0) {
		image_t* inImage = image;
		patcher->patcher(patcher->cookie, NULL, image, name, &inImage,
			symbol, type);
		patcher = patcher->next;
	}
}


void
patch_undefined_symbol(image_t* rootImage, image_t* image, const char* name,
	image_t** foundInImage, void** symbol, int32* type)
{
	if (*foundInImage != NULL)
		patch_defined_symbol(*foundInImage, name, symbol, type);

	RuntimeLoaderSymbolPatcher* patcher = image->undefined_symbol_patchers;
	while (patcher != NULL) {
		patcher->patcher(patcher->cookie, rootImage, image, name, foundInImage,
			symbol, type);
		patcher = patcher->next;
	}
}


static bool
is_symbol_visible(const Elf_Sym* symbol)
{
	if (ELF_ST_BIND(symbol->st_info) == STB_GLOBAL)
		return true;
	if (ELF_ST_BIND(symbol->st_info) == STB_WEAK)
		return true;
	if (ELF_ST_BIND(symbol->st_info) == STB_GNU_UNIQUE)
		return true;
	return false;
}

static bool matches_symbol(image_t *image,
		const SymbolLookupInfo& lookupInfo,
		bool allowLocal,
		const Elf_Sym * symbol,
		const Elf_Sym *& versionedSymbol,
		uint32& versionedSymbolCount,
		unsigned long symbol_index)
{
	const char * sym_name = image->SymbolName(symbol);

	switch(ELF_ST_TYPE(symbol->st_info)) {
	case STT_FUNC:
	case STT_NOTYPE:
	case STT_OBJECT:
	case STT_COMMON:
	case STT_GNU_IFUNC:
		if(symbol->st_value == 0)
			return false;
		/* fallthrough */
	case STT_TLS:
		if (symbol->st_shndx != SHN_UNDEF)
			break;
		/* fallthrough */
	default:
		return false;
	}

	if(!allowLocal && !is_symbol_visible(symbol))
		return false;

	if(lookupInfo.name[0] != sym_name[0])
		return false;

	if(strcmp(lookupInfo.name, sym_name) != 0)
		return false;

	// check if the type matches
	uint32 type = ELF_ST_TYPE(symbol->st_info);
	if ((lookupInfo.type == B_SYMBOL_TYPE_TEXT && type != STT_FUNC)
		|| (lookupInfo.type == B_SYMBOL_TYPE_DATA && type != STT_OBJECT))
	{
		return false;
	}

	// check the version

	// Handle the simple cases -- the image doesn't have version
	// information -- first.
	if (image->symbol_versions == NULL) {
		if (lookupInfo.version == NULL) {
			// No specific symbol version was requested either, so the
			// symbol is just fine.
			return true;
		}

		// A specific version is requested. If it's the dependency
		// referred to by the requested version, it's apparently an
		// older version of the dependency and we're not happy.
		if (equals_image_name(image, lookupInfo.version->file_name)) {
			// TODO: That should actually be kind of fatal!
			return false;
		}

		// This is some other image. We accept the symbol.
		return true;
	}

	// The image has version information. Let's see what we've got.
	uint32 versionID = image->symbol_versions[symbol_index];
	uint32 versionIndex = VER_NDX(versionID);
	elf_version_info& version = image->versions[versionIndex];

	// skip local versions
	if (versionIndex == VER_NDX_LOCAL)
		return false;

	if (lookupInfo.version != NULL) {
		// a specific version is requested

		// compare the versions
		if (version.hash == lookupInfo.version->hash
			&& strcmp(version.name, lookupInfo.version->name) == 0) {
			// versions match
			return true;
		}

		// The versions don't match. We're still fine with the
		// base version, if it is public and we're not looking for
		// the default version.
		if ((versionID & VER_NDX_HIDDEN) == 0
			&& versionIndex == VER_NDX_GLOBAL
			&& (lookupInfo.flags & LOOKUP_FLAG_DEFAULT_VERSION)
				== 0) {
			// TODO: Revise the default version case! That's how
			// FreeBSD implements it, but glibc doesn't handle it
			// specially.
			return true;
		}
	} else {
		// No specific version requested, but the image has version
		// information. This can happen in either of these cases:
		//
		// * The dependent object was linked against an older version
		//   of the now versioned dependency.
		// * The symbol is looked up via find_image_symbol() or dlsym().
		//
		// In the first case we return the base version of the symbol
		// (VER_NDX_GLOBAL or VER_NDX_GIVEN), or, if that doesn't
		// exist, the unique, non-hidden versioned symbol.
		//
		// In the second case we want to return the public default
		// version of the symbol. The handling is pretty similar to the
		// first case, with the exception that we treat VER_NDX_GIVEN
		// as regular version.

		// VER_NDX_GLOBAL is always good, VER_NDX_GIVEN is fine, if
		// we don't look for the default version.
		if (versionIndex == VER_NDX_GLOBAL
			|| ((lookupInfo.flags & LOOKUP_FLAG_DEFAULT_VERSION) == 0
				&& versionIndex == VER_NDX_GIVEN)) {
			return true;
		}

		// If not hidden, remember the version -- we'll return it, if
		// it is the only one.
		if ((versionID & VER_NDX_HIDDEN) == 0) {
			versionedSymbolCount++;
			versionedSymbol = symbol;
		}
	}

	return false;
}

const Elf_Sym*
find_symbol(image_t* image, const SymbolLookupInfo& lookupInfo, bool allowLocal)
{
	if (image->dynamic_ptr == 0)
		return NULL;

	const Elf_Sym* versionedSymbol = NULL;
	uint32 versionedSymbolCount = 0;
	const Elf_Sym * sym;

	if(image->valid_hash_sysv) {
		unsigned long symnum;

		for(symnum = image->buckets[lookupInfo.hash % image->nbuckets] ;
				symnum != STN_UNDEF ;
				symnum = image->chains[symnum])
		{
			if(symnum >= image->nchains)
				return NULL;

			sym = image->Symbol(symnum);

			if(matches_symbol(image,
					lookupInfo,
					allowLocal,
					sym,
					versionedSymbol,
					versionedSymbolCount,
					symnum))
			{
				return sym;
			}
		}
	}

	if(image->valid_hash_gnu) {
		Elf_Addr bloom_word;
		const Elf32_Word *hashval;
		Elf32_Word bucket;
		unsigned int h1, h2;
		unsigned long symnum;

		uint32 hash_gnu = lookupInfo.hash_gnu;

		/* Pick right bitmask word from Bloom filter array */
		bloom_word = image->bloom_gnu[(hash_gnu / __ELF_WORD_SIZE) & image->maskwords_bm_gnu];

		/* Calculate modulus word size of gnu hash and its derivative */
		h1 = hash_gnu & (__ELF_WORD_SIZE - 1);
		h2 = ((hash_gnu >> image->shift2_gnu) & (__ELF_WORD_SIZE - 1));

		/* Filter out the "definitely not in set" queries */
		if (((bloom_word >> h1) & (bloom_word >> h2) & 1) == 0)
			return NULL;

		/* Locate hash chain and corresponding value element*/
		bucket = image->buckets_gnu[hash_gnu % image->nbuckets_gnu];

		if (bucket == 0)
			return NULL;

		hashval = &image->chain_zero_gnu[bucket];

		do {
			if (((*hashval ^ hash_gnu) >> 1) == 0) {
				symnum = hashval - image->chain_zero_gnu;
				sym = image->Symbol(symnum);

				if(matches_symbol(image,
						lookupInfo,
						allowLocal,
						sym,
						versionedSymbol,
						versionedSymbolCount,
						symnum))
				{
					return sym;
				}
			}
		} while ((*hashval++ & 1) == 0);
	}

	if(versionedSymbolCount == 1) {
		return versionedSymbol;
	}

	return NULL;
}


status_t
find_symbol(image_t* image, const SymbolLookupInfo& lookupInfo,
	void **_location)
{
	// get the symbol in the image
	const Elf_Sym* symbol = find_symbol(image, lookupInfo);
	if (symbol == NULL)
		return B_ENTRY_NOT_FOUND;

	void* location = (void*)(symbol->st_value + image->regions[0].delta);
	int32 symbolType = lookupInfo.type;
	patch_defined_symbol(image, lookupInfo.name, &location, &symbolType);

	if (_location != NULL)
		*_location = location;

	return B_OK;
}


status_t
find_symbol_breadth_first(image_t* image, const SymbolLookupInfo& lookupInfo,
	image_t** _foundInImage, void** _location)
{
	image_t* queue[count_loaded_images()];
	uint32 count = 0;
	uint32 index = 0;
	queue[count++] = image;
	image->flags |= RFLAG_VISITED;

	const Elf_Sym* candidateSymbol = NULL;
	image_t* candidateImage = NULL;

	while (index < count) {
		// pop next image
		image = queue[index++];

		const Elf_Sym* symbol = find_symbol(image, lookupInfo);
		if (symbol != NULL) {
			bool isWeak = ELF_ST_BIND(symbol->st_info) == STB_WEAK;
			if (candidateImage == NULL || !isWeak) {
				candidateSymbol = symbol;
				candidateImage = image;

				if (!isWeak)
					break;
			}
		}

		// push needed images
		for (uint32 i = 0; i < image->num_needed; i++) {
			image_t* needed = image->needed[i];
			if ((needed->flags & RFLAG_VISITED) == 0) {
				queue[count++] = needed;
				needed->flags |= RFLAG_VISITED;
			}
		}
	}

	// clear visited flags
	for (uint32 i = 0; i < count; i++)
		queue[i]->flags &= ~RFLAG_VISITED;

	if (candidateSymbol == NULL)
		return B_ENTRY_NOT_FOUND;

	// compute the symbol location
	*_location = (void*)(candidateSymbol->st_value
		+ candidateImage->regions[0].delta);
	int32 symbolType = lookupInfo.type;
	patch_defined_symbol(candidateImage, lookupInfo.name, _location,
		&symbolType);

	if (_foundInImage != NULL)
		*_foundInImage = candidateImage;

	return B_OK;
}


const Elf_Sym*
find_undefined_symbol_beos(image_t*, image_t* image,
	const SymbolLookupInfo& lookupInfo, image_t** foundInImage)
{
	// BeOS style symbol resolution: It is sufficient to check the image itself
	// and its direct dependencies. The linker would have complained, if the
	// symbol wasn't there. First we check whether the requesting symbol is
	// defined already -- then we can simply return it, since, due to symbolic
	// linking, that's the one we'd find anyway.
	if (const Elf_Sym* symbol = lookupInfo.requestingSymbol) {
		if (symbol->st_shndx != SHN_UNDEF
			&& ((ELF_ST_BIND(symbol->st_info) == STB_GLOBAL)
				|| (ELF_ST_BIND(symbol->st_info) == STB_WEAK)
				|| (ELF_ST_BIND(symbol->st_info) == STB_GNU_UNIQUE))) {
			*foundInImage = image;
			return symbol;
		}
	}

	// lookup in image
	const Elf_Sym* symbol = find_symbol(image, lookupInfo);
	if (symbol != NULL) {
		*foundInImage = image;
		return symbol;
	}

	// lookup in dependencies
	for (uint32 i = 0; i < image->num_needed; i++) {
		if (image->needed[i]->dynamic_ptr) {
			symbol = find_symbol(image->needed[i], lookupInfo);
			if (symbol != NULL) {
				*foundInImage = image->needed[i];
				return symbol;
			}
		}
	}

	return NULL;
}


const Elf_Sym*
find_undefined_symbol_global(image_t* rootImage, image_t* image,
	const SymbolLookupInfo& lookupInfo, image_t** _foundInImage)
{
	// Global load order symbol resolution: All loaded images are searched for
	// the symbol in the order they have been loaded. We skip add-on images and
	// RTLD_LOCAL images though.
	image_t* candidateImage = NULL;
	const Elf_Sym* candidateSymbol = NULL;

	// If the requesting image is linked symbolically, look up the symbol there
	// first.
	bool symbolic = (image->flags & RFLAG_SYMBOLIC) != 0;
	if (symbolic) {
		candidateSymbol = find_symbol(image, lookupInfo);
		if (candidateSymbol != NULL) {
			if (ELF_ST_BIND(candidateSymbol->st_info) != STB_WEAK) {
				*_foundInImage = image;
				return candidateSymbol;
			}

			candidateImage = image;
		}
	}

	image_t* otherImage = get_loaded_images().head;
	while (otherImage != NULL) {
		if (otherImage == rootImage
				? !symbolic
				: (otherImage->type != B_ADD_ON_IMAGE
					&& (otherImage->flags
						& (RTLD_GLOBAL | RFLAG_USE_FOR_RESOLVING)) != 0)) {
			if (const Elf_Sym* symbol = find_symbol(otherImage, lookupInfo)) {
				if (ELF_ST_BIND(symbol->st_info) != STB_WEAK) {
					*_foundInImage = otherImage;
					return symbol;
				}

				if (candidateSymbol == NULL) {
					candidateSymbol = symbol;
					candidateImage = otherImage;
				}
			}
		}
		otherImage = otherImage->next;
	}

	if (candidateSymbol != NULL)
		*_foundInImage = candidateImage;

	return candidateSymbol;
}


const Elf_Sym*
find_undefined_symbol_add_on(image_t* rootImage, image_t* image,
	const SymbolLookupInfo& lookupInfo, image_t** _foundInImage)
{
	// Similar to global load order symbol resolution: All loaded images are
	// searched for the symbol in the order they have been loaded. We skip
	// add-on images and RTLD_LOCAL images though. The root image (i.e. the
	// add-on image) is skipped, too, but for the add-on itself we look up
	// a symbol that hasn't been found anywhere else in the add-on image.
	// The reason for skipping the add-on image is that we must not resolve
	// library symbol references to symbol definitions in the add-on, as
	// libraries can be shared between different add-ons and we must not
	// introduce connections between add-ons.

	// For the add-on image itself resolve non-weak symbols defined in the
	// add-on to themselves. This makes the symbol resolution order inconsistent
	// for those symbols, but avoids clashes of global symbols defined in the
	// add-on with symbols defined e.g. in the application. There's really the
	// same problem for weak symbols, but we don't have any way to discriminate
	// weak symbols that must be resolved globally from those that should be
	// resolved within the add-on.
	if (rootImage == image) {
		if (const Elf_Sym* symbol = lookupInfo.requestingSymbol) {
			if (symbol->st_shndx != SHN_UNDEF
				&& (ELF_ST_BIND(symbol->st_info) == STB_GLOBAL || ELF_ST_BIND(symbol->st_info) == STB_GNU_UNIQUE)) {
				*_foundInImage = image;
				return symbol;
			}
		}
	}

	image_t* candidateImage = NULL;
	const Elf_Sym* candidateSymbol = NULL;

	// If the requesting image is linked symbolically, look up the symbol there
	// first.
	bool symbolic = (image->flags & RFLAG_SYMBOLIC) != 0;
	if (symbolic) {
		candidateSymbol = find_symbol(image, lookupInfo);
		if (candidateSymbol != NULL) {
			if (ELF_ST_BIND(candidateSymbol->st_info) != STB_WEAK) {
				*_foundInImage = image;
				return candidateSymbol;
			}

			candidateImage = image;
		}
	}

	image_t* otherImage = get_loaded_images().head;
	while (otherImage != NULL) {
		if (otherImage != rootImage
			&& otherImage->type != B_ADD_ON_IMAGE
			&& (otherImage->flags
				& (RTLD_GLOBAL | RFLAG_USE_FOR_RESOLVING)) != 0) {
			if (const Elf_Sym* symbol = find_symbol(otherImage, lookupInfo)) {
				if (ELF_ST_BIND(symbol->st_info) != STB_WEAK) {
					*_foundInImage = otherImage;
					return symbol;
				}

				if (candidateSymbol == NULL) {
					candidateSymbol = symbol;
					candidateImage = otherImage;
				}
			}
		}
		otherImage = otherImage->next;
	}

	// If the symbol has not been found and we're trying to resolve a reference
	// in the add-on image, we also try to look it up there.
	if (!symbolic && candidateSymbol == NULL && image == rootImage) {
		candidateSymbol = find_symbol(image, lookupInfo);
		candidateImage = image;
	}

	if (candidateSymbol != NULL)
		*_foundInImage = candidateImage;

	return candidateSymbol;
}


int
resolve_symbol(image_t* rootImage, image_t* image, const Elf_Sym* sym,
	SymbolLookupCache* cache, addr_t* symAddress, image_t** symbolImage)
{
	uint32 index = sym - image->syms;

	// check the cache first
	if (cache->IsSymbolValueCached(index)) {
		*symAddress = cache->SymbolValueAt(index, symbolImage);
		return B_OK;
	}

	const Elf_Sym* sharedSym;
	image_t* sharedImage;
	const char* symName = image->SymbolName(sym);

	// get the symbol type
	int32 type = B_SYMBOL_TYPE_ANY;
	if (ELF_ST_TYPE(sym->st_info) == STT_FUNC)
		type = B_SYMBOL_TYPE_TEXT;
	else if (ELF_ST_TYPE(sym->st_info) == STT_OBJECT)
		type = B_SYMBOL_TYPE_DATA;

	if (ELF_ST_BIND(sym->st_info) == STB_LOCAL) {
		// Local symbols references are always resolved to the given symbol.
		sharedImage = image;
		sharedSym = sym;
	} else {
		// get the version info
		const elf_version_info* versionInfo = NULL;
		if (image->symbol_versions != NULL) {
			uint32 versionIndex = VER_NDX(image->symbol_versions[index]);
			if (versionIndex >= VER_NDX_GIVEN)
				versionInfo = image->versions + versionIndex;
		}

		// search the symbol
		sharedSym = rootImage->find_undefined_symbol(rootImage, image,
			SymbolLookupInfo(symName, type, versionInfo, 0, sym), &sharedImage);
	}

	enum {
		SUCCESS,
		ERROR_NO_SYMBOL,
		ERROR_WRONG_TYPE,
		ERROR_NOT_EXPORTED,
		ERROR_UNPATCHED
	};
	uint32 lookupError = ERROR_UNPATCHED;

	bool tlsSymbol = ELF_ST_TYPE(sym->st_info) == STT_TLS;
	void* location = NULL;
	if (sharedSym == NULL) {
		// symbol not found at all
		lookupError = ERROR_NO_SYMBOL;
		sharedImage = NULL;
	} else if (ELF_ST_TYPE(sym->st_info) != STT_NOTYPE
		&& ELF_ST_TYPE(sym->st_info) != ELF_ST_TYPE(sharedSym->st_info)) {
		// symbol not of the requested type
		lookupError = ERROR_WRONG_TYPE;
		sharedImage = NULL;
	} else if (ELF_ST_BIND(sharedSym->st_info) != STB_GLOBAL
		&& ELF_ST_BIND(sharedSym->st_info) != STB_WEAK
		&& ELF_ST_BIND(sharedSym->st_info) != STB_GNU_UNIQUE) {
		// symbol not exported
		lookupError = ERROR_NOT_EXPORTED;
		sharedImage = NULL;
	} else {
		// symbol is fine, get its location
		location = (void*)sharedSym->st_value;
		if (!tlsSymbol) {
			location
				= (void*)((addr_t)location + sharedImage->regions[0].delta);
		} else
			lookupError = SUCCESS;
	}

	if (!tlsSymbol) {
		patch_undefined_symbol(rootImage, image, symName, &sharedImage,
			&location, &type);
	}

	if (location == NULL && lookupError != SUCCESS) {
		if(ELF_ST_BIND(sym->st_info) == STB_WEAK) {
			if (symbolImage)
				*symbolImage = rootImage;
			*symAddress = 0;
			return B_OK;
		}
	}

	if (location == NULL && lookupError != SUCCESS) {
		switch (lookupError) {
			case ERROR_NO_SYMBOL:
				FATAL("%s: Could not resolve symbol '%s'\n",
					image->path, symName);
				break;
			case ERROR_WRONG_TYPE:
				FATAL("%s: Found symbol '%s' in shared image but wrong "
					"type\n", image->path, symName);
				break;
			case ERROR_NOT_EXPORTED:
				FATAL("%s: Found symbol '%s', but not exported\n",
					image->path, symName);
				break;
			case ERROR_UNPATCHED:
				FATAL("%s: Found symbol '%s', but was hidden by symbol "
					"patchers\n", image->path, symName);
				break;
		}

		if (report_errors())
			gErrorMessage.AddString("missing symbol", symName);

		return B_MISSING_SYMBOL;
	}

	cache->SetSymbolValueAt(index, (addr_t)location, sharedImage);

	if (symbolImage)
		*symbolImage = sharedImage;
	*symAddress = (addr_t)location;
	return B_OK;
}
