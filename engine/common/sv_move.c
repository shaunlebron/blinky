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
// sv_move.c -- monster movement

#ifdef NQ_HACK
#include "progs.h"
#endif

#ifdef QW_HACK
#include "bspfile.h"
#include "qwsvdef.h"
#endif

#include "server.h"
#include "world.h"

#define	STEPSIZE	18

/*
=============
SV_CheckBottom

Returns false if any part of the bottom of the entity is off an edge that
is not a staircase.

=============
*/
qboolean
SV_CheckBottom(edict_t *ent)
{
    vec3_t mins, maxs, start, stop;
    trace_t trace;
    int x, y;
    float mid, bottom;

    VectorAdd(ent->v.origin, ent->v.mins, mins);
    VectorAdd(ent->v.origin, ent->v.maxs, maxs);

// if all of the points under the corners are solid world, don't bother
// with the tougher checks
// the corners must be within 16 of the midpoint
    start[2] = mins[2] - 1;
    for (x = 0; x <= 1; x++) {
	for (y = 0; y <= 1; y++) {
	    start[0] = x ? maxs[0] : mins[0];
	    start[1] = y ? maxs[1] : mins[1];
	    if (SV_PointContents(start) != CONTENTS_SOLID)
		goto realcheck;
	}
    }
    return true;		// we got out easy

  realcheck:
//
// check it for real...
//
    start[2] = mins[2];

// the midpoint must be within 16 of the bottom
    start[0] = stop[0] = (mins[0] + maxs[0]) * 0.5;
    start[1] = stop[1] = (mins[1] + maxs[1]) * 0.5;
    stop[2] = start[2] - 2 * STEPSIZE;

    SV_TraceLine(start, stop, MOVE_NOMONSTERS, ent, &trace);
    if (trace.fraction == 1.0)
	return false;
    mid = bottom = trace.endpos[2];

// the corners must be within 16 of the midpoint
    for (x = 0; x <= 1; x++) {
	for (y = 0; y <= 1; y++) {
	    start[0] = stop[0] = x ? maxs[0] : mins[0];
	    start[1] = stop[1] = y ? maxs[1] : mins[1];

	    SV_TraceLine(start, stop, MOVE_NOMONSTERS, ent, &trace);
	    if (trace.fraction != 1.0 && trace.endpos[2] > bottom)
		bottom = trace.endpos[2];
	    if (trace.fraction == 1.0 || mid - trace.endpos[2] > STEPSIZE)
		return false;
	}
    }

    return true;
}


/*
=============
SV_movestep

Called by monster program code.
The move will be adjusted for slopes and stairs, but if the move isn't
possible, no move is done, false is returned, and
pr_global_struct->trace_normal is set to the normal of the blocking wall
=============
*/
qboolean
SV_movestep(edict_t *entity, vec3_t move, qboolean relink)
{
    float dz;
    vec3_t oldorg, neworg, end;
    trace_t trace;
    int i;
    edict_t *enemy;
    const edict_t *ground;

    /* try the move */
    VectorCopy(entity->v.origin, oldorg);
    VectorAdd(entity->v.origin, move, neworg);

    /* flying monsters don't step up */
    if ((int)entity->v.flags & (FL_SWIM | FL_FLY)) {
	/* try one move with vertical motion, then one without */
	for (i = 0; i < 2; i++) {
	    VectorAdd(entity->v.origin, move, neworg);
	    enemy = PROG_TO_EDICT(entity->v.enemy);
	    if (i == 0 && enemy != sv.edicts) {
		dz = entity->v.origin[2] -
		    PROG_TO_EDICT(entity->v.enemy)->v.origin[2];
		if (dz > 40)
		    neworg[2] -= 8;
		if (dz < 30)
		    neworg[2] += 8;
	    }
	    SV_TraceMoveEntity(entity, entity->v.origin, neworg, MOVE_NORMAL,
			       &trace);

	    if (trace.fraction == 1) {
		/* swimming monsters can't leave the water */
		if (((int)entity->v.flags & FL_SWIM)
		    && SV_PointContents(trace.endpos) == CONTENTS_EMPTY)
		    return false;

		VectorCopy(trace.endpos, entity->v.origin);
		if (relink)
		    SV_LinkEdict(entity, true);
		return true;
	    }

	    if (enemy == sv.edicts)
		break;
	}

	return false;
    }

    /* push down from a step height above the wished position */
    neworg[2] += STEPSIZE;
    VectorCopy(neworg, end);
    end[2] -= STEPSIZE * 2;

    ground = SV_TraceMoveEntity(entity, neworg, end, MOVE_NORMAL, &trace);
    if (trace.allsolid)
	return false;

    if (trace.startsolid) {
	neworg[2] -= STEPSIZE;
	ground = SV_TraceMoveEntity(entity, neworg, end, MOVE_NORMAL, &trace);
	if (trace.allsolid || trace.startsolid)
	    return false;
    }
    if (trace.fraction == 1) {
	/* if monster had the ground pulled out, go ahead and fall */
	if ((int)entity->v.flags & FL_PARTIALGROUND) {
	    VectorAdd(entity->v.origin, move, entity->v.origin);
	    if (relink)
		SV_LinkEdict(entity, true);
	    entity->v.flags = (int)entity->v.flags & ~FL_ONGROUND;

	    return true;
	}

	/* walked off an edge */
	return false;
    }

    /* check point traces down for dangling corners */
    VectorCopy(trace.endpos, entity->v.origin);
    if (!SV_CheckBottom(entity)) {
	if ((int)entity->v.flags & FL_PARTIALGROUND) {
	    /*
	     * entity had floor mostly pulled out from underneath it
	     * and is trying to correct
	     */
	    if (relink)
		SV_LinkEdict(entity, true);
	    return true;
	}
	VectorCopy(oldorg, entity->v.origin);
	return false;
    }

    /* the move is ok, put the entity back on the ground */
    if ((int)entity->v.flags & FL_PARTIALGROUND)
	entity->v.flags = (int)entity->v.flags & ~FL_PARTIALGROUND;

    entity->v.groundentity = EDICT_TO_PROG(ground);
    if (relink)
	SV_LinkEdict(entity, true);

    return true;
}


//============================================================================

/*
======================
SV_StepDirection

Turns to the movement direction, and walks the current distance if
facing it.

======================
*/
void PF_changeyaw(void);

qboolean
SV_StepDirection(edict_t *ent, float yaw, float dist)
{
    vec3_t move, oldorigin;
    float delta;

    ent->v.ideal_yaw = yaw;
    PF_changeyaw();

    yaw = yaw * M_PI * 2 / 360;
    move[0] = cos(yaw) * dist;
    move[1] = sin(yaw) * dist;
    move[2] = 0;

    VectorCopy(ent->v.origin, oldorigin);
    if (SV_movestep(ent, move, false)) {
	delta = ent->v.angles[YAW] - ent->v.ideal_yaw;
	if (delta > 45 && delta < 315) {	// not turned far enough, so don't take the step
	    VectorCopy(oldorigin, ent->v.origin);
	}
	SV_LinkEdict(ent, true);
	return true;
    }
    SV_LinkEdict(ent, true);

    return false;
}

/*
======================
SV_FixCheckBottom

======================
*/
void
SV_FixCheckBottom(edict_t *ent)
{
//      Con_Printf ("SV_FixCheckBottom\n");

    ent->v.flags = (int)ent->v.flags | FL_PARTIALGROUND;
}



/*
================
SV_NewChaseDir

================
*/
#define	DI_NODIR	-1
void
SV_NewChaseDir(edict_t *actor, edict_t *enemy, float dist)
{
    float deltax, deltay;
    float d[3];
    float tdir, olddir, turnaround;

    olddir = anglemod((int)(actor->v.ideal_yaw / 45) * 45);
    turnaround = anglemod(olddir - 180);

    deltax = enemy->v.origin[0] - actor->v.origin[0];
    deltay = enemy->v.origin[1] - actor->v.origin[1];
    if (deltax > 10)
	d[1] = 0;
    else if (deltax < -10)
	d[1] = 180;
    else
	d[1] = DI_NODIR;
    if (deltay < -10)
	d[2] = 270;
    else if (deltay > 10)
	d[2] = 90;
    else
	d[2] = DI_NODIR;

// try direct route
    if (d[1] != DI_NODIR && d[2] != DI_NODIR) {
	if (d[1] == 0)
	    tdir = d[2] == 90 ? 45 : 315;
	else
	    tdir = d[2] == 90 ? 135 : 215;

	if (tdir != turnaround && SV_StepDirection(actor, tdir, dist))
	    return;
    }
// try other directions
    if (((rand() & 3) & 1) || abs(deltay) > abs(deltax)) {
	tdir = d[1];
	d[1] = d[2];
	d[2] = tdir;
    }

    if (d[1] != DI_NODIR && d[1] != turnaround
	&& SV_StepDirection(actor, d[1], dist))
	return;

    if (d[2] != DI_NODIR && d[2] != turnaround
	&& SV_StepDirection(actor, d[2], dist))
	return;

/* there is no direct path to the player, so pick another direction */

    if (olddir != DI_NODIR && SV_StepDirection(actor, olddir, dist))
	return;

    if (rand() & 1) {		/*randomly determine direction of search */
	for (tdir = 0; tdir <= 315; tdir += 45)
	    if (tdir != turnaround && SV_StepDirection(actor, tdir, dist))
		return;
    } else {
	for (tdir = 315; tdir >= 0; tdir -= 45)
	    if (tdir != turnaround && SV_StepDirection(actor, tdir, dist))
		return;
    }

    if (turnaround != DI_NODIR && SV_StepDirection(actor, turnaround, dist))
	return;

    actor->v.ideal_yaw = olddir;	// can't move

// if a bridge was pulled out from underneath a monster, it may not have
// a valid standing position at all

    if (!SV_CheckBottom(actor))
	SV_FixCheckBottom(actor);

}

/*
======================
SV_CloseEnough

======================
*/
qboolean
SV_CloseEnough(edict_t *ent, edict_t *goal, float dist)
{
    int i;

    for (i = 0; i < 3; i++) {
	if (goal->v.absmin[i] > ent->v.absmax[i] + dist)
	    return false;
	if (goal->v.absmax[i] < ent->v.absmin[i] - dist)
	    return false;
    }
    return true;
}

/*
======================
SV_MoveToGoal

======================
*/
void
SV_MoveToGoal(void)
{
    edict_t *ent, *goal;
    float dist;

    ent = PROG_TO_EDICT(pr_global_struct->self);
    goal = PROG_TO_EDICT(ent->v.goalentity);
    dist = G_FLOAT(OFS_PARM0);

    if (!((int)ent->v.flags & (FL_ONGROUND | FL_FLY | FL_SWIM))) {
	G_FLOAT(OFS_RETURN) = 0;
	return;
    }
// if the next step hits the enemy, return immediately
    if (PROG_TO_EDICT(ent->v.enemy) != sv.edicts
	&& SV_CloseEnough(ent, goal, dist))
	return;

// bump around...
    if ((rand() & 3) == 1 || !SV_StepDirection(ent, ent->v.ideal_yaw, dist)) {
	SV_NewChaseDir(ent, goal, dist);
    }
}
