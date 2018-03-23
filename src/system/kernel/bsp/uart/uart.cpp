/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include "uart.h"

namespace BoardSupportPackage {

UARTBusAccess::UARTBusAccess(DeviceIOSpace * _bus) noexcept :
	bus(_bus)
{
}

UARTBusAccess::~UARTBusAccess() noexcept
{
}

}  // namespace BoardSupportPackage
