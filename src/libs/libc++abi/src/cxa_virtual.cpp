//===-------------------------- cxa_virtual.cpp ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "cxxabi.h"
#include "abort_message.h"
#if defined(_KERNEL_MODE)
#include <kernel/debug.h>
#endif

namespace __cxxabiv1 {
extern "C" {
_LIBCXXABI_FUNC_VIS _LIBCXXABI_NORETURN
void __cxa_pure_virtual(void) {
#if defined(_KERNEL_MODE)
  for(;;) panic("Pure virtual function called!");
#else
  abort_message("Pure virtual function called!");
#endif
}

_LIBCXXABI_FUNC_VIS _LIBCXXABI_NORETURN
void __cxa_deleted_virtual(void) {
#if defined(_KERNEL_MODE)
  for(;;) panic("Deleted virtual function called!");
#else
  abort_message("Deleted virtual function called!");
#endif
}
} // extern "C"
} // abi
