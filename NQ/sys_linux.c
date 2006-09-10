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

#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <errno.h>

#include "client.h"
#include "common.h"
#include "host.h"
#include "quakedef.h"
#include "sys.h"

qboolean isDedicated;

static qboolean noconinput = false;
static qboolean nostdout = false;

// FIXME - Used in NQ, not QW... why?
// set for entity display
cvar_t sys_linerefresh = { "sys_linerefresh", "0" };

// =======================================================================
// General routines
// =======================================================================

void
Sys_Printf(const char *fmt, ...)
{
    va_list argptr;
    char text[MAX_PRINTMSG];
    unsigned char *p;

    va_start(argptr, fmt);
    vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);

    if (nostdout)
	return;

    // FIXME - compare with NQ + use ctype functions?
    for (p = (unsigned char *)text; *p; p++) {
	if ((*p > 128 || *p < 32) && *p != 10 && *p != 13 && *p != 9)
	    printf("[%02x]", *p);
	else
	    putc(*p, stdout);
    }
}

#if 0
static char end1[] =
    "\x1b[?7h\x1b[40m\x1b[2J\x1b[0;1;41m\x1b[1;1H                QUAKE: The Doomed Dimension \x1b[33mby \x1b[44mid\x1b[41m Software                      \x1b[2;1H  ----------------------------------------------------------------------------  \x1b[3;1H           CALL 1-800-IDGAMES TO ORDER OR FOR TECHNICAL SUPPORT                 \x1b[4;1H             PRICE: $45.00 (PRICES MAY VARY OUTSIDE THE US.)                    \x1b[5;1H                                                                                \x1b[6;1H  \x1b[37mYes! You only have one fourth of this incredible epic. That is because most   \x1b[7;1H   of you have paid us nothing or at most, very little. You could steal the     \x1b[8;1H   game from a friend. But we both know you'll be punished by God if you do.    \x1b[9;1H        \x1b[33mWHY RISK ETERNAL DAMNATION? CALL 1-800-IDGAMES AND BUY NOW!             \x1b[10;1H             \x1b[37mRemember, we love you almost as much as He does.                   \x1b[11;1H                                                                                \x1b[12;1H            \x1b[33mProgramming: \x1b[37mJohn Carmack, Michael Abrash, John Cash                \x1b[13;1H       \x1b[33mDesign: \x1b[37mJohn Romero, Sandy Petersen, American McGee, Tim Willits         \x1b[14;1H                     \x1b[33mArt: \x1b[37mAdrian Carmack, Kevin Cloud                           \x1b[15;1H               \x1b[33mBiz: \x1b[37mJay Wilbur, Mike Wilson, Donna Jackson                      \x1b[16;1H            \x1b[33mProjects: \x1b[37mShawn Green   \x1b[33mSupport: \x1b[37mBarrett Alexander                  \x1b[17;1H              \x1b[33mSound Effects: \x1b[37mTrent Reznor and Nine Inch Nails                   \x1b[18;1H  For other information or details on ordering outside the US, check out the    \x1b[19;1H     files accompanying QUAKE or our website at http://www.idsoftware.com.      \x1b[20;1H    \x1b[0;41mQuake is a trademark of Id Software, inc., (c)1996 Id Software, inc.        \x1b[21;1H     All rights reserved. NIN logo is a registered trademark licensed to        \x1b[22;1H                 Nothing Interactive, Inc. All rights reserved.                 \x1b[40m\x1b[23;1H\x1b[0m";
static char end2[] =
    "\x1b[?7h\x1b[40m\x1b[2J\x1b[0;1;41m\x1b[1;1H        QUAKE \x1b[33mby \x1b[44mid\x1b[41m Software                                                    \x1b[2;1H -----------------------------------------------------------------------------  \x1b[3;1H        \x1b[37mWhy did you quit from the registered version of QUAKE? Did the          \x1b[4;1H        scary monsters frighten you? Or did Mr. Sandman tug at your             \x1b[5;1H        little lids? No matter! What is important is you love our               \x1b[6;1H        game, and gave us your money. Congratulations, you are probably         \x1b[7;1H        not a thief.                                                            \x1b[8;1H                                                           Thank You.           \x1b[9;1H        \x1b[33;44mid\x1b[41m Software is:                                                         \x1b[10;1H        PROGRAMMING: \x1b[37mJohn Carmack, Michael Abrash, John Cash                    \x1b[11;1H        \x1b[33mDESIGN: \x1b[37mJohn Romero, Sandy Petersen, American McGee, Tim Willits        \x1b[12;1H        \x1b[33mART: \x1b[37mAdrian Carmack, Kevin Cloud                                        \x1b[13;1H        \x1b[33mBIZ: \x1b[37mJay Wilbur, Mike Wilson     \x1b[33mPROJECTS MAN: \x1b[37mShawn Green              \x1b[14;1H        \x1b[33mBIZ ASSIST: \x1b[37mDonna Jackson        \x1b[33mSUPPORT: \x1b[37mBarrett Alexander             \x1b[15;1H        \x1b[33mSOUND EFFECTS AND MUSIC: \x1b[37mTrent Reznor and Nine Inch Nails               \x1b[16;1H                                                                                \x1b[17;1H        If you need help running QUAKE refer to the text files in the           \x1b[18;1H        QUAKE directory, or our website at http://www.idsoftware.com.           \x1b[19;1H        If all else fails, call our technical support at 1-800-IDGAMES.         \x1b[20;1H      \x1b[0;41mQuake is a trademark of Id Software, inc., (c)1996 Id Software, inc.      \x1b[21;1H        All rights reserved. NIN logo is a registered trademark licensed        \x1b[22;1H             to Nothing Interactive, Inc. All rights reserved.                  \x1b[23;1H\x1b[40m\x1b[0m";

#endif
void
Sys_Quit(void)
{
    Host_Shutdown();
    fcntl(STDIN_FILENO, F_SETFL,
	  fcntl(STDIN_FILENO, F_GETFL, 0) & ~O_NONBLOCK);
#if 0
    if (registered.value)
	printf("%s", end2);
    else
	printf("%s", end1);
#endif
    fflush(stdout);
    exit(0);
}

void
Sys_Init(void)
{
#ifdef USE_X86_ASM
    Sys_SetFPCW();
#endif
}

void
Sys_Error(const char *error, ...)
{
    va_list argptr;
    char string[MAX_PRINTMSG];

// change stdin to non blocking
    fcntl(STDIN_FILENO, F_SETFL,
	  fcntl(STDIN_FILENO, F_GETFL, 0) & ~O_NONBLOCK);

    va_start(argptr, error);
    vsnprintf(string, sizeof(string), error, argptr);
    va_end(argptr);
    fprintf(stderr, "Error: %s\n", string);

    Host_Shutdown();
    exit(1);

}

/*
============
Sys_FileTime

returns -1 if not present
============
*/
int
Sys_FileTime(const char *path)
{
    struct stat buf;

    if (stat(path, &buf) == -1)
	return -1;

    return buf.st_mtime;
}


void
Sys_mkdir(const char *path)
{
    mkdir(path, 0777);
}

int
Sys_FileOpenRead(const char *path, int *handle)
{
    int h;
    struct stat fileinfo;


    h = open(path, O_RDONLY, 0666);
    *handle = h;
    if (h == -1)
	return -1;

    if (fstat(h, &fileinfo) == -1)
	Sys_Error("Error fstating %s", path);

    return fileinfo.st_size;
}

int
Sys_FileOpenWrite(const char *path)
{
    int handle;

    umask(0);

    handle = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);

    if (handle == -1)
	Sys_Error("Error opening %s: %s", path, strerror(errno));

    return handle;
}

int
Sys_FileWrite(int handle, const void *src, int count)
{
    return write(handle, src, count);
}

void
Sys_FileClose(int handle)
{
    close(handle);
}

void
Sys_FileSeek(int handle, int position)
{
    lseek(handle, position, SEEK_SET);
}

int
Sys_FileRead(int handle, void *dest, int count)
{
    return read(handle, dest, count);
}

void
Sys_DebugLog(const char *file, const char *fmt, ...)
{
    va_list argptr;
    static char data[MAX_PRINTMSG];
    int fd;

    va_start(argptr, fmt);
    vsnprintf(data, sizeof(data), fmt, argptr);
    va_end(argptr);
//    fd = open(file, O_WRONLY | O_BINARY | O_CREAT | O_APPEND, 0666);
    fd = open(file, O_WRONLY | O_CREAT | O_APPEND, 0666);
    write(fd, data, strlen(data));
    close(fd);
}

double
Sys_DoubleTime(void)
{
    struct timeval tp;
    struct timezone tzp;
    static int secbase;

    gettimeofday(&tp, &tzp);

    if (!secbase) {
	secbase = tp.tv_sec;
	return tp.tv_usec / 1000000.0;
    }

    return (tp.tv_sec - secbase) + tp.tv_usec / 1000000.0;
}

// =======================================================================
// Sleeps for microseconds
// =======================================================================

/* FIXME - Unused only in QW? */
static void
Sys_LineRefresh(void)
{
}

static void
floating_point_exception_handler(int whatever)
{
//      Sys_Warn("floating point exception\n");
    signal(SIGFPE, floating_point_exception_handler);
}

// FIXME - need this at all? (see QW)
char *
Sys_ConsoleInput(void)
{
    static char text[256];
    int len;
    fd_set fdset;
    struct timeval timeout;

    if (cls.state == ca_dedicated) {
	FD_ZERO(&fdset);
	FD_SET(STDIN_FILENO, &fdset);	// stdin
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	if (select(STDIN_FILENO + 1, &fdset, NULL, NULL, &timeout) == -1
	    || !FD_ISSET(STDIN_FILENO, &fdset))
	    return NULL;

	len = read(STDIN_FILENO, text, sizeof(text));
	if (len < 1)
	    return NULL;
	text[len - 1] = 0;	// rip off the /n and terminate

	return text;
    }
    return NULL;
}

#ifndef USE_X86_ASM
void
Sys_HighFPPrecision(void)
{
}

void
Sys_LowFPPrecision(void)
{
}
#endif

int
main(int c, char **v)
{
    double time, oldtime, newtime;
    quakeparms_t parms;
    int j;

//      signal(SIGFPE, floating_point_exception_handler);
    signal(SIGFPE, SIG_IGN);

    memset(&parms, 0, sizeof(parms));

    COM_InitArgv(c, v);
    parms.argc = com_argc;
    parms.argv = com_argv;

#ifdef GLQUAKE
    parms.memsize = 16 * 1024 * 1024;
#else
    parms.memsize = 8 * 1024 * 1024;
#endif

    j = COM_CheckParm("-mem");
    if (j)
	parms.memsize = (int)(Q_atof(com_argv[j + 1]) * 1024 * 1024);
    parms.membase = malloc(parms.memsize);
    parms.basedir = stringify(QBASEDIR);
// caching is disabled by default, use -cachedir to enable
//      parms.cachedir = cachedir;

    fcntl(STDIN_FILENO, F_SETFL,
	  fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);

    Host_Init(&parms);

    Sys_Init();

    if (COM_CheckParm("-nostdout"))
	nostdout = true;

    // Make stdin non-blocking
    // FIXME - check both return values
    if (!noconinput)
	fcntl(STDIN_FILENO, F_SETFL,
	      fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
    if (!nostdout)
	printf("TyrQuake -- Version %s\n", stringify(TYR_VERSION));

    oldtime = Sys_DoubleTime() - 0.1;
    while (1) {
// find time spent rendering last frame
	newtime = Sys_DoubleTime();
	time = newtime - oldtime;

	if (cls.state == ca_dedicated) {
	    if (time < sys_ticrate.value) {
		usleep(1);
		continue;	// not time to run a server only tic yet
	    }
	    time = sys_ticrate.value;
	}

	if (time > sys_ticrate.value * 2)
	    oldtime = newtime;
	else
	    oldtime += time;

	Host_Frame(time);

// graphic debugging aids
	if (sys_linerefresh.value)
	    Sys_LineRefresh();
    }
}


/*
================
Sys_MakeCodeWriteable
================
*/
void
Sys_MakeCodeWriteable(unsigned long startaddr, unsigned long length)
{
    int r;
    unsigned long addr;
    int psize = getpagesize();

    addr = (startaddr & ~(psize - 1)) - psize;

//      fprintf(stderr, "writable code %lx(%lx)-%lx, length=%lx\n", startaddr,
//                      addr, startaddr+length, length);

    r = mprotect((char *)addr, length + startaddr - addr + psize, 7);

    if (r < 0)
	Sys_Error("Protection change failed");
}
