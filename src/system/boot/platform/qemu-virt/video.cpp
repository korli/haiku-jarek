/*
 * Copyright 2003-2010, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */

#include <arch/cpu.h>
#include <boot/stage2.h>
#include <boot/platform.h>
#include <boot/menu.h>
#include <boot/kernel_args.h>
#include <boot/platform/generic/video.h>
#include <util/list.h>
#include <drivers/driver_settings.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void
platform_set_palette(const uint8 *palette)
{
}


extern "C" void
platform_blit4(addr_t frameBuffer, const uint8 *data, uint16 width,
	uint16 height, uint16 imageWidth, uint16 left, uint16 top)
{
}


extern "C" void
platform_switch_to_logo(void)
{
}


extern "C" void
platform_switch_to_text_mode(void)
{
}


extern "C" status_t
platform_init_video(void)
{
	return B_OK;
}
