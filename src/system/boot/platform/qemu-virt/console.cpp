/*
 * Copyright 2004-2005, Axel D?rfler, axeld@pinc-software.de.
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * Copyright 2009 Jonas Sundström, jonas@kirilla.com
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "serial.h"
#include "console.h"

#include <SupportDefs.h>
#include <util/kernel_cpp.h>
#include <boot/stage2.h>

#include <string.h>

class Console : public ConsoleNode {
public:
							Console();

	virtual	ssize_t			ReadAt(void* cookie, off_t pos, void* buffer,
								size_t bufferSize);
	virtual	ssize_t			WriteAt(void* cookie, off_t pos,
								const void* buffer, size_t bufferSize);
};


class VTConsole : public ConsoleNode {
public:
							VTConsole();
			void			ClearScreen();
			void			SetCursor(int32 x, int32 y);
			void			SetColor(int32 foreground, int32 background);
};


class SerialConsole : public VTConsole {
public:
							SerialConsole();

	virtual ssize_t			ReadAt(void *cookie, off_t pos, void *buffer,
								size_t bufferSize);
	virtual ssize_t			WriteAt(void *cookie, off_t pos, const void *buffer,
								size_t bufferSize);
};


static Console sInput;
static Console sOutput;
static SerialConsole sSerial;

FILE* stdin;
FILE* stdout;
FILE* stderr;


//	#pragma mark -


Console::Console()
	:
	ConsoleNode()
{
}


ssize_t
Console::ReadAt(void* cookie, off_t pos, void* buffer, size_t bufferSize)
{
	// don't seek in character devices
	// not implemented (and not yet? needed)
	return B_ERROR;
}


ssize_t
Console::WriteAt(void* cookie, off_t /*pos*/, const void* buffer,
	size_t bufferSize)
{
	return 0;
}


//	#pragma mark -


VTConsole::VTConsole()
	:
	ConsoleNode()
{
}


void
VTConsole::ClearScreen()
{
	WriteAt(NULL, 0LL, "\033[2J", 4);
}


void
VTConsole::SetCursor(int32 x, int32 y)
{
	char buffer[9];
	x = MIN(80, MAX(1, x));
	y = MIN(25, MAX(1, y));
	int len = snprintf(buffer, sizeof(buffer),
		"\033[%" B_PRId32 ";%" B_PRId32 "H", y, x);
	WriteAt(NULL, 0LL, buffer, len);
}


static const char sVT100CMap[] = {
		0,
		4,
		2,
		6,
		1,
		5,
		3,
		4,
		3,
		4,
		2,
		6,
		1,
		5,
		3,
		7
};

void
VTConsole::SetColor(int32 foreground, int32 background)
{
	char buffer[16];

	if (foreground < 0 || foreground >= (int)sizeof(sVT100CMap))
		return;
	if (background < 0 || background >= (int)sizeof(sVT100CMap))
		return;

	int attr;

	if(foreground == DARK_GRAY) {
		attr = 2;
	} else if(foreground >= BRIGHT_BLUE && foreground <= BRIGHT_RED) {
		attr = 1;
	} else {
		attr = 0;
	}

	// We assume normal display attributes here
	int len = snprintf(buffer, sizeof(buffer),
		"\033[%" B_PRId32 ";%" B_PRId32  ";%" B_PRId32 "m",
		attr, sVT100CMap[foreground] + 30, sVT100CMap[background] + 40);

	WriteAt(NULL, 0LL, buffer, len);
}

//     #pragma mark -


SerialConsole::SerialConsole()
	: VTConsole()
{
}


ssize_t
SerialConsole::ReadAt(void *cookie, off_t pos, void *buffer, size_t bufferSize)
{
	// don't seek in character devices
	// not implemented (and not yet? needed)
	return B_ERROR;
}


ssize_t
SerialConsole::WriteAt(void *cookie, off_t /*pos*/, const void *buffer,
	size_t bufferSize)
{
	serial_puts((const char *)buffer, bufferSize);
	return bufferSize;
}


//     #pragma mark -


void
console_clear_screen(void)
{
	sSerial.ClearScreen();
}


int32
console_width(void)
{
	return 80;
}


int32
console_height(void)
{
	return 25;
}


void
console_set_cursor(int32 x, int32 y)
{
	sSerial.SetCursor(x, y);
}


void
console_show_cursor(void)
{
}


void
console_hide_cursor(void)
{
}


int
console_wait_for_key(void)
{
	int key = serial_getc(true);
	if(key == 27) {
		key = serial_getc(true);
		if(key == '[') {
			key = serial_getc(true);
			switch(key)
			{
			case 'A': return TEXT_CONSOLE_KEY_UP;
			case 'B': return TEXT_CONSOLE_KEY_DOWN;
			case 'D': return TEXT_CONSOLE_KEY_LEFT;
			case 'C': return TEXT_CONSOLE_KEY_RIGHT;
			case 'H': return TEXT_CONSOLE_KEY_HOME;
			case 'F': return TEXT_CONSOLE_KEY_END;
			default: break;
			}

			if(key >= '0' && key <= '9') {
				int other = serial_getc(true);
				if(key == '5' && other == '~') {
					return TEXT_CONSOLE_KEY_PAGE_UP;
				}
				if(key == '6' && other == '~') {
					return TEXT_CONSOLE_KEY_PAGE_DOWN;
				}
				return other;
			}
			return key;
		}
		return key;
	}

	return key;
}


void
console_set_color(int32 foreground, int32 background)
{
	sSerial.SetColor(foreground, background);
}


status_t
console_init(void)
{
	stdin = (FILE *)&sSerial;
	stdout = (FILE *)&sSerial;
	stderr = (FILE *)&sSerial;
	return B_OK;
}

