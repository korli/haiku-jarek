/*
 * Copyright 2014, Paweł Dziepak, pdziepak@quarnos.org.
 * Copyright 2012, Alex Smith, alex@alex-smith.me.uk.
 * Distributed under the terms of the MIT License.
 */


#ifndef _NO_INLINE_ASM
#	define _NO_INLINE_ASM 1
#endif

#include <atomic>

#include <runtime_loader/runtime_loader.h>

#include <support/TLS.h>
#include <tls.h>

#include <assert.h>


struct tls_index {
	unsigned long int	module;
	unsigned long int	offset;
};

extern "C" void*
__tls_get_addr(tls_index* ti)
{
	return __gRuntimeLoader->get_tls_address(ti->module, ti->offset);
}
