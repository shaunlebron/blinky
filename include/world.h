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

#ifndef WORLD_H
#define WORLD_H

#include "mathlib.h"
#include "progs.h"
#include "qtypes.h"

typedef enum {
    MOVE_NORMAL = 0,
    MOVE_NOMONSTERS = 1,
    MOVE_MISSILE = 2,
} movetype_t;


void SV_ClearWorld(void);

// called after the world model has been loaded, before linking any entities

void SV_UnlinkEdict(edict_t *ent);

// call before removing an entity, and before trying to move one,
// so it doesn't clip against itself
// flags ent->v.modified

void SV_LinkEdict(edict_t *ent, qboolean touch_triggers);

// Needs to be called any time an entity changes origin, mins, maxs, or solid
// flags ent->v.modified
// sets ent->v.absmin and ent->v.absmax
// if touchtriggers, calls prog functions for the intersected triggers

int SV_PointContents(const vec3_t point);

// returns the CONTENTS_* value from the world at the given point.
// does not check any entities at all

edict_t *SV_TestEntityPosition(const edict_t *ent);

/*
 * SV_Move
 * - mins and maxs are reletive
 * - if the entire move stays in a solid volume, trace.allsolid will be set
 * - if the starting point is in a solid, it will be allowed to move out
 *   to an open area
 * - MOVE_NOMONSTERS is used for line of sight or edge testing, where monsters
 *   shouldn't be considered solid objects
 * - passedict is explicitly excluded from clipping checks (normally NULL)
 */
const edict_t *SV_TraceMove(const vec3_t start,
			    const vec3_t mins, const vec3_t maxs,
			    const vec3_t end,
			    const movetype_t type, const edict_t *passedict,
			    trace_t *trace);

static inline const edict_t *
SV_TraceMoveEntity(const edict_t *entity, const vec3_t start, const vec3_t end,
		   movetype_t type, trace_t *trace)
{
    return SV_TraceMove(start, entity->v.mins, entity->v.maxs, end, type,
			entity, trace);
}

static inline const edict_t *
SV_TraceLine(const vec3_t start, const vec3_t end, movetype_t type,
	     const edict_t *passedict, trace_t *trace)
{
    return SV_TraceMove(start, vec3_origin, vec3_origin, end, type, passedict,
			trace);
}

#if defined(QW_HACK) && defined(SERVERONLY)
#include "pmove.h"
void SV_AddLinksToPhysents(const edict_t *player, const vec3_t mins,
			   const vec3_t maxs, physent_stack_t *pestack);
#endif

#endif /* WORLD_H */
