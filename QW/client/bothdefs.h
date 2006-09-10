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

#ifndef CLIENT_BOTHDEFS_H
#define CLIENT_BOTHDEFS_H

// defs common to client and server

#ifdef SERVERONLY		// no asm in dedicated server
#undef USE_X86_ASM
#endif

/* UNALIGNED_OK - undef if unaligned accesses are not supported */
#ifdef USE_X86_ASM
#define UNALIGNED_OK
#else
#undef UNALIGNED_OK
#endif

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
#define CACHE_SIZE	32	// used to align key data structures

#define UNUSED(x)	(x = x)	// for pesky compiler / lint warnings

#define	MINIMUM_MEMORY	0x550000

// up / down
#define	PITCH	0

// left / right
#define	YAW		1

// fall over
#define	ROLL	2


#define	MAX_SCOREBOARD		16	// max numbers of players

#define	SOUND_CHANNELS		8


#define	MAX_QPATH		64	// max length of a quake game pathname
#define	MAX_OSPATH		128	// max length of a filesystem pathname

#define	ON_EPSILON		0.1	// point on plane side epsilon

#define	MAX_MSGLEN		1450	// max length of a reliable message
#define	MAX_DATAGRAM	1450	// max length of unreliable message

//
// per-level limits
//
#define	MAX_EDICTS		768	// FIXME: ouch! ouch! ouch!
#define	MAX_LIGHTSTYLES	64
#define	MAX_MODELS		256	// these are sent over the net as bytes
#define	MAX_SOUNDS		256	// so they cannot be blindly increased

#define	SAVEGAME_COMMENT_LENGTH	39

#define	MAX_STYLESTRING	64

//
// stats are integers communicated to the client by the server
//
#define MAX_CL_STATS            32
#define STAT_HEALTH             0
//#define STAT_FRAGS              1
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
#define STAT_ITEMS              15
//#define STAT_VIEWHEIGHT         16


//
// item flags
//
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

//
// print flags
//
#define PRINT_LOW               0 /* pickup messages */
#define PRINT_MEDIUM            1 /* death messages */
#define PRINT_HIGH              2 /* critical messages */
#define PRINT_CHAT              3 /* chat messages */

#endif /* CLIENT_BOTHDEFS_H */
