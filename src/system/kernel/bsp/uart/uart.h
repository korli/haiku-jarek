/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef SYSTEM_KERNEL_BSP_UART_UART_H_
#define SYSTEM_KERNEL_BSP_UART_UART_H_

#include "uart_bus_access.h"

namespace BoardSupportPackage {

/* Drain and flush targets. */
#define	UART_DRAIN_RECEIVER	0x0001
#define	UART_DRAIN_TRANSMITTER	0x0002
#define	UART_FLUSH_RECEIVER	UART_DRAIN_RECEIVER
#define	UART_FLUSH_TRANSMITTER	UART_DRAIN_TRANSMITTER

/* Received character status bits. */
#define	UART_STAT_BREAK		0x0100
#define	UART_STAT_FRAMERR	0x0200
#define	UART_STAT_OVERRUN	0x0400
#define	UART_STAT_PARERR	0x0800

/* UART_IOCTL() requests */
#define	UART_IOCTL_BREAK	1
#define	UART_IOCTL_IFLOW	2
#define	UART_IOCTL_OFLOW	3
#define	UART_IOCTL_BAUD		4

class UARTBase
{
public:

};

}  // namespace BoardSupportPackage

#endif /* SYSTEM_KERNEL_BSP_UART_UART_H_ */
