/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <KernelExport.h>

#include <boot/disk_identifier.h>
#include <boot/vfs.h>
#include <boot/platform.h>
#include <boot/partitions.h>
#include <boot/stage2.h>
#include <boot/stdio.h>

#define TRACE_DEVICES
#ifdef TRACE_DEVICES
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...)
#endif


status_t 
platform_add_boot_device(struct stage2_args *args, NodeList *devicesList)
{
	return B_OK;
}


status_t
platform_get_boot_partitions(struct stage2_args *args, Node *device,
	NodeList *list, NodeList *partitionList)
{
	return B_ENTRY_NOT_FOUND;
}


status_t
platform_add_block_devices(stage2_args *args, NodeList *devicesList)
{
	return B_OK;
}


status_t 
platform_register_boot_device(Node *device)
{
	return B_OK;
}
