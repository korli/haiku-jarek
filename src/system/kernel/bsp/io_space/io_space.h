/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef SYSTEM_KERNEL_BSP_IO_SPACE_IO_SPACE_H_
#define SYSTEM_KERNEL_BSP_IO_SPACE_IO_SPACE_H_

#include "io_space_base.h"

#if defined(__HAIKU_ARCH_X86_64) || defined(__HAIKU_ARCH_X86)
#include "io_space_x86.h"
#elif defined(__HAIKU_ARCH_AARCH64)
#include "io_space_aarch64.h"
#elif defined(__HAIKU_ARCH_ARM)
#include "io_space_arm.h"
#else
namespace BoardSupportPackage {
typedef IOSpaceGeneric MemoryIOSpace;
}  // namespace BoardSupportPackage
#endif

#endif /* SYSTEM_KERNEL_BSP_IO_SPACE_IO_SPACE_H_ */
