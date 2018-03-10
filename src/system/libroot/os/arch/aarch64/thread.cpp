/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <OS.h>
#include "syscalls.h"

thread_id
find_thread(const char* name)
{
	if (!name) {
		unsigned long * tls;
		__asm__ __volatile__("mrs %0, tpidr_el0" : "=&r"(tls));
		return tls[1];
	}
	return _kern_find_thread(name);
}



