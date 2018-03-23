/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef SYSTEM_KERNEL_BSP_IO_SPACE_IO_SPACE_BASE_H_
#define SYSTEM_KERNEL_BSP_IO_SPACE_IO_SPACE_BASE_H_

#include <kernel/OS.h>

namespace BoardSupportPackage {

enum class IOBarrier {
	READ,
	WRITE,
	READ_WRITE
};

class IOSpace
{
public:
	virtual ~IOSpace() noexcept;

	virtual uint8 read_1(unsigned int offset) const noexcept = 0;
	virtual uint16 read_2(unsigned int offset) const noexcept = 0;
	virtual uint32 read_4(unsigned int offset) const noexcept = 0;
#ifdef __HAIKU_ARCH_64_BIT
	virtual uint64 read_8(unsigned int offset) const noexcept = 0;
#endif

	virtual void write_1(unsigned int offset, uint8 value) const noexcept = 0;
	virtual void write_2(unsigned int offset, uint16 value) const noexcept = 0;
	virtual void write_4(unsigned int offset, uint32 value) const noexcept = 0;
#ifdef __HAIKU_ARCH_64_BIT
	virtual void write_8(unsigned int offset, uint64 value) const noexcept = 0;
#endif

	virtual void read_1(unsigned int offset, uint8 * buffer, size_t count) const noexcept = 0;
	virtual void read_2(unsigned int offset, uint16 * buffer, size_t count) const noexcept = 0;
	virtual void read_4(unsigned int offset, uint32 * buffer, size_t count) const noexcept = 0;
#ifdef __HAIKU_ARCH_64_BIT
	virtual void read_8(unsigned int offset, uint64 * buffer, size_t count) const noexcept = 0;
#endif

	virtual void write_1(unsigned int offset, const uint8 * buffer, size_t count) const noexcept = 0;
	virtual void write_2(unsigned int offset, const uint16 * buffer, size_t count) const noexcept = 0;
	virtual void write_4(unsigned int offset, const uint32 * buffer, size_t count) const noexcept = 0;
#ifdef __HAIKU_ARCH_64_BIT
	virtual void write_8(unsigned int offset, const uint64 * buffer, size_t count) const noexcept = 0;
#endif

	virtual void set_1(unsigned int offset, uint8 value, size_t count) const noexcept = 0;
	virtual void set_2(unsigned int offset, uint16 value, size_t count) const noexcept = 0;
	virtual void set_4(unsigned int offset, uint32 value, size_t count) const noexcept = 0;
#ifdef __HAIKU_ARCH_64_BIT
	virtual void set_8(unsigned int offset, uint64 value, size_t count) const noexcept = 0;
#endif

	virtual void read_region_1(unsigned int offset, uint8 * buffer, size_t count) const noexcept = 0;
	virtual void read_region_2(unsigned int offset, uint16 * buffer, size_t count) const noexcept = 0;
	virtual void read_region_4(unsigned int offset, uint32 * buffer, size_t count) const noexcept = 0;
#ifdef __HAIKU_ARCH_64_BIT
	virtual void read_region_8(unsigned int offset, uint64 * buffer, size_t count) const noexcept = 0;
#endif

	virtual void write_region_1(unsigned int offset, const uint8 * buffer, size_t count) const noexcept = 0;
	virtual void write_region_2(unsigned int offset, const uint16 * buffer, size_t count) const noexcept = 0;
	virtual void write_region_4(unsigned int offset, const uint32 * buffer, size_t count) const noexcept = 0;
#ifdef __HAIKU_ARCH_64_BIT
	virtual void write_region_8(unsigned int offset, const uint64 * buffer, size_t count) const noexcept = 0;
#endif

	virtual void io_barrier(IOBarrier barrier) { }
};

}  // namespace BoardSupportPackage

#endif /* SYSTEM_KERNEL_BSP_IO_SPACE_IO_SPACE_BASE_H_ */
