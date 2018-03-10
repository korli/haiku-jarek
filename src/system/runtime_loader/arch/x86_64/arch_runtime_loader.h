#ifndef _RUNTIME_LOADER_ARCH_X86_64_ARCH_RUNTIME_LOADER_H_
#define _RUNTIME_LOADER_ARCH_X86_64_ARCH_RUNTIME_LOADER_H_

#include <sys/types.h>

#define round(size, align) \
    (((size) + (align) - 1) & ~((align) - 1))
#define calculate_first_tls_offset(size, align) \
	round(size, align)
#define calculate_tls_offset(prev_offset, prev_size, size, align) \
	round((prev_offset) + (size), align)
#define calculate_tls_end(off, size) 	(off)

typedef struct {
    unsigned long ti_module;
    unsigned long ti_offset;
} tls_index;

extern "C" {

void *__tls_get_addr(tls_index *ti);

}

#endif /* _RUNTIME_LOADER_ARCH_X86_64_ARCH_RUNTIME_LOADER_H_ */
