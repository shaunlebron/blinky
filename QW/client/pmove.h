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

#ifndef CLIENT_PMOVE_H
#define CLIENT_PMOVE_H

#include "model.h"
#include "protocol.h"

#ifndef GLQUAKE
#include "d_iface.h"
#endif

#define	MAX_PHYSENTS	32
typedef struct {
    vec3_t origin;
    const brushmodel_t *brushmodel;
    vec3_t mins, maxs;		// only for non-bsp models
#ifdef SERVERONLY
    int entitynum;		// for server to identify
#endif
} physent_t;

typedef struct {
    // player state
    vec3_t origin;
    vec3_t angles;
    vec3_t velocity;
    int oldbuttons;
    float waterjumptime;
    qboolean dead;
    int spectator;

    const physent_t *onground;
    int watertype;
    int waterlevel;

    // input
    const usercmd_t *cmd;

    // results
#ifdef SERVERONLY
    int numtouch;
    const physent_t *touch[MAX_PHYSENTS];
#endif
} playermove_t;

typedef struct {
    int numphysent;
    physent_t physents[MAX_PHYSENTS];
} physent_stack_t;

typedef struct {
    float gravity;
    float stopspeed;
    float maxspeed;
    float spectatormaxspeed;
    float accelerate;
    float airaccelerate;
    float wateraccelerate;
    float friction;
    float waterfriction;
    float entgravity;
} movevars_t;

extern movevars_t movevars;
extern const vec3_t player_mins;
extern const vec3_t player_maxs;

void PlayerMove(playermove_t *pmove, const physent_stack_t *pestack);

int PM_PointContents(const vec3_t point, const physent_stack_t *pestack);
qboolean PM_TestPlayerPosition(const vec3_t point, const physent_stack_t *pestack);

/* Returns the physent that the trace hit, NULL otherwise */
const physent_t *PM_PlayerMove(const vec3_t start, const vec3_t stop,
			       const physent_stack_t *pestack, trace_t *trace);

#endif /* CLIENT_PMOVE_H */
