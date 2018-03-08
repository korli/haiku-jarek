/*
 * Copyright 2002-2016 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ELF_H
#define _ELF_H


#include <SupportDefs.h>
#include <ByteOrder.h>
#include <sys/elf.h>

/* values for note name */
#define ELF_NOTE_CORE		"CORE"
#define ELF_NOTE_HAIKU		"Haiku"

/* values for note type (n_type) */
/* ELF_NOTE_CORE/... */
#define NT_FILE				0x46494c45 /* mapped files */

/* ELF_NOTE_HAIKU/... */
#define NT_TEAM				0x7465616d 	/* team */
#define NT_AREAS			0x61726561 	/* areas */
#define NT_IMAGES			0x696d6167 	/* images */
#define NT_THREADS			0x74687264 	/* threads */
#define NT_SYMBOLS			0x73796d73 	/* symbols */

/* NT_TEAM: uint32 entrySize; Elf32_Note_Team; char[] args */
typedef struct {
	int32		nt_id;				/* team ID */
	int32		nt_uid;				/* team owner ID */
	int32		nt_gid;				/* team group ID */
} Elf32_Note_Team;

typedef Elf32_Note_Team Elf64_Note_Team;

/* NT_AREAS:
 * uint32 count;
 * uint32 entrySize;
 * Elf32_Note_Area_Entry[count];
 * char[] names
 */
typedef struct {
	int32		na_id;				/* area ID */
	uint32		na_lock;			/* lock type (B_NO_LOCK, ...) */
	uint32		na_protection;		/* protection (B_READ_AREA | ...) */
	uint32		na_base;			/* area base address */
	uint32		na_size;			/* area size */
	uint32		na_ram_size;		/* physical memory used */
} Elf32_Note_Area_Entry;

/* NT_AREAS:
 * uint32 count;
 * uint32 entrySize;
 * Elf64_Note_Area_Entry[count];
 * char[] names
 */
typedef struct {
	int32		na_id;				/* area ID */
	uint32		na_lock;			/* lock type (B_NO_LOCK, ...) */
	uint32		na_protection;		/* protection (B_READ_AREA | ...) */
	uint32		na_pad1;
	uint64		na_base;			/* area base address */
	uint64		na_size;			/* area size */
	uint64		na_ram_size;		/* physical memory used */
} Elf64_Note_Area_Entry;

/* NT_IMAGES:
 * uint32 count;
 * uint32 entrySize;
 * Elf32_Note_Image_Entry[count];
 * char[] names
 */
typedef struct {
	int32		ni_id;				/* image ID */
	int32		ni_type;			/* image type (B_APP_IMAGE, ...) */
	uint32		ni_init_routine;	/* address of init function */
	uint32		ni_term_routine;	/* address of termination function */
	int32		ni_device;			/* device ID of mapped file */
	int64		ni_node;			/* node ID of mapped file */
	uint32		ni_text_base;		/* base address of text segment */
	uint32		ni_text_size;		/* size of text segment */
	int32		ni_text_delta;		/* delta of the text segment relative to
									   load address specified in the ELF file */
	uint32		ni_data_base;		/* base address of data segment */
	uint32		ni_data_size;		/* size of data segment */
	uint32		ni_symbol_table;	/* address of dynamic symbol table */
	uint32		ni_symbol_hash;		/* address of dynamic symbol hash */
	uint32		ni_string_table;	/* address of dynamic string table */
} Elf32_Note_Image_Entry;

/* NT_IMAGES:
 * uint32 count;
 * uint32 entrySize;
 * Elf64_Note_Image_Entry[count];
 * char[] names
 */
typedef struct {
	int32		ni_id;				/* image ID */
	int32		ni_type;			/* image type (B_APP_IMAGE, ...) */
	uint64		ni_init_routine;	/* address of init function */
	uint64		ni_term_routine;	/* address of termination function */
	uint32		ni_pad1;
	int32		ni_device;			/* device ID of mapped file */
	int64		ni_node;			/* node ID of mapped file */
	uint64		ni_text_base;		/* base address of text segment */
	uint64		ni_text_size;		/* size of text segment */
	int64		ni_text_delta;		/* delta of the text segment relative to
									   load address specified in the ELF file */
	uint64		ni_data_base;		/* base address of data segment */
	uint64		ni_data_size;		/* size of data segment */
	uint64		ni_symbol_table;	/* address of dynamic symbol table */
	uint64		ni_symbol_hash;		/* address of dynamic symbol hash */
	uint64		ni_string_table;	/* address of dynamic string table */
} Elf64_Note_Image_Entry;

/* NT_THREADS:
 * uint32 count;
 * uint32 entrySize;
 * uint32 cpuStateSize;
 * {Elf32_Note_Thread_Entry, uint8[cpuStateSize] cpuState}[count];
 * char[] names
 */
typedef struct {
	int32		nth_id;				/* thread ID */
	int32		nth_state;			/* thread state (B_THREAD_RUNNING, ...) */
	int32		nth_priority;		/* thread priority */
	uint32		nth_stack_base;		/* thread stack base address */
	uint32		nth_stack_end;		/* thread stack end address */
} Elf32_Note_Thread_Entry;

/* NT_THREADS:
 * uint32 count;
 * uint32 entrySize;
 * uint32 cpuStateSize;
 * {Elf64_Note_Thread_Entry, uint8[cpuStateSize] cpuState}[count];
 * char[] names
 */
typedef struct {
	int32		nth_id;				/* thread ID */
	int32		nth_state;			/* thread state (B_THREAD_RUNNING, ...) */
	int32		nth_priority;		/* thread priority */
	uint32		nth_pad1;
	uint64		nth_stack_base;		/* thread stack base address */
	uint64		nth_stack_end;		/* thread stack end address */
} Elf64_Note_Thread_Entry;

/* NT_SYMBOLS:
 * int32 imageId;
 * uint32 symbolCount;
 * uint32 entrySize;
 * Elf{32,64}_Sym[count];
 * char[] strings
 */


/*** inline functions ***/

#if 0

inline bool
Elf32_Ehdr::IsHostEndian() const
{
#if B_HOST_IS_LENDIAN
	return e_ident[EI_DATA] == ELFDATA2LSB;
#elif B_HOST_IS_BENDIAN
	return e_ident[EI_DATA] == ELFDATA2MSB;
#endif
}


inline bool
Elf64_Ehdr::IsHostEndian() const
{
#if B_HOST_IS_LENDIAN
	return e_ident[EI_DATA] == ELFDATA2LSB;
#elif B_HOST_IS_BENDIAN
	return e_ident[EI_DATA] == ELFDATA2MSB;
#endif
}


inline bool
Elf32_Phdr::IsReadWrite() const
{
	return !(~p_flags & (PF_READ | PF_WRITE));
}


inline bool
Elf32_Phdr::IsExecutable() const
{
	return (p_flags & PF_EXECUTE) != 0;
}


inline bool
Elf64_Phdr::IsReadWrite() const
{
	return !(~p_flags & (PF_READ | PF_WRITE));
}


inline bool
Elf64_Phdr::IsExecutable() const
{
	return (p_flags & PF_EXECUTE) != 0;
}


inline uint8
Elf32_Sym::Bind() const
{
	return ELF32_ST_BIND(st_info);
}


inline uint8
Elf32_Sym::Type() const
{
	return ELF32_ST_TYPE(st_info);
}


inline void
Elf32_Sym::SetInfo(uint8 bind, uint8 type)
{
	st_info = ELF32_ST_INFO(bind, type);
}


inline uint8
Elf64_Sym::Bind() const
{
	return ELF64_ST_BIND(st_info);
}


inline uint8
Elf64_Sym::Type() const
{
	return ELF64_ST_TYPE(st_info);
}


inline void
Elf64_Sym::SetInfo(uint8 bind, uint8 type)
{
	st_info = ELF64_ST_INFO(bind, type);
}


inline uint8
Elf32_Rel::SymbolIndex() const
{
	return ELF32_R_SYM(r_info);
}


inline uint8
Elf32_Rel::Type() const
{
	return ELF32_R_TYPE(r_info);
}


inline uint8
Elf64_Rel::SymbolIndex() const
{
	return ELF64_R_SYM(r_info);
}


inline uint8
Elf64_Rel::Type() const
{
	return ELF64_R_TYPE(r_info);
}

#endif	/* __cplusplus */


#endif	/* _ELF_H */
