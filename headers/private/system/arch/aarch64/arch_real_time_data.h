/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#ifndef _SYSTEM_ARCH_AARCH64_ARCH_REAL_TIME_DATA_H_
#define _SYSTEM_ARCH_AARCH64_ARCH_REAL_TIME_DATA_H_

#include <StorageDefs.h>
#include <SupportDefs.h>

struct arch_real_time_data {
	bigtime_t	system_time_offset;
	uint32		system_time_conversion_factor;
};

#endif /* _SYSTEM_ARCH_AARCH64_ARCH_REAL_TIME_DATA_H_ */
