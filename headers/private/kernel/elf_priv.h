/*
 * Copyright 2002-2007, Haiku Inc. All Rights Reserved.
 * Distributed under the terms of the MIT license.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */
#ifndef _KERNEL_ELF_PRIV_H
#define _KERNEL_ELF_PRIV_H


#include <elf_private.h>

#include <image.h>
#include <sys/link_elf.h>


struct elf_version_info;


typedef struct elf_region {
	area_id		id;
	addr_t		start;
	addr_t		size;
	long		delta;
	uint32		protection;
} elf_region;

#define ELF_IMAGE_MAX_REGIONS		(8)

struct elf_image_info {
	struct elf_image_info* next;		// next image in the hash
	char*			name;
	image_id		id;
	int32			ref_count;
	struct vnode*	vnode;

	elf_region		regions[ELF_IMAGE_MAX_REGIONS];
	uint32			num_regions;

	addr_t			dynamic_section;	// pointer to the dynamic section
	struct elf_linked_image* linked_images;

	bool			symbolic;
	bool			textrel;

	Elf_Ehdr*		elf_header;

	// pointer to symbol participation data structures
	const char*		needed;
	Elf_Sym*		syms;
	const char*		strtab;
	Elf_Rel*		rel;
	int				rel_len;
	Elf_Rela*		rela;
	int				rela_len;
	Elf_Rel*		pltrel;
	int				pltrel_len;
	int				pltrel_type;

    const Elf_Hashelt *buckets;		/* Hash table buckets array */
    unsigned long nbuckets;			/* Number of buckets */
    const Elf_Hashelt *chains;		/* Hash table chain array */
    unsigned long nchains;			/* Number of entries in chain array */

    Elf32_Word nbuckets_gnu;		/* Number of GNU hash buckets*/
    Elf32_Word symndx_gnu;			/* 1st accessible symbol on dynsym table */
    Elf32_Word maskwords_bm_gnu;  	/* Bloom filter words - 1 (bitmask) */
    Elf32_Word shift2_gnu;			/* Bloom filter shift count */
    Elf32_Word dynsymcount;			/* Total entries in dynsym table */
    Elf_Addr *bloom_gnu;			/* Bloom filter used by GNU hash func */
    const Elf_Hashelt *buckets_gnu;	/* GNU hash table bucket array */
    const Elf_Hashelt *chain_zero_gnu;	/* GNU hash table value array (Zeroed) */

    bool valid_hash_sysv;
    bool valid_hash_gnu;

	Elf_Sym*		debug_symbols;
	uint32			num_debug_symbols;
	const char*		debug_string_table;

	// versioning related structures
	uint32			num_version_definitions;
	Elf_Verdef*		version_definitions;
	uint32			num_needed_versions;
	Elf_Verneed*	needed_versions;
	elf_versym*		symbol_versions;
	struct elf_version_info* versions;
	uint32			num_versions;

#if defined(__ARM__)
	addr_t			arm_exidx_base;
	int				arm_exidx_count;
#endif

	Elf_Addr 		relro_page;
	size_t			relro_size;

    struct link_map linkmap;	/* For GDB and dlinfo() */

	inline const char * String(size_t offset) const {
		return strtab + offset;
	}

	inline const char * SymbolName(const Elf_Sym * sym) const {
		return strtab + sym->st_name;
	}

	inline const Elf_Sym* Symbol(size_t index) const {
		return &syms[index];
	}
};

#ifdef __cplusplus
extern "C" {
#endif

extern status_t elf_resolve_symbol(struct elf_image_info* image,
	const Elf_Sym* symbol, struct elf_image_info* sharedImage,
	addr_t* _symbolAddress);

#ifdef __cplusplus
}
#endif


#endif	/* _KERNEL_ELF_PRIV_H */
