/*
 * Copyright 2004-2008, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "serial.h"

#include <boot/platform.h>
#include <arch/cpu.h>
#include <boot/stage2.h>

#include <string.h>

#include "uart_bus_access.h"
#include <type_traits>

//#define ENABLE_SERIAL
	// define this to always enable serial output


enum serial_register_offsets {
	SERIAL_TRANSMIT_BUFFER		= 0,
	SERIAL_RECEIVE_BUFFER		= 0,
	SERIAL_DIVISOR_LATCH_LOW	= 0,
	SERIAL_DIVISOR_LATCH_HIGH	= 1,
	SERIAL_FIFO_CONTROL			= 2,
	SERIAL_LINE_CONTROL			= 3,
	SERIAL_MODEM_CONTROL		= 4,
	SERIAL_LINE_STATUS			= 5,
	SERIAL_MODEM_STATUS			= 6,
};

static const uint32 kSerialBaudRate = 115200;

static int32 sSerialEnabled = 0;
static uint16 sSerialBasePort = 0x3f8;
static std::aligned_storage<sizeof(BoardSupportPackage::X86PortIOSpace),
			alignof(BoardSupportPackage::X86PortIOSpace)>::type sUARTIO;
static std::aligned_storage<BoardSupportPackage::kUARTBusAccessStorageSize,
			alignof(BoardSupportPackage::UARTBusAccess)>::type sUART_;
static BoardSupportPackage::UARTBusAccess * sUART;

static void
serial_putc(char c)
{
	sUART->PutC(c);
}


extern "C" void
serial_puts(const char* string, size_t size)
{
	if (sSerialEnabled <= 0)
		return;

	while (size-- != 0) {
		char c = string[0];

		if (c == '\n') {
			serial_putc('\r');
			serial_putc('\n');
		} else if (c != '\r')
			serial_putc(c);

		string++;
	}
}


extern "C" void
serial_disable(void)
{
#ifdef ENABLE_SERIAL
	sSerialEnabled = 0;
#else
	sSerialEnabled--;
#endif
}


extern "C" void
serial_enable(void)
{
	sSerialEnabled++;
}


extern "C" void
serial_init(void)
{
	// copy the base ports of the optional 4 serial ports to the kernel args
	// 0x0000:0x0400 is the location of that information in the BIOS data
	// segment
	uint16* ports = (uint16*)0x400;
	memcpy(gKernelArgs.platform_args.serial_base_ports, ports,
		sizeof(uint16) * MAX_SERIAL_PORTS);

	// only use the port if we could find one, else use the standard port
	if (gKernelArgs.platform_args.serial_base_ports[0] != 0)
		sSerialBasePort = gKernelArgs.platform_args.serial_base_ports[0];


	BoardSupportPackage::X86PortIOSpace * port = new(&sUARTIO) BoardSupportPackage::X86PortIOSpace(sSerialBasePort);
	sUART = BoardSupportPackage::UART_NS8250CreateBusAccess(&sUART_, sizeof(sUART_), port);

	if(sUART->Probe()) {
		sUART->Init(115200, 8, 1, BoardSupportPackage::UARTParity::None);
#ifdef ENABLE_SERIAL
		serial_enable();
#endif
	}
}
