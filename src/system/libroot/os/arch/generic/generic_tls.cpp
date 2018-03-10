/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <sys/cdefs.h>
#include <atomic>

#include <runtime_loader/runtime_loader.h>

#include "support/TLS.h"
#include "tls.h"

#ifdef _Thread_local

static std::atomic<int> gNextSlot(TLS_FIRST_FREE_SLOT);
static thread_local void * gTLSValues[TLS_MAX_KEYS];

int32
tls_allocate()
{
	if (gNextSlot < TLS_MAX_KEYS) {
		auto next = gNextSlot++;
		if (next < TLS_MAX_KEYS)
			return next;
	}

	return B_NO_MEMORY;
}

void*
tls_get(int32 index)
{
	return gTLSValues[index];
}


void**
tls_address(int32 index)
{
	return &gTLSValues[index];
}


void
tls_set(int32 index, void* value)
{
	gTLSValues[index] = value;
}

#endif
