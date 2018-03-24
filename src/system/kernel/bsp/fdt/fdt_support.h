/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef SYSTEM_KERNEL_BSP_FDT_FDT_SUPPORT_H_
#define SYSTEM_KERNEL_BSP_FDT_FDT_SUPPORT_H_

#include <kernel/OS.h>
#include <kernel/boot/addr_range.h>

#include <memory>
#include <iterator>

namespace fdt {

struct Node;

#define	FDT_INTR_EDGE_RISING	1
#define	FDT_INTR_EDGE_FALLING	2
#define	FDT_INTR_LEVEL_HIGH	4
#define	FDT_INTR_LEVEL_LOW	8
#define	FDT_INTR_LOW_MASK	(FDT_INTR_EDGE_FALLING | FDT_INTR_LEVEL_LOW)
#define	FDT_INTR_EDGE_MASK	(FDT_INTR_EDGE_RISING | FDT_INTR_EDGE_FALLING)
#define	FDT_INTR_MASK		0xf

struct Property
{
public:
	inline Property(const void * fdt, int handle) noexcept :
		fFDT(fdt),
		fHandle(handle)
	{
	}

	inline Property() noexcept :
		fFDT(nullptr),
		fHandle(-1)
	{
	}

	inline bool operator == (const Property& other) const noexcept
	{
		return fHandle == other.fHandle;
	}

	inline bool operator != (const Property& other) const noexcept
	{
		return fHandle == other.fHandle;
	}

	inline bool IsNull() const noexcept
	{
		return fHandle < 0;
	}

	int Length() const noexcept;
	inline int CellLength() const noexcept { int len = Length(); return len < 0 ? -1 : len / sizeof(uint32); }
	const char * Name() const noexcept;
	const void * Data() const noexcept;
	uint32 EncodedGet(int index) const noexcept;
	uint64 DataGet(int index, int cells) const noexcept;
	Property Next() const noexcept;
	bool DataToRes(int index, int addressCells, int sizeCells, uint64& start, uint64& size) const noexcept;

private:
	inline const void * _Handle() const noexcept {
		return reinterpret_cast<const void *>(reinterpret_cast<const char *>(fFDT) + fHandle);
	}

	const void * fFDT;
	int fHandle;
};

struct PropertyList
{
public:
	struct const_iterator {
		typedef std::forward_iterator_tag				iterator_category;
	    typedef const_iterator                          iterator_type;
	    typedef std::ptrdiff_t						    difference_type;
	    typedef const Property *                        pointer;
	    typedef Property						        value_type;
	    typedef const Property&                         reference;

	    inline const_iterator() noexcept { }
	    inline explicit const_iterator(const Property& prop) noexcept : fProperty(prop) { }

	    inline reference operator*() const noexcept { return fProperty; }
	    inline pointer operator->() const noexcept { return &fProperty; }
	    inline iterator_type& operator++() noexcept { fProperty = fProperty.Next(); return *this; }
	    inline iterator_type operator++(int) noexcept { return iterator_type(fProperty.Next()); }
	    inline bool operator==(const iterator_type& __x) noexcept { return fProperty == __x.fProperty; }
	    inline bool operator!=(const iterator_type& __x) noexcept { return fProperty != __x.fProperty; }

	private:
		Property	fProperty;
	};

	inline PropertyList(const void * fdt = nullptr, int handle = -1) noexcept :
		fFDT(fdt), fNodeHandle(handle)
	{
	}

	const_iterator begin() const noexcept;
	const_iterator cbegin() const noexcept;
	const_iterator end() const noexcept { return const_iterator(); }
	const_iterator cend() const noexcept { return const_iterator(); }

private:
	const void * fFDT;
	int fNodeHandle;
};

struct Node
{
public:
	inline Node() noexcept :
		fFDT(nullptr),
		fHandle(-1),
		fParentHandle(-1)
	{
	}

	inline explicit Node(const void * fdt, int handle = 0, int phandle = -2) noexcept :
		fFDT(fdt),
		fHandle(handle),
		fParentHandle(phandle),
		fProperties(fdt, handle)
	{
	}

	inline const PropertyList& Properties() const noexcept {
		return fProperties;
	}

	bool IsDTBValid() const noexcept;
	size_t DeviceTreeSize() const noexcept;

	Node Parent() const noexcept;
	Node FirstChild() const noexcept;
	Node NextSibling() const noexcept;
	Node NextNode() const noexcept;

	const char * Name() const noexcept;
	Property GetProperty(const char * name) const noexcept;
	Property SearchProperty(const char * name) const noexcept;
	Node GetPath(const char * path) const noexcept;

	inline bool operator == (const Node& other) const noexcept { return fHandle == other.fHandle; }
	inline bool operator != (const Node& other) const noexcept { return fHandle != other.fHandle; }

	struct const_iterator;

	inline const_iterator begin() const noexcept;
	inline const_iterator cbegin() const noexcept;
	inline const_iterator end() const noexcept;
	inline const_iterator cend() const noexcept;

	inline bool IsNull() const noexcept { return fHandle < 0; }

	int ParentAddressCells() const noexcept;
	bool AddressSizeCells(int& addressCells, int& sizeCells) const noexcept;
	bool RegSize(int range_id, uint64& base, uint64& size) const noexcept;
	bool GetRange(int range_id, uint64& base, uint64& size) const noexcept;
	bool RegToRanges(addr_range * ranges, uint32& countRanges, uint32 maxRanges, bool merge = false) const noexcept;
	bool IsCompatible(const char ** names) const noexcept;
	bool IsCompatible(const char * name) const noexcept;
	bool GetMemoryRegions(addr_range * range, uint32& countRanges, uint32 maxRanges) const noexcept;
	bool GetReservedRegions(addr_range * ranges, uint32& countRanges, uint32 maxRanges) const noexcept;
	const char * GetBootArgs() const noexcept;

	uint32 GetPHandle() const noexcept;
	Node FromPHandle(uint32 node) const noexcept;
	uint32 FindInterruptParent() const noexcept;

	Node FindCompatibleNode(const char * name) const noexcept;

	inline int NativeHandle() const { return fHandle; }
	inline const void * DeviceTree() const { return fFDT; }

private:
	int GetRangeByBusAddress(uint64 addr, uint64& base, uint64& size) const noexcept;

private:
	const void *			fFDT;
	int						fHandle;
	int						fParentHandle;
	PropertyList			fProperties;
};

struct Node::const_iterator {
	typedef std::forward_iterator_tag			iterator_category;
    typedef const_iterator                      iterator_type;
    typedef std::ptrdiff_t						difference_type;
    typedef const Node *                        pointer;
    typedef Node						        value_type;
    typedef const Node&                         reference;

    inline const_iterator() noexcept { }
    inline explicit const_iterator(const Node& f) noexcept : fNode(f) { }

    inline reference operator*() const noexcept { return fNode; }
    inline pointer operator->() const noexcept { return &fNode; }
    inline iterator_type& operator++() noexcept { fNode = fNode.NextSibling(); return *this; }
    inline iterator_type operator++(int) noexcept { return iterator_type(fNode.NextSibling()); }
    inline bool operator==(const iterator_type& __x) noexcept { return fNode == __x.fNode; }
    inline bool operator!=(const iterator_type& __x) noexcept { return fNode != __x.fNode; }

private:
    Node	fNode;
};

inline Node::const_iterator Node::begin() const noexcept { return const_iterator(FirstChild()); }
inline Node::const_iterator Node::cbegin() const noexcept { return const_iterator(FirstChild()); }
inline Node::const_iterator Node::end() const noexcept { return const_iterator(); }
inline Node::const_iterator Node::cend() const noexcept { return const_iterator(); }

}  // namespace fdt

#endif /* SYSTEM_KERNEL_BSP_FDT_FDT_SUPPORT_H_ */
