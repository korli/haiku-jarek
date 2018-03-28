/*
 * Copyright 2018, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2009-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

/*!	Contains the ELF loader */


#include <elf.h>

#include <OS.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/param.h>

#include <algorithm>

#include <AutoDeleter.h>
#include <commpage.h>
#include <boot/kernel_args.h>
#include <debug.h>
#include <image_defs.h>
#include <kernel.h>
#include <kimage.h>
#include <syscalls.h>
#include <team.h>
#include <thread.h>
#include <runtime_loader/runtime_loader.h>
#include <util/AutoLock.h>
#include <vfs.h>
#include <vm/vm.h>
#include <vm/vm_types.h>
#include <vm/VMAddressSpace.h>
#include <vm/VMArea.h>

#include <arch/cpu.h>
#include <arch/elf.h>
#include <elf_priv.h>
#include <boot/elf.h>

#include <sys/link_elf.h>

//#define TRACE_ELF
#ifdef TRACE_ELF
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


namespace {

#define IMAGE_HASH_SIZE 16

struct ImageHashDefinition {
	typedef struct elf_image_info ValueType;
	typedef image_id KeyType;

	size_t Hash(ValueType* entry) const
		{ return HashKey(entry->id); }
	ValueType*& GetLink(ValueType* entry) const
		{ return entry->next; }

	size_t HashKey(KeyType key) const
	{
		return (size_t)key;
	}

	bool Compare(KeyType key, ValueType* entry) const
	{
		return key == entry->id;
	}
};

typedef BOpenHashTable<ImageHashDefinition> ImageHash;

} // namespace


static ImageHash *sImagesHash;

static struct elf_image_info *sKernelImage = NULL;
static mutex sImageMutex = MUTEX_INITIALIZER("kimages_lock");
	// guards sImagesHash
static mutex sImageLoadMutex = MUTEX_INITIALIZER("kimages_load_lock");
	// serializes loading/unloading add-ons locking order
	// sImageLoadMutex -> sImageMutex
static bool sInitialized = false;

enum {
	KFLAG_RW					= 0x0010,
	KFLAG_ANON					= 0x0020,
	KFLAG_EXECUTABLE			= 0x0040,
	KFLAG_TEXTREL				= 0x40000,
	KFLAG_CLEAR_TRAILER			= 0x80000,
};

extern "C" {
struct r_debug _r_debug;	/* for GDB; */
}

static spinlock r_debug_lock = B_SPINLOCK_INITIALIZER;

#define GDB_STATE(s,m)	_r_debug.r_state = s; r_debug_state(&_r_debug,m);

/*
 * Function for the debugger to set a breakpoint on to gain control.
 *
 * The two parameters allow the debugger to easily find and determine
 * what the runtime loader is doing and to whom it is doing it.
 *
 * When the loadhook trap is hit (r_debug_state, set at program
 * initialization), the arguments can be found on the stack:
 *
 *  +8   struct link_map *m
 *  +4   struct r_debug  *rd
 *  +0   RetAddr
 */
extern "C" void r_debug_state(struct r_debug* rd, struct link_map *m)
{
    /*
     * The following is a hack to force the compiler to emit calls to
     * this function, even when optimizing.  If the function is empty,
     * the compiler is not obliged to emit any code for calls to it,
     * even when marked __noinline.  However, gdb depends on those
     * calls being made.
     */
    __asm__ __volatile__("":::"memory");
}

/*
 * A function called after init routines have completed. This can be used to
 * break before a program's entry routine is called, and can be used when
 * main is not available in the symbol table.
 */
extern "C" void _r_debug_postinit(struct link_map *m)
{

	/* See r_debug_state(). */
    __asm__ __volatile__("":::"memory");
}

static void linkmap_add(elf_image_info *obj)
{
    struct link_map *l = &obj->linkmap;
    struct link_map *prev;

    obj->linkmap.l_name = obj->name;
    obj->linkmap.l_addr = (caddr_t)obj->regions[0].start;
    obj->linkmap.l_ld = (const void *)obj->dynamic_section;
#ifdef __mips__
    /* GDB needs load offset on MIPS to use the symbols */
    obj->linkmap.l_offs = (caddr_t)obj->regions[0].delta;
#endif

    InterruptsSpinLocker locker(r_debug_lock);

    if (_r_debug.r_map == NULL) {
    	_r_debug.r_map = l;
    	return;
    }

    /*
     * Scan to the end of the list, but not past the entry for the
     * dynamic linker, which we want to keep at the very end.
     */
    for (prev = _r_debug.r_map;
    	 prev->l_next != NULL && prev->l_next != &sKernelImage->linkmap;
    	 prev = prev->l_next);

    /* Link in the new entry. */
    l->l_prev = prev;
    l->l_next = prev->l_next;
    if (l->l_next != NULL)
	l->l_next->l_prev = l;
    prev->l_next = l;
}

static void linkmap_delete(elf_image_info *obj)
{
    struct link_map *l = &obj->linkmap;

    InterruptsSpinLocker locker(r_debug_lock);

	if (l->l_prev == NULL) {
		if ((_r_debug.r_map = l->l_next) != NULL) {
			l->l_next->l_prev = NULL;
		}
		return;
	}

	if ((l->l_prev->l_next = l->l_next) != NULL) {
		l->l_next->l_prev = l->l_prev;
	}
}

static const Elf_Sym *elf_find_symbol(struct elf_image_info *image, const char *name,
	const elf_version_info *version, bool lookupDefault, bool partialMatch = false);


static void
unregister_elf_image(struct elf_image_info *image)
{
	linkmap_delete(image);
	unregister_image(team_get_kernel_team(), image->id);
	sImagesHash->Remove(image);
}

static void size_elf_image(struct elf_image_info *image, void *& text, size_t& text_size,void *& data, size_t& data_size)
{
	addr_t text_base = ~((addr_t)0);
	addr_t text_end = 0;
	addr_t data_base = ~((addr_t)0);
	addr_t data_end = 0;

	for(unsigned int i = 0 ; i < image->num_regions ; ++i) {
		if(image->regions[i].protection & (B_EXECUTE_AREA | B_KERNEL_EXECUTE_AREA)) {
			text_base = std::min<addr_t>(text_base, image->regions[i].start);
			text_end = std::max<addr_t>(text_end, image->regions[i].start + image->regions[i].size);
		} else {
			data_base = std::min<addr_t>(data_base, image->regions[i].start);
			data_end = std::max<addr_t>(data_end, image->regions[i].start + image->regions[i].size);
		}
	}

	if(text_end) {
		text = (void *)text_base;
		text_size = text_end - text_base;
	} else {
		text = NULL;
		text_size = 0;
	}

	if(data_end) {
		data = (void *)data_base;
		data_size = data_end - data_base;
	} else {
		data = NULL;
		data_size = 0;
	}
}

static void
register_elf_image(struct elf_image_info *image)
{
	extended_image_info imageInfo;

	memset(&imageInfo, 0, sizeof(imageInfo));
	imageInfo.basic_info.id = image->id;
	imageInfo.basic_info.type = B_SYSTEM_IMAGE;
	strlcpy(imageInfo.basic_info.name, image->name,
		sizeof(imageInfo.basic_info.name));

	size_t text_size, data_size;

	size_elf_image(image,
			imageInfo.basic_info.text,
			text_size,
			imageInfo.basic_info.data,
			data_size);

	imageInfo.basic_info.text_size = text_size;
	imageInfo.basic_info.data_size = data_size;

	if (image->num_regions > 0 && image->regions[0].id >= 0) {
		// evaluate the API/ABI version symbols

		// Haiku API version
		imageInfo.basic_info.api_version = 0;
		const Elf_Sym* symbol = elf_find_symbol(image,
			B_SHARED_OBJECT_HAIKU_VERSION_VARIABLE_NAME, NULL, true);
		if (symbol != NULL && symbol->st_shndx != SHN_UNDEF
			&& symbol->st_value > 0
			&& ELF_ST_TYPE(symbol->st_info) == STT_OBJECT
			&& symbol->st_size >= sizeof(uint32)) {
			addr_t symbolAddress = symbol->st_value + image->regions[0].delta;
			if (symbolAddress >= image->regions[0].start
				&& symbolAddress - image->regions[0].start + sizeof(uint32)
					<= image->regions[0].size) {
				imageInfo.basic_info.api_version = *(const uint32*)symbolAddress;
			}
		}

		// Haiku ABI
		imageInfo.basic_info.abi = 0;
		symbol = elf_find_symbol(image,
			B_SHARED_OBJECT_HAIKU_ABI_VARIABLE_NAME, NULL, true);
		if (symbol != NULL && symbol->st_shndx != SHN_UNDEF
			&& symbol->st_value > 0
			&& ELF_ST_TYPE(symbol->st_info) == STT_OBJECT
			&& symbol->st_size >= sizeof(uint32)) {
			addr_t symbolAddress = symbol->st_value + image->regions[0].delta;
			if (symbolAddress >= image->regions[0].start
				&& symbolAddress - image->regions[0].start + sizeof(uint32)
					<= image->regions[0].size) {
				imageInfo.basic_info.api_version = *(const uint32*)symbolAddress;
			}
		}
	} else {
		// in-memory image -- use the current values
		imageInfo.basic_info.api_version = B_HAIKU_VERSION;
		imageInfo.basic_info.abi = B_HAIKU_ABI;
	}

	image->id = register_image(team_get_kernel_team(), &imageInfo,
		sizeof(imageInfo));
	sImagesHash->Insert(image);
	linkmap_add(image);
}


/*!	Note, you must lock the image mutex when you call this function. */
static struct elf_image_info *
find_image_at_address(addr_t address)
{
#if KDEBUG
	if (!debug_debugger_running())
		ASSERT_LOCKED_MUTEX(&sImageMutex);
#endif

	ImageHash::Iterator iterator(sImagesHash);

	// get image that may contain the address

	while (iterator.HasNext()) {
		struct elf_image_info* image = iterator.Next();

		for(unsigned int i = 0 ; i < image->num_regions ; ++i) {
			if(address >= image->regions[i].start && address < (image->regions[i].start + image->regions[i].size)) {
				return image;
			}
		}
	}

	return NULL;
}


static int
dump_address_info(int argc, char **argv)
{
	const char *symbol, *imageName;
	bool exactMatch;
	addr_t address, baseAddress;

	if (argc < 2) {
		kprintf("usage: ls <address>\n");
		return 0;
	}

	address = strtoul(argv[1], NULL, 16);

	status_t error;

	if (IS_KERNEL_ADDRESS(address)) {
		error = elf_debug_lookup_symbol_address(address, &baseAddress, &symbol,
			&imageName, &exactMatch);
	} else {
		error = elf_debug_lookup_user_symbol_address(
			debug_get_debugged_thread()->team, address, &baseAddress, &symbol,
			&imageName, &exactMatch);
	}

	if (error == B_OK) {
		kprintf("%p = %s + 0x%lx (%s)%s\n", (void*)address, symbol,
			address - baseAddress, imageName, exactMatch ? "" : " (nearest)");
	} else
		kprintf("There is no image loaded at this address!\n");

	return 0;
}


static struct elf_image_info *
find_image(image_id id)
{
	return sImagesHash->Lookup(id);
}


static struct elf_image_info *
find_image_by_vnode(void *vnode)
{
	MutexLocker locker(sImageMutex);

	ImageHash::Iterator iterator(sImagesHash);
	while (iterator.HasNext()) {
		struct elf_image_info* image = iterator.Next();
		if (image->vnode == vnode)
			return image;
	}

	return NULL;
}


static struct elf_image_info *
create_image_struct()
{
	struct elf_image_info *image
		= (struct elf_image_info *)malloc(sizeof(struct elf_image_info));
	if (image == NULL)
		return NULL;

	memset(image, 0, sizeof(struct elf_image_info));

	for(unsigned int i = 0 ; i < ELF_IMAGE_MAX_REGIONS ; ++i) {
		image->regions[i].id = -1;
	}

	image->ref_count = 1;

	return image;
}


static void
delete_elf_image(struct elf_image_info *image)
{
	for(unsigned int i = 0 ; i < image->num_regions ; ++i) {
		if(image->regions[i].id >= 0) {
			delete_area(image->regions[i].id);
		}
	}

	if (image->vnode)
		vfs_put_vnode(image->vnode);

	free(image->versions);
	free(image->debug_symbols);
	free((void*)image->debug_string_table);
	free(image->elf_header);
	free(image->name);
	free(image);
}


static uint32
elf_hash(const char *name)
{
	uint32 hash = 0;
	uint32 temp;

	while (*name) {
		hash = (hash << 4) + (uint8)*name++;
		if ((temp = hash & 0xf0000000) != 0)
			hash ^= temp >> 24;
		hash &= ~temp;
	}
	return hash;
}

/*
 * The GNU hash function is the Daniel J. Bernstein hash clipped to 32 bits
 * unsigned in case it's implemented with a wider type.
 */
static uint32_t
gnu_hash(const char *s)
{
	uint32_t h;
	unsigned char c;

	h = 5381;
	for (c = *s; c != '\0'; c = *++s)
		h = h * 33 + c;
	return (h & 0xffffffff);
}

static const char *
get_symbol_type_string(const Elf_Sym *symbol)
{
	switch (ELF_ST_TYPE(symbol->st_info)) {
		case STT_FUNC:
			return "func";
		case STT_OBJECT:
			return " obj";
		case STT_FILE:
			return "file";
		default:
			return "----";
	}
}


static const char *
get_symbol_bind_string(const Elf_Sym *symbol)
{
	switch (ELF_ST_BIND(symbol->st_info)) {
		case STB_LOCAL:
			return "loc ";
		case STB_GLOBAL:
			return "glob";
		case STB_WEAK:
			return "weak";
		case STB_GNU_UNIQUE:
			return "uniq";
		default:
			return "----";
	}
}


/*!	Searches a symbol (pattern) in all kernel images */
static int
dump_symbol(int argc, char **argv)
{
	if (argc != 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s <symbol-name>\n", argv[0]);
		return 0;
	}

	struct elf_image_info *image = NULL;
	const char *pattern = argv[1];

	void* symbolAddress = NULL;

	ImageHash::Iterator iterator(sImagesHash);
	while (iterator.HasNext()) {
		image = iterator.Next();
		if (image->num_debug_symbols > 0) {
			// search extended debug symbol table (contains static symbols)
			for (uint32 i = 0; i < image->num_debug_symbols; i++) {
				Elf_Sym *symbol = &image->debug_symbols[i];
				const char *name = image->debug_string_table + symbol->st_name;

				if (symbol->st_value > 0 && strstr(name, pattern) != 0) {
					symbolAddress
						= (void*)(symbol->st_value + image->regions[0].delta);
					kprintf("%p %5lu %s:%s\n", symbolAddress, (unsigned long)symbol->st_size,
						image->name, name);
				}
			}
		} else {
			const Elf_Sym * symbol = elf_find_symbol(image,
					pattern,
					NULL,
					true,
					true);

			if (symbol) {
				symbolAddress = (void*)(symbol->st_value + image->regions[0].delta);
				kprintf("%p %5lu %s:%s\n", symbolAddress,
							(unsigned long)symbol->st_size, image->name, image->SymbolName(symbol));

			}
		}
	}

	if (symbolAddress != NULL)
		set_debug_variable("_", (addr_t)symbolAddress);

	return 0;
}


static int
dump_symbols(int argc, char **argv)
{
	struct elf_image_info *image = NULL;
	uint32 i;

	// if the argument looks like a hex number, treat it as such
	if (argc > 1) {
		if (isdigit(argv[1][0])) {
			addr_t num = strtoul(argv[1], NULL, 0);

			if (IS_KERNEL_ADDRESS(num)) {
				// find image at address

				ImageHash::Iterator iterator(sImagesHash);
				while (iterator.HasNext()) {
					elf_image_info* current = iterator.Next();
					if (current->regions[0].start <= num
						&& current->regions[0].start
							+ current->regions[0].size	>= num) {
						image = current;
						break;
					}
				}

				if (image == NULL) {
					kprintf("No image covers %#" B_PRIxADDR " in the kernel!\n",
						num);
				}
			} else {
				image = sImagesHash->Lookup(num);
				if (image == NULL) {
					kprintf("image %#" B_PRIxADDR " doesn't exist in the "
						"kernel!\n", num);
				}
			}
		} else {
			// look for image by name
			ImageHash::Iterator iterator(sImagesHash);
			while (iterator.HasNext()) {
				elf_image_info* current = iterator.Next();
				if (!strcmp(current->name, argv[1])) {
					image = current;
					break;
				}
			}

			if (image == NULL)
				kprintf("No image \"%s\" found in kernel!\n", argv[1]);
		}
	} else {
		kprintf("usage: %s image_name/image_id/address_in_image\n", argv[0]);
		return 0;
	}

	if (image == NULL)
		return -1;

	// dump symbols

	kprintf("Symbols of image %" B_PRId32 " \"%s\":\n", image->id, image->name);
	kprintf("%-*s Type       Size Name\n", B_PRINTF_POINTER_WIDTH, "Address");

	if (image->num_debug_symbols > 0) {
		// search extended debug symbol table (contains static symbols)
		for (i = 0; i < image->num_debug_symbols; i++) {
			Elf_Sym *symbol = &image->debug_symbols[i];

			if (symbol->st_value == 0 || symbol->st_size
					>= image->regions[0].size + image->regions[1].size)
				continue;

			kprintf("%0*lx %s/%s %5ld %s\n", B_PRINTF_POINTER_WIDTH,
				symbol->st_value + image->regions[0].delta,
				get_symbol_type_string(symbol), get_symbol_bind_string(symbol),
				(unsigned long)symbol->st_size, image->debug_string_table + symbol->st_name);
		}
	} else {
		// search standard symbol lookup table
		for(unsigned long i = 0 ; i < image->dynsymcount ; ++i) {
			const Elf_Sym *symbol = &image->syms[i];

			if (symbol->st_value == 0 ||
				symbol->st_size >= image->regions[0].size + image->regions[1].size)
			{
					continue;
			}

			kprintf("%08lx %s/%s %5ld %s\n",
				symbol->st_value + image->regions[0].delta,
				get_symbol_type_string(symbol),
				get_symbol_bind_string(symbol),
				(unsigned long)symbol->st_size, image->SymbolName(symbol));
		}
	}

	return 0;
}


static void
dump_elf_region(struct elf_region *region, const char *name)
{
	kprintf("   %s.id %" B_PRId32 "\n", name, region->id);
	kprintf("   %s.start %#" B_PRIxADDR "\n", name, region->start);
	kprintf("   %s.size %#" B_PRIxSIZE "\n", name, region->size);
	kprintf("   %s.delta %ld\n", name, region->delta);
}


static void
dump_image_info(struct elf_image_info *image)
{
	kprintf("elf_image_info at %p:\n", image);
	kprintf(" next %p\n", image->next);
	kprintf(" id %" B_PRId32 "\n", image->id);
	for(unsigned int i = 0 ; i < image->num_regions ; ++i) {
		dump_elf_region(&image->regions[i],
				(image->regions[i].protection & (B_EXECUTE_AREA | B_KERNEL_EXECUTE_AREA)) ? "text" :
				(image->regions[i].protection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA)) ? "data" : "ro  ");
	}
	kprintf(" dynamic_section %#" B_PRIxADDR "\n", image->dynamic_section);
	kprintf(" needed %p\n", image->needed);
	kprintf(" symhash %p\n", image->buckets);
	kprintf(" syms %p\n", image->syms);
	kprintf(" strtab %p\n", image->strtab);
	kprintf(" rel %p\n", image->rel);
	kprintf(" rel_len %#x\n", image->rel_len);
	kprintf(" rela %p\n", image->rela);
	kprintf(" rela_len %#x\n", image->rela_len);
	kprintf(" pltrel %p\n", image->pltrel);
	kprintf(" pltrel_len %#x\n", image->pltrel_len);

	kprintf(" debug_symbols %p (%" B_PRIu32 ")\n",
		image->debug_symbols, image->num_debug_symbols);
}


static int
dump_image(int argc, char **argv)
{
	struct elf_image_info *image;

	// if the argument looks like a hex number, treat it as such
	if (argc > 1) {
		addr_t num = strtoul(argv[1], NULL, 0);

		if (IS_KERNEL_ADDRESS(num)) {
			// semi-hack
			dump_image_info((struct elf_image_info *)num);
		} else {
			image = sImagesHash->Lookup(num);
			if (image == NULL) {
				kprintf("image %#" B_PRIxADDR " doesn't exist in the kernel!\n",
					num);
			} else
				dump_image_info(image);
		}
		return 0;
	}

	kprintf("loaded kernel images:\n");

	ImageHash::Iterator iterator(sImagesHash);

	while (iterator.HasNext()) {
		image = iterator.Next();
		kprintf("%p (%" B_PRId32 ") %s\n", image, image->id, image->name);
	}

	return 0;
}


// Currently unused
#if 0
static
void dump_symbol(struct elf_image_info *image, Elf_Sym *sym)
{

	kprintf("symbol at %p, in image %p\n", sym, image);

	kprintf(" name index %d, '%s'\n", sym->st_name, SYMNAME(image, sym));
	kprintf(" st_value 0x%x\n", sym->st_value);
	kprintf(" st_size %d\n", sym->st_size);
	kprintf(" st_info 0x%x\n", sym->st_info);
	kprintf(" st_other 0x%x\n", sym->st_other);
	kprintf(" st_shndx %d\n", sym->st_shndx);
}
#endif

static bool matches_symbol(struct elf_image_info *image,
			const char *name,
			const elf_version_info *lookupVersion,
			bool lookupDefault,
			const Elf_Sym * symbol,
			const Elf_Sym *& versionedSymbol,
			uint32& versionedSymbolCount,
			bool partialMatch,
			unsigned long symbol_index)
{
	const char * sym_name = image->SymbolName(symbol);

	switch(ELF_ST_TYPE(symbol->st_info)) {
	case STT_FUNC:
	case STT_NOTYPE:
	case STT_OBJECT:
	case STT_COMMON:
	case STT_GNU_IFUNC:
		if(symbol->st_value == 0)
			return false;
		/* fallthrough */
	case STT_TLS:
		if (symbol->st_shndx != SHN_UNDEF)
			break;
		/* fallthrough */
	default:
		return false;
	}

	if(partialMatch) {
		if(!strstr(sym_name, name)) {
			return false;
		}
	} else {
		if(name[0] != sym_name[0])
			return false;

		if(strcmp(name, sym_name) != 0)
			return false;
	}


	// Handle the simple cases -- the image doesn't have version
	// information -- first.
	if (image->symbol_versions == NULL) {
		if (lookupVersion == NULL) {
			// No specific symbol version was requested either, so the
			// symbol is just fine.
			return symbol;
		}

		// A specific version is requested. Since the only possible
		// dependency is the kernel itself, the add-on was obviously linked
		// against a newer kernel.
		dprintf("Kernel add-on requires version support, but the kernel "
			"is too old.\n");
		return false;
	}


	// The image has version information. Let's see what we've got.
	uint32 versionID = image->symbol_versions[symbol_index];
	uint32 versionIndex = VER_NDX(versionID);
	elf_version_info& version = image->versions[versionIndex];

	// skip local versions
	if (versionIndex == VER_NDX_LOCAL)
		return false;

	if (lookupVersion != NULL) {
		// a specific version is requested

		// compare the versions
		if (version.hash == lookupVersion->hash
			&& strcmp(version.name, lookupVersion->name) == 0) {
			// versions match
			return true;
		}

		// The versions don't match. We're still fine with the
		// base version, if it is public and we're not looking for
		// the default version.
		if ((versionID & VER_NDX_HIDDEN) == 0
			&& versionIndex == VER_NDX_GLOBAL
			&& !lookupDefault) {
			// TODO: Revise the default version case! That's how
			// FreeBSD implements it, but glibc doesn't handle it
			// specially.
			return true;
		}
	} else {
		// No specific version requested, but the image has version
		// information. This can happen in either of these cases:
		//
		// * The dependent object was linked against an older version
		//   of the now versioned dependency.
		// * The symbol is looked up via find_image_symbol() or dlsym().
		//
		// In the first case we return the base version of the symbol
		// (VER_NDX_GLOBAL or VER_NDX_GIVEN), or, if that doesn't
		// exist, the unique, non-hidden versioned symbol.
		//
		// In the second case we want to return the public default
		// version of the symbol. The handling is pretty similar to the
		// first case, with the exception that we treat VER_NDX_GIVEN
		// as regular version.

		// VER_NDX_GLOBAL is always good, VER_NDX_GIVEN is fine, if
		// we don't look for the default version.
		if (versionIndex == VER_NDX_GLOBAL
			|| (!lookupDefault && versionIndex == VER_NDX_GIVEN)) {
			return true;
		}

		// If not hidden, remember the version -- we'll return it, if
		// it is the only one.
		if ((versionID & VER_NDX_HIDDEN) == 0) {
			versionedSymbolCount++;
			versionedSymbol = symbol;
		}
	}

	return false;
}

static const Elf_Sym *
elf_find_symbol(struct elf_image_info *image, const char *name,
	const elf_version_info *lookupVersion, bool lookupDefault, bool partialMatch)
{
	if (image->dynamic_section == 0 ||
		(!image->valid_hash_sysv && !image->valid_hash_gnu))
		return NULL;

	const Elf_Sym* versionedSymbol = NULL;
	uint32 versionedSymbolCount = 0;
	const Elf_Sym * sym;

	if(image->valid_hash_sysv) {
		uint32 hash = elf_hash(name);
		unsigned long symnum;

		for(symnum = image->buckets[hash % image->nbuckets] ;
				symnum != STN_UNDEF ;
				symnum = image->chains[symnum])
		{
			if(symnum >= image->nchains)
				return NULL;
			sym = image->Symbol(symnum);
			if(matches_symbol(image,
					name,
					lookupVersion,
					lookupDefault,
					sym,
					versionedSymbol,
					versionedSymbolCount,
					partialMatch,
					symnum))
			{
				return sym;
			}
		}
	}

	if(image->valid_hash_gnu) {
		Elf_Addr bloom_word;
		const Elf32_Word *hashval;
		Elf32_Word bucket;
		unsigned int h1, h2;
		unsigned long symnum;

		uint32 hash_gnu = gnu_hash(name);

		/* Pick right bitmask word from Bloom filter array */
		bloom_word = image->bloom_gnu[(hash_gnu / __ELF_WORD_SIZE) & image->maskwords_bm_gnu];

		/* Calculate modulus word size of gnu hash and its derivative */
		h1 = hash_gnu & (__ELF_WORD_SIZE - 1);
		h2 = ((hash_gnu >> image->shift2_gnu) & (__ELF_WORD_SIZE - 1));

		/* Filter out the "definitely not in set" queries */
		if (((bloom_word >> h1) & (bloom_word >> h2) & 1) == 0)
			return NULL;

		/* Locate hash chain and corresponding value element*/
		bucket = image->buckets_gnu[hash_gnu % image->nbuckets_gnu];
		if (bucket == 0)
			return NULL;

		hashval = &image->chain_zero_gnu[bucket];

		do {
			if (((*hashval ^ hash_gnu) >> 1) == 0) {
				symnum = hashval - image->chain_zero_gnu;
				sym = image->Symbol(symnum);

				if(matches_symbol(image,
						name,
						lookupVersion,
						lookupDefault,
						sym,
						versionedSymbol,
						versionedSymbolCount,
						partialMatch,
						symnum))
				{
					return sym;
				}
			}
		} while ((*hashval++ & 1) == 0);
	}

	if(versionedSymbolCount == 1) {
		return versionedSymbol;
	}

	return NULL;
}


static status_t
elf_parse_dynamic_section(struct elf_image_info *image)
{
	Elf_Dyn *d;
	ssize_t neededOffset = -1;
	const Elf_Hashelt * hashtab;
    Elf32_Word bkt, nmaskwords;
    int bloom_size32;

	TRACE(("top of elf_parse_dynamic_section\n"));

	image->syms = 0;
	image->strtab = 0;
	image->valid_hash_sysv = false;
	image->valid_hash_gnu = false;

	d = (Elf_Dyn *)image->dynamic_section;
	if (!d) {
		TRACE(("Image has no dynamic section\n"));
		return B_ERROR;
	}

	for (int32 i = 0; d[i].d_tag != DT_NULL; i++) {
		switch (d[i].d_tag) {
			case DT_NEEDED:
				neededOffset = d[i].d_un.d_ptr + image->regions[0].delta;
				break;
			case DT_HASH:
				TRACE(("SysV hash table detected for image\n"));
				hashtab = (const Elf_Hashelt *)(d[i].d_un.d_ptr	+ image->regions[0].delta);
				image->nbuckets = hashtab[0];
				image->nchains = hashtab[1];
				image->buckets = hashtab + 2;
				image->chains = image->buckets + image->nbuckets;
				image->valid_hash_sysv = image->nbuckets > 0 && image->nchains > 0 && image->buckets != NULL;
				break;
			case DT_GNU_HASH:
				TRACE(("GNU hash table detected for image\n"));
				hashtab = (const Elf_Hashelt *)(d[i].d_un.d_ptr	+ image->regions[0].delta);
				image->nbuckets_gnu = hashtab[0];
				image->symndx_gnu = hashtab[1];
				nmaskwords = hashtab[2];
				bloom_size32 = (__ELF_WORD_SIZE / 32) * nmaskwords;
				image->maskwords_bm_gnu = nmaskwords - 1;
				image->shift2_gnu = hashtab[3];
				image->bloom_gnu = (Elf_Addr *) (hashtab + 4);
				image->buckets_gnu = hashtab + 4 + bloom_size32;
				image->chain_zero_gnu = image->buckets_gnu + image->nbuckets_gnu - image->symndx_gnu;
				/* Number of bitmask words is required to be power of 2 */
				image->valid_hash_gnu = powerof2(nmaskwords) && image->nbuckets_gnu > 0 && image->buckets_gnu != NULL;
				break;
			case DT_STRTAB:
				image->strtab = (char *)(d[i].d_un.d_ptr
					+ image->regions[0].delta);
				break;
			case DT_SYMTAB:
				image->syms = (Elf_Sym *)(d[i].d_un.d_ptr
					+ image->regions[0].delta);
				break;
			case DT_REL:
				image->rel = (Elf_Rel *)(d[i].d_un.d_ptr
					+ image->regions[0].delta);
				break;
			case DT_RELSZ:
				image->rel_len = d[i].d_un.d_val;
				break;
			case DT_RELA:
				image->rela = (Elf_Rela *)(d[i].d_un.d_ptr
					+ image->regions[0].delta);
				break;
			case DT_RELASZ:
				image->rela_len = d[i].d_un.d_val;
				break;
			case DT_JMPREL:
				image->pltrel = (Elf_Rel *)(d[i].d_un.d_ptr
					+ image->regions[0].delta);
				break;
			case DT_PLTRELSZ:
				image->pltrel_len = d[i].d_un.d_val;
				break;
			case DT_PLTREL:
				image->pltrel_type = d[i].d_un.d_val;
				break;
			case DT_VERSYM:
				image->symbol_versions = (elf_versym*)
					(d[i].d_un.d_ptr + image->regions[0].delta);
				break;
			case DT_VERDEF:
				image->version_definitions = (Elf_Verdef*)
					(d[i].d_un.d_ptr + image->regions[0].delta);
				break;
			case DT_VERDEFNUM:
				image->num_version_definitions = d[i].d_un.d_val;
				break;
			case DT_VERNEED:
				image->needed_versions = (Elf_Verneed*)
					(d[i].d_un.d_ptr + image->regions[0].delta);
				break;
			case DT_VERNEEDNUM:
				image->num_needed_versions = d[i].d_un.d_val;
				break;
			case DT_SYMBOLIC:
				image->symbolic = true;
				break;
			case DT_FLAGS:
			{
				uint32 flags = d[i].d_un.d_val;
				if ((flags & DF_SYMBOLIC) != 0)
					image->symbolic = true;
				if ((flags & DT_TEXTREL) != 0)
					image->textrel = true;
				if ((flags & DF_STATIC_TLS) != 0) {
					TRACE(("Image can't have DT_STATIC_TLS flag\n"));
					return B_ERROR;
				}
				break;
			}

			case DT_TEXTREL:
			{
				image->textrel = true;
				break;
			}

#ifndef __mips__
			case DT_DEBUG:
				d[i].d_un.d_ptr = (Elf_Addr) &_r_debug;
				break;
#endif

			default:
				continue;
		}
	}

	// lets make sure we found all the required sections
	if (!image->syms) {
		TRACE(("Image has no symbol table\n"));
		return B_ERROR;
	}

	if(!image->strtab) {
		TRACE(("Image has no string table\n"));
		return B_ERROR;
	}

	if(!image->valid_hash_sysv && !image->valid_hash_gnu) {
		TRACE(("Image has no symbol hash table\n"));
		return B_ERROR;
	}

	if (image->valid_hash_sysv) {
		image->dynsymcount = image->nchains;
		TRACE(("Image has %" B_PRIu32 " dynamic symbol (SysV hash style)\n", (uint32_t)image->dynsymcount));
	} else if (image->valid_hash_gnu) {
	    const Elf32_Word *hashval;
		image->dynsymcount = 0;
		for (bkt = 0; bkt < image->nbuckets_gnu; bkt++) {
			if (image->buckets_gnu[bkt] == 0)
				continue;
			hashval = &image->chain_zero_gnu[image->buckets_gnu[bkt]];
			do {
				image->dynsymcount++;
			} while ((*hashval++ & 1u) == 0);
		}
		image->dynsymcount += image->symndx_gnu;
		TRACE(("Image has %" B_PRIu32 " dynamic symbol (GNU hash style)\n", (uint32_t)image->dynsymcount));
	}

	TRACE(("needed_offset = %lx\n", neededOffset));

	if (neededOffset >= 0) {
		image->needed = image->String(neededOffset);
	}

	return B_OK;
}

static status_t elf_handle_textrel(team_id team, const char * path, elf_image_info * image, bool before, bool kernel)
{
	TRACE(("%s: Handle text relocations (%s)\n", path, before ? "before" : "after"));

	for(uint32 i = 0 ; i < image->num_regions ; ++i) {
		if(image->regions[i].protection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA))
			continue;

		uint32 protection = before ?
				(kernel ? (B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA) : (B_READ_AREA | B_WRITE_AREA)) :
				image->regions[i].protection;

		TRACE(("%s: set protection of %p--%p to %x\n",
				path,
				(void *)image->regions[i].start,
				(void *)(image->regions[i].start + image->regions[i].size),
				protection));

		status_t status = vm_set_area_protection(team, image->regions[i].id, protection, kernel);

		if(status != B_OK) {
			TRACE(("Can't enforce region protection for %s (%d)\n", path, status));
			return status;
		}
	}

	return B_OK;
}

static status_t
assert_defined_image_version(elf_image_info* dependentImage,
	elf_image_info* image, const elf_version_info& neededVersion, bool weak)
{
	// If the image doesn't have version definitions, we print a warning and
	// succeed. Weird, but that's how glibc does it. Not unlikely we'll fail
	// later when resolving versioned symbols.
	if (image->version_definitions == NULL) {
		dprintf("%s: No version information available (required by %s)\n",
			image->name, dependentImage->name);
		return B_OK;
	}

	// iterate through the defined versions to find the given one
	Elf_Verdef* definition = image->version_definitions;
	for (uint32 i = 0; i < image->num_version_definitions; i++) {
		uint32 versionIndex = VER_NDX(definition->vd_ndx);
		elf_version_info& info = image->versions[versionIndex];

		if (neededVersion.hash == info.hash
			&& strcmp(neededVersion.name, info.name) == 0) {
			return B_OK;
		}

		definition = (Elf_Verdef*)
			((uint8*)definition + definition->vd_next);
	}

	// version not found -- fail, if not weak
	if (!weak) {
		dprintf("%s: version \"%s\" not found (required by %s)\n", image->name,
			neededVersion.name, dependentImage->name);
		return B_MISSING_SYMBOL;
	}

	return B_OK;
}


static status_t
init_image_version_infos(elf_image_info* image)
{
	// First find out how many version infos we need -- i.e. get the greatest
	// version index from the defined and needed versions (they use the same
	// index namespace).
	uint32 maxIndex = 0;

	if (image->version_definitions != NULL) {
		Elf_Verdef* definition = image->version_definitions;
		for (uint32 i = 0; i < image->num_version_definitions; i++) {
			if (definition->vd_version != 1) {
				dprintf("Unsupported version definition revision: %u\n",
					definition->vd_version);
				return B_BAD_VALUE;
			}

			uint32 versionIndex = VER_NDX(definition->vd_ndx);
			if (versionIndex > maxIndex)
				maxIndex = versionIndex;

			definition = (Elf_Verdef*)
				((uint8*)definition	+ definition->vd_next);
		}
	}

	if (image->needed_versions != NULL) {
		Elf_Verneed* needed = image->needed_versions;
		for (uint32 i = 0; i < image->num_needed_versions; i++) {
			if (needed->vn_version != 1) {
				dprintf("Unsupported version needed revision: %u\n",
					needed->vn_version);
				return B_BAD_VALUE;
			}

			Elf_Vernaux* vernaux
				= (Elf_Vernaux*)((uint8*)needed + needed->vn_aux);
			for (uint32 k = 0; k < needed->vn_cnt; k++) {
				uint32 versionIndex = VER_NDX(vernaux->vna_other);
				if (versionIndex > maxIndex)
					maxIndex = versionIndex;

				vernaux = (Elf_Vernaux*)((uint8*)vernaux + vernaux->vna_next);
			}

			needed = (Elf_Verneed*)((uint8*)needed + needed->vn_next);
		}
	}

	if (maxIndex == 0)
		return B_OK;

	// allocate the version infos
	image->versions
		= (elf_version_info*)malloc(sizeof(elf_version_info) * (maxIndex + 1));
	if (image->versions == NULL) {
		dprintf("Memory shortage in init_image_version_infos()\n");
		return B_NO_MEMORY;
	}
	image->num_versions = maxIndex + 1;

	// init the version infos

	// version definitions
	if (image->version_definitions != NULL) {
		Elf_Verdef* definition = image->version_definitions;
		for (uint32 i = 0; i < image->num_version_definitions; i++) {
			if (definition->vd_cnt > 0
				&& (definition->vd_flags & VER_FLG_BASE) == 0) {
				Elf_Verdaux* verdaux
					= (Elf_Verdaux*)((uint8*)definition + definition->vd_aux);

				uint32 versionIndex = VER_NDX(definition->vd_ndx);
				elf_version_info& info = image->versions[versionIndex];
				info.hash = definition->vd_hash;
				info.name = image->String(verdaux->vda_name);
				info.file_name = NULL;
			}

			definition = (Elf_Verdef*)
				((uint8*)definition + definition->vd_next);
		}
	}

	// needed versions
	if (image->needed_versions != NULL) {
		Elf_Verneed* needed = image->needed_versions;
		for (uint32 i = 0; i < image->num_needed_versions; i++) {
			const char* fileName = image->String(needed->vn_file);

			Elf_Vernaux* vernaux
				= (Elf_Vernaux*)((uint8*)needed + needed->vn_aux);
			for (uint32 k = 0; k < needed->vn_cnt; k++) {
				uint32 versionIndex = VER_NDX(vernaux->vna_other);
				elf_version_info& info = image->versions[versionIndex];
				info.hash = vernaux->vna_hash;
				info.name = image->String(vernaux->vna_name);
				info.file_name = fileName;

				vernaux = (Elf_Vernaux*)((uint8*)vernaux + vernaux->vna_next);
			}

			needed = (Elf_Verneed*)((uint8*)needed + needed->vn_next);
		}
	}

	return B_OK;
}


static status_t
check_needed_image_versions(elf_image_info* image)
{
	if (image->needed_versions == NULL)
		return B_OK;

	Elf_Verneed* needed = image->needed_versions;
	for (uint32 i = 0; i < image->num_needed_versions; i++) {
		elf_image_info* dependency = sKernelImage;

		Elf_Vernaux* vernaux
			= (Elf_Vernaux*)((uint8*)needed + needed->vn_aux);
		for (uint32 k = 0; k < needed->vn_cnt; k++) {
			uint32 versionIndex = VER_NDX(vernaux->vna_other);
			elf_version_info& info = image->versions[versionIndex];

			status_t error = assert_defined_image_version(image, dependency,
				info, (vernaux->vna_flags & VER_FLG_WEAK) != 0);
			if (error != B_OK)
				return error;

			vernaux = (Elf_Vernaux*)((uint8*)vernaux + vernaux->vna_next);
		}

		needed = (Elf_Verneed*)((uint8*)needed + needed->vn_next);
	}

	return B_OK;
}


/*!	Resolves the \a symbol by linking against \a sharedImage if necessary.
	Returns the resolved symbol's address in \a _symbolAddress.
*/
status_t
elf_resolve_symbol(struct elf_image_info *image, const Elf_Sym *symbol,
	struct elf_image_info *sharedImage, addr_t *_symbolAddress)
{
	// Local symbols references are always resolved to the given symbol.
	if (ELF_ST_BIND(symbol->st_info) == STB_LOCAL) {
		*_symbolAddress = symbol->st_value + image->regions[0].delta;
		return B_OK;
	}

	// Non-local symbols we try to resolve to the kernel image first. Unless
	// the image is linked symbolically, then vice versa.
	elf_image_info* firstImage = sharedImage;
	elf_image_info* secondImage = image;
	if (image->symbolic)
		std::swap(firstImage, secondImage);

	const char *symbolName = image->SymbolName(symbol);

	// get the version info
	const elf_version_info* versionInfo = NULL;
	if (image->symbol_versions != NULL) {
		uint32 index = symbol - image->syms;
		uint32 versionIndex = VER_NDX(image->symbol_versions[index]);
		if (versionIndex >= VER_NDX_GIVEN)
			versionInfo = image->versions + versionIndex;
	}

	// find the symbol
	elf_image_info* foundImage = firstImage;
	const Elf_Sym* foundSymbol = elf_find_symbol(firstImage, symbolName, versionInfo,
		false);
	if (foundSymbol == NULL
		|| ELF_ST_BIND(foundSymbol->st_info) == STB_WEAK) {
		// Not found or found a weak definition -- try to resolve in the other
		// image.
		const Elf_Sym* secondSymbol = elf_find_symbol(secondImage, symbolName,
			versionInfo, false);
		// If we found a symbol -- take it in case we didn't have a symbol
		// before or the new symbol is not weak.
		if (secondSymbol != NULL
			&& (foundSymbol == NULL
				|| ELF_ST_BIND(secondSymbol->st_info) != STB_WEAK)) {
			foundImage = secondImage;
			foundSymbol = secondSymbol;
		}
	}

	if (foundSymbol == NULL) {
		// Weak undefined symbols get a value of 0, if unresolved.
		if (ELF_ST_BIND(symbol->st_info) == STB_WEAK) {
			*_symbolAddress = 0;
			return B_OK;
		}

		dprintf("\"%s\": could not resolve symbol '%s'\n", image->name,
			symbolName);
		return B_MISSING_SYMBOL;
	}

	// make sure they're the same type
	if (ELF_ST_TYPE(symbol->st_info) != ELF_ST_TYPE(foundSymbol->st_info)) {
		dprintf("elf_resolve_symbol: found symbol '%s' in image '%s' "
			"(requested by image '%s') but wrong type (%d vs. %d)\n",
			symbolName, foundImage->name, image->name,
			ELF_ST_TYPE(foundSymbol->st_info), ELF_ST_TYPE(symbol->st_info));
		return B_MISSING_SYMBOL;
	}

	*_symbolAddress = foundSymbol->st_value + foundImage->regions[0].delta;
	return B_OK;
}


/*! Until we have shared library support, just this links against the kernel */
static int
elf_relocate(struct elf_image_info* image, struct elf_image_info* resolveImage)
{
	int status = B_NO_ERROR;

	TRACE(("elf_relocate(%p (\"%s\"))\n", image, image->name));

	// deal with the rels first
	if (image->rel) {
		TRACE(("total %i rel relocs\n", image->rel_len / (int)sizeof(Elf_Rel)));

		status = arch_elf_relocate_rel(image, resolveImage, image->rel,
			image->rel_len);
		if (status < B_OK)
			return status;
	}

	if (image->pltrel) {
		if (image->pltrel_type == DT_REL) {
			TRACE(("total %i plt-relocs\n",
				image->pltrel_len / (int)sizeof(Elf_Rel)));
			status = arch_elf_relocate_rel(image, resolveImage, image->pltrel,
				image->pltrel_len);
		} else {
			TRACE(("total %i plt-relocs\n",
				image->pltrel_len / (int)sizeof(Elf_Rela)));
			status = arch_elf_relocate_rela(image, resolveImage,
				(Elf_Rela *)image->pltrel, image->pltrel_len);
		}
		if (status < B_OK)
			return status;
	}

	if (image->rela) {
		TRACE(("total %i rel relocs\n",
			image->rela_len / (int)sizeof(Elf_Rela)));

		status = arch_elf_relocate_rela(image, resolveImage, image->rela,
			image->rela_len);
		if (status < B_OK)
			return status;
	}

	return status;
}


static int
verify_eheader(Elf_Ehdr *elfHeader)
{
	if (memcmp(elfHeader->e_ident, ELFMAG, 4) != 0)
		return B_NOT_AN_EXECUTABLE;

	if (elfHeader->e_ident[EI_CLASS] != ELF_CLASS)
		return B_NOT_AN_EXECUTABLE;

	if (elfHeader->e_ident[EI_DATA] != ELF_TARG_DATA)
		return B_NOT_AN_EXECUTABLE;

	if (elfHeader->e_machine != ELF_TARG_MACH)
		return B_NOT_AN_EXECUTABLE;

	if (elfHeader->e_phoff == 0)
		return B_NOT_AN_EXECUTABLE;

	if (elfHeader->e_phentsize < sizeof(Elf_Phdr))
		return B_NOT_AN_EXECUTABLE;

	return 0;
}


static void
unload_elf_image(struct elf_image_info *image)
{
	if (atomic_add(&image->ref_count, -1) > 1)
		return;

	TRACE(("unload image %" B_PRId32 ", %s\n", image->id, image->name));

	unregister_elf_image(image);
	delete_elf_image(image);
}


static status_t
load_elf_symbol_table(int fd, struct elf_image_info *image)
{
	Elf_Ehdr *elfHeader = image->elf_header;
	Elf_Sym *symbolTable = NULL;
	Elf_Shdr *stringHeader = NULL;
	uint32 numSymbols = 0;
	char *stringTable;
	status_t status;
	ssize_t length;
	int32 i;

	// get section headers

	ssize_t size = elfHeader->e_shnum * elfHeader->e_shentsize;
	Elf_Shdr *sectionHeaders = (Elf_Shdr *)malloc(size);
	if (sectionHeaders == NULL) {
		dprintf("error allocating space for section headers\n");
		return B_NO_MEMORY;
	}

	length = read_pos(fd, elfHeader->e_shoff, sectionHeaders, size);
	if (length < size) {
		TRACE(("error reading in program headers\n"));
		status = B_ERROR;
		goto error1;
	}

	// find symbol table in section headers

	for (i = 0; i < elfHeader->e_shnum; i++) {
		if (sectionHeaders[i].sh_type == SHT_SYMTAB) {
			stringHeader = &sectionHeaders[sectionHeaders[i].sh_link];

			if (stringHeader->sh_type != SHT_STRTAB) {
				TRACE(("doesn't link to string table\n"));
				status = B_BAD_DATA;
				goto error1;
			}

			// read in symbol table
			size = sectionHeaders[i].sh_size;
			symbolTable = (Elf_Sym *)malloc(size);
			if (symbolTable == NULL) {
				status = B_NO_MEMORY;
				goto error1;
			}

			length
				= read_pos(fd, sectionHeaders[i].sh_offset, symbolTable, size);
			if (length < size) {
				TRACE(("error reading in symbol table\n"));
				status = B_ERROR;
				goto error2;
			}

			numSymbols = size / sizeof(Elf_Sym);
			break;
		}
	}

	if (symbolTable == NULL) {
		TRACE(("no symbol table\n"));
		status = B_BAD_VALUE;
		goto error1;
	}

	// read in string table

	stringTable = (char *)malloc(size = stringHeader->sh_size);
	if (stringTable == NULL) {
		status = B_NO_MEMORY;
		goto error2;
	}

	length = read_pos(fd, stringHeader->sh_offset, stringTable, size);
	if (length < size) {
		TRACE(("error reading in string table\n"));
		status = B_ERROR;
		goto error3;
	}

	TRACE(("loaded %" B_PRId32 " debug symbols\n", numSymbols));

	// insert tables into image
	image->debug_symbols = symbolTable;
	image->num_debug_symbols = numSymbols;
	image->debug_string_table = stringTable;

	free(sectionHeaders);
	return B_OK;

error3:
	free(stringTable);
error2:
	free(symbolTable);
error1:
	free(sectionHeaders);

	return status;
}


static status_t
insert_preloaded_image(preloaded_elf_image *preloadedImage, bool kernel)
{
	status_t status;

	status = verify_eheader(&preloadedImage->elf_header);
	if (status != B_OK)
		return status;

	elf_image_info *image = create_image_struct();

	if (image == NULL)
		return B_NO_MEMORY;

	image->name = strdup(preloadedImage->name);
	image->dynamic_section = preloadedImage->dynamic_section.start;

#if defined(__ARM__)
	image->arm_exidx_base = preloadedImage->exidx_section.start;
	image->arm_exidx_count = preloadedImage->exidx_section.size / 8;
#endif

	image->num_regions = preloadedImage->count_regions;

	for(unsigned int i = 0 ; i < preloadedImage->count_regions ; ++i) {
		image->regions[i].id = preloadedImage->regions[i].id;
		image->regions[i].start = preloadedImage->regions[i].start;
		image->regions[i].size = preloadedImage->regions[i].size;
		image->regions[i].delta = preloadedImage->regions[i].delta;
		image->regions[i].protection = preloadedImage->regions[i].protection;
	}

	status = elf_parse_dynamic_section(image);
	if (status != B_OK)
		goto error1;

	status = init_image_version_infos(image);
	if (status != B_OK)
		goto error1;

	if (!kernel) {
		status = check_needed_image_versions(image);
		if (status != B_OK)
			goto error1;

		status = elf_relocate(image, sKernelImage);
		if (status != B_OK)
			goto error1;

		/* Set final protection attributes */
		for(unsigned int i = 0 ; i < preloadedImage->count_regions ; ++i) {
			set_area_protection(image->regions[i].id, image->regions[i].protection);
		}
	} else {
		sKernelImage = image;
	}

	// copy debug symbols to the kernel heap
	if (preloadedImage->debug_symbols != NULL) {
		int32 debugSymbolsSize = sizeof(Elf_Sym)
			* preloadedImage->num_debug_symbols;
		image->debug_symbols = (Elf_Sym*)malloc(debugSymbolsSize);
		if (image->debug_symbols != NULL) {
			memcpy(image->debug_symbols, preloadedImage->debug_symbols,
				debugSymbolsSize);
		}
	}
	image->num_debug_symbols = preloadedImage->num_debug_symbols;

	// copy debug string table to the kernel heap
	if (preloadedImage->debug_string_table != NULL) {
		image->debug_string_table = (char*)malloc(
			preloadedImage->debug_string_table_size);
		if (image->debug_string_table != NULL) {
			memcpy((void*)image->debug_string_table,
				preloadedImage->debug_string_table,
				preloadedImage->debug_string_table_size);
		}
	}

	register_elf_image(image);
	preloadedImage->id = image->id;
		// modules_init() uses this information to get the preloaded images

	return B_OK;

error1:
	delete_elf_image(image);

	preloadedImage->id = -1;

	return status;
}


//	#pragma mark - userland symbol lookup


class UserSymbolLookup {
public:
	static UserSymbolLookup& Default()
	{
		return sLookup;
	}

	status_t Init(Team* team)
	{
		// find the runtime loader debug area
		VMArea* area;
		for (VMAddressSpace::AreaIterator it
					= team->address_space->GetAreaIterator();
				(area = it.Next()) != NULL;) {
			if (strcmp(area->name, RUNTIME_LOADER_DEBUG_AREA_NAME) == 0)
				break;
		}

		if (area == NULL)
			return B_ERROR;

		// copy the runtime loader data structure
		if (!_Read((runtime_loader_debug_area*)area->Base(), fDebugArea))
			return B_BAD_ADDRESS;

		fTeam = team;
		return B_OK;
	}

	status_t LookupSymbolAddress(addr_t address, addr_t *_baseAddress,
		const char **_symbolName, const char **_imageName, bool *_exactMatch)
	{
		// Note, that this function doesn't find all symbols that we would like
		// to find. E.g. static functions do not appear in the symbol table
		// as function symbols, but as sections without name and size. The
		// .symtab section together with the .strtab section, which apparently
		// differ from the tables referred to by the .dynamic section, also
		// contain proper names and sizes for those symbols. Therefore, to get
		// completely satisfying results, we would need to read those tables
		// from the shared object.

		// get the image for the address
		image_t image;
		status_t error = _FindImageAtAddress(address, image);
		if (error != B_OK) {
			// commpage requires special treatment since kernel stores symbol
			// information
			addr_t commPageAddress = (addr_t)fTeam->commpage_address;
			if (address >= commPageAddress
				&& address < commPageAddress + COMMPAGE_SIZE) {
				if (*_imageName)
					*_imageName = "commpage";
				address -= (addr_t)commPageAddress;
				error = elf_debug_lookup_symbol_address(address, _baseAddress,
					_symbolName, NULL, _exactMatch);
				if (_baseAddress)
					*_baseAddress += (addr_t)fTeam->commpage_address;
			}
			return error;
		}

		strlcpy(fImageName, image.name, sizeof(fImageName));

		const elf_region_t& textRegion = image.regions[0];

		// search the image for the symbol
		Elf_Sym symbolFound;
		addr_t deltaFound = INT_MAX;
		bool exactMatch = false;

		// to get rid of the erroneous "uninitialized" warnings
		symbolFound.st_name = 0;
		symbolFound.st_value = 0;

		for(Elf32_Word i = 0 ; i < image.dynsymcount ; ++i) {
			Elf_Sym symbol;

			if (!_Read(image.syms + i, symbol)) {
				continue;
			}

			// The symbol table contains not only symbols referring to
			// functions and data symbols within the shared object, but also
			// referenced symbols of other shared objects, as well as
			// section and file references. We ignore everything but
			// function and data symbols that have an st_value != 0 (0
			// seems to be an indication for a symbol defined elsewhere
			// -- couldn't verify that in the specs though).
			if ((ELF_ST_TYPE(symbol.st_info) != STT_FUNC && ELF_ST_TYPE(symbol.st_info) != STT_OBJECT)
				|| symbol.st_value == 0
				|| symbol.st_value + symbol.st_size + textRegion.delta
					> textRegion.vmstart + textRegion.size) {
				continue;
			}

			// skip symbols starting after the given address
			addr_t symbolAddress = symbol.st_value + textRegion.delta;

			if (symbolAddress > address)
				continue;

			addr_t symbolDelta = address - symbolAddress;

			if (symbolDelta < deltaFound) {
				deltaFound = symbolDelta;
				symbolFound = symbol;

				if (/* symbolDelta >= 0 &&*/ symbolDelta < symbol.st_size) {
					// exact match
					exactMatch = true;
					break;
				}
			}
		}

		if (_imageName)
			*_imageName = fImageName;

		if (_symbolName) {
			*_symbolName = NULL;

			if (deltaFound < INT_MAX) {
				if (_ReadString(image, symbolFound.st_name, fSymbolName,
						sizeof(fSymbolName))) {
					*_symbolName = fSymbolName;
				} else {
					// we can't get its name, so forget the symbol
					deltaFound = INT_MAX;
				}
			}
		}

		if (_baseAddress) {
			if (deltaFound < INT_MAX)
				*_baseAddress = symbolFound.st_value + textRegion.delta;
			else
				*_baseAddress = textRegion.vmstart;
		}

		if (_exactMatch)
			*_exactMatch = exactMatch;

		return B_OK;
	}

	status_t _FindImageAtAddress(addr_t address, image_t& image)
	{
		image_queue_t imageQueue;
		if (!_Read(fDebugArea.loaded_images, imageQueue))
			return B_BAD_ADDRESS;

		image_t* imageAddress = imageQueue.head;
		while (imageAddress != NULL) {
			if (!_Read(imageAddress, image))
				return B_BAD_ADDRESS;

			if (image.regions[0].vmstart <= address
				&& address < image.regions[0].vmstart + image.regions[0].size) {
				return B_OK;
			}

			imageAddress = image.next;
		}

		return B_ENTRY_NOT_FOUND;
	}

	bool _ReadString(const image_t& image, uint32 offset, char* buffer,
		size_t bufferSize)
	{
		const char* address = image.strtab + offset;

		if (!IS_USER_ADDRESS(address))
			return false;

		if (debug_debugger_running()) {
			return debug_strlcpy(B_CURRENT_TEAM, buffer, address, bufferSize)
				>= 0;
		}
		return user_strlcpy(buffer, address, bufferSize) >= 0;
	}

	template<typename T> bool _Read(const T* address, T& data);
		// gcc 2.95.3 doesn't like it defined in-place

private:
	Team*						fTeam;
	runtime_loader_debug_area	fDebugArea;
	char						fImageName[B_OS_NAME_LENGTH];
	char						fSymbolName[256];
	static UserSymbolLookup		sLookup;
};


template<typename T>
bool
UserSymbolLookup::_Read(const T* address, T& data)
{
	if (!IS_USER_ADDRESS(address))
		return false;

	if (debug_debugger_running())
		return debug_memcpy(B_CURRENT_TEAM, &data, address, sizeof(T)) == B_OK;
	return user_memcpy(&data, address, sizeof(T)) == B_OK;
}


UserSymbolLookup UserSymbolLookup::sLookup;
	// doesn't need construction, but has an Init() method


//	#pragma mark - public kernel API


status_t
get_image_symbol(image_id id, const char *name, int32 /* symbolClass*/,
	void **_symbol)
{
	struct elf_image_info *image;
	const Elf_Sym *symbol;
	status_t status = B_OK;

	TRACE(("get_image_symbol(%s)\n", name));

	mutex_lock(&sImageMutex);

	image = find_image(id);
	if (image == NULL) {
		status = B_BAD_IMAGE_ID;
		goto done;
	}

	symbol = elf_find_symbol(image, name, NULL, true);
	if (symbol == NULL || symbol->st_shndx == SHN_UNDEF) {
		status = B_ENTRY_NOT_FOUND;
		goto done;
	}

	// TODO: support the "symbolClass" parameter!

	TRACE(("found: %lx (%lx + %lx)\n",
		(unsigned long)symbol->st_value + image->regions[0].delta,
		(unsigned long)symbol->st_value, (unsigned long)image->regions[0].delta));

	*_symbol = (void *)(symbol->st_value + image->regions[0].delta);

done:
	mutex_unlock(&sImageMutex);
	return status;
}


//	#pragma mark - kernel private API


/*!	Looks up a symbol by address in all images loaded in kernel space.
	Note, if you need to call this function outside a debugger, make
	sure you fix locking and the way it returns its information, first!
*/
status_t
elf_debug_lookup_symbol_address(addr_t address, addr_t *_baseAddress,
	const char **_symbolName, const char **_imageName, bool *_exactMatch)
{
	struct elf_image_info *image;
	Elf_Sym *symbolFound = NULL;
	const char *symbolName = NULL;
	addr_t deltaFound = INT_MAX;
	bool exactMatch = false;
	status_t status;

	TRACE(("looking up %p\n", (void *)address));

	if (!sInitialized)
		return B_ERROR;

	//mutex_lock(&sImageMutex);

	image = find_image_at_address(address);
		// get image that may contain the address

	if (image != NULL) {
		addr_t symbolDelta;

		TRACE((" image %p, base = %p, size = %p\n", image,
			(void *)image->regions[0].start, (void *)image->regions[0].size));

		if (image->debug_symbols != NULL) {
			// search extended debug symbol table (contains static symbols)

			TRACE((" searching debug symbols...\n"));

			for (unsigned long i = 0; i < image->num_debug_symbols; i++) {
				Elf_Sym *symbol = &image->debug_symbols[i];

				if (symbol->st_value == 0 || symbol->st_size
						>= image->regions[0].size + image->regions[1].size)
					continue;

				symbolDelta
					= address - (symbol->st_value + image->regions[0].delta);
				if (/* symbolDelta >= 0 && */ symbolDelta < symbol->st_size)
					exactMatch = true;

				if (exactMatch || symbolDelta < deltaFound) {
					deltaFound = symbolDelta;
					symbolFound = symbol;
					symbolName = image->debug_string_table + symbol->st_name;

					if (exactMatch)
						break;
				}
			}
		} else {
			// search standard symbol lookup table

			TRACE((" searching standard symbols...\n"));

			for (unsigned long i = 0 ; i < image->dynsymcount ; ++i) {
				Elf_Sym *symbol = &image->syms[i];

				if (symbol->st_value == 0
					|| symbol->st_size >= image->regions[0].size
						+ image->regions[1].size)
					continue;

				symbolDelta = address - (long)(symbol->st_value
					+ image->regions[0].delta);
				if (/* symbolDelta >= 0 && */ symbolDelta < symbol->st_size)
					exactMatch = true;

				if (exactMatch || symbolDelta < deltaFound) {
					deltaFound = symbolDelta;
					symbolFound = symbol;
					symbolName = image->SymbolName(symbol);

					if (exactMatch)
						goto symbol_found;
				}
			}
		}
	}
symbol_found:

	if (symbolFound != NULL) {
		if (_symbolName)
			*_symbolName = symbolName;
		if (_imageName)
			*_imageName = image->name;
		if (_baseAddress)
			*_baseAddress = symbolFound->st_value + image->regions[0].delta;
		if (_exactMatch)
			*_exactMatch = exactMatch;

		status = B_OK;
	} else if (image != NULL) {
		TRACE(("symbol not found!\n"));

		if (_symbolName)
			*_symbolName = NULL;
		if (_imageName)
			*_imageName = image->name;
		if (_baseAddress)
			*_baseAddress = image->regions[0].start;
		if (_exactMatch)
			*_exactMatch = false;

		status = B_OK;
	} else {
		TRACE(("image not found!\n"));
		status = B_ENTRY_NOT_FOUND;
	}

	// Note, theoretically, all information we return back to our caller
	// would have to be locked - but since this function is only called
	// from the debugger, it's safe to do it this way

	//mutex_unlock(&sImageMutex);

	return status;
}


#ifdef __ARM__
status_t elf_arm_lookup_exidx_section(addr_t address, addr_t * _baseAddress, int * _count) {
	bool debugger_running = debug_debugger_running();
	status_t error;

	if(!debugger_running) {
		mutex_lock(&sImageMutex);
	}

	auto image = find_image_at_address(address);

	if(image) {
		*_baseAddress = image->arm_exidx_base;
		*_count = image->arm_exidx_count;
		error = B_OK;
	} else {
		error = B_ENTRY_NOT_FOUND;
	}

	if(!debugger_running) {
		mutex_unlock(&sImageMutex);
	}

	return error;
}
#endif

/*!	Tries to find a matching user symbol for the given address.
	Note that the given team's address space must already be in effect.
*/
status_t
elf_debug_lookup_user_symbol_address(Team* team, addr_t address,
	addr_t *_baseAddress, const char **_symbolName, const char **_imageName,
	bool *_exactMatch)
{
	if (team == NULL || team == team_get_kernel_team())
		return B_BAD_VALUE;

	UserSymbolLookup& lookup = UserSymbolLookup::Default();
	status_t error = lookup.Init(team);
	if (error != B_OK)
		return error;

	return lookup.LookupSymbolAddress(address, _baseAddress, _symbolName,
		_imageName, _exactMatch);
}


/*!	Looks up a symbol in all kernel images. Note, this function is thought to
	be used in the kernel debugger, and therefore doesn't perform any locking.
*/
addr_t
elf_debug_lookup_symbol(const char* searchName)
{
	struct elf_image_info *image = NULL;

	ImageHash::Iterator iterator(sImagesHash);
	while (iterator.HasNext()) {
		image = iterator.Next();
		if (image->num_debug_symbols > 0) {
			// search extended debug symbol table (contains static symbols)
			for (uint32 i = 0; i < image->num_debug_symbols; i++) {
				Elf_Sym *symbol = &image->debug_symbols[i];
				const char *name = image->debug_string_table + symbol->st_name;

				if (symbol->st_value > 0 && !strcmp(name, searchName))
					return symbol->st_value + image->regions[0].delta;
			}
		} else {
			// search standard symbol lookup table
			for (uint32 i = 0; i <  image->dynsymcount ; i++) {
				const Elf_Sym *symbol = &image->syms[i];
				const char *name = image->SymbolName(symbol);

				if (symbol->st_value > 0 && !strcmp(name, searchName))
					return symbol->st_value + image->regions[0].delta;
			}
		}
	}

	return 0;
}


status_t
elf_lookup_kernel_symbol(const char* name, elf_symbol_info* info)
{
	// find the symbol
	const Elf_Sym* foundSymbol = elf_find_symbol(sKernelImage, name, NULL, false);
	if (foundSymbol == NULL)
		return B_MISSING_SYMBOL;

	info->address = foundSymbol->st_value + sKernelImage->regions[0].delta;
	info->size = foundSymbol->st_size;
	return B_OK;
}

static int32 count_regions(const char * imagePath, const Elf_Phdr * pheaders, int32 phnum)
{
	int32 count = 0;
	int i;

	for (i = 0; i < phnum; ++i, ++pheaders) {
		switch (pheaders->p_type) {
			case PT_LOAD:
				++count;

				if (pheaders->p_align & (B_PAGE_SIZE - 1)) {
					dprintf("%s: PT_LOAD segment is not page aligned\n", imagePath);
					return -1;
				}

				if (pheaders->p_filesz != pheaders->p_memsz) {
					Elf_Addr bss_vaddr = (pheaders->p_vaddr + pheaders->p_filesz + B_PAGE_SIZE - 1) & ~addr_t(B_PAGE_SIZE - 1);
					Elf_Addr bss_vlimit = (pheaders->p_vaddr + pheaders->p_memsz + B_PAGE_SIZE - 1) & ~addr_t(B_PAGE_SIZE - 1);
					if(bss_vlimit > bss_vaddr) {
						++count;
					}
				}

				break;
			default:
				break;
		}
	}

	return count;
}

static status_t parse_program_headers(elf_image_info* image, const Elf_Phdr * pheader, int32 phnum, elf_region_t * regions)
{
	uint32 regcount;
	int32 i;

	regcount = 0;

	for (i = 0; i < phnum; ++i, ++pheader) {

		switch (pheader->p_type) {
			case PT_NULL:
				/* NOP header */
				break;
			case PT_LOAD:
				ASSERT(regcount < image->num_regions);

				regions[regcount].start = pheader->p_vaddr;
				regions[regcount].size = pheader->p_filesz;
				regions[regcount].vmstart = pheader->p_vaddr & ~addr_t(B_PAGE_SIZE - 1);
				regions[regcount].vmsize = ((pheader->p_vaddr + pheader->p_filesz + B_PAGE_SIZE - 1) &
						~addr_t(B_PAGE_SIZE - 1)) - regions[regcount].vmstart;
				regions[regcount].fdstart = pheader->p_offset;
				regions[regcount].fdsize = pheader->p_filesz;
				regions[regcount].delta = 0;
				regions[regcount].flags = 0;

				TRACE(("%s: vmstart=%p vmsize=%zd\n",
						image->name,
						(void *)regions[regcount].vmstart,
						(size_t)regions[regcount].vmsize));

				if(pheader->p_flags & PF_W) {
					regions[regcount].flags |= KFLAG_RW;
				}

				if(pheader->p_flags & PF_X) {
					regions[regcount].flags |= KFLAG_EXECUTABLE;
				}

				++regcount;

				if(pheader->p_filesz != pheader->p_memsz) {
					regions[regcount - 1].flags |= KFLAG_CLEAR_TRAILER;

					Elf_Addr bss_vaddr = (pheader->p_vaddr + pheader->p_filesz + B_PAGE_SIZE - 1) & ~addr_t(B_PAGE_SIZE - 1);
					Elf_Addr bss_vlimit = (pheader->p_vaddr + pheader->p_memsz + B_PAGE_SIZE - 1) & ~addr_t(B_PAGE_SIZE - 1);

					if(bss_vlimit > bss_vaddr) {
						ASSERT(regcount < image->num_regions);

						regions[regcount].start = pheader->p_vaddr + pheader->p_filesz;
						regions[regcount].size = pheader->p_memsz - pheader->p_filesz;
						regions[regcount].vmstart = bss_vaddr;
						regions[regcount].vmsize	= bss_vlimit - bss_vaddr;
						regions[regcount].fdstart = 0;
						regions[regcount].fdsize = 0;
						regions[regcount].delta = 0;
						regions[regcount].flags = KFLAG_ANON;

						TRACE(("%s: bss - vmstart=%p vmsize=%zd\n",
								image->name,
								(void *)regions[regcount].vmstart,
								(size_t)regions[regcount].vmsize));

						if(pheader->p_flags & PF_W) {
							regions[regcount].flags |= KFLAG_RW;
						}

						if(pheader->p_flags & PF_X) {
							regions[regcount].flags |= KFLAG_EXECUTABLE;
						}

						++regcount;
					}
				}

				break;
			case PT_DYNAMIC:
				image->dynamic_section = pheader->p_vaddr;
				break;
			case PT_INTERP:
				// should check here for appropiate interpreter
				break;
			case PT_NOTE:
				// unsupported
				break;
			case PT_SHLIB:
				// undefined semantics
				break;
			case PT_PHDR:
				// we don't use it
				break;
			case PT_GNU_RELRO:
				image->relro_page = pheader->p_vaddr & ~addr_t(B_PAGE_SIZE - 1);
				image->relro_size = (pheader->p_memsz + B_PAGE_SIZE - 1) & ~addr_t(B_PAGE_SIZE - 1);
				break;
			case PT_GNU_STACK:
				// we don't use it
				break;
			case PT_GNU_EH_FRAME:
				// don't care
				break;
			case PT_TLS:
				dprintf("%s: Found PT_TLS segment. Loading via kernel mode not allowed\n", image->name);
				break;
#if defined(__ARM__)
			case PT_ARM_EXIDX:
				image->exidx_base = pheader->p_vaddr;
				image->exidx_count = pheader->p_memsz / 8;
				break;
#endif
			default:
				dprintf("%s: Unhandled pheader type in parse 0x%lx\n", image->name, (unsigned long)pheader->p_type);
				return B_BAD_DATA;
		}
	}

	ASSERT(regcount == image->num_regions);

	return B_OK;
}

status_t
elf_load_user_image(const char *path, Team *team, int flags, addr_t *entry)
{
	Elf_Ehdr elfHeader;
	Elf_Phdr *programHeaders = NULL;
	char baseName[B_OS_NAME_LENGTH];
	status_t status;
	ssize_t length;
	int fd;
	int i;
	int countLoadSegments;
	elf_region_t regions[ELF_IMAGE_MAX_REGIONS];
	void * reservedAddress = nullptr;
	addr_t reservedLimit = 0;
	size_t reservedSize = 0;
	uint32 addressSpecifier;
	Elf_Addr base_vaddr;

	TRACE(("elf_load: entry path '%s', team %p\n", path, team));

	fd = _kern_open(-1, path, O_RDONLY, 0);
	if (fd < 0)
		return fd;

	struct stat st;
	status = _kern_read_stat(fd, NULL, false, &st, sizeof(st));
	if (status != B_OK)
		return status;

	// read and verify the ELF header

	length = _kern_read(fd, 0, &elfHeader, sizeof(elfHeader));
	if (length < B_OK) {
		status = length;
		goto error;
	}

	if (length != sizeof(elfHeader)) {
		// short read
		status = B_NOT_AN_EXECUTABLE;
		goto error;
	}
	status = verify_eheader(&elfHeader);
	if (status < B_OK)
		goto error;

	struct elf_image_info* image;
	image = create_image_struct();
	if (image == NULL) {
		status = B_NO_MEMORY;
		goto error;
	}
	image->elf_header = &elfHeader;

	// read program header

	programHeaders = (elf_phdr *)malloc(
		elfHeader.e_phnum * elfHeader.e_phentsize);
	if (programHeaders == NULL) {
		dprintf("error allocating space for program headers\n");
		status = B_NO_MEMORY;
		goto error2;
	}

	TRACE(("reading in program headers at 0x%lx, length 0x%x\n",
		elfHeader.e_phoff, elfHeader.e_phnum * elfHeader.e_phentsize));
	length = _kern_read(fd, elfHeader.e_phoff, programHeaders,
		elfHeader.e_phnum * elfHeader.e_phentsize);
	if (length < B_OK) {
		status = length;
		dprintf("error reading in program headers\n");
		goto error2;
	}
	if (length != elfHeader.e_phnum * elfHeader.e_phentsize) {
		dprintf("short read while reading in program headers\n");
		status = -1;
		goto error2;
	}

	// construct a nice name for the region we have to create below
	{
		int32 length;

		const char *leaf = strrchr(path, '/');
		if (leaf == NULL)
			leaf = path;
		else
			leaf++;

		length = strlen(leaf);
		if (length > B_OS_NAME_LENGTH - 8)
			sprintf(baseName, "...%s", leaf + length + 8 - B_OS_NAME_LENGTH);
		else
			strcpy(baseName, leaf);
	}

	// count numer of PT_LOAD segments
	countLoadSegments = count_regions(baseName, programHeaders, elfHeader.e_phnum);

	if(countLoadSegments == 0) {
		dprintf("%s: ELF has no PT_LOAD segments\n", baseName);
		status = B_NOT_AN_EXECUTABLE;
		goto error2;
	}

	if(countLoadSegments > ELF_IMAGE_MAX_REGIONS) {
		status = B_NOT_AN_EXECUTABLE;
		TRACE(("%s: image has too many (%d > %d) PT_LOAD segments. Increase ELF_IMAGE_MAX_REGIONS\n", baseName, countLoadSegments, ELF_IMAGE_MAX_REGIONS));
		goto error2;
	}

	image->num_regions = countLoadSegments;

	// map the program's segments into memory, initially with rw access
	// correct area protection will be set after relocation

	extended_image_info imageInfo;
	memset(&imageInfo, 0, sizeof(imageInfo));

	TRACE(("%s: %d loadable regions found\n", baseName, countLoadSegments));

	// Temporarily reset for printing
	image->name = baseName;
	status = parse_program_headers(image, programHeaders, elfHeader.e_phnum, regions);
	image->name = nullptr;

	if(status != B_OK) {
		TRACE(("%s: Can't parse program headers", baseName));
		goto error2;
	}

	reservedAddress = (void *)regions[0].vmstart;
	reservedLimit =
			regions[0].vmstart +
			(regions[image->num_regions - 1].vmstart +
			 regions[image->num_regions - 1].vmsize -
			 regions[0].vmstart);
	reservedSize = reservedLimit - regions[0].vmstart;
	addressSpecifier = regions[0].vmstart ? B_EXACT_ADDRESS : B_RANDOMIZED_ANY_ADDRESS;

	TRACE(("%s: Before reserve address %p -- %p\n", baseName, reservedAddress, (void *)reservedLimit));

	// reserve that space and allocate the areas from that one
	if (vm_reserve_address_range(team->id, &reservedAddress, addressSpecifier, reservedSize, 0) < B_OK) {
		TRACE(("%s: Failed to reserve kernel range\n", image->name));
		status = B_NO_MEMORY;
		goto error2;
	}

	TRACE(("%s: After reserve address %p -- %p\n", baseName, (void *)reservedAddress, (char *)reservedAddress + reservedSize));

	base_vaddr = regions[0].vmstart;

	for(uint32 i = 0 ; i < image->num_regions ; ++i)
	{
		char regionName[B_OS_NAME_LENGTH];
		elf_region_t * region = &regions[i];
		elf_region * image_region = &image->regions[i];

		Elf_Addr data_vaddr = regions[i].vmstart;
		Elf_Addr data_addr = (addr_t)reservedAddress + (data_vaddr - base_vaddr);

		if(region->flags & KFLAG_EXECUTABLE) {
			if(region->flags & KFLAG_RW) {
				snprintf(regionName, B_OS_NAME_LENGTH, "%s_rwtext%" B_PRIu32, baseName, i);
			} else {
				snprintf(regionName, B_OS_NAME_LENGTH, "%s_text%" B_PRIu32, baseName, i);
			}
		} else {
			if(region->flags & KFLAG_ANON) {
				snprintf(regionName, B_OS_NAME_LENGTH, "%s_bss%" B_PRIu32, baseName, i);
			} else if(region->flags & KFLAG_RW) {
				snprintf(regionName, B_OS_NAME_LENGTH, "%s_data%" B_PRIu32, baseName, i);
			} else {
				snprintf(regionName, B_OS_NAME_LENGTH, "%s_rodata%" B_PRIu32, baseName, i);
			}
		}

		image_region->start = data_addr;
		image_region->size = regions[i].vmsize;
		image_region->protection = B_READ_AREA
				| ((regions[i].flags & KFLAG_EXECUTABLE) ? B_EXECUTE_AREA : 0)
				| ((regions[i].flags & KFLAG_RW) != 0 ? B_WRITE_AREA : 0);
		image_region->delta = data_addr - regions[i].vmstart;

		TRACE(("%s: Region %" B_PRIu32 " has name %s with %p -- %p\n", baseName, i, regionName, (void *)image_region->start,
				(void*)(image_region->start + image_region->size)));

		if(region->flags & KFLAG_ANON)
		{
			void * regionAddress = (void *)image_region->start;;
			virtual_address_restrictions virtualRestrictions = {};
			virtualRestrictions.address = regionAddress;
			virtualRestrictions.address_specification = B_EXACT_ADDRESS;
			physical_address_restrictions physicalRestrictions = {};

			image_region->id = create_area_etc(team->id,
				regionName,
				image_region->size,
				B_NO_LOCK,
				image_region->protection,
				0,
				0,
				&virtualRestrictions,
				&physicalRestrictions,
				&regionAddress);

			if (image_region->id < B_OK) {
				dprintf("%s: error allocating .bss area: %s\n", baseName,
					strerror(image_region->id));
				status = B_NOT_AN_EXECUTABLE;
				goto error3;
			}
		} else {
			void * regionAddress = (void *)image_region->start;;
			image_region->id = vm_map_file(team->id,
					regionName,
					&regionAddress,
					B_EXACT_ADDRESS,
					image_region->size,
					image_region->protection,
					REGION_PRIVATE_MAP,
					false,
					fd,
					ROUNDDOWN(regions[i].fdstart, B_PAGE_SIZE));

			if (image_region->id < B_OK) {
				dprintf("%s: error allocating file area: %s\n", image->name,
					strerror(image_region->id));
				status = B_NOT_AN_EXECUTABLE;
				goto error3;
			}

			if(regions[i].flags & KFLAG_CLEAR_TRAILER) {
				addr_t startClearing = data_addr
					+ (regions[i].start & (B_PAGE_SIZE - 1))
					+ regions[i].size;

				addr_t toClear = regions[i].vmsize
					- (regions[i].start & (B_PAGE_SIZE - 1))
					- regions[i].size;

				TRACE(("%s: Need to clear %p -- %p\n", baseName, (void *)startClearing, (void *)(startClearing + toClear)));

				if(toClear > 0) {
					if(!(image_region->protection & B_WRITE_AREA)) {
						status = vm_set_area_protection(team->id, image_region->id, B_READ_AREA | B_WRITE_AREA, false);

						if(status != B_OK) {
							dprintf("%s: Can't change area protection to R/W\n", baseName);
							goto error3;
						}
					}

					status = lock_memory_etc(team->id, (void *)startClearing, toClear, 0);

					if(status != B_OK) {
						dprintf("%s: Can't lock memory for writing for %p\n", baseName, (void *)startClearing);
						goto error3;
					}

					status = user_memset((void *)startClearing, 0, toClear);

					unlock_memory_etc(team->id, (void *)startClearing, toClear, 0);

					if(status != B_OK) {
						dprintf("%s: Can't clear memory for %p\n", baseName, (void *)startClearing);
						goto error3;
					}

					if(!(image_region->protection & B_WRITE_AREA)) {
						status = vm_set_area_protection(team->id, image_region->id, image_region->protection, false);

						if(status != B_OK) {
							dprintf("%s: Can't restore area protection to original value\n", baseName);
							goto error3;
						}
					}
				}
			}
		}
	}

	if(image->relro_page) {
		image->relro_page += image->regions[0].delta;
	}

	// modify the dynamic ptr by the delta of the regions
	if(image->dynamic_section) {
		image->dynamic_section += image->regions[0].delta;
	}

#if defined(__ARM__)
	if (image->exidx_count) {
		image->exidx_base = += image->text_region.delta;
	}
#endif

	set_ac();
	status = elf_parse_dynamic_section(image);
	if (status != B_OK) {
		clear_ac();
		goto error2;
	}

	status = elf_handle_textrel(team->id, baseName, image, true, false);
	if (status != B_OK) {
		clear_ac();
		goto error2;
	}

	status = elf_relocate(image, image);
	if (status != B_OK) {
		clear_ac();
		goto error2;
	}

	status = elf_handle_textrel(team->id, baseName, image, false, false);
	if (status != B_OK) {
		clear_ac();
		goto error2;
	}

	clear_ac();

	// register the loaded image
	imageInfo.basic_info.type = B_LIBRARY_IMAGE;
	imageInfo.basic_info.device = st.st_dev;
	imageInfo.basic_info.node = st.st_ino;
	strlcpy(imageInfo.basic_info.name, path, sizeof(imageInfo.basic_info.name));

	imageInfo.basic_info.api_version = B_HAIKU_VERSION;
	imageInfo.basic_info.abi = B_HAIKU_ABI;
		// TODO: Get the actual values for the shared object. Currently only
		// the runtime loader is loaded, so this is good enough for the time
		// being.

	imageInfo.text_delta = image->regions[0].delta;
	imageInfo.symbol_table = image->syms;
	imageInfo.buckets = image->buckets;
	imageInfo.nbuckets = image->nbuckets;
	imageInfo.chains = image->chains;
	imageInfo.nchains = image->nchains;
	imageInfo.nbuckets_gnu = image->nbuckets_gnu;
	imageInfo.symndx_gnu = image->symndx_gnu;
	imageInfo.maskwords_bm_gnu = image->maskwords_bm_gnu;
	imageInfo.shift2_gnu = image->shift2_gnu;
	imageInfo.dynsymcount = image->dynsymcount;
	imageInfo.bloom_gnu = image->bloom_gnu;
	imageInfo.buckets_gnu = image->buckets_gnu;
	imageInfo.chain_zero_gnu = image->chain_zero_gnu;
	imageInfo.valid_hash_sysv = image->valid_hash_sysv;
	imageInfo.valid_hash_gnu = image->valid_hash_gnu;

	imageInfo.basic_info.id = register_image(team, &imageInfo,
		sizeof(imageInfo));
	if (imageInfo.basic_info.id >= 0 && team_get_current_team_id() == team->id)
		user_debug_image_created(&imageInfo.basic_info);
		// Don't care, if registering fails. It's not crucial.

	TRACE(("elf_load: done!\n"));

	// Reset regions as we won't touch them
	for(uint32 i = 0 ; i < image->num_regions ; ++i) {
		image->regions[i].id = -1;
	}

	*entry = elfHeader.e_entry + image->regions[0].delta;
	status = B_OK;

error3:
	vm_unreserve_address_range(team->id, reservedAddress, reservedSize);

error2:
	image->elf_header = NULL;
	delete_elf_image(image);

error:
	free(programHeaders);
	_kern_close(fd);

	return status;
}


image_id
load_kernel_add_on(const char *path)
{
	Elf_Phdr *programHeaders;
	Elf_Ehdr *elfHeader;
	struct elf_image_info *image;
	const char *fileName;
	status_t status;
	ssize_t length;
	elf_region_t regions[ELF_IMAGE_MAX_REGIONS];
	void * reservedAddress = nullptr;
	addr_t reservedLimit = 0;
	size_t reservedSize = 0;
	uint32 addressSpecifier;
	int countHeaders;
	Elf_Addr base_vaddr;

	TRACE(("elf_load_kspace: entry path '%s'\n", path));

	int fd = _kern_open(-1, path, O_RDONLY, 0);
	if (fd < 0)
		return fd;

	struct vnode *vnode;
	status = vfs_get_vnode_from_fd(fd, true, &vnode);
	if (status < B_OK)
		goto error0;

	// get the file name
	fileName = strrchr(path, '/');
	if (fileName == NULL)
		fileName = path;
	else
		fileName++;

	// Prevent someone else from trying to load this image
	mutex_lock(&sImageLoadMutex);

	// make sure it's not loaded already. Search by vnode
	image = find_image_by_vnode(vnode);
	if (image) {
		atomic_add(&image->ref_count, 1);
		goto done;
	}

	elfHeader = (Elf_Ehdr *)malloc(sizeof(*elfHeader));
	if (!elfHeader) {
		status = B_NO_MEMORY;
		goto error;
	}

	length = _kern_read(fd, 0, elfHeader, sizeof(*elfHeader));
	if (length < B_OK) {
		status = length;
		goto error1;
	}
	if (length != sizeof(*elfHeader)) {
		// short read
		status = B_NOT_AN_EXECUTABLE;
		goto error1;
	}
	status = verify_eheader(elfHeader);
	if (status < B_OK)
		goto error1;

	image = create_image_struct();
	if (!image) {
		status = B_NO_MEMORY;
		goto error1;
	}
	image->vnode = vnode;
	image->elf_header = elfHeader;
	image->name = strdup(path);
	vnode = NULL;

	programHeaders = (Elf_Phdr *)malloc(elfHeader->e_phnum
		* elfHeader->e_phentsize);

	if (programHeaders == NULL) {
		dprintf("%s: error allocating space for program headers\n", fileName);
		status = B_NO_MEMORY;
		goto error2;
	}

	TRACE(("reading in program headers at 0x%lx, length 0x%x\n",
		(unsigned long)elfHeader->e_phoff, elfHeader->e_phnum * elfHeader->e_phentsize));

	length = _kern_read(fd, elfHeader->e_phoff, programHeaders,
		elfHeader->e_phnum * elfHeader->e_phentsize);
	if (length < B_OK) {
		status = length;
		TRACE(("%s: error reading in program headers\n", fileName));
		goto error3;
	}
	if (length != elfHeader->e_phnum * elfHeader->e_phentsize) {
		TRACE(("%s: short read while reading in program headers\n", fileName));
		status = B_ERROR;
		goto error3;
	}

	countHeaders = count_regions(fileName, programHeaders, elfHeader->e_phnum);

	if(countHeaders == 0) {
		status = B_NOT_AN_EXECUTABLE;
		TRACE(("%s: image has no PT_LOAD segments\n", fileName));
		goto error3;
	}

	if(countHeaders > ELF_IMAGE_MAX_REGIONS) {
		status = B_NOT_AN_EXECUTABLE;
		TRACE(("%s: image has too many (%d > %d) PT_LOAD segments. Increase ELF_IMAGE_MAX_REGIONS\n", fileName, countHeaders, ELF_IMAGE_MAX_REGIONS));
		goto error3;
	}

	image->num_regions = countHeaders;

	TRACE(("%s: %d loadable regions found\n", fileName, countHeaders));

	status = parse_program_headers(image, programHeaders, elfHeader->e_phnum, regions);

	if(status != B_OK) {
		goto error3;
	}

	reservedAddress = (void *)regions[0].vmstart;
	reservedLimit =
			regions[0].vmstart +
			(regions[image->num_regions - 1].vmstart +
			 regions[image->num_regions - 1].vmsize -
			 regions[0].vmstart);
	reservedSize = reservedLimit - regions[0].vmstart;
	addressSpecifier = regions[0].vmstart ? B_EXACT_ADDRESS : B_ANY_KERNEL_ADDRESS;

	TRACE(("%s: Before reserve address %p -- %p\n", image->name, reservedAddress, (void *)reservedLimit));

	// reserve that space and allocate the areas from that one
	if (vm_reserve_address_range(VMAddressSpace::KernelID(), &reservedAddress, addressSpecifier, reservedSize, 0) < B_OK) {
		TRACE(("%s: Failed to reserve kernel range\n", image->name));
		status = B_NO_MEMORY;
		goto error3;
	}

	TRACE(("%s: After reserve address %p -- %p\n", image->name, (void *)reservedAddress, (char *)reservedAddress + reservedSize));

	base_vaddr = regions[0].vmstart;

	for(uint32 i = 0 ; i < image->num_regions ; ++i)
	{
		char regionName[B_OS_NAME_LENGTH];
		elf_region_t * region = &regions[i];
		elf_region * image_region = &image->regions[i];

		Elf_Addr data_vaddr = regions[i].vmstart;
		Elf_Addr data_addr = (addr_t)reservedAddress + (data_vaddr - base_vaddr);

		if(region->flags & KFLAG_EXECUTABLE) {
			if(region->flags & KFLAG_RW) {
				snprintf(regionName, B_OS_NAME_LENGTH, "%s_rwtext", fileName);
			} else {
				snprintf(regionName, B_OS_NAME_LENGTH, "%s_text", fileName);
			}
		} else {
			if(region->flags & KFLAG_ANON) {
				snprintf(regionName, B_OS_NAME_LENGTH, "%s_bss", fileName);
			} else if(region->flags & KFLAG_RW) {
				snprintf(regionName, B_OS_NAME_LENGTH, "%s_data", fileName);
			} else {
				snprintf(regionName, B_OS_NAME_LENGTH, "%s_rodata", fileName);
			}
		}

		TRACE(("%s: Region %" B_PRIu32 " has name %s\n", image->name, i, regionName));

		image_region->start = data_addr;
		image_region->size = regions[i].vmsize;
		image_region->protection = B_KERNEL_READ_AREA
				| ((regions[i].flags & KFLAG_EXECUTABLE) ? B_KERNEL_EXECUTE_AREA : 0)
				| ((regions[i].flags & KFLAG_RW) != 0 ? B_KERNEL_WRITE_AREA : 0);
		image_region->delta = data_addr - regions[i].vmstart;

		if(region->flags & KFLAG_ANON)
		{
			// .bss segments have final protection set
			image_region->id = create_area(regionName,
				(void **)&image_region->start,
				B_EXACT_ADDRESS,
				image_region->size,
				B_FULL_LOCK,
				image_region->protection);

			if (image_region->id < B_OK) {
				dprintf("%s: error allocating .bss area: %s\n", image->name,
					strerror(image_region->id));
				status = B_NOT_AN_EXECUTABLE;
				goto error4;
			}
		} else {
			// Create R/W segment for file mappings
			image_region->id = create_area(regionName,
				(void **)&image_region->start,
				B_EXACT_ADDRESS,
				image_region->size,
				B_FULL_LOCK,
				B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);

			if (image_region->id < B_OK) {
				dprintf("%s: error allocating file area: %s\n", image->name,
					strerror(image_region->id));
				status = B_NOT_AN_EXECUTABLE;
				goto error4;
			}

			length = _kern_read(fd,
				region->fdstart,
				(void *)(image_region->start + (region->start & (B_PAGE_SIZE - 1))),
				region->fdsize);

			if (length < B_OK) {
				status = length;
				dprintf("%s: error reading in segment %" B_PRIu32 "\n", fileName, i);
				goto error5;
			}
		}
	}

	if(image->dynamic_section) {
		image->dynamic_section += image->regions[0].delta;
	}

	if(image->relro_page) {
		image->relro_page += image->regions[0].delta;
	}

#if defined(__ARM__)
	if(image->exidx_base) {
		image->exidx_base += image->regions[0].delta;
	}
#endif

	status = elf_parse_dynamic_section(image);
	if (status < B_OK)
		goto error5;

	status = init_image_version_infos(image);
	if (status != B_OK)
		goto error5;

	status = check_needed_image_versions(image);
	if (status != B_OK)
		goto error5;

	status = elf_handle_textrel(VMAddressSpace::KernelID(), image->name, image, true, true);
	if (status != B_OK)
		goto error5;

	status = elf_relocate(image, sKernelImage);
	if (status < B_OK)
		goto error5;

	status = elf_handle_textrel(VMAddressSpace::KernelID(), image->name, image, false, true);
	if (status != B_OK)
		goto error5;

	for(uint32 i = 0 ; i < image->num_regions ; ++i) {
		if(!(regions[i].flags & KFLAG_ANON)) {
			set_area_protection(image->regions[i].id, image->regions[i].protection);
		}
	}

	// There might be a hole between the two segments, and we don't need to
	// reserve this any longer
	vm_unreserve_address_range(VMAddressSpace::KernelID(), reservedAddress,
		reservedSize);

	// ToDo: this should be enabled by kernel settings!
	if (1)
		load_elf_symbol_table(fd, image);

	free(programHeaders);
	mutex_lock(&sImageMutex);
	register_elf_image(image);
	mutex_unlock(&sImageMutex);

done:
	_kern_close(fd);
	mutex_unlock(&sImageLoadMutex);

	return image->id;

error5:
error4:
	vm_unreserve_address_range(VMAddressSpace::KernelID(), reservedAddress,
		reservedSize);
error3:
	free(programHeaders);
error2:
	delete_elf_image(image);
	elfHeader = NULL;
error1:
	free(elfHeader);
error:
	mutex_unlock(&sImageLoadMutex);
error0:
	dprintf("Could not load kernel add-on \"%s\": %s\n", path,
		strerror(status));

	if (vnode)
		vfs_put_vnode(vnode);
	_kern_close(fd);

	return status;
}


status_t
unload_kernel_add_on(image_id id)
{
	MutexLocker _(sImageLoadMutex);
	MutexLocker _2(sImageMutex);

	elf_image_info *image = find_image(id);
	if (image == NULL)
		return B_BAD_IMAGE_ID;

	GDB_STATE(r_debug::RT_DELETE,&image->linkmap);
	unload_elf_image(image);
	GDB_STATE(r_debug::RT_CONSISTENT,NULL);

	return B_OK;
}


struct elf_image_info*
elf_get_kernel_image()
{
	return sKernelImage;
}


status_t
elf_get_image_info_for_address(addr_t address, image_info* info)
{
	MutexLocker _(sImageMutex);
	struct elf_image_info* elfInfo = find_image_at_address(address);
	if (elfInfo == NULL)
		return B_ENTRY_NOT_FOUND;

	info->id = elfInfo->id;
	info->type = B_SYSTEM_IMAGE;
	info->sequence = 0;
	info->init_order = 0;
	info->init_routine = NULL;
	info->term_routine = NULL;
	info->device = -1;
	info->node = -1;
		// TODO: We could actually fill device/node in.
	strlcpy(info->name, elfInfo->name, sizeof(info->name));

	size_t text_size, data_size;

	size_elf_image(elfInfo,
			info->text,
			text_size,
			info->data,
			data_size);

	info->text_size = text_size;
	info->data_size = data_size;

	return B_OK;
}


image_id
elf_create_memory_image(const char* imageName, addr_t text, size_t textSize,
	addr_t data, size_t dataSize)
{
	// allocate the image
	elf_image_info* image = create_image_struct();
	if (image == NULL)
		return B_NO_MEMORY;
	MemoryDeleter imageDeleter(image);

	// allocate symbol and string tables -- we allocate an empty symbol table,
	// so that elf_debug_lookup_symbol_address() won't try the dynamic symbol
	// table, which we don't have.
	Elf_Sym* symbolTable = (Elf_Sym*)malloc(0);
	char* stringTable = (char*)malloc(1);
	MemoryDeleter symbolTableDeleter(symbolTable);
	MemoryDeleter stringTableDeleter(stringTable);
	if (symbolTable == NULL || stringTable == NULL)
		return B_NO_MEMORY;

	// the string table always contains the empty string
	stringTable[0] = '\0';

	image->debug_symbols = symbolTable;
	image->num_debug_symbols = 0;
	image->debug_string_table = stringTable;

	// dup image name
	image->name = strdup(imageName);
	if (image->name == NULL)
		return B_NO_MEMORY;

	image->num_regions = 2;

	image->regions[0].id = -1;
	image->regions[0].start = text;
	image->regions[0].size = textSize;
	image->regions[0].delta = 0;
	image->regions[0].protection = B_READ_AREA | B_EXECUTE_AREA;

	image->regions[1].id = -1;
	image->regions[1].start = data;
	image->regions[1].size = dataSize;
	image->regions[1].delta = 0;
	image->regions[1].protection = B_READ_AREA | B_WRITE_AREA;

	mutex_lock(&sImageMutex);
	register_elf_image(image);
	image_id imageID = image->id;
	mutex_unlock(&sImageMutex);

	// keep the allocated memory
	imageDeleter.Detach();
	symbolTableDeleter.Detach();
	stringTableDeleter.Detach();

	return imageID;
}


status_t
elf_add_memory_image_symbol(image_id id, const char* name, addr_t address,
	size_t size, int32 type)
{
	MutexLocker _(sImageMutex);

	// get the image
	struct elf_image_info* image = find_image(id);
	if (image == NULL)
		return B_ENTRY_NOT_FOUND;

	// get the current string table size
	size_t stringTableSize = 1;
	if (image->num_debug_symbols > 0) {
		for (int32 i = image->num_debug_symbols - 1; i >= 0; i--) {
			int32 nameIndex = image->debug_symbols[i].st_name;
			if (nameIndex != 0) {
				stringTableSize = nameIndex
					+ strlen(image->debug_string_table + nameIndex) + 1;
				break;
			}
		}
	}

	// enter the name in the string table
	char* stringTable = (char*)image->debug_string_table;
	size_t stringIndex = 0;
	if (name != NULL) {
		size_t nameSize = strlen(name) + 1;
		stringIndex = stringTableSize;
		stringTableSize += nameSize;
		stringTable = (char*)realloc((char*)image->debug_string_table,
			stringTableSize);
		if (stringTable == NULL)
			return B_NO_MEMORY;
		image->debug_string_table = stringTable;
		memcpy(stringTable + stringIndex, name, nameSize);
	}

	// resize the symbol table
	int32 symbolCount = image->num_debug_symbols + 1;
	Elf_Sym* symbolTable = (Elf_Sym*)realloc(
		(Elf_Sym*)image->debug_symbols, sizeof(Elf_Sym) * symbolCount);
	if (symbolTable == NULL)
		return B_NO_MEMORY;
	image->debug_symbols = symbolTable;

	// enter the symbol
	Elf_Sym& symbol = symbolTable[symbolCount - 1];
	symbol.st_info = ELF_ST_INFO(STB_GLOBAL, type == B_SYMBOL_TYPE_DATA ? STT_OBJECT : STT_FUNC);
	symbol.st_name = stringIndex;
	symbol.st_value = address;
	symbol.st_size = size;
	symbol.st_other = 0;
	symbol.st_shndx = 0;
	image->num_debug_symbols++;

	return B_OK;
}


/*!	Reads the symbol and string table for the kernel image with the given ID.
	\a _symbolCount and \a _stringTableSize are both in- and output parameters.
	When called they call the size of the buffers given by \a symbolTable and
	\a stringTable respectively. When the function returns successfully, they
	will contain the actual sizes (which can be greater than the original ones).
	The function will copy as much as possible into the buffers. For only
	getting the required buffer sizes, it can be invoked with \c NULL buffers.
	On success \a _imageDelta will contain the offset to be added to the symbol
	values in the table to get the actual symbol addresses.
*/
status_t
elf_read_kernel_image_symbols(image_id id, Elf_Sym* symbolTable,
	int32* _symbolCount, char* stringTable, size_t* _stringTableSize,
	addr_t* _imageDelta, bool kernel)
{
	// check params
	if (_symbolCount == NULL || _stringTableSize == NULL)
		return B_BAD_VALUE;
	if (!kernel) {
		if (!IS_USER_ADDRESS(_symbolCount) || !IS_USER_ADDRESS(_stringTableSize)
			|| (_imageDelta != NULL && !IS_USER_ADDRESS(_imageDelta))
			|| (symbolTable != NULL && !IS_USER_ADDRESS(symbolTable))
			|| (stringTable != NULL && !IS_USER_ADDRESS(stringTable))) {
			return B_BAD_ADDRESS;
		}
	}

	// get buffer sizes
	int32 maxSymbolCount;
	size_t maxStringTableSize;
	if (kernel) {
		maxSymbolCount = *_symbolCount;
		maxStringTableSize = *_stringTableSize;
	} else {
		if (user_memcpy(&maxSymbolCount, _symbolCount, sizeof(maxSymbolCount))
				!= B_OK
			|| user_memcpy(&maxStringTableSize, _stringTableSize,
				sizeof(maxStringTableSize)) != B_OK) {
			return B_BAD_ADDRESS;
		}
	}

	// find the image
	MutexLocker _(sImageMutex);
	struct elf_image_info* image = find_image(id);
	if (image == NULL)
		return B_ENTRY_NOT_FOUND;

	// get the tables and infos
	addr_t imageDelta = image->regions[0].delta;
	const Elf_Sym* symbols;
	int32 symbolCount;
	const char* strings;

	if (image->debug_symbols != NULL) {
		symbols = image->debug_symbols;
		symbolCount = image->num_debug_symbols;
		strings = image->debug_string_table;
	} else {
		symbols = image->syms;
		symbolCount = image->dynsymcount;
		strings = image->strtab;
	}

	// The string table size isn't stored in the elf_image_info structure. Find
	// out by iterating through all symbols.
	size_t stringTableSize = 0;
	for (int32 i = 0; i < symbolCount; i++) {
		size_t index = symbols[i].st_name;
		if (index > stringTableSize)
			stringTableSize = index;
	}
	stringTableSize += strlen(strings + stringTableSize) + 1;
		// add size of the last string

	// copy symbol table
	int32 symbolsToCopy = min_c(symbolCount, maxSymbolCount);
	if (symbolTable != NULL && symbolsToCopy > 0) {
		if (kernel) {
			memcpy(symbolTable, symbols, sizeof(Elf_Sym) * symbolsToCopy);
		} else if (user_memcpy(symbolTable, symbols,
				sizeof(Elf_Sym) * symbolsToCopy) != B_OK) {
			return B_BAD_ADDRESS;
		}
	}

	// copy string table
	size_t stringsToCopy = min_c(stringTableSize, maxStringTableSize);
	if (stringTable != NULL && stringsToCopy > 0) {
		if (kernel) {
			memcpy(stringTable, strings, stringsToCopy);
		} else {
			if (user_memcpy(stringTable, strings, stringsToCopy)
					!= B_OK) {
				return B_BAD_ADDRESS;
			}
		}
	}

	// copy sizes
	if (kernel) {
		*_symbolCount = symbolCount;
		*_stringTableSize = stringTableSize;
		if (_imageDelta != NULL)
			*_imageDelta = imageDelta;
	} else {
		if (user_memcpy(_symbolCount, &symbolCount, sizeof(symbolCount)) != B_OK
			|| user_memcpy(_stringTableSize, &stringTableSize,
					sizeof(stringTableSize)) != B_OK
			|| (_imageDelta != NULL && user_memcpy(_imageDelta, &imageDelta,
					sizeof(imageDelta)) != B_OK)) {
			return B_BAD_ADDRESS;
		}
	}

	return B_OK;
}


status_t
elf_init(kernel_args *args)
{
	struct preloaded_image *image;

	image_init();

    _r_debug.r_brk = r_debug_state;
    _r_debug.r_state = r_debug::RT_CONSISTENT;

	sImagesHash = new(std::nothrow) ImageHash();
	if (sImagesHash == NULL)
		return B_NO_MEMORY;
	status_t init = sImagesHash->Init(IMAGE_HASH_SIZE);
	if (init != B_OK)
		return init;

	// Build a image structure for the kernel, which has already been loaded.
	// The preloaded_images were already prepared by the VM.
	image = args->kernel_image;
	if (insert_preloaded_image(static_cast<preloaded_elf_image *>(image),
			true) < B_OK)
		panic("could not create kernel image.\n");

	// Build image structures for all preloaded images.
	for (image = args->preloaded_images; image != NULL; image = image->next)
		insert_preloaded_image(static_cast<preloaded_elf_image *>(image),
			false);

    r_debug_state(NULL, &sKernelImage->linkmap); /* say hello to gdb! */

	add_debugger_command("ls", &dump_address_info,
		"lookup symbol for a particular address");
	add_debugger_command("symbols", &dump_symbols, "dump symbols for image");
	add_debugger_command("symbol", &dump_symbol, "search symbol in images");
	add_debugger_command_etc("image", &dump_image, "dump image info",
		"Prints info about the specified image.\n"
		"  <image>  - pointer to the semaphore structure, or ID\n"
		"           of the image to print info for.\n", 0);

    _r_debug_postinit(&sKernelImage->linkmap);

	sInitialized = true;
	return B_OK;
}


// #pragma mark -


/*!	Reads the symbol and string table for the kernel image with the given ID.
	\a _symbolCount and \a _stringTableSize are both in- and output parameters.
	When called they call the size of the buffers given by \a symbolTable and
	\a stringTable respectively. When the function returns successfully, they
	will contain the actual sizes (which can be greater than the original ones).
	The function will copy as much as possible into the buffers. For only
	getting the required buffer sizes, it can be invoked with \c NULL buffers.
	On success \a _imageDelta will contain the offset to be added to the symbol
	values in the table to get the actual symbol addresses.
*/
status_t
_user_read_kernel_image_symbols(image_id id, Elf_Sym* symbolTable,
	int32* _symbolCount, char* stringTable, size_t* _stringTableSize,
	addr_t* _imageDelta)
{
	return elf_read_kernel_image_symbols(id, symbolTable, _symbolCount,
		stringTable, _stringTableSize, _imageDelta, false);
}

int dl_iterate_phdr(__dl_iterate_hdr_callback callback, void * arg)
{
	return -1;
}
