#include <system/arch/x86_64/asm_defs.h>

/* sizeof(long) == sizeof(long long) */
#define	fn	llrint
#include "s_lrint.S"
