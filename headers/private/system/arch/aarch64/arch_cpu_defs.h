/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef _SYSTEM_ARCH_AARCH64_ARCH_CPU_DEFS_H_
#define _SYSTEM_ARCH_AARCH64_ARCH_CPU_DEFS_H_

#define SPINLOCK_PAUSE()	__asm__ __volatile__("yield")

#endif /* _SYSTEM_ARCH_AARCH64_ARCH_CPU_DEFS_H_ */
