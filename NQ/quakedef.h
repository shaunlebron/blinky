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

// quakedef.h -- primary header for client
//            -- FIXME - needs splitting up into components...

#ifndef QUAKEDEF_H
#define QUAKEDEF_H

#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

#include "cvar.h"
#include "mathlib.h"

//define        PARANOID                        // speed sapping error checking

#define	GAMENAME	"id1"	// directory to look in by default

/* UNALIGNED_OK - undef if unaligned accesses are not supported */
#ifdef USE_X86_ASM
#define UNALIGNED_OK
#else
#undef UNALIGNED_OK
#endif

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
#define CACHE_SIZE	32	// used to align key data structures

#define UNUSED(x)	(x = x)	// for pesky compiler / lint warnings

#define	MINIMUM_MEMORY		0x550000
#define	MINIMUM_MEMORY_LEVELPAK	(MINIMUM_MEMORY + 0x100000)

#define MAX_NUM_ARGVS	50

// up / down
#define	PITCH	0

// left / right
#define	YAW	1

// fall over
#define	ROLL	2


#define	MAX_QPATH	64	// max length of a quake game pathname
#define	MAX_OSPATH	128	// max length of a filesystem pathname

#define	ON_EPSILON	0.1	// point on plane side epsilon

#define	MAX_MSGLEN	8000	// max length of a reliable message
#define	MAX_DATAGRAM	1024	// max length of unreliable message

//
// per-level limits
//
//#define       MAX_EDICTS      600     // FIXME: ouch! ouch! ouch!
#define	MAX_EDICTS	2048	// FIXME: Arbitrary increase, make dynamic?
#define	MAX_LIGHTSTYLES	64
#define	MAX_MODELS	256	// these are sent over the net as bytes
#define	MAX_SOUNDS	256	// so they cannot be blindly increased

#define	SAVEGAME_COMMENT_LENGTH	39

#define	MAX_STYLESTRING	64

//
// stats are integers communicated to the client by the server
//
#define MAX_CL_STATS            32
#define STAT_HEALTH             0
#define STAT_FRAGS              1
#define STAT_WEAPON             2
#define STAT_AMMO               3
#define STAT_ARMOR              4
#define STAT_WEAPONFRAME        5
#define STAT_SHELLS             6
#define STAT_NAILS              7
#define STAT_ROCKETS            8
#define STAT_CELLS              9
#define STAT_ACTIVEWEAPON       10
#define STAT_TOTALSECRETS       11
#define STAT_TOTALMONSTERS      12
#define STAT_SECRETS            13 /* bumped client side by svc_foundsecret */
#define STAT_MONSTERS           14 /* bumped by svc_killedmonster */

// stock defines

#define IT_SHOTGUN              (1 << 0)
#define IT_SUPER_SHOTGUN        (1 << 1)
#define IT_NAILGUN              (1 << 2)
#define IT_SUPER_NAILGUN        (1 << 3)
#define IT_GRENADE_LAUNCHER     (1 << 4)
#define IT_ROCKET_LAUNCHER      (1 << 5)
#define IT_LIGHTNING            (1 << 6)
#define IT_SUPER_LIGHTNING      (1 << 7)
#define IT_SHELLS               (1 << 8)
#define IT_NAILS                (1 << 9)
#define IT_ROCKETS              (1 << 10)
#define IT_CELLS                (1 << 11)
#define IT_AXE                  (1 << 12)
#define IT_ARMOR1               (1 << 13)
#define IT_ARMOR2               (1 << 14)
#define IT_ARMOR3               (1 << 15)
#define IT_SUPERHEALTH          (1 << 16)
#define IT_KEY1                 (1 << 17)
#define IT_KEY2                 (1 << 18)
#define IT_INVISIBILITY         (1 << 19)
#define IT_INVULNERABILITY      (1 << 20)
#define IT_SUIT                 (1 << 21)
#define IT_QUAD                 (1 << 22)

#define IT_SIGIL1               (1 << 28)
#define IT_SIGIL2               (1 << 29)
#define IT_SIGIL3               (1 << 30)
#define IT_SIGIL4               (1 << 31)

//===========================================
//rogue changed and added defines

#define RIT_SHELLS              (1 << 7)
#define RIT_NAILS               (1 << 8)
#define RIT_ROCKETS             (1 << 9)
#define RIT_CELLS               (1 << 10)
#define RIT_AXE                 (1 << 11)
#define RIT_LAVA_NAILGUN        (1 << 12)
#define RIT_LAVA_SUPER_NAILGUN  (1 << 13)
#define RIT_MULTI_GRENADE       (1 << 14)
#define RIT_MULTI_ROCKET        (1 << 15)
#define RIT_PLASMA_GUN          (1 << 16)

#define RIT_ARMOR1              (1 << 23)
#define RIT_ARMOR2              (1 << 24)
#define RIT_ARMOR3              (1 << 25)
#define RIT_LAVA_NAILS          (1 << 26)
#define RIT_PLASMA_AMMO         (1 << 27)
#define RIT_MULTI_ROCKETS       (1 << 28)
#define RIT_SHIELD              (1 << 29)
#define RIT_ANTIGRAV            (1 << 30)
#define RIT_SUPERHEALTH         (1 << 31)

//MED 01/04/97 added hipnotic defines
//===========================================
//hipnotic added defines
#define HIT_MJOLNIR_BIT         7
#define HIT_PROXIMITY_GUN_BIT   16
#define HIT_LASER_CANNON_BIT    23
#define HIT_WETSUIT_BIT         25
#define HIT_EMPATH_SHIELDS_BIT  26
#define HIT_MJOLNIR             (1 << HIT_MJOLNIR_BIT)
#define HIT_PROXIMITY_GUN       (1 << HIT_PROXIMITY_GUN_BIT)
#define HIT_LASER_CANNON        (1 << HIT_LASER_CANNON_BIT)
#define HIT_WETSUIT             (1 << HIT_WETSUIT_BIT)
#define HIT_EMPATHY_SHIELDS     (1 << HIT_EMPATHY_SHIELDS_BIT)

//===========================================

#define	MAX_SCOREBOARD		16

#define	SOUND_CHANNELS		8

typedef struct {
    vec3_t origin;
    vec3_t angles;
    int modelindex;
    int frame;
    int colormap;
    int skinnum;
    int effects;
} entity_state_t;


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

extern qboolean noclip_anglehack;

//
// chase
//
extern cvar_t chase_active;

void Chase_Init(void);
void Chase_Reset(void);
void Chase_Update(void);

extern cvar_t r_lockfrustum;	// FIXME - with rendering stuff please...
extern cvar_t r_lockpvs;	// FIXME - with rendering stuff please...
extern cvar_t r_drawflat;	// FIXME - with rendering stuff please...

#endif /* QUAKEDEF_H */
