/*
 * Copyright 2003-2007, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de.
 *		Ingo Weinhold, bonefish@users.sf.net.
 */

//!	C++ in the kernel


#include "util/kernel_cpp.h"

#ifdef _BOOT_MODE
#	include <boot/platform.h>
#else
#	include <KernelExport.h>
#	include <stdio.h>
#endif

#include <assert.h>

#ifdef _LOADER_MODE
#	define panic printf
#	define dprintf printf
#	define kernel_debugger printf
#endif

// full C++ support in the kernel

#ifndef _BOOT_MODE

FILE *stderr = NULL;

extern "C"
int
fprintf(FILE *f, const char *format, ...)
{
	// TODO: Introduce a vdprintf()...
	dprintf("fprintf(`%s',...)\n", format);
	return 0;
}

extern "C"
size_t
fwrite(const void *buffer, size_t size, size_t numItems, FILE *stream)
{
	dprintf("%.*s", int(size * numItems), (char*)buffer);
	return 0;
}

extern "C"
int
fputs(const char *string, FILE *stream)
{
	dprintf("%s", string);
	return 0;
}

extern "C"
int
fputc(int c, FILE *stream)
{
	dprintf("%c", c);
	return 0;
}

#ifndef _LOADER_MODE
extern "C"
int
printf(const char *format, ...)
{
	// TODO: Introduce a vdprintf()...
	dprintf("printf(`%s',...)\n", format);
	return 0;
}
#endif // #ifndef _LOADER_MODE

extern "C"
int
puts(const char *string)
{
	return fputs(string, NULL);
}

#endif	// #ifndef _BOOT_MODE

extern "C"
void
abort()
{
	panic("abort() called!");
}


#ifndef _BOOT_MODE

extern "C"
void
debugger(const char *message)
{
	kernel_debugger(message);
}

#endif	// #ifndef _BOOT_MODE


extern "C"
void
exit(int status)
{
	panic("exit() called with status code = %d!", status);
}


void
__assert_fail(const char *assertion, const char *file,
	unsigned int line, const char *function)
{
	for(;;) panic("%s: %s:%d:%s: %s\n", "kernel", file, line, function, assertion);
}
