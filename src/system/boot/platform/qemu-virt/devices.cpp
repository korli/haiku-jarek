/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <fdt_support.h>

#include <KernelExport.h>

#include <kernel/boot/disk_identifier.h>
#include <kernel/boot/vfs.h>
#include <kernel/boot/platform.h>
#include <kernel/boot/partitions.h>
#include <kernel/boot/stage2.h>
#include <kernel/boot/stdio.h>
#include <kernel/boot/memory.h>

#include <cassert>

#define TRACE_DEVICES
#ifdef TRACE_DEVICES
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...)
#endif


status_t 
platform_add_boot_device(struct stage2_args *args, NodeList *devicesList)
{
	fdt::Node deviceTree(args->platform.fdt_virt);

	fdt::Node chosen(deviceTree.GetPath("/chosen"));

	if(!chosen.IsNull()) {
		fdt::Property initrd_start(chosen.GetProperty("linux,initrd-start"));
		fdt::Property initrd_end(chosen.GetProperty("linux,initrd-end"));

		if(!initrd_start.IsNull() && !initrd_end.IsNull()) {
			addr_t tgz_base = initrd_start.EncodedGet(0) & ~(B_PAGE_SIZE - 1);
			addr_t tgz_end = ((initrd_end.EncodedGet(0) + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE -1));

			void * tgz_mapping = nullptr;

			int error = gBootLoaderVirtualRegionAllocator->AllocateVirtualMemoryRegion(&tgz_mapping,
					tgz_end - tgz_base,
					// Prefer superpage alignment
					0x400000,
					false,
					false);

			assert(error == 0);

			error = gBootVirtualMemoryMapper->MapVirtualMemoryRegion((addr_t)tgz_mapping,
					tgz_base,
					tgz_end - tgz_base,
					B_KERNEL_READ_AREA);

			assert(error == 0);

			MemoryDisk * disk = new(std::nothrow) MemoryDisk((const uint8 *)tgz_mapping + (initrd_start.EncodedGet(0) & (B_PAGE_SIZE - 1)),
					initrd_end.EncodedGet(0) - 	initrd_start.EncodedGet(0),
					"initrd");

			devicesList->Add(disk);
		}
	}

	return B_OK;
}


status_t
platform_get_boot_partitions(struct stage2_args *args, Node *device,
	NodeList *list, NodeList *partitionList)
{
	NodeIterator iterator = list->GetIterator();
	boot::Partition *partition = NULL;
	while ((partition = (boot::Partition *)iterator.Next()) != NULL) {
		partitionList->Insert(partition);
		return B_OK;
	}

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
	disk_identifier disk_ident;
	disk_ident.bus_type = UNKNOWN_BUS;
	disk_ident.device_type = UNKNOWN_DEVICE;
	disk_ident.device.unknown.size = device->Size();

	for (int32 i = 0; i < NUM_DISK_CHECK_SUMS; i++) {
		disk_ident.device.unknown.check_sums[i].offset = -1;
		disk_ident.device.unknown.check_sums[i].sum = 0;
	}

	gBootVolume.SetData(BOOT_VOLUME_DISK_IDENTIFIER, B_RAW_TYPE, &disk_ident,
			sizeof(disk_ident));

	return B_OK;
}
