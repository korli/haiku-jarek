/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <arch/platform.h>
#include <boot_item.h>
#include <boot/stage2.h>


status_t
arch_platform_init(struct kernel_args *args)
{
	return B_OK;
}


status_t
arch_platform_init_post_vm(struct kernel_args *args)
{
	return B_OK;
}


status_t
arch_platform_init_post_thread(struct kernel_args *args)
{
	return B_OK;
}
