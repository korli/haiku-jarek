/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef SYSTEM_BOOT_PLATFORM_QEMU_VIRT_CONSOLE_H_
#define SYSTEM_BOOT_PLATFORM_QEMU_VIRT_CONSOLE_H_

#include <boot/platform/generic/text_console.h>

#ifdef __cplusplus
extern "C" {
#endif

extern status_t console_init(void);

#ifdef __cplusplus
}
#endif


#endif /* SYSTEM_BOOT_PLATFORM_QEMU_VIRT_CONSOLE_H_ */
