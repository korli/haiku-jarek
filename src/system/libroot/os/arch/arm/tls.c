/* 
** Copyright 2003, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the MIT License.
*/

// ToDo: this is a dummy implementation - I've not yet gained enough knowledge
//	to decide how this should be done, so it's just broken now (okay for single
//	threaded apps, though).

// we don't want to have the inline assembly included here
#ifndef _NO_INLINE_ASM
#	define _NO_INLINE_ASM 1
#endif

#include <runtime_loader/runtime_loader.h>

extern "C" void*
__tls_get_addr(void* ti)
{
	return __gRuntimeLoader->get_tls_address(ti);
}
