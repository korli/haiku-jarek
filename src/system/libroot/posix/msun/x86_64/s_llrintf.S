#include <system/arch/x86_64/asm_defs.h>
	
/* sizeof(long) == sizeof(long long) */
#define	fn	llrintf
#include "s_lrintf.S"
