/*
** Copyright 2003, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the MIT License.
*/
#ifndef FATFS_H
#define FATFS_H


#include <SupportDefs.h>
#include <ByteOrder.h>
#include <endian.h>

namespace FATFS {

class Volume;

// mode bits
#define FAT_READ_ONLY   1
#define FAT_HIDDEN              2
#define FAT_SYSTEM              4
#define FAT_VOLUME              8
#define FAT_SUBDIR              16
#define FAT_ARCHIVE             32

#define read32(buffer,off) \
		le32dec(reinterpret_cast<const char *>(buffer) + (off))

#define read16(buffer,off) \
		le16dec(reinterpret_cast<const char *>(buffer) + (off))

#define write32(buffer, off, value) \
		le32enc(reinterpret_cast<char *>(buffer) + (off), value)

#define write16(buffer, off, value) \
		le16enc(reinterpret_cast<char *>(buffer) + (off), value)

enum name_lengths {
	FATFS_BASENAME_LENGTH	= 8,
	FATFS_EXTNAME_LENGTH	= 3,
	FATFS_NAME_LENGTH	= 12,
};

status_t get_root_block(int fDevice, char *buffer, int32 blockSize, off_t partitionSize);


}	// namespace FATFS

#endif	/* FATFS_H */

