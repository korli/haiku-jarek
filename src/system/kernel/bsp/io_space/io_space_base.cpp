/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include "io_space.h"

namespace BoardSupportPackage {

IOSpace::~IOSpace() noexcept {
}

#if defined(__HAIKU_ARCH_X86_64) || defined(__HAIKU_ARCH_X86)
MemoryIOSpace::~MemoryIOSpace() noexcept {
}

X86PortIOSpace::~X86PortIOSpace() noexcept {
}
#elif defined(__HAIKU_ARCH_AARCH64) || defined(__HAIKU_ARCH_ARM)
MemoryIOSpace::~MemoryIOSpace() noexcept {
}
#endif

}  // namespace BoardSupportPackage
