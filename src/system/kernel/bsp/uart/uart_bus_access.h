/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef SYSTEM_KERNEL_BSP_UART_UART_BUS_ACCESS_H_
#define SYSTEM_KERNEL_BSP_UART_UART_BUS_ACCESS_H_

#include "io_space.h"

namespace BoardSupportPackage {

enum class UARTParity {
	None		= 0,
	Odd			= 1,
	Even		= 3,
	Mark		= 5,
	Space		= 7
};

struct UARTBusAccess
{
	DeviceIOSpace *			bus = nullptr;
	uint32					channel = 0;
	uint32					clock = 0;
	uint8					register_shift = 0;
	uint8					register_width = 0;

	UARTBusAccess(DeviceIOSpace * _bus) noexcept;
	virtual ~UARTBusAccess() noexcept;

	inline uint32 ReadRegister(int reg) const noexcept {
		switch(register_width) {
		case 4:
			return bus->read_4(reg << register_shift);
		case 2:
			return bus->read_2(reg << register_shift);
		default:
			return bus->read_1(reg << register_shift);
		}
	}

	inline void WriteRegister(int reg, uint32 value) const noexcept
	{
		switch(register_width)
		{
		case 4:
			bus->write_4(reg << register_shift, value);
			break;
		case 2:
			bus->write_2(reg << register_shift, value);
			break;
		default:
			bus->write_1(reg << register_shift, value);
			break;
		}
	}

	inline void Barrier() const noexcept
	{
		bus->io_barrier(IOBarrier::READ_WRITE);
	}

	virtual bool Probe() noexcept = 0;
	virtual void Init(int baudRate, int dataBits, int stopBits, UARTParity parity) noexcept = 0;
	virtual void Close() noexcept = 0;
	virtual void PutC(int c) noexcept = 0;
	virtual bool RxReady() noexcept = 0;
	virtual int GetC() noexcept = 0;
};

static const size_t kUARTBusAccessStorageSize = sizeof(UARTBusAccess) + 2 * sizeof(void *);

UARTBusAccess * UART_PL011CreateBusAccess(void * memory, size_t size, DeviceIOSpace * bus);
UARTBusAccess * UART_NS8250CreateBusAccess(void * memory, size_t size, DeviceIOSpace * bus);

}  // namespace BoardSupportPackage

#endif /* SYSTEM_KERNEL_BSP_UART_UART_BUS_ACCESS_H_ */
