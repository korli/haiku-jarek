/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef SYSTEM_KERNEL_BSP_IO_SPACE_IO_SPACE_X86_H_
#define SYSTEM_KERNEL_BSP_IO_SPACE_IO_SPACE_X86_H_

#include "io_space_generic.h"

namespace BoardSupportPackage {

class MemoryIOSpace final : public IOSpaceGeneric {
public:
	MemoryIOSpace(addr_t baseAddress) noexcept :
		IOSpaceGeneric(baseAddress)
	{
	}

	virtual ~MemoryIOSpace() noexcept;

	virtual void read_1(unsigned int offset, uint8 * buffer, size_t count) const noexcept override {
		__asm__ __volatile__(
				"cld\n"
				"1:\n\t"
				"movb (%2), %%al\n\t"
				"stosb\n\t"
				"loop 1b"
				: "=D"(buffer), "=c"(count)
				: "r"(fBaseAddress + offset), "0"(buffer), "1"(count)
				: "%eax", "memory", "cc");
	}

	virtual void read_2(unsigned int offset, uint16 * buffer, size_t count) const noexcept override {
		__asm__ __volatile__(
				"cld\n"
				"1:\n\t"
				"movw (%2), %%ax\n\t"
				"stosw\n\t"
				"loop 1b"
				: "=D"(buffer), "=c"(count)
				: "r"(fBaseAddress + offset), "0"(buffer), "1"(count)
				: "%eax", "memory", "cc");
	}

	virtual void read_4(unsigned int offset, uint32 * buffer, size_t count) const noexcept override {
		__asm__ __volatile__(
				"cld\n"
				"1:\n\t"
				"movl (%2), %%eax\n\t"
				"stosl\n\t"
				"loop 1b"
				: "=D"(buffer), "=c"(count)
				: "r"(fBaseAddress + offset), "0"(buffer), "1"(count)
				: "%eax", "memory", "cc");
	}

	virtual void write_1(unsigned int offset, const uint8 * buffer, size_t count) const noexcept override {
		addr_t addr = fBaseAddress + offset;
		__asm__ __volatile__(
				"cld\n"
				"1:\n\t"
				"lodsb\n\t"
				"movb %%al, (%2)\n\t"
				"loop 1b"
				: "=S"(buffer), "=c"(count)
				: "r"(addr), "0"(buffer), "1"(count)
				: "%eax", "memory", "cc");
	}

	virtual void write_2(unsigned int offset, const uint16 * buffer, size_t count) const noexcept override {
		addr_t addr = fBaseAddress + offset;
		__asm__ __volatile__(
				"cld\n"
				"1:\n\t"
				"lodsw\n\t"
				"movw %%ax, (%2)\n\t"
				"loop 1b"
				: "=S"(buffer), "=c"(count)
				: "r"(addr), "0"(buffer), "1"(count)
				: "%eax", "memory", "cc");
	}

	virtual void write_4(unsigned int offset, const uint32 * buffer, size_t count) const noexcept override {
		addr_t addr = fBaseAddress + offset;
		__asm__ __volatile__(
				"cld\n"
				"1:\n\t"
				"lodsl\n\t"
				"movl %%eax, (%2)\n\t"
				"loop 1b"
				: "=S"(buffer), "=c"(count)
				: "r"(addr), "0"(buffer), "1"(count)
				: "%eax", "memory", "cc");
	}

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

	virtual void read_region_1(unsigned int offset, uint8 * buffer, size_t count) const noexcept override {
		addr_t base = fBaseAddress + offset;
		__asm__ __volatile__(
				"cld; repne; movsb"
				: "=D"(buffer), "=c"(count), "=S"(base)
				: "0"(buffer), "1"(count), "2"(base)
				: "%eax", "memory", "cc");
	}

	virtual void read_region_2(unsigned int offset, uint16 * buffer, size_t count) const noexcept override {
		addr_t base = fBaseAddress + offset;
		__asm__ __volatile__(
				"cld; repne; movsw"
				: "=D"(buffer), "=c"(count), "=S"(base)
				: "0"(buffer), "1"(count), "2"(base)
				: "%eax", "memory", "cc");
	}

	virtual void read_region_4(unsigned int offset, uint32 * buffer, size_t count) const noexcept override{
		addr_t base = fBaseAddress + offset;
		__asm__ __volatile__(
				"cld; repne; movsl"
				: "=D"(buffer), "=c"(count), "=S"(base)
				: "0"(buffer), "1"(count), "2"(base)
				: "%eax", "memory", "cc");
	}

	virtual void write_region_1(unsigned int offset, const uint8 * buffer, size_t count) const noexcept override {
		addr_t base = fBaseAddress + offset;
		__asm__ __volatile__(
				"cld; repne; movsb"
				: "=S"(buffer), "=c"(count), "=D"(base)
				: "0"(buffer), "1"(count), "2"(base)
				: "%eax", "memory", "cc");
	}

	virtual void write_region_2(unsigned int offset, const uint16 * buffer, size_t count) const noexcept override {
		addr_t base = fBaseAddress + offset;
		__asm__ __volatile__(
				"cld; repne; movsw"
				: "=S"(buffer), "=c"(count), "=D"(base)
				: "0"(buffer), "1"(count), "2"(base)
				: "%eax", "memory", "cc");
	}

	virtual void write_region_4(unsigned int offset, const uint32 * buffer, size_t count) const noexcept override {
		addr_t base = fBaseAddress + offset;
		__asm__ __volatile__(
				"cld; repne; movsl"
				: "=S"(buffer), "=c"(count), "=D"(base)
				: "0"(buffer), "1"(count), "2"(base)
				: "%eax", "memory", "cc");
	}
};

class X86PortIOSpace final : public IOSpace
{
	const uint16 fBase;
public:
	X86PortIOSpace(uint16 base) noexcept :
		fBase(base)
	{
	}

	virtual ~X86PortIOSpace() noexcept;

	virtual uint8 read_1(unsigned int offset) const noexcept {
		uint8 data;
		__asm__ __volatile__("inb %%dx, %%al" : "=a" (data) : "d" (fBase + offset));
		return (data);
	}

	virtual uint16 read_2(unsigned int offset) const noexcept {
		uint16 data;
		__asm__ __volatile__("inw %%dx, %%ax" : "=a" (data) : "d" (fBase + offset));
		return (data);
	}

	virtual uint32 read_4(unsigned int offset) const noexcept {
		uint32 data;
		__asm__ __volatile__("inl %%dx, %%eax" : "=a" (data) : "d" (fBase + offset));
		return (data);
	}

#ifdef __HAIKU_ARCH_64_BIT
private:
	virtual uint64 read_8(unsigned int offset) const noexcept { abort(); return 0; }
public:
#endif

	virtual void write_1(unsigned int offset, uint8 value) const noexcept {
		__asm__ __volatile__("outb %%al, %%dx" : : "a" (value), "d" (fBase + offset));
	}

	virtual void write_2(unsigned int offset, uint16 value) const noexcept {
		__asm__ __volatile__("outw %%ax, %%dx" : : "a" (value), "d" (fBase + offset));
	}

	virtual void write_4(unsigned int offset, uint32 value) const noexcept {
		__asm__ __volatile__("outl %%eax, %%dx" : : "a" (value), "d" (fBase + offset));
	}

#ifdef __HAIKU_ARCH_64_BIT
private:
	virtual void write_8(unsigned int offset, uint64 value) const noexcept { abort(); }
public:
#endif

	virtual void read_1(unsigned int offset, uint8 * buffer, size_t count) const noexcept {
		uint16 port = fBase + offset;
		__asm__ __volatile__("cld; rep; insb"
				 : "+D" (buffer), "+c" (count)
				 : "d" (port)
				 : "memory");
	}

	virtual void read_2(unsigned int offset, uint16 * buffer, size_t count) const noexcept {
		uint16 port = fBase + offset;
		__asm__ __volatile__("cld; rep; insw"
				 : "+D" (buffer), "+c" (count)
				 : "d" (port)
				 : "memory");
	}

	virtual void read_4(unsigned int offset, uint32 * buffer, size_t count) const noexcept {
		uint16 port = fBase + offset;
		__asm__ __volatile__("cld; rep; insl"
				 : "+D" (buffer), "+c" (count)
				 : "d" (port)
				 : "memory");
	}

#ifdef __HAIKU_ARCH_64_BIT
private:
	virtual void read_8(unsigned int offset, uint64 * buffer, size_t count) const noexcept { abort(); }
public:
#endif

	virtual void write_1(unsigned int offset, const uint8 * buffer, size_t count) const noexcept {
		uint16 port = fBase + offset;
		__asm__ __volatile__("cld; rep; outsb"
				 : "+S" (buffer), "+c" (count)
				 : "d" (port));
	}

	virtual void write_2(unsigned int offset, const uint16 * buffer, size_t count) const noexcept {
		uint16 port = fBase + offset;
		__asm__ __volatile__("cld; rep; outsw"
				 : "+S" (buffer), "+c" (count)
				 : "d" (port));
	}

	virtual void write_4(unsigned int offset, const uint32 * buffer, size_t count) const noexcept {
		uint16 port = fBase + offset;
		__asm__ __volatile__("cld; rep; outsl"
				 : "+S" (buffer), "+c" (count)
				 : "d" (port));
	}

	virtual void write_8(unsigned int offset, const uint64 * buffer, size_t count) const noexcept { abort(); }

	virtual void set_1(unsigned int offset, uint8 value, size_t count) const noexcept {
		while(count-->0)
			write_1(offset, value);
	}

	virtual void set_2(unsigned int offset, uint16 value, size_t count) const noexcept {
		while(count-->0)
			write_2(offset, value);
	}

	virtual void set_4(unsigned int offset, uint32 value, size_t count) const noexcept {
		while(count-->0)
			write_4(offset, value);
	}

#ifdef __HAIKU_ARCH_64_BIT
private:
	virtual void set_8(unsigned int offset, uint64 value, size_t count) const noexcept { abort(); }\
public:
#endif

	virtual void read_region_1(unsigned int offset, uint8 * buffer, size_t count) const noexcept {
		uint16 port = fBase + offset;
		__asm__ __volatile__("				\n\
			cld					\n\
		1:	inb %%dx,%%al				\n\
			stosb					\n\
			incl %%edx				\n\
			loop 1b"				:
		    "=D" (buffer), "=c" (count), "=d" (port)	:
		    "0" (buffer), "1" (count), "2" (port)	:
		    "%eax", "memory", "cc");
	}

	virtual void read_region_2(unsigned int offset, uint16 * buffer, size_t count) const noexcept {
		uint16 port = fBase + offset;
		__asm__ __volatile__("				\n\
			cld					\n\
		1:	inw %%dx,%%ax				\n\
			stosw					\n\
			addl $2,%%edx				\n\
			loop 1b"				:
		    "=D" (buffer), "=c" (count), "=d" (port)	:
		    "0" (buffer), "1" (count), "2" (port)	:
		    "%eax", "memory", "cc");
	}

	virtual void read_region_4(unsigned int offset, uint32 * buffer, size_t count) const noexcept {
		uint16 port = fBase + offset;
		__asm__ __volatile__("				\n\
			cld					\n\
		1:	inl %%dx,%%eax				\n\
			stosl					\n\
			addl $4,%%edx			\n\
			loop 1b"				:
		    "=D" (buffer), "=c" (count), "=d" (port)	:
		    "0" (buffer), "1" (count), "2" (port)	:
		    "%eax", "memory", "cc");
	}

#ifdef __HAIKU_ARCH_64_BIT
private:
	virtual void read_region_8(unsigned int offset, uint64 * buffer, size_t count) const noexcept { abort(); }
public:
#endif

	virtual void write_region_1(unsigned int offset, const uint8 * buffer, size_t count) const noexcept {
		uint16 port = fBase + offset;
		__asm__ __volatile__("				\n\
			cld					\n\
		1:	lodsb					\n\
			outb %%al,%%dx				\n\
			incl %%edx					\n\
			loop 1b"				:
		    "=d" (port), "=S" (buffer), "=c" (count)	:
		    "0" (port), "1" (buffer), "2" (count)	:
		    "%eax", "memory", "cc");
	}

	virtual void write_region_2(unsigned int offset, const uint16 * buffer, size_t count) const noexcept {
		uint16 port = fBase + offset;
		__asm__ __volatile__("				\n\
			cld					\n\
		1:	lodsw					\n\
			outw %%ax,%%dx				\n\
			addl $2,%%edx				\n\
			loop 1b"				:
		    "=d" (port), "=S" (buffer), "=c" (count)	:
		    "0" (port), "1" (buffer), "2" (count)	:
		    "%eax", "memory", "cc");
	}

	virtual void write_region_4(unsigned int offset, const uint32 * buffer, size_t count) const noexcept {
		uint16 port = fBase + offset;
		__asm__ __volatile__("				\n\
			cld					\n\
		1:	lodsl					\n\
			outl %%eax,%%dx				\n\
			addl $4,%%edx				\n\
			loop 1b"				:
		    "=d" (port), "=S" (buffer), "=c" (count)	:
		    "0" (port), "1" (buffer), "2" (count)	:
		    "%eax", "memory", "cc");
	}

#ifdef __HAIKU_ARCH_64_BIT
private:
	virtual void write_region_8(unsigned int offset, const uint64 * buffer, size_t count) const noexcept { abort(); }
#endif
};

}  // namespace BoardSupportPackage

#endif /* SYSTEM_KERNEL_BSP_IO_SPACE_IO_SPACE_X86_H_ */
