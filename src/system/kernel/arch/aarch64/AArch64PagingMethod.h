/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef SYSTEM_KERNEL_ARCH_AARCH64_AARCH64PAGINGMETHOD_H_
#define SYSTEM_KERNEL_ARCH_AARCH64_AARCH64PAGINGMETHOD_H_

#include <SupportDefs.h>
#include <vm/vm_types.h>
#include <atomic>

struct kernel_args;
struct VMPhysicalPageMapper;
struct VMTranslationMap;

class AArch64PhysicalPageMapper;
class TranslationMapPhysicalPageMapper;

struct AArch64PagingMethod {
	static status_t Init(kernel_args * args, VMPhysicalPageMapper ** _mapper);
	static status_t InitPostArea(kernel_args * args);
	static status_t CreateTranslationMap(bool kernel, VMTranslationMap ** _map);
	static status_t MapEarly(kernel_args* args, addr_t virtualAddress,
			phys_addr_t physicalAddress, uint8 attributes,
			page_num_t (*get_free_page)(kernel_args*));
	static bool IsKernelPageAccessible(addr_t virtualAddress, uint32 protection);

	static inline void SetTableEntry(uint64 * entry, uint64 value) {
		reinterpret_cast<std::atomic<uint64> *>(entry)->store(value, std::memory_order_release);
	}

	static inline uint64 ChangeTableEntry(uint64 * entry, uint64 value) {
		return reinterpret_cast<std::atomic<uint64> *>(entry)->exchange(value, std::memory_order_acq_rel);
	}

	static inline uint64 SetTableEntryFlags(uint64 * entry, uint64 value) {
		return reinterpret_cast<std::atomic<uint64> *>(entry)->fetch_or(value, std::memory_order_release);
	}

	static inline uint64 ClearTableEntryFlags(uint64 * entry, uint64 value) {
		return reinterpret_cast<std::atomic<uint64> *>(entry)->fetch_and(~value, std::memory_order_release);
	}

	static inline uint64 ClearTableEntry(uint64 * entry) {
		return reinterpret_cast<std::atomic<uint64> *>(entry)->exchange(0, std::memory_order_release);
	}

	static inline uint64 LoadTableEntry(uint64 * entry) {
		return reinterpret_cast<std::atomic<uint64> *>(entry)->load(std::memory_order_acquire);
	}

	static inline bool TestAndSetTableEntry(uint64 * entry, uint64 newValue, uint64& expectedValue) {
		return reinterpret_cast<std::atomic<uint64> *>(entry)->compare_exchange_strong(expectedValue, newValue, std::memory_order_seq_cst);
	}

	static uint64 AttributesForMemoryFlags(uint32 protection, uint32 memoryType);

	static phys_addr_t fKernelPhysicalPgDir;
	static uint64 * fKernelVirtualPgDir;
	static AArch64PhysicalPageMapper * fPhysicalPageMapper;
	static TranslationMapPhysicalPageMapper * fKernelPhysicalPageMapper;

	static uint64 * PageTableEntryForAddress(uint64 * pageDirectory,
			addr_t virtualAddress,
			bool isKernelMap,
			bool allocateMemory,
			vm_page_reservation * reservation,
			TranslationMapPhysicalPageMapper * mapper,
			int32& mapCount);
};

#endif /* SYSTEM_KERNEL_ARCH_AARCH64_AARCH64PAGINGMETHOD_H_ */
