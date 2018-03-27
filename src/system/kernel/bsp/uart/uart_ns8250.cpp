/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "uart.h"
#include "ns16550.h"
#include <assert.h>
#include <kernel/cpu.h>
#include <drivers/KernelExport.h>
#include <new>

namespace BoardSupportPackage {

#define	DEFAULT_RCLK	1843200

/*
 * Set the default baudrate tolerance to 3.0%.
 *
 * Some embedded boards have odd reference clocks (eg 25MHz)
 * and we need to handle higher variances in the target baud rate.
 */
#ifndef	UART_DEV_TOLERANCE_PCT
#define	UART_DEV_TOLERANCE_PCT	30
#endif	/* UART_DEV_TOLERANCE_PCT */

class NS8250UARTBusAccess final : public UARTBusAccess
{
public:
	NS8250UARTBusAccess(DeviceIOSpace * _bus) noexcept :
		UARTBusAccess(_bus)
	{
	}

	/*
	 * Clear pending interrupts. THRE is cleared by reading IIR. Data
	 * that may have been received gets lost here.
	 */
	void ns8250_clrint()
	{
		uint8 iir, lsr;

		iir = ReadRegister(REG_IIR);
		while ((iir & IIR_NOPEND) == 0) {
			iir &= IIR_IMASK;
			if (iir == IIR_RLS) {
				lsr = ReadRegister(REG_LSR);
				if (lsr & (LSR_BI|LSR_FE|LSR_PE))
					(void)ReadRegister(REG_DATA);
			} else if (iir == IIR_RXRDY || iir == IIR_RXTOUT)
				(void)ReadRegister(REG_DATA);
			else if (iir == IIR_MLSC)
				(void)ReadRegister(REG_MSR);
			Barrier();
			iir = ReadRegister(REG_IIR);
		}
	}

	int ns8250_delay()
	{
		int divisor;
		uint8 lcr;

		lcr = ReadRegister(REG_LCR);
		WriteRegister(REG_LCR, lcr | LCR_DLAB);
		Barrier();
		divisor = ReadRegister(REG_DLL) | (ReadRegister(REG_DLH) << 8);
		Barrier();
		WriteRegister(REG_LCR, lcr);
		Barrier();

		/* 1/10th the time to transmit 1 character (estimate). */
		if (divisor <= 134)
			return (16000000 * divisor / this->clock);
		return (16000 * divisor / (this->clock / 1000));
	}

	static int ns8250_divisor(int rclk, int baudrate)
	{
		int actual_baud, divisor;
		int error;

		if (baudrate == 0)
			return (0);

		divisor = (rclk / (baudrate << 3) + 1) >> 1;
		if (divisor == 0 || divisor >= 65536)
			return (0);
		actual_baud = rclk / (divisor << 4);

		/* 10 times error in percent: */
		error = ((actual_baud - baudrate) * 2000 / baudrate + 1) >> 1;

		/* enforce maximum error tolerance: */
		if (error < -UART_DEV_TOLERANCE_PCT || error > UART_DEV_TOLERANCE_PCT)
			return (0);

		return (divisor);
	}

	bool ns8250_drain(int what) {
		int delay, limit;

		delay = ns8250_delay();

		if (what & UART_DRAIN_TRANSMITTER) {
			/*
			 * Pick an arbitrary high limit to avoid getting stuck in
			 * an infinite loop when the hardware is broken. Make the
			 * limit high enough to handle large FIFOs.
			 */
			limit = 10 * 1024;
			while ((ReadRegister(REG_LSR) & LSR_TEMT) == 0 && --limit)
				spin(delay);
			if (limit == 0) {
				/* printf("ns8250: transmitter appears stuck... "); */
				return false;
			}
		}

		if (what & UART_DRAIN_RECEIVER) {
			/*
			 * Pick an arbitrary high limit to avoid getting stuck in
			 * an infinite loop when the hardware is broken. Make the
			 * limit high enough to handle large FIFOs and integrated
			 * UARTs. The HP rx2600 for example has 3 UARTs on the
			 * management board that tend to get a lot of data send
			 * to it when the UART is first activated.
			 */
			limit = 10 * 4096;
			while ((ReadRegister(REG_LSR) & LSR_RXRDY) && --limit) {
				(void) ReadRegister(REG_DATA);
				Barrier();
				spin(delay << 2);
			}
			if (limit == 0) {
				/* printf("ns8250: receiver appears broken... "); */
				return false;
			}
		}

		return true;
	}

	void ns8250_flush(int what)
	{
		uint8 fcr;

		fcr = FCR_ENABLE;
#ifdef CPU_XBURST
		fcr |= FCR_UART_ON;
#endif

		if (what & UART_FLUSH_TRANSMITTER)
			fcr |= FCR_XMT_RST;
		if (what & UART_FLUSH_RECEIVER)
			fcr |= FCR_RCV_RST;

		WriteRegister(REG_FCR, fcr);
		Barrier();
	}

	bool ns8250_param(int baudrate, int databits, int stopbits, UARTParity parity)
	{
		int divisor;
		uint8_t lcr;

		lcr = 0;
		if (databits >= 8)
			lcr |= LCR_8BITS;
		else if (databits == 7)
			lcr |= LCR_7BITS;
		else if (databits == 6)
			lcr |= LCR_6BITS;
		else
			lcr |= LCR_5BITS;
		if (stopbits > 1)
			lcr |= LCR_STOPB;
		lcr |= ((int)parity) << 3;

		/* Set baudrate. */
		if (baudrate > 0) {
			divisor = ns8250_divisor(this->clock, baudrate);
			if (divisor == 0)
				return false;
			WriteRegister(REG_LCR, lcr | LCR_DLAB);
			Barrier();
			WriteRegister(REG_DLL, divisor & 0xff);
			WriteRegister(REG_DLH, (divisor >> 8) & 0xff);
			Barrier();
		}

		/* Set LCR and clear DLAB. */
		WriteRegister(REG_LCR, lcr);
		Barrier();
		return true;
	}

	virtual ~NS8250UARTBusAccess() noexcept
	{
	}

	virtual bool Probe() noexcept
	{
		uint8 val;

#ifdef CPU_XBURST
		WriteRegister(REG_FCR, FCR_UART_ON);
#endif

		/* Check known 0 bits that don't depend on DLAB. */
		val = ReadRegister(REG_IIR);
		if (val & 0x30)
			return false;
		/*
		 * Bit 6 of the MCR (= 0x40) appears to be 1 for the Sun1699
		 * chip, but otherwise doesn't seem to have a function. In
		 * other words, uart(4) works regardless. Ignore that bit so
		 * the probe succeeds.
		 */
		val = ReadRegister(REG_MCR);
		if (val & 0xa0)
			return false;

		return true;
	}

	virtual void Init(int baudRate, int dataBits, int stopBits, UARTParity parity) noexcept
	{
		uint8 ier, val;

		if(clock == 0)
			clock = DEFAULT_RCLK;

		ns8250_param(baudRate, dataBits, stopBits, parity);

		/* Disable all interrupt sources. */
		/*
		 * We use 0xe0 instead of 0xf0 as the mask because the XScale PXA
		 * UARTs split the receive time-out interrupt bit out separately as
		 * 0x10.  This gets handled by ier_mask and ier_rxbits below.
		 */
		ier = ReadRegister(REG_IER) & 0xe0;
		WriteRegister(REG_IER, ier);
		Barrier();

		/* Disable the FIFO (if present). */
		val = 0;
#ifdef CPU_XBURST
		val |= FCR_UART_ON;
#endif
		WriteRegister(REG_FCR, val);
		Barrier();

		/* Set RTS & DTR. */
		WriteRegister(REG_MCR, MCR_IE | MCR_RTS | MCR_DTR);
		Barrier();

		ns8250_clrint();
	}

	virtual void Close() noexcept
	{
		/* Clear RTS & DTR. */
		WriteRegister(REG_MCR, MCR_IE);
		Barrier();
	}

	virtual void PutC(int c) noexcept
	{
		int limit;
		limit = 250000;
		while ((ReadRegister(REG_LSR) & LSR_THRE) == 0 && --limit)
			spin(4);
		WriteRegister(REG_DATA, c);
		Barrier();
	}

	virtual bool RxReady() noexcept {
		return (ReadRegister(REG_LSR) & LSR_RXRDY) != 0;

	}

	virtual int GetC() noexcept {
		while ((ReadRegister(REG_LSR) & LSR_RXRDY) == 0) {
			spin(4);
		}

		return ReadRegister(REG_DATA);
	}
};

UARTBusAccess * UART_NS8250CreateBusAccess(void * memory, size_t size, DeviceIOSpace * bus) {
	assert(size >= sizeof(NS8250UARTBusAccess));
	return new(memory) NS8250UARTBusAccess(bus);
}

}  // namespace BoardSupportPackage
