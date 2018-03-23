/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef SYSTEM_KERNEL_BSP_HOTPLUG_IO_SPACE_GENERIC_H_
#define SYSTEM_KERNEL_BSP_HOTPLUG_IO_SPACE_GENERIC_H_

#include "io_space_base.h"
#include <stdlib.h>

namespace BoardSupportPackage {

class IOSpaceGeneric : public IOSpace
{
protected:
	const addr_t		fBaseAddress;

public:
	IOSpaceGeneric(addr_t baseAddress) noexcept :
		fBaseAddress(baseAddress)
	{
	}

	virtual ~IOSpaceGeneric() noexcept {
	}

	virtual uint8 read_1(unsigned int offset) const noexcept override {
		return *(volatile uint8 *)(fBaseAddress + offset);
	}

	virtual uint16 read_2(unsigned int offset) const noexcept override {
		return *(volatile uint16 *)(fBaseAddress + offset);
	}

	virtual uint32 read_4(unsigned int offset) const noexcept override {
		return *(volatile uint32 *)(fBaseAddress + offset);
	}

#ifdef __HAIKU_ARCH_64_BIT
	virtual uint64 read_8(unsigned int offset) const noexcept override {
		return *(volatile uint64 *)(fBaseAddress + offset);
	}
#endif

	virtual void write_1(unsigned int offset, uint8 value) const noexcept override {
		*(volatile uint8 *)(fBaseAddress + offset) = value;
	}

	virtual void write_2(unsigned int offset, uint16 value) const noexcept override {
		*(volatile uint16 *)(fBaseAddress + offset) = value;
	}

	virtual void write_4(unsigned int offset, uint32 value) const noexcept override {
		*(volatile uint32 *)(fBaseAddress + offset) = value;
	}

#ifdef __HAIKU_ARCH_64_BIT
	virtual void write_8(unsigned int offset, uint64 value) const noexcept override {
		*(volatile uint64 *)(fBaseAddress + offset) = value;
	}
#endif

	virtual void read_1(unsigned int offset, uint8 * buffer, size_t count) const noexcept override {
		while(count-->0) {
			*buffer++= *(volatile uint8 *)(fBaseAddress + offset);
		}
	}

	virtual void read_2(unsigned int offset, uint16 * buffer, size_t count) const noexcept override {
		while(count-->0) {
			*buffer++= *(volatile uint16 *)(fBaseAddress + offset);
		}
	}

	virtual void read_4(unsigned int offset, uint32 * buffer, size_t count) const noexcept override {
		while(count-->0) {
			*buffer++= *(volatile uint32 *)(fBaseAddress + offset);
		}
	}

#ifdef __HAIKU_ARCH_64_BIT
	virtual void read_8(unsigned int offset, uint64 * buffer, size_t count) const noexcept override {
		while(count-->0) {
			*buffer++= *(volatile uint64 *)(fBaseAddress + offset);
		}
	}
#endif

	virtual void write_1(unsigned int offset, const uint8 * buffer, size_t count) const noexcept override {
		while(count-->0) {
			*(volatile uint8 *)(fBaseAddress + offset) = *buffer++;
		}
	}

	virtual void write_2(unsigned int offset, const uint16 * buffer, size_t count) const noexcept override {
		while(count-->0) {
			*(volatile uint16 *)(fBaseAddress + offset) = *buffer++;
		}
	}

	virtual void write_4(unsigned int offset, const uint32 * buffer, size_t count) const noexcept override {
		while(count-->0) {
			*(volatile uint32 *)(fBaseAddress + offset) = *buffer++;
		}
	}

#ifdef __HAIKU_ARCH_64_BIT
	virtual void write_8(unsigned int offset, const uint64 * buffer, size_t count) const noexcept override {
		while(count-->0) {
			*(volatile uint64 *)(fBaseAddress + offset) = *buffer++;
		}
	}
#endif

	virtual void set_1(unsigned int offset, uint8 value, size_t count) const noexcept override {
		while(count-->0) {
			*(volatile uint8 *)(fBaseAddress + offset) = value;
		}
	}

	virtual void set_2(unsigned int offset, uint16 value, size_t count) const noexcept override {
		while(count-->0) {
			*(volatile uint16 *)(fBaseAddress + offset) = value;
		}
	}

	virtual void set_4(unsigned int offset, uint32 value, size_t count) const noexcept override {
		while(count-->0) {
			*(volatile uint32 *)(fBaseAddress + offset) = value;
		}
	}

#ifdef __HAIKU_ARCH_64_BIT
	virtual void set_8(unsigned int offset, uint64 value, size_t count) const noexcept override {
		while(count-->0) {
			*(volatile uint64 *)(fBaseAddress + offset) = value;
		}
	}
#endif

	virtual void read_region_1(unsigned int offset, uint8 * buffer, size_t count) const noexcept override {
		volatile const uint8 * source = (volatile const uint8 *)(fBaseAddress + offset);
		while(count-->0) {
			*buffer++ = *source++;
		}
	}

	virtual void read_region_2(unsigned int offset, uint16 * buffer, size_t count) const noexcept override {
		volatile const uint16 * source = (volatile const uint16 *)(fBaseAddress + offset);
		while(count-->0) {
			*buffer++ = *source++;
		}
	}

	virtual void read_region_4(unsigned int offset, uint32 * buffer, size_t count) const noexcept override{
		volatile const uint32 * source = (volatile const uint32 *)(fBaseAddress + offset);
		while(count-->0) {
			*buffer++ = *source++;
		}
	}
#ifdef __HAIKU_ARCH_64_BIT
	virtual void read_region_8(unsigned int offset, uint64 * buffer, size_t count) const noexcept override {
		volatile const uint64 * source = (volatile const uint64 *)(fBaseAddress + offset);
		while(count-->0) {
			*buffer++ = *source++;
		}
	}
#endif

	virtual void write_region_1(unsigned int offset, const uint8 * buffer, size_t count) const noexcept override {
		volatile uint8 * dest = (volatile uint8 *)(fBaseAddress + offset);
		while(count-->0) {
			*dest++ = *buffer++;
		}
	}

	virtual void write_region_2(unsigned int offset, const uint16 * buffer, size_t count) const noexcept override {
		volatile uint16 * dest = (volatile uint16 *)(fBaseAddress + offset);
		while(count-->0) {
			*dest++ = *buffer++;
		}
	}

	virtual void write_region_4(unsigned int offset, const uint32 * buffer, size_t count) const noexcept override {
		volatile uint32 * dest = (volatile uint32 *)(fBaseAddress + offset);
		while(count-->0) {
			*dest++ = *buffer++;
		}
	}

#ifdef __HAIKU_ARCH_64_BIT
	virtual void write_region_8(unsigned int offset, const uint64 * buffer, size_t count) const noexcept override {
		volatile uint64 * dest = (volatile uint64 *)(fBaseAddress + offset);
		while(count-->0) {
			*dest++ = *buffer++;
		}
	}
#endif
};

}  // namespace BoardSupportPackage

#endif /* SYSTEM_KERNEL_BSP_HOTPLUG_IO_SPACE_GENERIC_H_ */
