/*
 * Copyright 2014, Paweł Dziepak, pdziepak@quarnos.org.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_ARCH_X86_64_ATOMIC_H
#define _KERNEL_ARCH_X86_64_ATOMIC_H


static inline void
memory_read_barrier_inline(void)
{
	__asm__ __volatile__("lfence" : : : "memory");
}


static inline void
memory_write_barrier_inline(void)
{
	__asm__ __volatile__("sfence" : : : "memory");
}


static inline void
memory_full_barrier_inline(void)
{
	__asm__ __volatile__("mfence" : : : "memory");
}


#define memory_read_barrier		memory_read_barrier_inline
#define memory_write_barrier	memory_write_barrier_inline
#define memory_full_barrier		memory_full_barrier_inline

#endif	// _KERNEL_ARCH_X86_64_ATOMIC_H

