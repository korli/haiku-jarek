/*
 * Copyright 2017, Jaroslaw Pelczar
 * Distributed under the terms of the MIT License.
 */

#include "fdt_support.h"
#include <kernel/kernel.h>
#include <cerrno>

extern "C" {
#include "libfdt/libfdt.h"
}

namespace fdt {

int Property::Length() const noexcept
{
	if(!fFDT)
		return -1;
	const struct fdt_property * property = fdt_get_property_by_offset(fFDT, fHandle, nullptr);
	return property ? (int)fdt32_to_cpu(property->len) : -1;
}

const char * Property::Name() const noexcept
{
	if(!fFDT)
		return nullptr;
	const struct fdt_property * property = fdt_get_property_by_offset(fFDT, fHandle, nullptr);
	return property ? fdt_string(fFDT, fdt32_to_cpu(property->nameoff)) : nullptr;
}

const void * Property::Data() const noexcept
{
	if(!fFDT)
		return nullptr;
	const struct fdt_property * property = fdt_get_property_by_offset(fFDT, fHandle, nullptr);
	return property ? property->data : nullptr;
}

uint32 Property::EncodedGet(int index) const noexcept
{
	if(!fFDT)
		return 0;
	const struct fdt_property * property = fdt_get_property_by_offset(fFDT, fHandle, nullptr);
	return property ? fdt32_to_cpu(((const uint32 *)property->data)[index]) : 0;
}

uint64 Property::DataGet(int index, int cells) const noexcept
{
	if(!fFDT)
		return 0;
	const struct fdt_property * property = fdt_get_property_by_offset(fFDT, fHandle, nullptr);
	if(!property)
		return 0;
	if(cells == 1)
		return fdt32_to_cpu(((const uint32 *)property->data)[index]);
	return fdt64_to_cpu(*(const uint64 *)(((const uint32 *)property->data) + index));
}

Property Property::Next() const noexcept
{
	if(!fFDT)
		return Property();
	int nextOffset = fdt_next_property_offset(fFDT, fHandle);
	return nextOffset < 0 ? Property() : Property(fFDT, nextOffset);
}

bool Property::DataToRes(int index, int addressCells, int sizeCells, uint64& start, uint64& size) const noexcept
{
	if(addressCells > 2 || sizeCells > 2)
		return false;
	start = DataGet(index, addressCells);
	size = DataGet(index + addressCells, sizeCells);
	return true;
}

PropertyList::const_iterator PropertyList::begin() const noexcept
{
	if(!fFDT)
		return const_iterator();
	int offset = fdt_first_property_offset(fFDT, fNodeHandle);
	return offset < 0 ? const_iterator() : const_iterator(Property(fFDT, offset));
}

PropertyList::const_iterator PropertyList::cbegin() const noexcept
{
	if(!fFDT)
		return const_iterator();
	int offset = fdt_first_property_offset(fFDT, fNodeHandle);
	return offset < 0 ? const_iterator() : const_iterator(Property(fFDT, offset));
}

Node Node::Parent() const noexcept
{
	if(!fFDT)
		return Node();
	if(fParentHandle == -1)
		return Node();
	if(fParentHandle >= 0)
		return Node(fFDT, fParentHandle, -2);
	int parentOffset = fdt_parent_offset(fFDT, fHandle);
	if(parentOffset < 0)
		return Node();
	return Node(fFDT, parentOffset, -2);
}

Node Node::FirstChild() const noexcept
{
	if(!fFDT)
		return Node();
	int childOffset = fdt_first_subnode(fFDT, fHandle);
	return childOffset < 0 ? Node() : Node(fFDT, childOffset, fHandle);
}

Node Node::NextSibling() const noexcept
{
	if(!fFDT)
		return Node();
	int nextOffset = fdt_next_subnode(fFDT, fHandle);
	return nextOffset < 0 ? Node() : Node(fFDT, nextOffset, fParentHandle);
}

Node Node::NextNode() const noexcept
{
	if(!fFDT)
		return Node();
	int nextOffset = fdt_next_node(fFDT, fHandle, nullptr);
	return nextOffset < 0 ? Node() : Node(fFDT, nextOffset, -2);
}

const char * Node::Name() const noexcept
{
	if(!fFDT)
		return nullptr;
	return fdt_get_name(fFDT, fHandle, nullptr);
}

Property Node::GetProperty(const char * name) const noexcept
{
	if(!fFDT)
		return Property();

	for (int offset = fdt_first_property_offset(fFDT, fHandle);
	     (offset >= 0);
	     (offset = fdt_next_property_offset(fFDT, offset)))
	{
		const struct fdt_property *prop;

		if (!(prop = fdt_get_property_by_offset(fFDT, offset, nullptr))) {
			return Property();
		}

		const char * f_name = fdt_string(fFDT, fdt32_to_cpu(prop->nameoff));

		if(!strcmp(name, f_name))
			return Property(fFDT, offset);
	}

	return Property();
}

Property Node::SearchProperty(const char * name) const noexcept
{
	Node self(*this);
	while(!self.IsNull()) {
		Property property(self.GetProperty(name));
		if(!property.IsNull())
			return property;
		self = self.Parent();
	}
	return Property();
}

Node Node::GetPath(const char * path) const noexcept
{
	if(!fFDT)
		return Node();

	int offset = fdt_path_offset(fFDT, path);
	return offset < 0 ? Node() : Node(fFDT, offset, -2);
}

int Node::ParentAddressCells() const noexcept
{
	Property property(Parent().SearchProperty("#address-cells"));
	if(property.IsNull() || property.CellLength() < 1)
		return 2;
	return property.EncodedGet(0);
}

bool Node::AddressSizeCells(int& addressCells, int& sizeCells) const noexcept
{
	addressCells = fdt_address_cells(fFDT, fHandle);
	sizeCells = fdt_size_cells(fFDT, fHandle);

	return addressCells > 0 && sizeCells >= 0;
}

bool Node::RegSize(int range_id, uint64& base, uint64& size) const noexcept
{
	int addressCells, sizeCells;

	if(!Parent().AddressSizeCells(addressCells, sizeCells))
		return false;

	Property reg(GetProperty("reg"));

	if(reg.IsNull() || reg.CellLength() < (addressCells + sizeCells))
		return false;

	base = reg.DataGet(0, addressCells);
	size = reg.DataGet(addressCells, sizeCells);

	return true;
}

int Node::GetRangeByBusAddress(uint64 addr, uint64& base, uint64& size) const noexcept
{
	if(IsNull()) {
		base = 0;
		size = ~0UL;
		return 0;
	}

	int addressCells, sizeCells;

	if(!AddressSizeCells(addressCells, sizeCells))
		return ENXIO;

	int par_addr_cells = ParentAddressCells();

	if(par_addr_cells > 2)
		return ERANGE;

	Property ranges(GetProperty("ranges"));

	if(ranges.IsNull())
		return -1;

	if(ranges.CellLength() == 0) {
		return Parent().GetRangeByBusAddress(addr, base, size);
	}

	int tuple_size = addressCells + par_addr_cells + sizeCells;
	int tuples = ranges.CellLength() / tuple_size;

	if (par_addr_cells > 2 || addressCells > 2 || sizeCells > 2)
		return ERANGE;

	base = 0;
	size = 0;

	for (int i = 0; i < tuples; i++)
	{
		int range_index = i * tuple_size;

		uint64 bus_addr = ranges.DataGet(range_index, addressCells);

		if (bus_addr != addr)
			continue;

		range_index += addressCells;

		uint64 par_bus_addr = ranges.DataGet(range_index, par_addr_cells);
		range_index += par_addr_cells;

		uint64 pbase, psize;
		int err = Parent().GetRangeByBusAddress(par_bus_addr, pbase, psize);

		if (err > 0)
			return (err);
		if (err == 0)
			base = pbase;
		else
			base = par_bus_addr;

		size = ranges.DataGet(range_index, sizeCells);

		return (0);
	}

	return EINVAL;
}

bool Node::GetRange(int range_id, uint64& base, uint64& size) const noexcept
{
	int addressCells, sizeCells;

	if(!AddressSizeCells(addressCells, sizeCells))
		return false;

	int par_addr_cells = ParentAddressCells();

	if(par_addr_cells > 2)
		return false;

	Property prop(GetProperty("ranges"));

	if(prop.Length() < 0)
		return false;

	if(prop.Length() == 0) {
		base = 0;
		size = ~0UL;
		return true;
	}

	if(range_id >= prop.CellLength())
		return false;

	if (par_addr_cells > 2 || addressCells > 2 || sizeCells > 2)
		return false;

	base = prop.DataGet(range_id, addressCells);
	uint64 par_bus_addr = prop.DataGet(range_id + addressCells, par_addr_cells);

	uint64 pbase, psize;
	int err = Parent().GetRangeByBusAddress(par_bus_addr, pbase, psize);

	if(err == 0)
		base += pbase;
	else
		base += par_bus_addr;

	size = prop.DataGet(range_id + addressCells + par_addr_cells, sizeCells);

	return true;
}

bool Node::RegToRanges(addr_range * ranges, uint32& countRanges, uint32 maxRanges, bool merge) const noexcept
{
	int addressCells, sizeCells;
	uint64 busAddress, busSize;

	if(!Parent().AddressSizeCells(addressCells, sizeCells))
		return false;

	if(!Parent().GetRange(0, busAddress, busSize)) {
		busAddress = 0;
		busSize = 0;
	}

	int tupleSize = addressCells + sizeCells;
	Property reg(GetProperty("reg"));
	if(reg.IsNull())
		return false;
	int tuples = reg.CellLength() / tupleSize;
	if(tuples <= 0)
		return false;

	int index = 0;
	for(int i = 0 ; i < tuples ; ++i) {
		uint64 start, count;

		if(!reg.DataToRes(index, addressCells, sizeCells, start, count)) {
			return false;
		}

		index += addressCells + sizeCells;

		start += busAddress;

		if(merge) {
			if(insert_address_range(ranges, &countRanges, maxRanges, start, count) != 0)
				return false;
		} else {
			if(countRanges >= maxRanges)
				return false;
			ranges[countRanges].start = start;
			ranges[countRanges].size = count;
			++countRanges;
		}
	}

	return true;
}

bool Node::IsCompatible(const char ** names) const noexcept
{
	if(!fFDT || fHandle < 0)
		return false;
	for(int i = 0 ; names[i] ; ++i)
		if(fdt_node_check_compatible(fFDT, fHandle, names[i]) == 0)
			return true;
	return false;
}

bool Node::IsCompatible(const char * name) const noexcept
{
	if(!fFDT || fHandle < 0)
		return false;
	if(fdt_node_check_compatible(fFDT, fHandle, name) == 0)
		return true;
	return false;
}

Node Node::FindCompatibleNode(const char * name) const noexcept
{
	if(IsNull())
		return Node();
	int offset = fdt_node_offset_by_compatible(fFDT, fHandle, name);
	if(offset < 0)
		return Node();
	return Node(fFDT, offset);
}

uint32 Node::GetPHandle() const noexcept
{
	if(IsNull())
		return 0;
	return fdt_get_phandle(fFDT, fHandle);
}

Node Node::FromPHandle(uint32 node) const noexcept
{
	if(IsNull())
		return Node();
	int nodeOffset = fdt_node_offset_by_phandle(fFDT, node);
	return nodeOffset < 0 ? Node() : Node(fFDT, nodeOffset, -2);
}

uint32 Node::FindInterruptParent() const noexcept
{
	Property property(SearchProperty("interrupt-parent"));
	if(property.IsNull()) {
		Node node;
		for(node = Parent() ; !node.IsNull() ; node = node.Parent()) {
			if(!node.GetProperty("interrupt-controller").IsNull())
				break;
		}
		return node.GetPHandle();
	}
	return property.EncodedGet(0);
}

bool Node::GetMemoryRegions(addr_range * range, uint32& countRanges, uint32 maxRanges) const noexcept
{
	int addressCells, sizeCells;

	Node memory(GetPath("/memory"));
	if(memory.IsNull())
		return false;

	if(!memory.Parent().AddressSizeCells(addressCells, sizeCells))
		return false;

	if(addressCells > 2)
		return false;

	Property reg(memory.GetProperty("reg"));
	if(reg.IsNull())
		return false;

	int tupleSize = addressCells + sizeCells;
	int regLen = reg.CellLength() / tupleSize;
	int index = 0;

	for(int i = 0 ; i < regLen ; ++i, index += tupleSize) {
		uint64 start, size;
		if(!reg.DataToRes(index, addressCells, sizeCells, start, size))
			return false;
		if(insert_address_range(range, &countRanges, maxRanges, start, size) != 0)
			return false;
	}

	return true;
}

bool Node::GetReservedRegions(addr_range * ranges, uint32& countRanges, uint32 maxRanges) const noexcept
{
	int addressCells, sizeCells;

	Node memory(GetPath("/memory"));
	if(memory.IsNull())
		return false;

	if(!memory.Parent().AddressSizeCells(addressCells, sizeCells))
		return false;

	if(addressCells > 2)
		return false;

	for(int n = 0  ; ; ++n) {
		uint64_t base, size;
		fdt_get_mem_rsv(this->fFDT, n, &base, &size);
		if(!size)
			break;
		if(countRanges >= maxRanges)
			return false;
		ranges[countRanges].start = base;
		ranges[countRanges].size = size;
		++countRanges;
	}

	Property memreserve(GetPath("/").GetProperty("memreserve"));

	if(memreserve.IsNull())
		return true;

	int tupleSize = addressCells + sizeCells;
	int regLen = memreserve.CellLength();
	int index = 0;

	for(int i = 0 ; i < regLen ; ++i, index += tupleSize) {
		uint64 start, size;
		if(!memreserve.DataToRes(index, addressCells, sizeCells, start, size))
			return false;
		if(countRanges >= maxRanges)
			return false;
		ranges[countRanges].start = start;
		ranges[countRanges].size = size;
		++countRanges;
	}

	return true;
}

const char * Node::GetBootArgs() const noexcept
{
	return (const char *)GetPath("/chosen").GetProperty("bootargs").Data();
}

}  // namespace fdt

#ifndef _BOOT_MODE
static void
remove_range_index(addr_range* ranges, uint32& numRanges, uint32 index)
{
	if (index + 1 == numRanges) {
		// remove last range
		numRanges--;
		return;
	}

	memmove(&ranges[index], &ranges[index + 1],
		sizeof(addr_range) * (numRanges - 1 - index));
	numRanges--;
}

extern "C" status_t
remove_address_range(addr_range* ranges, uint32* _numRanges, uint32 maxRanges,
	uint64 start, uint64 size)
{
	uint32 numRanges = *_numRanges;

	uint64 end = ROUNDUP(start + size, B_PAGE_SIZE);
	start = ROUNDDOWN(start, B_PAGE_SIZE);

	for (uint32 i = 0; i < numRanges; i++) {
		uint64 rangeStart = ranges[i].start;
		uint64 rangeEnd = rangeStart + ranges[i].size;

		if (start <= rangeStart) {
			if (end <= rangeStart) {
				// no intersection
			} else if (end >= rangeEnd) {
				// remove the complete range
				remove_range_index(ranges, numRanges, i);
				i--;
			} else {
				// remove the head of the range
				ranges[i].start = end;
				ranges[i].size = rangeEnd - end;
			}
		} else if (end >= rangeEnd) {
			if (start < rangeEnd) {
				// remove the tail
				ranges[i].size = start - rangeStart;
			}	// else: no intersection
		} else {
			// rangeStart < start < end < rangeEnd
			// The ugly case: We have to remove something from the middle of
			// the range. We keep the head of the range and insert its tail
			// as a new range.
			ranges[i].size = start - rangeStart;
			return insert_address_range(ranges, _numRanges, maxRanges, end,
				rangeEnd - end);
		}
	}

	*_numRanges = numRanges;
	return B_OK;
}

extern "C" status_t
insert_address_range(addr_range* ranges, uint32* _numRanges, uint32 maxRanges,
	uint64 start, uint64 size)
{
	uint32 numRanges = *_numRanges;

	start = ROUNDDOWN(start, B_PAGE_SIZE);
	size = ROUNDUP(size, B_PAGE_SIZE);
	uint64 end = start + size;

	for (uint32 i = 0; i < numRanges; i++) {
		uint64 rangeStart = ranges[i].start;
		uint64 rangeEnd = rangeStart + ranges[i].size;

		if (end < rangeStart || start > rangeEnd) {
			// ranges don't intersect or touch each other
			continue;
		}
		if (start >= rangeStart && end <= rangeEnd) {
			// range is already completely covered
			return B_OK;
		}

		if (start < rangeStart) {
			// prepend to the existing range
			ranges[i].start = start;
			ranges[i].size += rangeStart - start;
		}
		if (end > ranges[i].start + ranges[i].size) {
			// append to the existing range
			ranges[i].size = end - ranges[i].start;
		}

		// join ranges if possible

		for (uint32 j = 0; j < numRanges; j++) {
			if (i == j)
				continue;

			rangeStart = ranges[i].start;
			rangeEnd = rangeStart + ranges[i].size;
			uint64 joinStart = ranges[j].start;
			uint64 joinEnd = joinStart + ranges[j].size;

			if (rangeStart <= joinEnd && joinEnd <= rangeEnd) {
				// join range that used to be before the current one, or
				// the one that's now entirely included by the current one
				if (joinStart < rangeStart) {
					ranges[i].size += rangeStart - joinStart;
					ranges[i].start = joinStart;
				}

				remove_range_index(ranges, numRanges, j--);
			} else if (joinStart <= rangeEnd && joinEnd > rangeEnd) {
				// join range that used to be after the current one
				ranges[i].size += joinEnd - rangeEnd;

				remove_range_index(ranges, numRanges, j--);
			}
		}

		*_numRanges = numRanges;
		return B_OK;
	}

	// no range matched, we need to create a new one

	if (numRanges >= maxRanges)
		return B_ENTRY_NOT_FOUND;

	ranges[numRanges].start = (uint64)start;
	ranges[numRanges].size = size;
	(*_numRanges)++;

	return B_OK;
}
#endif
