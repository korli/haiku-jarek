/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef SYSTEM_KERNEL_BSP_IO_SPACE_IO_SPACE_ARM_H_
#define SYSTEM_KERNEL_BSP_IO_SPACE_IO_SPACE_ARM_H_

#include "io_space_base.h"

namespace BoardSupportPackage {

class MemoryIOSpace final : public IOSpace {
	const addr_t fBaseAddress;
public:
	MemoryIOSpace(addr_t baseAddress) noexcept :
		fBaseAddress(baseAddress)
	{
	}

	virtual ~MemoryIOSpace() noexcept;

	virtual uint8 read_1(unsigned int offset) const noexcept override {
		uint8 result;
		__asm__ __volatile__("ldrb %0, [%1]" : "=r"(result) : "r"(fBaseAddress + offset));
		return result;
	}

	virtual uint16 read_2(unsigned int offset) const noexcept override {
		uint16 result;
		__asm__ __volatile__("ldrh %0, [%1]" : "=r"(result) : "r"(fBaseAddress + offset));
		return result;
	}

	virtual uint32 read_4(unsigned int offset) const noexcept override {
		uint32 result;
		__asm__ __volatile__("ldr %0, [%1]" : "=r"(result) : "r"(fBaseAddress + offset));
		return result;
	}

	virtual void write_1(unsigned int offset, uint8 value) const noexcept override {
		__asm__ __volatile__("strb %1, [%0]" :: "r"(fBaseAddress + offset), "r"(value) : "memory");
	}

	virtual void write_2(unsigned int offset, uint16 value) const noexcept override {
		__asm__ __volatile__("strh %1, [%0]" :: "r"(fBaseAddress + offset), "r"(value) : "memory");
	}

	virtual void write_4(unsigned int offset, uint32 value) const noexcept override {
		__asm__ __volatile__("str %1, [%0]" :: "r"(fBaseAddress + offset), "r"(value) : "memory");
	}

	virtual void read_1(unsigned int offset, uint8 * buffer, size_t count) const noexcept override {
		uint32 temp1, temp2, temp3, temp4;
		__asm__ __volatile__(
			"1:\n\t"
			"cmp %3, #0\n\t"
			"beq 2f\n\t"
			"ldrb %0, [%1]\n\t"
			"strb %0, [%2], #1\n\t"
			"sub %3, %3, #1\n\t"
			"b	1b\n"
			"2:"
			: "=r"(temp1), "=r"(temp2), "=r"(temp3), "=r"(temp4)
			: "1"(fBaseAddress + offset), "2"(buffer), "3"(count)
			: "memory", "cc");
	}

	virtual void read_2(unsigned int offset, uint16 * buffer, size_t count) const noexcept override {
		uint32 temp1, temp2, temp3, temp4;
		__asm__ __volatile__(
			"1:\n\t"
			"cmp %3, #0\n\t"
			"beq 2f\n\t"
			"ldrh %0, [%1]\n\t"
			"strh %0, [%2], #2\n\t"
			"sub %3, %3, #1\n\t"
			"b	1b\n"
			"2:"
			: "=r"(temp1), "=r"(temp2), "=r"(temp3), "=r"(temp4)
			: "1"(fBaseAddress + offset), "2"(buffer), "3"(count)
			: "memory", "cc");
	}

	virtual void read_4(unsigned int offset, uint32 * buffer, size_t count) const noexcept override {
		uint32 temp1, temp2, temp3, temp4;
		__asm__ __volatile__(
			"1:\n\t"
			"cmp %3, #0\n\t"
			"beq 2f\n\t"
			"ldr %0, [%1]\n\t"
			"str %0, [%2], #4\n\t"
			"sub %3, %3, #1\n\t"
			"b	1b\n"
			"2:"
			: "=r"(temp1), "=r"(temp2), "=r"(temp3), "=r"(temp4)
			: "1"(fBaseAddress + offset), "2"(buffer), "3"(count)
			: "memory", "cc");
	}

	virtual void write_1(unsigned int offset, const uint8 * buffer, size_t count) const noexcept override {
		uint32 temp1, temp2, temp3, temp4;
		__asm__ __volatile__(
			"1:\n\t"
			"cmp %3, #0\n\t"
			"beq 2f\n\t"
			"ldrb %0, [%2], #1\n\t"
			"strb %0, [%1]\n\t"
			"sub %3, %3, #1\n\t"
			"b	1b\n"
			"2:"
			: "=r"(temp1), "=r"(temp2), "=r"(temp3), "=r"(temp4)
			: "1"(fBaseAddress + offset), "2"(buffer), "3"(count)
			: "memory", "cc");
	}

	virtual void write_2(unsigned int offset, const uint16 * buffer, size_t count) const noexcept override {
		uint32 temp1, temp2, temp3, temp4;
		__asm__ __volatile__(
			"1:\n\t"
			"cmp %3, #0\n\t"
			"beq 2f\n\t"
			"ldrh %0, [%2], #2\n\t"
			"strh %0, [%1]\n\t"
			"sub %3, %3, #1\n\t"
			"b	1b\n"
			"2:"
			: "=r"(temp1), "=r"(temp2), "=r"(temp3), "=r"(temp4)
			: "1"(fBaseAddress + offset), "2"(buffer), "3"(count)
			: "memory", "cc");
	}

	virtual void write_4(unsigned int offset, const uint32 * buffer, size_t count) const noexcept override {
		uint32 temp1, temp2, temp3, temp4;
		__asm__ __volatile__(
			"1:\n\t"
			"cmp %3, #0\n\t"
			"beq 2f\n\t"
			"ldr %0, [%2], #4\n\t"
			"str %0, [%1]\n\t"
			"sub %3, %3, #1\n\t"
			"b	1b\n"
			"2:"
			: "=r"(temp1), "=r"(temp2), "=r"(temp3), "=r"(temp4)
			: "1"(fBaseAddress + offset), "2"(buffer), "3"(count)
			: "memory", "cc");
	}

	virtual void set_1(unsigned int offset, uint8 value, size_t count) const noexcept override {
		uint32 temp1, temp2, temp3;
		__asm__ __volatile__(
			"1:\n"
			"cmp %2, #0\n\t"
			"beq 2f\n\t"
			"strb %0, [%1]\n\t"
			"sub %2, %2, #1\n\t"
			"b 1b\n"
			"2:"
			: "=r"(temp1), "=r"(temp2), "=r"(temp3)
			: "1"(fBaseAddress + offset), "2"(count));
	}

	virtual void set_2(unsigned int offset, uint16 value, size_t count) const noexcept override {
		uint32 temp1, temp2, temp3;
		__asm__ __volatile__(
			"1:\n"
			"cmp %2, #0\n\t"
			"beq 2f\n\t"
			"strh %0, [%1]\n\t"
			"sub %2, %2, #1\n\t"
			"b 1b\n"
			"2:"
			: "=r"(temp1), "=r"(temp2), "=r"(temp3)
			: "1"(fBaseAddress + offset), "2"(count));
	}

	virtual void set_4(unsigned int offset, uint32 value, size_t count) const noexcept override {
		uint32 temp1, temp2, temp3;
		__asm__ __volatile__(
			"1:\n"
			"cmp %2, #0\n\t"
			"beq 2f\n\t"
			"str %0, [%1]\n\t"
			"sub %2, %2, #1\n\t"
			"b 1b\n"
			"2:"
			: "=r"(temp1), "=r"(temp2), "=r"(temp3)
			: "1"(fBaseAddress + offset), "2"(count));
	}

	virtual void read_region_1(unsigned int offset, uint8 * buffer, size_t count) const noexcept override {
		uint32 temp1, temp2, temp3, temp4;
		__asm__ __volatile__(
			"1:\n\t"
			"cmp %3, #0\n\t"
			"beq 2f\n\t"
			"ldrb %0, [%1], #1\n\t"
			"strb %0, [%2], #1\n\t"
			"sub %3, %3, #1\n\t"
			"b	1b\n"
			"2:"
			: "=r"(temp1), "=r"(temp2), "=r"(temp3), "=r"(temp4)
			: "1"(fBaseAddress + offset), "2"(buffer), "3"(count)
			: "memory", "cc");
	}

	virtual void read_region_2(unsigned int offset, uint16 * buffer, size_t count) const noexcept override {
		uint32 temp1, temp2, temp3, temp4;
		__asm__ __volatile__(
			"1:\n\t"
			"cmp %3, #0\n\t"
			"beq 2f\n\t"
			"ldrh %0, [%1], #2\n\t"
			"strh %0, [%2], #2\n\t"
			"sub %3, %3, #1\n\t"
			"b	1b\n"
			"2:"
			: "=r"(temp1), "=r"(temp2), "=r"(temp3), "=r"(temp4)
			: "1"(fBaseAddress + offset), "2"(buffer), "3"(count)
			: "memory", "cc");
	}

	virtual void read_region_4(unsigned int offset, uint32 * buffer, size_t count) const noexcept override{
		uint32 temp1, temp2, temp3, temp4;
		__asm__ __volatile__(
			"1:\n\t"
			"cmp %3, #0\n\t"
			"beq 2f\n\t"
			"ldr %0, [%1], #4\n\t"
			"str %0, [%2], #4\n\t"
			"sub %3, %3, #1\n\t"
			"b	1b\n"
			"2:"
			: "=r"(temp1), "=r"(temp2), "=r"(temp3), "=r"(temp4)
			: "1"(fBaseAddress + offset), "2"(buffer), "3"(count)
			: "memory", "cc");
	}

	virtual void write_region_1(unsigned int offset, const uint8 * buffer, size_t count) const noexcept override {
		uint32 temp1, temp2, temp3, temp4;
		__asm__ __volatile__(
			"1:\n\t"
			"cmp %3, #0\n\t"
			"beq 2f\n\t"
			"ldrb %0, [%2], #1\n\t"
			"strb %0, [%1], #1\n\t"
			"sub %3, %3, #1\n\t"
			"b	1b\n"
			"2:"
			: "=r"(temp1), "=r"(temp2), "=r"(temp3), "=r"(temp4)
			: "1"(fBaseAddress + offset), "2"(buffer), "3"(count)
			: "memory", "cc");
	}

	virtual void write_region_2(unsigned int offset, const uint16 * buffer, size_t count) const noexcept override {
		uint32 temp1, temp2, temp3, temp4;
		__asm__ __volatile__(
			"1:\n\t"
			"cmp %3, #0\n\t"
			"beq 2f\n\t"
			"ldrh %0, [%2], #2\n\t"
			"strh %0, [%1], #2\n\t"
			"sub %3, %3, #1\n\t"
			"b	1b\n"
			"2:"
			: "=r"(temp1), "=r"(temp2), "=r"(temp3), "=r"(temp4)
			: "1"(fBaseAddress + offset), "2"(buffer), "3"(count)
			: "memory", "cc");
	}

	virtual void write_region_4(unsigned int offset, const uint32 * buffer, size_t count) const noexcept override {
		uint32 temp1, temp2, temp3, temp4;
		__asm__ __volatile__(
			"1:\n\t"
			"cmp %3, #0\n\t"
			"beq 2f\n\t"
			"ldr %0, [%2], #4\n\t"
			"str %0, [%1], #4\n\t"
			"sub %3, %3, #1\n\t"
			"b	1b\n"
			"2:"
			: "=r"(temp1), "=r"(temp2), "=r"(temp3), "=r"(temp4)
			: "1"(fBaseAddress + offset), "2"(buffer), "3"(count)
			: "memory", "cc");
	}
};

}  // namespace BoardSupportPackage

#endif /* SYSTEM_KERNEL_BSP_IO_SPACE_IO_SPACE_AARCH64_H_ */
