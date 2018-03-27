/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Semihalf.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "uart.h"
#include <kernel/cpu.h>
#include <assert.h>
#include <new>

/* PL011 UART registers and masks*/
#define	UART_DR		0x00		/* Data register */
#define	DR_FE		(1 << 8)	/* Framing error */
#define	DR_PE		(1 << 9)	/* Parity error */
#define	DR_BE		(1 << 10)	/* Break error */
#define	DR_OE		(1 << 11)	/* Overrun error */

#define	UART_FR		0x06		/* Flag register */
#define	FR_RXFE		(1 << 4)	/* Receive FIFO/reg empty */
#define	FR_TXFF		(1 << 5)	/* Transmit FIFO/reg full */
#define	FR_RXFF		(1 << 6)	/* Receive FIFO/reg full */
#define	FR_TXFE		(1 << 7)	/* Transmit FIFO/reg empty */

#define	UART_IBRD	0x09		/* Integer baud rate register */
#define	IBRD_BDIVINT	0xffff	/* Significant part of int. divisor value */

#define	UART_FBRD	0x0a		/* Fractional baud rate register */
#define	FBRD_BDIVFRAC	0x3f	/* Significant part of frac. divisor value */

#define	UART_LCR_H	0x0b		/* Line control register */
#define	LCR_H_WLEN8	(0x3 << 5)
#define	LCR_H_WLEN7	(0x2 << 5)
#define	LCR_H_WLEN6	(0x1 << 5)
#define	LCR_H_FEN	(1 << 4)	/* FIFO mode enable */
#define	LCR_H_STP2	(1 << 3)	/* 2 stop frames at the end */
#define	LCR_H_EPS	(1 << 2)	/* Even parity select */
#define	LCR_H_PEN	(1 << 1)	/* Parity enable */

#define	UART_CR		0x0c		/* Control register */
#define	CR_RXE		(1 << 9)	/* Receive enable */
#define	CR_TXE		(1 << 8)	/* Transmit enable */
#define	CR_UARTEN	(1 << 0)	/* UART enable */

#define	UART_IFLS	0x0d		/* FIFO level select register */
#define	IFLS_RX_SHIFT	3		/* RX level in bits [5:3] */
#define	IFLS_TX_SHIFT	0		/* TX level in bits [2:0] */
#define	IFLS_MASK	0x07		/* RX/TX level is 3 bits */
#define	IFLS_LVL_1_8th	0		/* Interrupt at 1/8 full */
#define	IFLS_LVL_2_8th	1		/* Interrupt at 1/4 full */
#define	IFLS_LVL_4_8th	2		/* Interrupt at 1/2 full */
#define	IFLS_LVL_6_8th	3		/* Interrupt at 3/4 full */
#define	IFLS_LVL_7_8th	4		/* Interrupt at 7/8 full */

#define	UART_IMSC	0x0e		/* Interrupt mask set/clear register */
#define	IMSC_MASK_ALL	0x7ff		/* Mask all interrupts */

#define	UART_RIS	0x0f		/* Raw interrupt status register */
#define	UART_RXREADY	(1 << 4)	/* RX buffer full */
#define	UART_TXEMPTY	(1 << 5)	/* TX buffer empty */
#define	RIS_RTIM	(1 << 6)	/* Receive timeout */
#define	RIS_FE		(1 << 7)	/* Framing error interrupt status */
#define	RIS_PE		(1 << 8)	/* Parity error interrupt status */
#define	RIS_BE		(1 << 9)	/* Break error interrupt status */
#define	RIS_OE		(1 << 10)	/* Overrun interrupt status */

#define	UART_MIS	0x10		/* Masked interrupt status register */
#define	UART_ICR	0x11		/* Interrupt clear register */

#define	UART_PIDREG_0	0x3f8		/* Peripheral ID register 0 */
#define	UART_PIDREG_1	0x3f9		/* Peripheral ID register 1 */
#define	UART_PIDREG_2	0x3fa		/* Peripheral ID register 2 */
#define	UART_PIDREG_3	0x3fb		/* Peripheral ID register 3 */

/*
 * The hardware FIFOs are 16 bytes each on rev 2 and earlier hardware, 32 bytes
 * on rev 3 and later.  We configure them to interrupt when 3/4 full/empty.  For
 * RX we set the size to the full hardware capacity so that the uart core
 * allocates enough buffer space to hold a complete fifo full of incoming data.
 * For TX, we need to limit the size to the capacity we know will be available
 * when the interrupt occurs; uart_core will feed exactly that many bytes to
 * uart_pl011_bus_transmit() which must consume them all.
 */
#define	FIFO_RX_SIZE_R2	16
#define	FIFO_TX_SIZE_R2	12
#define	FIFO_RX_SIZE_R3	32
#define	FIFO_TX_SIZE_R3	24
#define	FIFO_IFLS_BITS	((IFLS_LVL_6_8th << IFLS_RX_SHIFT) | (IFLS_LVL_2_8th))

namespace BoardSupportPackage {

class PL011UARTBusAccess final : public UARTBusAccess
{
public:
	PL011UARTBusAccess(DeviceIOSpace * _bus) noexcept :
		UARTBusAccess(_bus)
	{
		this->register_width = 4;
		this->register_shift = 2;
	}

	virtual ~PL011UARTBusAccess() noexcept
	{
	}

	virtual bool Probe() noexcept
	{
		return true;
	}

	void uart_pl011_param(int baudrate, int databits, int stopbits, UARTParity parity)
	{
		uint32 ctrl, line;
		uint32 baud;

		/*
		 * Zero all settings to make sure
		 * UART is disabled and not configured
		 */
		ctrl = line = 0x0;
		WriteRegister(UART_CR, ctrl);

		/* As we know UART is disabled we may setup the line */
		switch (databits) {
		case 7:
			line |= LCR_H_WLEN7;
			break;
		case 6:
			line |= LCR_H_WLEN6;
			break;
		case 8:
		default:
			line |= LCR_H_WLEN8;
			break;
		}

		if (stopbits == 2)
			line |= LCR_H_STP2;
		else
			line &= ~LCR_H_STP2;

		if (parity != UARTParity::None)
			line |= LCR_H_PEN;
		else
			line &= ~LCR_H_PEN;
		line |= LCR_H_FEN;

		/* Configure the rest */
		ctrl |= (CR_RXE | CR_TXE | CR_UARTEN);

		if (this->clock != 0 && baudrate != 0) {
			baud = this->clock * 4 / baudrate;
			WriteRegister(UART_IBRD, ((uint32_t)(baud >> 6)) & IBRD_BDIVINT);
			WriteRegister(UART_FBRD, (uint32_t)(baud & 0x3F) & FBRD_BDIVFRAC);
		}

		/* Add config. to line before reenabling UART */
		WriteRegister(UART_LCR_H, (ReadRegister(UART_LCR_H) & ~0xff) | line);

		/* Set rx and tx fifo levels. */
		WriteRegister(UART_IFLS, FIFO_IFLS_BITS);

		WriteRegister(UART_CR, ctrl);
	}


	virtual void Init(int baudRate, int dataBits, int stopBits, UARTParity parity) noexcept
	{
		WriteRegister(UART_IMSC, ReadRegister(UART_IMSC) & ~IMSC_MASK_ALL);
		uart_pl011_param(baudRate, dataBits, stopBits, parity);
	}

	virtual void Close() noexcept
	{
	}

	virtual void PutC(int c) noexcept
	{
		while(ReadRegister(UART_FR) & FR_TXFF) {
			cpu_pause();
		}
		WriteRegister(UART_DR, c & 0xff);
	}

	virtual bool RxReady() noexcept {
		return !(ReadRegister(UART_FR) & FR_RXFE);
	}

	virtual int GetC() noexcept {
		while(!RxReady()) {
			cpu_pause();
		}
		return ReadRegister(UART_DR) & 0xff;
	}
};

UARTBusAccess * UART_PL011CreateBusAccess(void * memory, size_t size, DeviceIOSpace * bus) {
	assert(size >= sizeof(PL011UARTBusAccess));
	return new(memory) PL011UARTBusAccess(bus);
}

}  // namespace BoardSupportPackage
