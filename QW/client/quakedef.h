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

#ifndef CLIENT_QUAKEDEF_H
#define CLIENT_QUAKEDEF_H

// quakedef.h -- primary header for client

//define        PARANOID                        // speed sapping error checking

#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <time.h>

#include "qtypes.h"
#include "cvar.h"

//=============================================================================

// the host system specifies the base of the directory tree, the
// command line parms passed to the program, and the amount of memory
// available for the program to use

typedef struct {
    char *basedir;
    char *cachedir;		// for development over ISDN lines
    int argc;
    char **argv;
    void *membase;
    int memsize;
} quakeparms_t;


//=============================================================================

#define	MINIMUM_MEMORY	0x550000

#define MAX_NUM_ARGVS	50


extern qboolean noclip_anglehack;


//
// host
//
extern quakeparms_t host_parms;

extern cvar_t sys_ticrate;
extern cvar_t sys_nostdout;
extern cvar_t developer;

extern cvar_t password;

extern qboolean host_initialized;	// true if into command execution
extern double host_frametime;
extern byte *host_basepal;
extern byte *host_colormap;
extern int host_framecount;	// incremented every frame, never reset
extern double realtime;		// not bounded in any way, changed at
				// start of every frame, never reset

void Host_ServerFrame(void);
void Host_InitCommands(void);
void Host_Init(quakeparms_t *parms);
void Host_Shutdown(void);
void Host_Error(const char *error, ...)
    __attribute__((noreturn, format(printf,1,2)));
void Host_EndGame(const char *message, ...)
    __attribute__((noreturn, format(printf,1,2)));
qboolean Host_SimulationTime(float time);
void Host_Frame(float time);
void Host_Quit_f(void);
void Host_ClientCommands(const char *fmt, ...)
    __attribute__((format(printf,1,2)));
void Host_ShutdownServer(qboolean crash);

extern qboolean msg_suppress_1;	// suppresses resolution and cache size console

				// output and fullscreen DIB focus gain/loss

//
// Hacks - FIXME - well, "hacks" says it all really...
//
extern cvar_t r_lockfrustum;	// FIXME - with rendering stuff please...
extern cvar_t r_lockpvs;	// FIXME - with rendering stuff please...
extern cvar_t r_drawflat;	// FIXME - with rendering stuff please...

#endif /* CLIENT_QUAKEDEF_H */
