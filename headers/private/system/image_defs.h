/*
 * Copyright 2009-2016, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SYSTEM_IMAGE_DEFS_H
#define _SYSTEM_IMAGE_DEFS_H


#include <SupportDefs.h>
#include <image.h>
#include <sys/elf.h>


#define B_SHARED_OBJECT_HAIKU_VERSION_VARIABLE		_gSharedObjectHaikuVersion
#define B_SHARED_OBJECT_HAIKU_VERSION_VARIABLE_NAME	"_gSharedObjectHaikuVersion"

#define B_SHARED_OBJECT_HAIKU_ABI_VARIABLE			_gSharedObjectHaikuABI
#define B_SHARED_OBJECT_HAIKU_ABI_VARIABLE_NAME		"_gSharedObjectHaikuABI"


typedef struct extended_image_info {
	image_info	basic_info;
	ssize_t		text_delta;
	void*		symbol_table;
	void*		string_table;

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
} extended_image_info;


#endif	/* _SYSTEM_IMAGE_DEFS_H */
