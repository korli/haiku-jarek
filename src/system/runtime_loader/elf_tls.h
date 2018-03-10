/*
 * Copyright 2014, Paweł Dziepak, pdziepak@quarnos.org.
 * Copyright 2018, Jaroslaw Pelczar, jarek@jpelczar.com
 * Distributed under the terms of the MIT License.
 */
#ifndef ELF_TLS_H
#define ELF_TLS_H


#include "runtime_loader_private.h"
#include "utility.h"

class TLSState
{
public:
	/*
	 * Globals to control TLS allocation.
	 */
	static size_t tls_last_offset;		/* Static TLS offset of last module */
	static size_t tls_last_size;		/* Static TLS size of last module */
	static size_t tls_static_space;	/* Static TLS space allocated */
	static size_t tls_static_max_align;
	static int tls_dtv_generation;		/* Used to detect when dtv size changes  */
	static int tls_max_index;			/* Largest module index allocated */
};

void * tls_get_addr_common(Elf_Addr **dtvp, int index, size_t offset);
void * allocate_tls(void *oldtcb, size_t tcbsize, size_t tcbalign);
void free_tls(void *tcb, size_t tcbsize, size_t tcbalign);

void * allocate_module_tls(int index);
bool allocate_tls_offset(image_t *image);
void free_tls_offset(image_t *image);

void allocate_initial_tls();

#endif	// ELF_TLS_H
