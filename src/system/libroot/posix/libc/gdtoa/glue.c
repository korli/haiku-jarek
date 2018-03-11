/*
 * Machine-independent glue to integrate David Gay's gdtoa
 * package into libc.
 *
 * $FreeBSD$
 */

#include <shared/locks.h>

struct mutex __gdtoa_locks[] = {
	MUTEX_INITIALIZER("gdtoa"),
	MUTEX_INITIALIZER("gdtoa")
};
