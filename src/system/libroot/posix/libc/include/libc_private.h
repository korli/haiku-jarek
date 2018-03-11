/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1998 John Birrell <jb@cimlogic.com.au>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 * Private definitions for libc, libc_r and libpthread.
 *
 */

#ifndef _LIBC_PRIVATE_H_
#define _LIBC_PRIVATE_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <pthread.h>

#include <libroot_private.h>
#include <shared/locks.h>

/*
 * This global flag is non-zero when a process has created one
 * or more threads. It is used to avoid calling locking functions
 * when they are not required.
 */
#ifndef __LIBC_ISTHREADED_DECLARED
#define __LIBC_ISTHREADED_DECLARED
extern int	__isthreaded;
#endif

/*
 * Elf_Auxinfo *__elf_aux_vector, the pointer to the ELF aux vector
 * provided by kernel. Either set for us by rtld, or found at runtime
 * on stack for static binaries.
 *
 * Type is void to avoid polluting whole libc with ELF types.
 */
extern void	*__elf_aux_vector;

/*
 * File lock contention is difficult to diagnose without knowing
 * where locks were set. Allow a debug library to be built which
 * records the source file and line number of each lock call.
 */
#ifdef	_FLOCK_DEBUG
#define _FLOCKFILE(x)	_flockfile_debug(x, __FILE__, __LINE__)
#else
#define _FLOCKFILE(x)	_flockfile(x)
#endif

/*
 * Macros for locking and unlocking FILEs. These test if the
 * process is threaded to avoid locking when not required.
 */
#define	FLOCKFILE(fp)		if (__isthreaded) _FLOCKFILE(fp)
#define	FUNLOCKFILE(fp)		if (__isthreaded) _funlockfile(fp)

extern struct mutex __stdio_thread_lock __hidden;
#define STDIO_THREAD_LOCK()				\
do {							\
	if (__isthreaded)				\
		mutex_lock(&__stdio_thread_lock);	\
} while (0)
#define STDIO_THREAD_UNLOCK()				\
do {							\
	if (__isthreaded)				\
		mutex_unlock(&__stdio_thread_lock);	\
} while (0)

/*
 * This function is used by the threading libraries to notify libc that a
 * thread is exiting, so its thread-local dtors should be called.
 */
void __cxa_thread_call_dtors(void);
int __cxa_thread_atexit_hidden(void (*dtor_func)(void *), void *obj,
    void *dso_symbol) __hidden;

/*
 * Function to clean up streams, called from abort() and exit().
 */
extern void (*__cleanup)(void) __hidden;

int _elf_aux_info(int aux, void *buf, int buflen);
struct dl_phdr_info;
int __elf_phdr_match_addr(struct dl_phdr_info *, void *);
void __init_elf_aux_vector(void);

#endif /* _LIBC_PRIVATE_H_ */
