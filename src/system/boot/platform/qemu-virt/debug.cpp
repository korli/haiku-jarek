/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <boot/platform.h>
#include <boot/stdio.h>
#include <stdarg.h>

/*!	This works only after console_init() was called.
*/
extern "C" void
panic(const char* format, ...)
{
	const char hint[] = "*** PANIC ***";
	char buffer[512];
	va_list list;
	int length;

	platform_switch_to_text_mode();

	puts(hint);

	va_start(list, format);
	length = vsnprintf(buffer, sizeof(buffer), format, list);
	va_end(list);

	if (length >= (int)sizeof(buffer))
		length = sizeof(buffer) - 1;

	//fprintf(stderr, "%s", buffer);
	puts(buffer);

	for(;;);
}


extern "C" void
dprintf(const char* format, ...)
{
	char buffer[512];
	va_list list;
	int length;

	va_start(list, format);
	length = vsnprintf(buffer, sizeof(buffer), format, list);
	va_end(list);

	if (length >= (int)sizeof(buffer))
		length = sizeof(buffer) - 1;

	if (platform_boot_options() & BOOT_OPTION_DEBUG_OUTPUT)
		fprintf(stderr, "%s", buffer);
}


char*
platform_debug_get_log_buffer(size_t* _size)
{
	return NULL;
}
