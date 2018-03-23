/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include "serial.h"

#include <kernel/boot/platform.h>
#include <kernel/arch/cpu.h>
#include <kernel/boot/stage2.h>
#include <kernel/boot/memory.h>

#include <string.h>
#include <assert.h>

#include "uart_bus_access.h"
#include <type_traits>

static int sSerialEnabled = 0;

static std::aligned_storage<sizeof(BoardSupportPackage::MemoryIOSpace),
			alignof(BoardSupportPackage::MemoryIOSpace)>::type sUARTIO;
static std::aligned_storage<BoardSupportPackage::kUARTBusAccessStorageSize,
			alignof(BoardSupportPackage::UARTBusAccess)>::type sUART_;
static BoardSupportPackage::UARTBusAccess * sUART;

extern "C" int serial_getc(bool wait)
{
	if(sSerialEnabled <= 0)
		return 0;
	if(!wait && !sUART->RxReady())
		return 0;
	return sUART->GetC();
}

extern "C" void serial_puts(const char* string, size_t size)
{
	if (sSerialEnabled <= 0)
		return;

	while(size-->0) {
		char c = *string++;
		if(c == '\n') {
			sUART->PutC('\r');
		}
		sUART->PutC(c);
	}
}

extern "C" void serial_disable(void)
{
	sSerialEnabled = 0;
}

extern "C" void serial_enable(void)
{
	sSerialEnabled++;
}

extern "C" void serial_cleanup(void)
{
	if (sSerialEnabled <= 0)
		return;
	sUART->Close();
}

static const char * sDefaultConsolePaths[] = {
	"stdout-path",
	"linux,stdout-path",
	"stdout",
	"stdin-path",
	"stdin",
	nullptr
};

static const char * sPL011Compats[] = {
	"arm,pl011",
	nullptr
};

static const char * s8250Compats[] = {
	"ns16550",
	"ns16550a",
	nullptr
};

extern "C" void serial_init(fdt::Node dtb)
{
	fdt::Node chosen(dtb.GetPath("/chosen"));
	fdt::Node device;

	if(!chosen.IsNull()) {
		for(int i = 0 ; sDefaultConsolePaths[i] ; ++i) {
			fdt::Property path = chosen.GetProperty(sDefaultConsolePaths[i]);
			if(!path.IsNull()) {
				char * separator = strchr((const char *)path.Data(), ':');
				if(!separator) {
					device = dtb.GetPath((const char *)path.Data());
				} else {
					char buffer[128];
					strlcpy(buffer, (const char *)path.Data(), sizeof(buffer));
					device = dtb.GetPath(buffer);
				}
				if(!device.IsNull())
					break;
			}
		}
	}

	if(device.IsNull()) {
		device = dtb.GetPath("/serial0");
	}

	if(device.IsNull()) {
		return;
	}

	addr_range ranges[4];
	uint32 num_ranges = 0;

	if(!device.RegToRanges(ranges, num_ranges, 4)) {
		return;
	}

	if(num_ranges < 1) {
		return;
	}

	void * serial0 = gBootVirtualMemoryMapper->MapPhysicalLoaderMemory(ranges[0].start, ranges[0].size, true);
	assert(serial0);

	auto ioSpace = new(&sUARTIO) BoardSupportPackage::MemoryIOSpace((addr_t)serial0);

	if(device.IsCompatible(sPL011Compats)) {
		sUART = BoardSupportPackage::UART_PL011CreateBusAccess(&sUART_, sizeof(sUART_), ioSpace);
	} else if(device.IsCompatible(s8250Compats)) {
		sUART = BoardSupportPackage::UART_NS8250CreateBusAccess(&sUART_, sizeof(sUART_), ioSpace);
	} else {
		return;
	}

	fdt::Property clock(device.GetProperty("clock-frequency"));
	if(clock.IsNull())
		clock = device.Parent().GetProperty("bus-frequency");

	if(!clock.IsNull()) {
		sUART->clock = clock.EncodedGet(0);
	}

	fdt::Property reg_shift_property(device.GetProperty("reg-shift"));
	fdt::Property io_width_property(device.GetProperty("reg-io-width"));

	if(!reg_shift_property.IsNull()) {
		sUART->register_shift = reg_shift_property.EncodedGet(0);
	}

	if(!io_width_property.IsNull()) {
		sUART->register_width = io_width_property.EncodedGet(0);
	}

	sUART->Init(115200, 8, 1, BoardSupportPackage::UARTParity::None);

	serial_enable();
}
