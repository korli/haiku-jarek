/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <KernelExport.h>
#include <driver_settings.h>
#include <int.h>

#include <arch/cpu.h>
#include <arch/debug_console.h>
#include <boot/stage2.h>
#include <debug.h>

#include <string.h>
#include <stdlib.h>

void
arch_debug_remove_interrupt_handler(uint32 line)
{
}

void
arch_debug_install_interrupt_handlers(void)
{
}

int
arch_debug_blue_screen_try_getchar(void)
{
	return -1;
}


char
arch_debug_blue_screen_getchar(void)
{
	return 0;
}


int
arch_debug_serial_try_getchar(void)
{
	return -1;
}


char
arch_debug_serial_getchar(void)
{
	return 0;
}


void
arch_debug_serial_putchar(const char c)
{
}


void
arch_debug_serial_puts(const char *s)
{
}


void
arch_debug_serial_early_boot_message(const char *string)
{
}


status_t
arch_debug_console_init(kernel_args *args)
{
	return B_OK;
}


status_t
arch_debug_console_init_settings(kernel_args *args)
{
	return B_OK;
}
