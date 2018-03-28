/*
 * Copyright 2004-2007, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "debug.h"

#include <string.h>

#include <boot/platform.h>
#include <boot/stage2.h>
#include <boot/stdio.h>
#include <kernel.h>
#include <util/ring_buffer.h>
#include <kernel/boot/memory.h>

#include "keyboard.h"
#include "mmu.h"
#include "serial.h"


//#define PRINT_TIME_STAMPS
	// Define to print a TSC timestamp before each line of output.


static const char* const kDebugSyslogSignature = "Haiku syslog";

static char sBuffer[16384];
static uint32 sBufferPosition;

static ring_buffer* sDebugSyslogBuffer = NULL;
static bool sPostCleanup = false;


#ifdef PRINT_TIME_STAMPS
extern "C" uint64 rdtsc();
#endif


static void
syslog_write(const char* buffer, size_t length)
{
	if (sPostCleanup && sDebugSyslogBuffer != NULL) {
		ring_buffer_write(sDebugSyslogBuffer, (const uint8*)buffer, length);
	} else if (sBufferPosition + length < sizeof(sBuffer)) {
		memcpy(sBuffer + sBufferPosition, buffer, length);
		sBufferPosition += length;
	}
}


static void
dprintf_args(const char *format, va_list args)
{
	char buffer[512];
	int length = vsnprintf(buffer, sizeof(buffer), format, args);
	if (length == 0)
		return;

	if (length >= (int)sizeof(buffer))
		length = sizeof(buffer) - 1;

#ifdef PRINT_TIME_STAMPS
	static bool sNewLine = true;

	if (sNewLine) {
		char timeBuffer[32];
		snprintf(timeBuffer, sizeof(timeBuffer), "[%" B_PRIu64 "] ", rdtsc());
		syslog_write(timeBuffer, strlen(timeBuffer));
		serial_puts(timeBuffer, strlen(timeBuffer));
	}

	sNewLine = buffer[length - 1] == '\n';
#endif	// PRINT_TIME_STAMPS

	syslog_write(buffer, length);
	serial_puts(buffer, length);

	if (platform_boot_options() & BOOT_OPTION_DEBUG_OUTPUT)
		fprintf(stderr, "%s", buffer);
}


// #pragma mark -


/*!	This works only after console_init() was called.
*/
void
panic(const char *format, ...)
{
	va_list list;

	platform_switch_to_text_mode();

	puts("*** PANIC ***");

	va_start(list, format);
	vprintf(format, list);
	va_end(list);

	puts("\nPress key to reboot.");

	clear_key_buffer();
	wait_for_key();
	platform_exit();
}


void
dprintf(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	dprintf_args(format, args);
	va_end(args);
}


void
kprintf(const char *format, ...)
{
	va_list args;

	va_start(args, format);

	// print to console, if available
	if (stdout != NULL)
		vfprintf(stdout, format, args);

	// always print to serial line
	dprintf_args(format, args);

	va_end(args);
}


// #pragma mark -


void
debug_init_post_mmu(void)
{
	size_t size = 1024 * 1024;
	uint64 physicalBase;

	// Allocate physical memory
	status_t error = gBootPhysicalMemoryAllocator->AllocatePhysicalMemory(
			size,
			B_PAGE_SIZE,
			physicalBase);

	if(error != B_OK)
		return;


	// Allocate virtual region
	void * virtualBase = NULL;
	error = gBootKernelVirtualRegionAllocator.AllocateVirtualMemoryRegion(
			&virtualBase,
			size,
			B_PAGE_SIZE,
			false,
			true);

	if(error != B_OK) {
		panic("Virtual address space exhausted");
	}

	error = gBootVirtualMemoryMapper->MapVirtualMemoryRegion((addr_t)virtualBase,
				physicalBase,
				size,
				B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);

	if(error != B_OK) {
		panic("Can't remap memory for log buffer");
	}

	void * buffer = (void *)(addr_t)virtualBase;

	// check whether there's a previous syslog we can recover
	size_t signatureLength = strlen(kDebugSyslogSignature);
	bool recover = memcmp(buffer, kDebugSyslogSignature, signatureLength) == 0;

	size -= signatureLength;
	buffer = (uint8*)buffer + ROUNDUP(signatureLength, sizeof(void*));

	sDebugSyslogBuffer = create_ring_buffer_etc(buffer, size,
		recover ? RING_BUFFER_INIT_FROM_BUFFER : 0);

	gKernelArgs.debug_output = sDebugSyslogBuffer;
	gKernelArgs.debug_size = sDebugSyslogBuffer->size;
}


void
debug_cleanup(void)
{
	if (sDebugSyslogBuffer != NULL) {
		// If desired, store the debug syslog data from the previous session for
		// the kernel.
		size_t bytesReadable = 0;
		if (gKernelArgs.previous_debug_size != 0) {
			bytesReadable = ring_buffer_readable(sDebugSyslogBuffer);
			gKernelArgs.previous_debug_size = bytesReadable;
		}

		if (bytesReadable != 0) {
			if (uint8* buffer = (uint8*)kernel_args_malloc(bytesReadable)) {
				ring_buffer_read(sDebugSyslogBuffer, buffer, bytesReadable);
				gKernelArgs.previous_debug_output = buffer;
			} else
				gKernelArgs.previous_debug_size = 0;
		}

		// Prepare the debug syslog buffer for this session.
		size_t signatureLength = strlen(kDebugSyslogSignature);
		void* buffer
			= (void*)ROUNDDOWN((addr_t)sDebugSyslogBuffer, B_PAGE_SIZE);

		if (gKernelArgs.keep_debug_output_buffer) {
			// copy the output gathered so far into the ring buffer
			ring_buffer_clear(sDebugSyslogBuffer);
			ring_buffer_write(sDebugSyslogBuffer, (uint8*)sBuffer,
				sBufferPosition);

			memcpy(buffer, kDebugSyslogSignature, signatureLength);
		} else {
			// clear the signature
			memset(buffer, 0, signatureLength);
		}
	} else
		gKernelArgs.keep_debug_output_buffer = false;

	if (!gKernelArgs.keep_debug_output_buffer) {
		gKernelArgs.debug_output = kernel_args_malloc(sBufferPosition);
		if (gKernelArgs.debug_output != NULL) {
			memcpy(gKernelArgs.debug_output, sBuffer, sBufferPosition);
			gKernelArgs.debug_size = sBufferPosition;
		}
	}

	sPostCleanup = true;
}


char*
platform_debug_get_log_buffer(size_t* _size)
{
	if (_size != NULL)
		*_size = sizeof(sBuffer);

	return sBuffer;
}
