/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <sys/types.h>
#include <stdint.h>

static uint64_t cv_factor;
static uint64_t cv_factor_nsec;

extern "C" void __aarch64_setup_system_time(uint64_t cv, uint64_t cv_nsec)
{
	cv_factor = cv;
	cv_factor_nsec = cv_nsec;
}

extern "C" int64_t system_time()
{
	uint64_t counter;
	__asm__ __volatile__("mrs %0, cntvct_el0" : "=&r"(counter));
	__uint128_t time = static_cast<__uint128_t>(counter) * cv_factor;
	return time >> 64;
}

extern "C" int64_t system_time_nsecs()
{
	uint64_t counter;
	__asm__ __volatile__("mrs %0, cntvct_el0" : "=&r"(counter));
	__uint128_t t = static_cast<__uint128_t>(counter) * cv_factor_nsec;
	return t >> 32;
}
