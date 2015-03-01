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
// sv_phys.c

#include "console.h"
#include "progs.h"
#include "server.h"
#include "world.h"

#ifdef NQ_HACK
#include "host.h"
#include "quakedef.h"
#include "sys.h"
/* FIXME - quick hack to enable merging of NQ/QWSV shared code */
#define SV_Error Sys_Error
#endif
#ifdef QW_HACK
#include "pmove.h"
#include "qwsvdef.h"
#endif

/*
 * pushmove objects do not obey gravity, and do not interact with each
 * other or trigger fields, but block normal movement and push normal
 * objects when they move.
 *
 * onground is set for toss objects when they come to a complete rest.
 * it is set for steping or walking objects
 *
 * doors, plats, etc are SOLID_BSP, and MOVETYPE_PUSH
 * bonus items are SOLID_TRIGGER touch, and MOVETYPE_TOSS
 * corpses are SOLID_NOT and MOVETYPE_TOSS
 * crates are SOLID_BBOX and MOVETYPE_TOSS
 * walking monsters are SOLID_SLIDEBOX and MOVETYPE_STEP
 * flying/floating monsters are SOLID_SLIDEBOX and MOVETYPE_FLY
 *
 * solid_edge items only clip against bsp models.
 */

cvar_t sv_friction = { "sv_friction", "4" };
cvar_t sv_gravity = { "sv_gravity", "800" };
cvar_t sv_stopspeed = { "sv_stopspeed", "100" };
cvar_t sv_maxvelocity = { "sv_maxvelocity", "2000" };

#ifdef NQ_HACK
static void SV_Physics_Toss(edict_t *ent);
cvar_t sv_nostep = { "sv_nostep", "0" };
#endif
#ifdef QW_HACK
cvar_t sv_maxspeed = { "sv_maxspeed", "320" };
cvar_t sv_spectatormaxspeed = { "sv_spectatormaxspeed", "500" };
cvar_t sv_accelerate = { "sv_accelerate", "10" };
cvar_t sv_airaccelerate = { "sv_airaccelerate", "0.7" };
cvar_t sv_wateraccelerate = { "sv_wateraccelerate", "10" };
cvar_t sv_waterfriction = { "sv_waterfriction", "4" };
#endif

#define	MOVE_EPSILON 0.01

/*
================
SV_CheckAllEnts
================
*/
static void
SV_CheckAllEnts(void)
{
#ifdef PARANOID
    int i;
    edict_t *check;

// see if any solid entities are inside the final position
    check = NEXT_EDICT(sv.edicts);
    for (i = 1; i < sv.num_edicts; i++, check = NEXT_EDICT(check)) {
	if (check->free)
	    continue;
	if (check->v.movetype == MOVETYPE_PUSH
	    || check->v.movetype == MOVETYPE_NONE
	    || check->v.movetype == MOVETYPE_NOCLIP)
	    continue;

	if (SV_TestEntityPosition(check))
	    Con_Printf("entity in invalid position\n");
    }
#endif
}

/*
================
SV_CheckVelocity
================
*/
static void
SV_CheckVelocity(edict_t *ent)
{
    int i;

    /* clamp velocity */
    for (i = 0; i < 3; i++) {
	if (IS_NAN(ent->v.velocity[i])) {
	    Con_Printf("Got a NaN velocity on %s\n",
		       PR_GetString(ent->v.classname));
	    ent->v.velocity[i] = 0;
	}
	if (IS_NAN(ent->v.origin[i])) {
	    Con_Printf("Got a NaN origin on %s\n",
		       PR_GetString(ent->v.classname));
	    ent->v.origin[i] = 0;
	}
	if (ent->v.velocity[i] > sv_maxvelocity.value)
	    ent->v.velocity[i] = sv_maxvelocity.value;
	else if (ent->v.velocity[i] < -sv_maxvelocity.value)
	    ent->v.velocity[i] = -sv_maxvelocity.value;
    }
}

/*
=============
SV_RunThink

Runs thinking code if time.  There is some play in the exact time the think
function will be called, because it is called before any movement is done
in a frame.  Not used for pushmove objects, because they must be exact.
Returns false if the entity removed itself.
=============
*/
qboolean
SV_RunThink(edict_t *ent)
{
    float thinktime;

#ifdef QW_HACK
 repeat:
#endif
    thinktime = ent->v.nextthink;
    if (thinktime <= 0 || thinktime > sv.time + host_frametime)
	return true;

    /*
     * Don't let things stay in the past.  It is possible to start
     * that way by a trigger with a local time.
     */
    if (thinktime < sv.time)
	thinktime = sv.time;

    ent->v.nextthink = 0;
    pr_global_struct->time = thinktime;
    pr_global_struct->self = EDICT_TO_PROG(ent);
    pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
    PR_ExecuteProgram(ent->v.think);

    if (ent->free)
	return false;
#ifdef QW_HACK
    goto repeat;
#endif
#ifdef NQ_HACK
    return true;
#endif
}

/*
==================
SV_Impact

Two entities have touched, so run their touch functions
==================
*/
static void
SV_Impact(const edict_t *e1, const edict_t *e2)
{
    int old_self, old_other;

    old_self = pr_global_struct->self;
    old_other = pr_global_struct->other;

    pr_global_struct->time = sv.time;
    if (e1->v.touch && e1->v.solid != SOLID_NOT) {
	pr_global_struct->self = EDICT_TO_PROG(e1);
	pr_global_struct->other = EDICT_TO_PROG(e2);
	PR_ExecuteProgram(e1->v.touch);
    }

    if (e2->v.touch && e2->v.solid != SOLID_NOT) {
	pr_global_struct->self = EDICT_TO_PROG(e2);
	pr_global_struct->other = EDICT_TO_PROG(e1);
	PR_ExecuteProgram(e2->v.touch);
    }

    pr_global_struct->self = old_self;
    pr_global_struct->other = old_other;
}


/*
==================
ClipVelocity

Slide off of the impacting object
returns the blocked flags (1 = floor, 2 = step / wall)
==================
*/
#define	STOP_EPSILON	0.1

static int
ClipVelocity(const vec3_t in, const vec3_t normal, vec3_t out, float overbounce)
{
    float backoff;
    float change;
    int i, blocked;

    blocked = 0;
    if (normal[2] > 0)
	blocked |= 1;		// floor
    if (!normal[2])
	blocked |= 2;		// step

    backoff = DotProduct(in, normal) * overbounce;

    for (i = 0; i < 3; i++) {
	change = normal[i] * backoff;
	out[i] = in[i] - change;
	if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
	    out[i] = 0;
    }

    return blocked;
}


/*
============
SV_FlyMove

The basic solid body movement clip that slides along multiple planes
Returns the clipflags if the velocity was modified (hit something solid)
1 = floor
2 = wall / step
4 = dead stop
If steptrace is not NULL, the trace of any vertical wall hit will be stored
============
*/
#define MOVE_CLIP_NONE  0
#define MOVE_CLIP_FLOOR (1 << 0)
#define MOVE_CLIP_WALL  (1 << 1)
#define MOVE_CLIP_STOP  (1 << 2)

#define	MAX_CLIP_PLANES	5
static int
SV_FlyMove(edict_t *ent, float time, trace_t *steptrace)
{
    int bumpcount, numbumps;
    vec3_t dir;
    float d;
    int numplanes;
    vec3_t planes[MAX_CLIP_PLANES];
    vec3_t primal_velocity, original_velocity, new_velocity;
    int i, j;
    trace_t trace;
    vec3_t end;
    float time_left;
    int blocked;
    const edict_t *ground;

    numbumps = 4;

    blocked = MOVE_CLIP_NONE;
    VectorCopy(ent->v.velocity, original_velocity);
    VectorCopy(ent->v.velocity, primal_velocity);
    numplanes = 0;

    time_left = time;

    for (bumpcount = 0; bumpcount < numbumps; bumpcount++) {
#ifdef NQ_HACK
	if (!ent->v.velocity[0] && !ent->v.velocity[1] && !ent->v.velocity[2])
	    break;
#endif

	for (i = 0; i < 3; i++)
	    end[i] = ent->v.origin[i] + time_left * ent->v.velocity[i];

	ground = SV_TraceMoveEntity(ent, ent->v.origin, end, MOVE_NORMAL,
				     &trace);

	if (trace.allsolid) {
	    /* entity is trapped in another solid */
	    VectorCopy(vec3_origin, ent->v.velocity);
	    return MOVE_CLIP_FLOOR | MOVE_CLIP_WALL;
	}

	if (trace.fraction > 0) {
	    /* actually covered some distance */
	    VectorCopy(trace.endpos, ent->v.origin);
	    VectorCopy(ent->v.velocity, original_velocity);
	    numplanes = 0;
	}

	/* moved the entire distance */
	if (trace.fraction == 1)
	    break;

	if (!ground)
	    SV_Error("%s: !ground", __func__);

	if (trace.plane.normal[2] > 0.7) {
	    blocked |= MOVE_CLIP_FLOOR;
	    if (ground->v.solid == SOLID_BSP) {
		ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
		ent->v.groundentity = EDICT_TO_PROG(ground);
	    }
	}
	if (!trace.plane.normal[2]) {
	    blocked |= MOVE_CLIP_WALL;
	    if (steptrace)
		/* save for player extrafriction */
		*steptrace = trace;
	}

	/*
	 * Run the impact function. Entity may be removed.
	 */
	SV_Impact(ent, ground);
	if (ent->free)
	    break;

	time_left -= time_left * trace.fraction;

	/* clipped to another plane */
	if (numplanes >= MAX_CLIP_PLANES) {
	    /* this shouldn't really happen... */
	    VectorCopy(vec3_origin, ent->v.velocity);
	    return MOVE_CLIP_FLOOR | MOVE_CLIP_WALL;
	}
	VectorCopy(trace.plane.normal, planes[numplanes]);
	numplanes++;

	/*
	 * modify original_velocity so it parallels all of the clip planes
	 */
	for (i = 0; i < numplanes; i++) {
	    ClipVelocity(original_velocity, planes[i], new_velocity, 1);
	    for (j = 0; j < numplanes; j++)
		if (j != i) {
		    if (DotProduct(new_velocity, planes[j]) < 0)
			break;	/* not ok */
		}
	    if (j == numplanes)
		break;
	}

	if (i != numplanes) {
	    /* go along this plane */
	    VectorCopy(new_velocity, ent->v.velocity);
	} else {
	    /* go along the crease */
	    if (numplanes != 2) {
		VectorCopy(vec3_origin, ent->v.velocity);
		return MOVE_CLIP_FLOOR | MOVE_CLIP_WALL | MOVE_CLIP_STOP;
	    }
	    CrossProduct(planes[0], planes[1], dir);
	    d = DotProduct(dir, ent->v.velocity);
	    VectorScale(dir, d, ent->v.velocity);
	}

	/*
	 * If velocity is against the original velocity, stop dead
	 * to avoid tiny occilations in sloping corners.
	 */
	if (DotProduct(ent->v.velocity, primal_velocity) <= 0) {
	    VectorCopy(vec3_origin, ent->v.velocity);
	    return blocked;
	}
    }

    return blocked;
}


/*
============
SV_AddGravity

============
*/
static void
SV_AddGravity(edict_t *ent)
{
#ifdef NQ_HACK
    float scale;
    eval_t *val;

    val = GetEdictFieldValue(ent, "gravity");
    scale = (val && val->_float) ? val->_float : 1.0;
    ent->v.velocity[2] -= scale * sv_gravity.value * host_frametime;
#endif
#ifdef QW_HACK
    ent->v.velocity[2] -= movevars.gravity * host_frametime;
#endif
}

/*
===============================================================================

PUSHMOVE

===============================================================================
*/

/*
============
SV_PushEntity

Does not change the entities velocity at all
============
*/
static const edict_t *
SV_PushEntity(edict_t *ent, const vec3_t push, trace_t *trace)
{
    vec3_t end;
    movetype_t movetype;
    const edict_t *blocker;

    VectorAdd(ent->v.origin, push, end);

    if (ent->v.movetype == MOVETYPE_FLYMISSILE)
	movetype = MOVE_MISSILE;
    else if (ent->v.solid == SOLID_TRIGGER || ent->v.solid == SOLID_NOT)
	/* only clip against bmodels */
	movetype = MOVE_NOMONSTERS;
    else
	movetype = MOVE_NORMAL;

    blocker = SV_TraceMoveEntity(ent, ent->v.origin, end, movetype, trace);
    VectorCopy(trace->endpos, ent->v.origin);
    SV_LinkEdict(ent, true);

    if (blocker)
	SV_Impact(ent, blocker);

    return blocker;
}


/*
============
SV_Push
============
*/
static qboolean
SV_Push(edict_t *pusher, const vec3_t move)
{
    int i;
    edict_t *check, *block;
    vec3_t mins, maxs;
    vec3_t pushorig;
    int num_moved;
    edict_t *moved_edict[MAX_EDICTS];
    vec3_t moved_from[MAX_EDICTS];
#ifdef NQ_HACK
    trace_t trace;
#endif

    for (i = 0; i < 3; i++) {
	mins[i] = pusher->v.absmin[i] + move[i];
	maxs[i] = pusher->v.absmax[i] + move[i];
    }

    /* move the pusher to it's final position */
    VectorCopy(pusher->v.origin, pushorig);
    VectorAdd(pusher->v.origin, move, pusher->v.origin);
    SV_LinkEdict(pusher, false);

    /* see if any solid entities are inside the final position */
    num_moved = 0;
    check = NEXT_EDICT(sv.edicts);
    for (i = 1; i < sv.num_edicts; i++, check = NEXT_EDICT(check)) {
	if (check->free)
	    continue;
	if (check->v.movetype == MOVETYPE_PUSH
	    || check->v.movetype == MOVETYPE_NONE
	    || check->v.movetype == MOVETYPE_NOCLIP)
	    continue;

#ifdef QW_HACK
	pusher->v.solid = SOLID_NOT;
	block = SV_TestEntityPosition(check);
	pusher->v.solid = SOLID_BSP;
	if (block)
	    continue;
#endif

	/* if entity is standing on the pusher, it will definitely be moved */
	if (!(((int)check->v.flags & FL_ONGROUND)
	      && PROG_TO_EDICT(check->v.groundentity) == pusher)) {
	    if (check->v.absmin[0] >= maxs[0]
		|| check->v.absmin[1] >= maxs[1]
		|| check->v.absmin[2] >= maxs[2]
		|| check->v.absmax[0] <= mins[0]
		|| check->v.absmax[1] <= mins[1]
		|| check->v.absmax[2] <= mins[2])
		continue;

	    /* see if the ent's bbox is inside the pusher's final position */
	    if (!SV_TestEntityPosition(check))
		continue;
	}
#ifdef NQ_HACK
	/* remove the onground flag for non-players */
	if (check->v.movetype != MOVETYPE_WALK)
	    check->v.flags = (int)check->v.flags & ~FL_ONGROUND;
#endif

	VectorCopy(check->v.origin, moved_from[num_moved]);
	moved_edict[num_moved] = check;
	num_moved++;

	/* try moving the contacted entity */
#ifdef NQ_HACK
	pusher->v.solid = SOLID_NOT;
	SV_PushEntity(check, move, &trace);
	pusher->v.solid = SOLID_BSP;
#endif
#ifdef QW_HACK
	VectorAdd(check->v.origin, move, check->v.origin);
#endif
	block = SV_TestEntityPosition(check);
	if (!block) {
	    /* TODO - fix redundant link in NQ due to SV_PushEntity above */
	    SV_LinkEdict(check, false);
	    continue;
	}

#ifdef QW_HACK
	/* if it is ok to leave in the old position, do it */
	VectorSubtract(check->v.origin, move, check->v.origin);
	block = SV_TestEntityPosition(check);
	if (!block) {
	    num_moved--;
	    continue;
	}
#endif
	/* if entity has no volume (no model?), leave it */
	if (check->v.mins[0] == check->v.maxs[0]) {
	    SV_LinkEdict(check, false);
	    continue;
	}

	if (check->v.solid == SOLID_NOT || check->v.solid == SOLID_TRIGGER) {
	    /* corpse */
	    check->v.mins[0] = check->v.mins[1] = 0;
	    VectorCopy(check->v.mins, check->v.maxs);
	    SV_LinkEdict(check, false);
	    continue;
	}
	VectorCopy(pushorig, pusher->v.origin);
	SV_LinkEdict(pusher, false);

	/*
	 * if the pusher has a "blocked" function, call it otherwise, just
	 * stay in place until the obstacle is gone
	 */
	if (pusher->v.blocked) {
	    pr_global_struct->self = EDICT_TO_PROG(pusher);
	    pr_global_struct->other = EDICT_TO_PROG(check);
	    PR_ExecuteProgram(pusher->v.blocked);
	}
	/* move back any entities we already moved */
	for (i = 0; i < num_moved; i++) {
	    VectorCopy(moved_from[i], moved_edict[i]->v.origin);
	    SV_LinkEdict(moved_edict[i], false);
	}
	return false;
    }

    return true;
}

/*
============
SV_PushMove

============
*/
static void
SV_PushMove(edict_t *pusher, float movetime)
{
    int i;
    vec3_t move;

    if (!pusher->v.velocity[0] && !pusher->v.velocity[1]
	&& !pusher->v.velocity[2]) {
	pusher->v.ltime += movetime;
	return;
    }

    for (i = 0; i < 3; i++)
	move[i] = pusher->v.velocity[i] * movetime;

    if (SV_Push(pusher, move))
	pusher->v.ltime += movetime;
}

/*
================
SV_Physics_Pusher
================
*/
static void
SV_Physics_Pusher(edict_t *ent)
{
    float thinktime;
    float oldltime;
    float movetime;
#ifdef QW_HACK
    vec3_t oldorg, move;
    float dist;
#endif

    oldltime = ent->v.ltime;

    thinktime = ent->v.nextthink;
    if (thinktime < ent->v.ltime + host_frametime) {
	movetime = thinktime - ent->v.ltime;
	if (movetime < 0)
	    movetime = 0;
    } else
	movetime = host_frametime;

    if (movetime) {
	/* advances ent->v.ltime if not blocked */
	SV_PushMove(ent, movetime);
    }

    if (thinktime > oldltime && thinktime <= ent->v.ltime) {
#ifdef QW_HACK
	VectorCopy(ent->v.origin, oldorg);
#endif
	ent->v.nextthink = 0;
	pr_global_struct->time = sv.time;
	pr_global_struct->self = EDICT_TO_PROG(ent);
	pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
	PR_ExecuteProgram(ent->v.think);
	if (ent->free)
	    return;
#ifdef QW_HACK
	VectorSubtract(ent->v.origin, oldorg, move);
	dist = Length(move);
	if (dist > 1.0 / 64) {
	    VectorCopy(oldorg, ent->v.origin);
	    SV_Push(ent, move);
	}
#endif
    }
}

#ifdef NQ_HACK
/*
===============================================================================

CLIENT MOVEMENT

===============================================================================
*/

/*
=============
SV_CheckStuck

This is a big hack to try and fix the rare case of getting stuck in the world
clipping hull.
=============
*/
static void
SV_CheckStuck(edict_t *player)
{
    int i, j;
    int z;
    vec3_t org;

    if (!SV_TestEntityPosition(player)) {
	VectorCopy(player->v.origin, player->v.oldorigin);
	return;
    }

    VectorCopy(player->v.origin, org);
    VectorCopy(player->v.oldorigin, player->v.origin);
    if (!SV_TestEntityPosition(player)) {
	Con_DPrintf("Unstuck.\n");
	SV_LinkEdict(player, true);
	return;
    }

    for (z = 0; z < 18; z++)
	for (i = -1; i <= 1; i++)
	    for (j = -1; j <= 1; j++) {
		player->v.origin[0] = org[0] + i;
		player->v.origin[1] = org[1] + j;
		player->v.origin[2] = org[2] + z;
		if (!SV_TestEntityPosition(player)) {
		    Con_DPrintf("Unstuck.\n");
		    SV_LinkEdict(player, true);
		    return;
		}
	    }

    VectorCopy(org, player->v.origin);
    Con_DPrintf("player is stuck.\n");
}


/*
=============
SV_CheckWater
=============
*/
static qboolean
SV_CheckWater(edict_t *player)
{
    vec3_t point;
    int contents;

    point[0] = player->v.origin[0];
    point[1] = player->v.origin[1];
    point[2] = player->v.origin[2] + player->v.mins[2] + 1;

    player->v.waterlevel = 0;
    player->v.watertype = CONTENTS_EMPTY;
    contents = SV_PointContents(point);
    if (contents <= CONTENTS_WATER) {
	player->v.watertype = contents;
	player->v.waterlevel = 1;
	point[2] = player->v.origin[2] + (player->v.mins[2] + player->v.maxs[2]) * 0.5;
	contents = SV_PointContents(point);
	if (contents <= CONTENTS_WATER) {
	    player->v.waterlevel = 2;
	    point[2] = player->v.origin[2] + player->v.view_ofs[2];
	    contents = SV_PointContents(point);
	    if (contents <= CONTENTS_WATER)
		player->v.waterlevel = 3;
	}
    }

    return player->v.waterlevel > 1;
}

/*
============
SV_WallFriction

============
*/
static void
SV_WallFriction(edict_t *player, trace_t *trace)
{
    vec3_t forward, right, up;
    float d, i;
    vec3_t into, side;

    AngleVectors(player->v.v_angle, forward, right, up);
    d = DotProduct(trace->plane.normal, forward);

    d += 0.5;
    if (d >= 0)
	return;

// cut the tangential velocity
    i = DotProduct(trace->plane.normal, player->v.velocity);
    VectorScale(trace->plane.normal, i, into);
    VectorSubtract(player->v.velocity, into, side);

    player->v.velocity[0] = side[0] * (1 + d);
    player->v.velocity[1] = side[1] * (1 + d);
}

/*
=====================
SV_TryUnstick

Player has come to a dead stop, possibly due to the problem with limited
float precision at some angle joins in the BSP hull.

Try fixing by pushing one pixel in each direction.

This is a hack, but in the interest of good gameplay...
======================
*/
static int
SV_TryUnstick(edict_t *player, const vec3_t oldvel)
{
    int i;
    vec3_t oldorg;
    vec3_t dir;
    int clip;
    trace_t trace;

    VectorCopy(player->v.origin, oldorg);
    VectorCopy(vec3_origin, dir);

    /* try pushing a little in an axial direction */
    for (i = 0; i < 8; i++) {
	switch (i) {
	case 0:
	    dir[0] = 2;
	    dir[1] = 0;
	    break;
	case 1:
	    dir[0] = 0;
	    dir[1] = 2;
	    break;
	case 2:
	    dir[0] = -2;
	    dir[1] = 0;
	    break;
	case 3:
	    dir[0] = 0;
	    dir[1] = -2;
	    break;
	case 4:
	    dir[0] = 2;
	    dir[1] = 2;
	    break;
	case 5:
	    dir[0] = -2;
	    dir[1] = 2;
	    break;
	case 6:
	    dir[0] = 2;
	    dir[1] = -2;
	    break;
	case 7:
	    dir[0] = -2;
	    dir[1] = -2;
	    break;
	}

	SV_PushEntity(player, dir, &trace);

	/* retry the original move */
	player->v.velocity[0] = oldvel[0];
	player->v.velocity[1] = oldvel[1];
	player->v.velocity[2] = 0;
	clip = SV_FlyMove(player, 0.1, &trace);

	if (fabs(oldorg[1] - player->v.origin[1]) > 4
	    || fabs(oldorg[0] - player->v.origin[0]) > 4) {
	    return clip;
	}

	/* go back to the original pos and try again */
	VectorCopy(oldorg, player->v.origin);
    }

    /* still can't move */
    VectorCopy(vec3_origin, player->v.velocity);
    return MOVE_CLIP_FLOOR | MOVE_CLIP_WALL | MOVE_CLIP_STOP;
}

/*
=====================
SV_WalkMove

Only used by players
======================
*/
#define	STEPSIZE	18
static void
SV_WalkMove(edict_t *player)
{
    vec3_t upmove, downmove;
    vec3_t oldorg, oldvel;
    vec3_t nosteporg, nostepvel;
    int clip;
    int oldonground;
    trace_t trace;
    const edict_t *ground;

//
// do a regular slide move unless it looks like you ran into a step
//
    oldonground = (int)player->v.flags & FL_ONGROUND;
    player->v.flags = (int)player->v.flags & ~FL_ONGROUND;

    VectorCopy(player->v.origin, oldorg);
    VectorCopy(player->v.velocity, oldvel);

    clip = SV_FlyMove(player, host_frametime, &trace);
    if (!(clip & MOVE_CLIP_WALL))
	return;

    /* don't stair up while jumping */
    if (!oldonground && player->v.waterlevel == 0)
	return;

    /* gibbed by a trigger */
    if (player->v.movetype != MOVETYPE_WALK)
	return;

    if (sv_nostep.value)
	return;

    if ((int)player->v.flags & FL_WATERJUMP)
	return;

    VectorCopy(player->v.origin, nosteporg);
    VectorCopy(player->v.velocity, nostepvel);

    /*
     * try moving up and forward to go up a step
     */

    /* back to start pos */
    VectorCopy(oldorg, player->v.origin);

    VectorCopy(vec3_origin, upmove);
    VectorCopy(vec3_origin, downmove);
    upmove[2] = STEPSIZE;
    downmove[2] = -STEPSIZE + oldvel[2] * host_frametime;

    /* move up - FIXME: don't link? */
    SV_PushEntity(player, upmove, &trace);

    /* move forward */
    player->v.velocity[0] = oldvel[0];
    player->v.velocity[1] = oldvel[1];
    player->v.velocity[2] = 0;
    clip = SV_FlyMove(player, host_frametime, &trace);

    /*
     * Check for stuckness, possibly due to the limited precision of
     * floats in the clipping hulls.
     */
    if (clip) {
	if (fabs(oldorg[1] - player->v.origin[1]) < 0.03125 &&
	    fabs(oldorg[0] - player->v.origin[0]) < 0.03125) {
	    /* stepping up didn't make any progress */
	    clip = SV_TryUnstick(player, oldvel);
	}
    }

    /* extra friction based on view angle */
    if (clip & MOVE_CLIP_WALL)
	SV_WallFriction(player, &trace);

    /* move down - FIXME: don't link? */
    ground = SV_PushEntity(player, downmove, &trace);

    if (trace.plane.normal[2] > 0.7) {
	if (player->v.solid == SOLID_BSP) {
	    player->v.flags = (int)player->v.flags | FL_ONGROUND;
	    player->v.groundentity = EDICT_TO_PROG(ground);
	}
    } else {
	/*
	 * If the push down didn't end up on good ground, use the move without
	 * the step up.  This happens near wall / slope combinations, and can
	 * cause the player to hop up higher on a slope too steep to climb.
	 */
	VectorCopy(nosteporg, player->v.origin);
	VectorCopy(nostepvel, player->v.velocity);
    }
}


/*
================
SV_Physics_Client

Player character actions
================
*/
static void
SV_Physics_Client(edict_t *player, int playernum)
{
    if (!svs.clients[playernum - 1].active)
	return;

//
// call standard client pre-think
//
    pr_global_struct->time = sv.time;
    pr_global_struct->self = EDICT_TO_PROG(player);
    PR_ExecuteProgram(pr_global_struct->PlayerPreThink);

//
// do a move
//
    SV_CheckVelocity(player);

//
// decide which move function to call
//
    switch ((int)player->v.movetype) {
    case MOVETYPE_NONE:
	if (!SV_RunThink(player))
	    return;
	break;

    case MOVETYPE_WALK:
	if (!SV_RunThink(player))
	    return;
	if (!SV_CheckWater(player) && !((int)player->v.flags & FL_WATERJUMP))
	    SV_AddGravity(player);
	SV_CheckStuck(player);
	SV_WalkMove(player);
	break;

    case MOVETYPE_TOSS:
    case MOVETYPE_BOUNCE:
	SV_Physics_Toss(player);
	break;

    case MOVETYPE_FLY:
	if (!SV_RunThink(player))
	    return;
	SV_FlyMove(player, host_frametime, NULL);
	break;

    case MOVETYPE_NOCLIP:
	if (!SV_RunThink(player))
	    return;
	VectorMA(player->v.origin, host_frametime, player->v.velocity,
		 player->v.origin);
	break;

    default:
	Sys_Error("%s: bad movetype %i", __func__, (int)player->v.movetype);
    }

//
// call standard player post-think
//
    SV_LinkEdict(player, true);

    pr_global_struct->time = sv.time;
    pr_global_struct->self = EDICT_TO_PROG(player);
    PR_ExecuteProgram(pr_global_struct->PlayerPostThink);
}

//============================================================================

#endif /* NQ_HACK */

/*
=============
SV_Physics_None

Non moving objects can only think
=============
*/
static void
SV_Physics_None(edict_t *ent)
{
// regular thinking
    SV_RunThink(ent);
}

/*
=============
SV_Physics_Noclip

A moving object that doesn't obey physics
=============
*/
static void
SV_Physics_Noclip(edict_t *ent)
{
// regular thinking
    if (!SV_RunThink(ent))
	return;

    VectorMA(ent->v.angles, host_frametime, ent->v.avelocity, ent->v.angles);
    VectorMA(ent->v.origin, host_frametime, ent->v.velocity, ent->v.origin);

    SV_LinkEdict(ent, false);
}

/*
==============================================================================

TOSS / BOUNCE

==============================================================================
*/

/*
=============
SV_CheckWaterTransition

=============
*/
static void
SV_CheckWaterTransition(edict_t *ent)
{
    int contents;

    contents = SV_PointContents(ent->v.origin);
    if (!ent->v.watertype) {
	/* just spawned here */
	ent->v.watertype = contents;
	ent->v.waterlevel = 1;
	return;
    }

    if (contents <= CONTENTS_WATER) {
	if (ent->v.watertype == CONTENTS_EMPTY) {
	    /* just crossed into water */
	    SV_StartSound(ent, 0, "misc/h2ohit1.wav", 255, 1);
	}
	ent->v.watertype = contents;
	ent->v.waterlevel = 1;
    } else {
	if (ent->v.watertype != CONTENTS_EMPTY) {
	    /* just crossed into water */
	    SV_StartSound(ent, 0, "misc/h2ohit1.wav", 255, 1);
	}
	ent->v.watertype = CONTENTS_EMPTY;
	ent->v.waterlevel = contents;
    }
}

/*
=============
SV_Physics_Toss

Toss, bounce, and fly movement.  When onground, do nothing.
=============
*/
static void
SV_Physics_Toss(edict_t *ent)
{
    const edict_t *ground;
    trace_t trace;
    vec3_t move;
    float backoff;

    /* regular thinking */
    if (!SV_RunThink(ent))
	return;

#ifdef QW_HACK
    if (ent->v.velocity[2] > 0)
	ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;
#endif

    /* if onground, return without moving */
    if (((int)ent->v.flags & FL_ONGROUND))
	return;

    SV_CheckVelocity(ent);

    /* add gravity */
    if (ent->v.movetype != MOVETYPE_FLY
	&& ent->v.movetype != MOVETYPE_FLYMISSILE)
	SV_AddGravity(ent);

    /* move angles */
    VectorMA(ent->v.angles, host_frametime, ent->v.avelocity, ent->v.angles);

    /* move origin */
    VectorScale(ent->v.velocity, host_frametime, move);
    ground = SV_PushEntity(ent, move, &trace);
    if (trace.fraction == 1)
	return;
    if (ent->free)
	return;

    if (ent->v.movetype == MOVETYPE_BOUNCE)
	backoff = 1.5;
    else
	backoff = 1;

    ClipVelocity(ent->v.velocity, trace.plane.normal, ent->v.velocity,
		 backoff);

    /* stop if on ground */
    if (trace.plane.normal[2] > 0.7) {
	if (ent->v.velocity[2] < 60 || ent->v.movetype != MOVETYPE_BOUNCE) {
	    ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
	    ent->v.groundentity = EDICT_TO_PROG(ground);
	    VectorCopy(vec3_origin, ent->v.velocity);
	    VectorCopy(vec3_origin, ent->v.avelocity);
	}
    }
    /* check for in water */
    SV_CheckWaterTransition(ent);
}

/*
===============================================================================

STEPPING MOVEMENT

===============================================================================
*/

/*
=============
SV_Physics_Step

Monsters freefall when they don't have a ground entity, otherwise
all movement is done with discrete steps.

This is also used for objects that have become still on the ground, but
will fall if the floor is pulled out from under them.
FIXME: is this true?
=============
*/
static void
SV_Physics_Step(edict_t *ent)
{
    qboolean hitsound;

    /* freefall if not onground */
    if (!((int)ent->v.flags & (FL_ONGROUND | FL_FLY | FL_SWIM))) {
#ifdef NQ_HACK
	hitsound = (ent->v.velocity[2] < sv_gravity.value * -0.1);
#endif
#ifdef QW_HACK
	hitsound = (ent->v.velocity[2] < movevars.gravity * -0.1);
#endif
	SV_AddGravity(ent);
	SV_CheckVelocity(ent);
	SV_FlyMove(ent, host_frametime, NULL);
	SV_LinkEdict(ent, true);

	if ((int)ent->v.flags & FL_ONGROUND) {
	    /* just hit ground */
	    if (hitsound)
		SV_StartSound(ent, 0, "demon/dland2.wav", 255, 1);
	}
    }

    /* regular thinking */
    SV_RunThink(ent);
    SV_CheckWaterTransition(ent);
}

//============================================================================

#ifdef QW_HACK

void
SV_SetMoveVars(void)
{
    movevars.gravity = sv_gravity.value;
    movevars.stopspeed = sv_stopspeed.value;
    movevars.maxspeed = sv_maxspeed.value;
    movevars.spectatormaxspeed = sv_spectatormaxspeed.value;
    movevars.accelerate = sv_accelerate.value;
    movevars.airaccelerate = sv_airaccelerate.value;
    movevars.wateraccelerate = sv_wateraccelerate.value;
    movevars.friction = sv_friction.value;
    movevars.waterfriction = sv_waterfriction.value;
    movevars.entgravity = 1.0;
}

void
SV_ProgStartFrame(void)
{
// let the progs know that a new frame has started
    pr_global_struct->self = EDICT_TO_PROG(sv.edicts);
    pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
    pr_global_struct->time = sv.time;
    PR_ExecuteProgram(pr_global_struct->StartFrame);
}

/*
================
SV_RunEntity

================
*/
static void
SV_RunEntity(edict_t *ent)
{
    if (ent->v.lastruntime == (float)realtime)
	return;
    ent->v.lastruntime = (float)realtime;

    switch ((int)ent->v.movetype) {
    case MOVETYPE_PUSH:
	SV_Physics_Pusher(ent);
	break;
    case MOVETYPE_NONE:
	SV_Physics_None(ent);
	break;
    case MOVETYPE_NOCLIP:
	SV_Physics_Noclip(ent);
	break;
    case MOVETYPE_STEP:
	SV_Physics_Step(ent);
	break;
    case MOVETYPE_TOSS:
    case MOVETYPE_BOUNCE:
    case MOVETYPE_FLY:
    case MOVETYPE_FLYMISSILE:
	SV_Physics_Toss(ent);
	break;
    default:
	SV_Error("SV_Physics: bad movetype %i", (int)ent->v.movetype);
    }
}

/*
================
SV_RunNewmis

================
*/
void
SV_RunNewmis(void)
{
    edict_t *ent;

    if (!pr_global_struct->newmis)
	return;
    ent = PROG_TO_EDICT(pr_global_struct->newmis);
    host_frametime = 0.05;
    pr_global_struct->newmis = 0;

    SV_RunEntity(ent);
}

#endif /* QW_HACK */

/*
================
SV_Physics

================
*/
void
SV_Physics(void)
{
#ifdef QW_HACK
    static double old_time;
#endif
    int i;
    edict_t *ent;

#ifdef NQ_HACK
    /*let the progs know that a new frame has started */
    pr_global_struct->self = EDICT_TO_PROG(sv.edicts);
    pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
    pr_global_struct->time = sv.time;
    PR_ExecuteProgram(pr_global_struct->StartFrame);
#endif
#ifdef QW_HACK
    /* don't bother running a frame if sys_ticrate seconds haven't passed */
    host_frametime = realtime - old_time;
    if (host_frametime < sv_mintic.value)
	return;
    if (host_frametime > sv_maxtic.value)
	host_frametime = sv_maxtic.value;
    old_time = realtime;
    pr_global_struct->frametime = host_frametime;
    SV_ProgStartFrame();
#endif

    SV_CheckAllEnts();

    /*
     * Treat each object in turn.
     * Even the world gets a chance to think
     */
    ent = sv.edicts;
    for (i = 0; i < sv.num_edicts; i++, ent = NEXT_EDICT(ent)) {
	if (ent->free)
	    continue;

	if (pr_global_struct->force_retouch)
	    /* force retouch even for stationary */
	    SV_LinkEdict(ent, true);

#ifdef NQ_HACK
	if (i > 0 && i <= svs.maxclients)
	    SV_Physics_Client(ent, i);
	else if (ent->v.movetype == MOVETYPE_PUSH)
	    SV_Physics_Pusher(ent);
	else if (ent->v.movetype == MOVETYPE_NONE)
	    SV_Physics_None(ent);
	else if (ent->v.movetype == MOVETYPE_NOCLIP)
	    SV_Physics_Noclip(ent);
	else if (ent->v.movetype == MOVETYPE_STEP)
	    SV_Physics_Step(ent);
	else if (ent->v.movetype == MOVETYPE_TOSS
		 || ent->v.movetype == MOVETYPE_BOUNCE
		 || ent->v.movetype == MOVETYPE_FLY
		 || ent->v.movetype == MOVETYPE_FLYMISSILE)
	    SV_Physics_Toss(ent);
	else
	    Sys_Error("%s: bad movetype %i", __func__, (int)ent->v.movetype);
#endif
#ifdef QW_HACK
	/* clients are run directly from packets */
	if (i > 0 && i <= MAX_CLIENTS)
	    continue;

	SV_RunEntity(ent);
	SV_RunNewmis();
#endif
    }

    if (pr_global_struct->force_retouch)
	pr_global_struct->force_retouch--;

#ifdef NQ_HACK
    sv.time += host_frametime;
#endif
}
