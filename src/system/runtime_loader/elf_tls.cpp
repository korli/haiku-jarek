/*
 * Copyright 2014, Paweł Dziepak, pdziepak@quarnos.org.
 * Distributed under the terms of the MIT License.
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 1996, 1997, 1998, 1999, 2000 John D. Polstra.
 * Copyright 2003 Alexander Kabaev <kan@FreeBSD.ORG>.
 * Copyright 2009-2013 Konstantin Belousov <kib@FreeBSD.ORG>.
 * Copyright 2012 John Marino <draco@marino.st>.
 * Copyright 2014-2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "elf_tls.h"
#include "images.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <support/TLS.h>

#include <tls.h>

#include <util/kernel_cpp.h>


size_t TLSState::tls_last_offset;		/* Static TLS offset of last module */
size_t TLSState::tls_last_size;		/* Static TLS size of last module */
size_t TLSState::tls_static_space;	/* Static TLS space allocated */
size_t TLSState::tls_static_max_align;
int TLSState::tls_dtv_generation;		/* Used to detect when dtv size changes  */
int TLSState::tls_max_index;			/* Largest module index allocated */

/*
 * Common code for MD __tls_get_addr().
 */
static void *tls_get_addr_slow(Elf_Addr **, int, size_t) __noinline;
static void * tls_get_addr_slow(Elf_Addr **dtvp, int index, size_t offset)
{
    Elf_Addr *newdtv, *dtv;
    int to_copy;

    dtv = *dtvp;
    /* Check dtv generation in case new modules have arrived */
    if (dtv[0] != TLSState::tls_dtv_generation) {
    	size_t block_size = (TLSState::tls_max_index + 2) * sizeof(Elf_Addr);
    	rld_lock();
    	newdtv = (Elf_Addr *)malloc(block_size);
    	// We can't really do much if this fails
    	assert(newdtv);
    	memset(newdtv, 0, sizeof(block_size));
    	to_copy = dtv[1];
    	if (to_copy > TLSState::tls_max_index)
    		to_copy = TLSState::tls_max_index;
    	memcpy(&newdtv[2], &dtv[2], to_copy * sizeof(Elf_Addr));
    	newdtv[0] = TLSState::tls_dtv_generation;
    	newdtv[1] = TLSState::tls_max_index;
    	free(dtv);
    	rld_unlock();
    	dtv = *dtvp = newdtv;
    }

    /* Dynamically allocate module TLS if necessary */
    if (dtv[index + 1] == 0) {
    	/* Signal safe, wlock will block out signals. */
    	rld_lock();
    	if (!dtv[index + 1])
    		dtv[index + 1] = (Elf_Addr)allocate_module_tls(index);
    	rld_unlock();
    }
    return ((void *)(dtv[index + 1] + offset));
}

void * tls_get_addr_common(Elf_Addr **dtvp, int index, size_t offset)
{
	Elf_Addr *dtv;

	dtv = *dtvp;
	/* Check dtv generation in case new modules have arrived */
	if (dtv[0] == TLSState::tls_dtv_generation && dtv[index + 1] != 0)
		return ((void *)(dtv[index + 1] + offset));
	return (tls_get_addr_slow(dtvp, index, offset));
}

#if defined(__HAIKU_ARCH_AARCH64) || defined(__HAIKU_ARCH_ARM) || defined(__HAIKU_ARCH_MIPSEL) || \
    defined(__HAIKU_ARCH_PPC) || defined(__HAIKU_ARCH_RISCV)

struct allocate_tls_arg {
	Elf_Addr ** tls;
	Elf_Addr * dtv;
};

static void allocate_tls_for_image(image_t * image, void * _arg) {
	allocate_tls_arg * arg = reinterpret_cast<allocate_tls_arg *>(_arg);
	if (image->tlsoffset > 0) {
		Elf_Addr addr = (Elf_Addr) arg->tls + image->tlsoffset;
		if (image->tlsinitsize > 0)
			memcpy((void*) addr, image->tlsinit, image->tlsinitsize);
		if (image->tlssize > image->tlsinitsize)
			memset((void*) (addr + image->tlsinitsize), 0,
					image->tlssize - image->tlsinitsize);
		arg->dtv[image->tlsindex + 1] = addr;
	}
}

/*
 * Allocate Static TLS using the Variant I method.
 */
void * allocate_tls(void *oldtcb, size_t tcbsize, size_t tcbalign) {
	char *tcb;
	Elf_Addr **tls;
	Elf_Addr *dtv;
	int i;
	allocate_tls_arg arg;

	if (oldtcb != NULL && tcbsize == TLS_TCB_SIZE)
		return (oldtcb);

	assert(tcbsize >= TLS_TCB_SIZE);
	tcb = (char *)malloc(TLSState::tls_static_space - TLS_TCB_SIZE + tcbsize);
	assert(tcb);
	memset(tcb, 0, TLSState::tls_static_space - TLS_TCB_SIZE + tcbsize);
	tls = (Elf_Addr **) (tcb + tcbsize - TLS_TCB_SIZE);

	if (oldtcb != NULL) {
		memcpy(tls, oldtcb, TLSState::tls_static_space);
		free(oldtcb);

		/* Adjust the DTV. */
		dtv = tls[0];
		for (i = 0; i < dtv[1]; i++) {
			if (dtv[i + 2] >= (Elf_Addr) oldtcb
					&& dtv[i + 2] < (Elf_Addr) oldtcb + TLSState::tls_static_space) {
				dtv[i + 2] = dtv[i + 2] - (Elf_Addr) oldtcb + (Elf_Addr) tls;
			}
		}
	} else {
		dtv = (Elf_Addr *)malloc((TLSState::tls_max_index + 2) * sizeof(Elf_Addr));
		assert(dtv);
		memset(dtv, 0, (TLSState::tls_max_index + 2) * sizeof(Elf_Addr));
		tls[0] = dtv;
		dtv[0] = TLSState::tls_dtv_generation;
		dtv[1] = TLSState::tls_max_index;

		arg.dtv = dtv;
		arg.tls = tls;

		for_each_image(allocate_tls_for_image, &arg);
	}

	return (tcb);
}

void free_tls(void *tcb, size_t tcbsize, size_t tcbalign) {
	Elf_Addr *dtv;
	Elf_Addr tlsstart, tlsend;
	int dtvsize, i;

	assert(tcbsize >= TLS_TCB_SIZE);

	tlsstart = (Elf_Addr) tcb + tcbsize - TLS_TCB_SIZE;
	tlsend = tlsstart + TLSState::tls_static_space;

	dtv = *(Elf_Addr **) tlsstart;
	dtvsize = dtv[1];
	for (i = 0; i < dtvsize; i++) {
		if (dtv[i + 2] && (dtv[i + 2] < tlsstart || dtv[i + 2] >= tlsend)) {
			free((void*) dtv[i + 2]);
		}
	}
	free(dtv);
	free(tcb);
}

#endif


#if defined(__HAIKU_ARCH_X86) || defined(__HAIKU_ARCH_X86_64) || defined(__HAIKU_ARCH_SPARC64)

struct allocate_tls_arg {
	Elf_Addr segbase;
	ELf_Addr * dtv;
};

static void allocate_tls_for_image(image_t * image, void * _arg) {
	allocate_tls_arg * arg = reinterpret_cast<allocate_tls_arg *>(_arg);
	Elf_Addr addr;

	if (image->tlsoffset > 0) {
		addr = arg->segbase - image->tlsoffset;
		memset((void*) (addr + image->tlsinitsize), 0,
				image->tlssize - image->tlsinitsize);
		if (image->tlsinit)
			memcpy((void*) addr, image->tlsinit, image->tlsinitsize);
		arg->dtv[image->tlsindex + 1] = addr;
	}
}

/*
 * Allocate Static TLS using the Variant II method.
 */
void * allocate_tls(void *oldtls, size_t tcbsize, size_t tcbalign)
{
    size_t size, ralign;
    char *tls;
    Elf_Addr *dtv, *olddtv;
    Elf_Addr segbase, oldsegbase;
    int i;
    struct allocate_tls_arg arg;

    ralign = tcbalign;
    if (TLSState::tls_static_max_align > ralign)
	    ralign = TLSState::tls_static_max_align;
    size = round(TLSState::tls_static_space, ralign) + round(tcbsize, ralign);

    assert(tcbsize >= 2*sizeof(Elf_Addr));
    tls = (char *)malloc_aligned(size, ralign);
    assert(tls);
    dtv = (Elf_addr *)malloc((TLSState::tls_max_index + 2) * sizeof(Elf_Addr));
    assert(dtv);
    memset(dtv, 0, (TLSState::tls_max_index + 2) * sizeof(Elf_Addr));

    segbase = (Elf_Addr)(tls + round(TLSState::tls_static_space, ralign));
    ((Elf_Addr*)segbase)[0] = segbase;
    ((Elf_Addr*)segbase)[1] = (Elf_Addr) dtv;

    dtv[0] = TLSState::tls_dtv_generation;
    dtv[1] = TLSState::tls_max_index;

	if (oldtls) {
		/*
		 * Copy the static TLS block over whole.
		 */
		oldsegbase = (Elf_Addr) oldtls;
		memcpy((void *) (segbase - TLSState::tls_static_space),
				(const void *) (oldsegbase - TLSState::tls_static_space),
				TLSState::tls_static_space);

		/*
		 * If any dynamic TLS blocks have been created tls_get_addr(),
		 * move them over.
		 */
		olddtv = ((Elf_Addr**) oldsegbase)[1];
		for (i = 0; i < olddtv[1]; i++) {
			if (olddtv[i + 2] < oldsegbase - size
					|| olddtv[i + 2] > oldsegbase) {
				dtv[i + 2] = olddtv[i + 2];
				olddtv[i + 2] = 0;
			}
		}

		/*
		 * We assume that this block was the one we created with
		 * allocate_initial_tls().
		 */
		free_tls(oldtls, 2 * sizeof(Elf_Addr), sizeof(Elf_Addr));
	} else {
		arg.dtv = dtv;
		arg.segbase = segbase;
		for_each_image(allocate_tls_for_image, &arg);
	}

	return (void*) segbase;
}

void free_tls(void *tls, size_t tcbsize, size_t tcbalign)
{
	Elf_Addr* dtv;
	size_t size, ralign;
	int dtvsize, i;
	Elf_Addr tlsstart, tlsend;

	/*
	 * Figure out the size of the initial TLS block so that we can
	 * find stuff which ___tls_get_addr() allocated dynamically.
	 */
	ralign = tcbalign;
	if (TLSState::tls_static_max_align > ralign)
		ralign = TLSState::tls_static_max_align;
	size = round(TLSState::tls_static_space, ralign);

	dtv = ((Elf_Addr**) tls)[1];
	dtvsize = dtv[1];
	tlsend = (Elf_Addr) tls;
	tlsstart = tlsend - size;
	for (i = 0; i < dtvsize; i++) {
		if (dtv[i + 2] != 0 && (dtv[i + 2] < tlsstart || dtv[i + 2] > tlsend)) {
			free_aligned((void *) dtv[i + 2]);
		}
	}

	free_aligned((void *) tlsstart);
	free((void*) dtv);
}
#endif

void * allocate_module_tls(int index)
{
	char * p;
	image_t * image = find_loaded_image_by_tls_index(index);
	assert(image != NULL);

    p = (char *)malloc_aligned(image->tlssize, image->tlsalign);
    memcpy(p, image->tlsinit, image->tlsinitsize);
    memset(p + image->tlsinitsize, 0, image->tlssize - image->tlsinitsize);

    return p;
}

bool allocate_tls_offset(image_t *image)
{
    size_t off;

    if (image->tls_done)
    	return true;

	if (image->tlssize == 0) {
		image->tls_done = true;
		return true;
	}

	if (TLSState::tls_last_offset == 0) {
		off = calculate_first_tls_offset(image->tlssize, image->tlsalign);
	} else {
		off = calculate_tls_offset(TLSState::tls_last_offset, tls_last_size, image->tlssize,
				image->tlsalign);
	}

    /*
     * If we have already fixed the size of the static TLS block, we
     * must stay within that size. When allocating the static TLS, we
     * leave a small amount of space spare to be used for dynamically
     * loading modules which use static TLS.
     */
	if (TLSState::tls_static_space != 0) {
		if (calculate_tls_end(off, obj->tlssize) > TLSState::tls_static_space)
			return false;
	} else if (image->tlsalign > TLSState::tls_static_max_align) {
		TLSState::tls_static_max_align = image->tlsalign;
	}

	TLSState::tls_last_offset = image->tlsoffset = off;
	TLSState::tls_last_size = image->tlssize;
	image->tls_done = true;

	return true;
}

void free_tls_offset(image_t *image)
{

    /*
     * If we were the last thing to allocate out of the static TLS
     * block, we give our space back to the 'allocator'. This is a
     * simplistic workaround to allow libGL.so.1 to be loaded and
     * unloaded multiple times.
     */
	if (calculate_tls_end(image->tlsoffset,
			image->tlssize) == calculate_tls_end(TLSState::tls_last_offset, tls_last_size))
	{
		TLSState::tls_last_offset -= image->tlssize;
		TLSState::tls_last_size = 0;
	}
}

void * _rtld_allocate_tls(void *oldtls, size_t tcbsize, size_t tcbalign)
{
    void *ret;

    rld_lock();
    ret = allocate_tls(oldtls,tcbsize, tcbalign);
    rld_unlock();

    return (ret);
}

void _rtld_free_tls(void *tcb, size_t tcbsize, size_t tcbalign)
{
	rld_lock();
    free_tls(tcb, tcbsize, tcbalign);
    rld_unlock();
}
