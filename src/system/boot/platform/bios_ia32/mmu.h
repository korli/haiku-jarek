/*
 * Copyright 2004-2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef MMU_H
#define MMU_H


#include <arch/x86/descriptors.h>


#ifndef _ASSEMBLER


#include <SupportDefs.h>

#ifdef __cplusplus
#include <kernel/boot/memory.h>
#endif

extern segment_descriptor gBootGDT[BOOT_GDT_SEGMENT_COUNT];


// For use with mmu_map_physical_memory()
static const uint32 kDefaultPageFlags = 0x3;	// present, R/W

#ifdef __cplusplus
extern "C" {
#endif

extern void mmu_init(void);
extern void mmu_init_for_kernel(void);

#ifdef __cplusplus
}
#endif

#endif	// !_ASSEMBLER

#endif	/* MMU_H */
