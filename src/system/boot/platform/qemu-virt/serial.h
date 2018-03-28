/*
 * Copyright 2004-2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SERIAL_H
#define SERIAL_H

#include <fdt_support.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void serial_init(fdt::Node dtb);
extern void serial_cleanup(void);

extern void serial_puts(const char *string, size_t size);
extern int serial_getc(bool wait);

extern void serial_disable(void);
extern void serial_enable(void);

#ifdef __cplusplus
}
#endif

#endif	/* SERIAL_H */
