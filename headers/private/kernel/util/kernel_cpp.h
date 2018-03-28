#ifndef KERNEL_CPP_H
#define KERNEL_CPP_H
/* cpp - C++ in the kernel
**
** Initial version by Axel Dörfler, axeld@pinc-software.de
** This file may be used under the terms of the MIT License.
*/

#ifdef __cplusplus

#include <new>
#include <stdlib.h>

#if _KERNEL_MODE || _LOADER_MODE

using namespace std;

#endif	// #if _KERNEL_MODE

#endif	// __cplusplus

#endif	/* KERNEL_CPP_H */
