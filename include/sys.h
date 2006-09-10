/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#ifndef SYS_H
#define SYS_H

// FIXME - QW/server doesn't want this much stuff...

// sys.h -- non-portable functions

// FIXME - don't want win only stuff in header
//         minimized could be useful on other systems anyway...
#ifdef _WIN32
#include "qtypes.h"
//extern qboolean Minimized;
extern qboolean window_visible(void);
#endif

//
// file IO
//

// returns the file size
// return -1 if file is not present
// the file should be in BINARY mode for stupid OSs that care
int Sys_FileOpenRead(const char *path, int *hndl);

int Sys_FileOpenWrite(const char *path);
void Sys_FileClose(int handle);
void Sys_FileSeek(int handle, int position);
int Sys_FileRead(int handle, void *dest, int count);
int Sys_FileWrite(int handle, const void *data, int count);
int Sys_FileTime(const char *path);
void Sys_mkdir(const char *path);

//
// memory protection
//
void Sys_MakeCodeWriteable(unsigned long startaddr, unsigned long length);

//
// system IO
//

#define MAX_PRINTMSG 4096

void Sys_Printf(const char *fmt, ...) __attribute__((format(printf,1,2)));
void Sys_DebugLog(const char *file, const char *fmt, ...)
    __attribute__((format(printf,2,3)));
void Sys_Error(const char *error, ...)
    __attribute__((format(printf,1,2), noreturn));

// send text to the console
// an error will cause the entire program to exit

void Sys_Quit(void) __attribute__((noreturn));

double Sys_DoubleTime(void);

char *Sys_ConsoleInput(void);

void Sys_Sleep(void);

// called to yield for a little bit so as
// not to hog cpu when paused or debugging

void Sys_SendKeyEvents(void);

// Perform Key_Event () callbacks until the input que is empty

void Sys_LowFPPrecision(void);
void Sys_HighFPPrecision(void);
void Sys_SetFPCW(void);

void Sys_Init(void);

#endif /* SYS_H */
