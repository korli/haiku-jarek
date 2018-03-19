/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef SYSTEM_BOOT_PLATFORM_QEMU_VIRT_CPU_H_
#define SYSTEM_BOOT_PLATFORM_QEMU_VIRT_CPU_H_

#include <fdt_support.h>

void cpu_init_via_device_tree(fdt::Node dtb);

#endif /* SYSTEM_BOOT_PLATFORM_QEMU_VIRT_CPU_H_ */
