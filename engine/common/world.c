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
// world.c -- world query functions

#include "bspfile.h"
#include "console.h"
#include "mathlib.h"
#include "model.h"
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
#if defined(QW_HACK) && defined(SERVERONLY)
#include "qwsvdef.h"
#include "pmove.h"
/* FIXME - quick hack to enable merging of NQ/QWSV shared code */
#define Host_Error SV_Error
#endif

/*
 * Entities never clip against themselves, or their owner.
 * Line of sight checks trace->crosscontent, but bullets don't.
 */

typedef struct {
    vec3_t mins;
    vec3_t maxs;
} bounds_t;

typedef struct {
    bounds_t move;		/* enclose the test object along entire move */
    bounds_t object;		/* size of the moving object */
    bounds_t monster;		/* size to test against monsters (>= object) */
    vec3_t start, end;
    movetype_t type;
    const edict_t *passedict;
} moveclip_t;

/*
================
SV_HullForEntity

Returns a hull that can be used for testing or clipping an object of mins/maxs
size.
Offset is filled in to contain the adjustment that must be added to the
testing object's origin to get a point to use with the returned hull.
================
*/
static const hull_t *
SV_HullForEntity(const edict_t *ent, const vec3_t mins, const vec3_t maxs,
		 vec3_t offset, boxhull_t *boxhull)
{
    model_t *model;
    const brushmodel_t *brushmodel;
    const hull_t *hull;
    vec3_t hullmins, hullmaxs, size;

// decide which clipping hull to use, based on the size
    if (ent->v.solid == SOLID_BSP) {	// explicit hulls in the BSP model
	if (ent->v.movetype != MOVETYPE_PUSH)
	    SV_Error("SOLID_BSP without MOVETYPE_PUSH");

	model = sv.models[(int)ent->v.modelindex];

	if (!model || model->type != mod_brush)
	    SV_Error("MOVETYPE_PUSH with a non bsp model");

	brushmodel = BrushModel(model);
	VectorSubtract(maxs, mins, size);
	if (size[0] < 3)
	    hull = &brushmodel->hulls[0];
	else if (size[0] <= 32)
	    hull = &brushmodel->hulls[1];
	else
	    hull = &brushmodel->hulls[2];

// calculate an offset value to center the origin
	VectorSubtract(hull->clip_mins, mins, offset);
	VectorAdd(offset, ent->v.origin, offset);
    } else {
	/* create a temp hull from bounding box sizes */
	VectorSubtract(ent->v.mins, maxs, hullmins);
	VectorSubtract(ent->v.maxs, mins, hullmaxs);
	Mod_CreateBoxhull(hullmins, hullmaxs, boxhull);
	hull = &boxhull->hull;

	VectorCopy(ent->v.origin, offset);
    }

    return hull;
}

/*
===============================================================================

ENTITY AREA CHECKING

===============================================================================
*/

typedef struct areanode_s {
    int axis;			// -1 = leaf node
    float dist;
    struct areanode_s *children[2];
    link_t trigger_edicts;
    link_t solid_edicts;
} areanode_t;

#define	AREA_DEPTH	4
#define	AREA_NODES	32

static areanode_t sv_areanodes[AREA_NODES];
static int sv_numareanodes;

#if defined(QW_HACK) && defined(SERVERONLY)
/*
====================
AddLinksToPhysents

====================
*/
static void
SV_AddLinksToPhysents_r(const areanode_t *node, const edict_t *player,
			const vec3_t mins, const vec3_t maxs,
			physent_stack_t *pestack)
{
    const link_t *link, *next;
    const link_t *const solids = &node->solid_edicts;
    const edict_t *check;
    int i, playernum;
    physent_t *physent;

    playernum = EDICT_TO_PROG(player);
    physent = &pestack->physents[pestack->numphysent];

    /* touch linked edicts */
    for (link = solids->next; link != solids; link = next) {
	next = link->next;
	check = const_container_of(link, edict_t, area);

	/* player's own missile */
	if (check->v.owner == playernum)
	    continue;
	if (check->v.solid == SOLID_BSP
	    || check->v.solid == SOLID_BBOX
	    || check->v.solid == SOLID_SLIDEBOX) {
	    if (check == player)
		continue;
	    for (i = 0; i < 3; i++)
		if (check->v.absmin[i] > maxs[i]
		    || check->v.absmax[i] < mins[i])
		    break;
	    if (i != 3)
		continue;
	    if (pestack->numphysent == MAX_PHYSENTS)
		return;

	    VectorCopy(check->v.origin, physent->origin);
	    physent->entitynum = NUM_FOR_EDICT(check);
	    if (check->v.solid == SOLID_BSP) {
		const model_t *model = sv.models[(int)(check->v.modelindex)];
		physent->brushmodel = ConstBrushModel(model);
	    } else {
		physent->brushmodel = NULL;
		VectorCopy(check->v.mins, physent->mins);
		VectorCopy(check->v.maxs, physent->maxs);
	    }
	    physent++;
	    pestack->numphysent++;
	}
    }

    /* recurse down both sides */
    if (node->axis == -1)
	return;

    if (maxs[node->axis] > node->dist)
	SV_AddLinksToPhysents_r(node->children[0], player, mins, maxs, pestack);
    if (mins[node->axis] < node->dist)
	SV_AddLinksToPhysents_r(node->children[1], player, mins, maxs, pestack);
}

void
SV_AddLinksToPhysents(const edict_t *player, const vec3_t mins,
		      const vec3_t maxs, physent_stack_t *pestack)
{
    SV_AddLinksToPhysents_r(sv_areanodes, player, mins, maxs, pestack);
}
#endif

/*
===============
SV_CreateAreaNode

===============
*/
static areanode_t *
SV_CreateAreaNode(int depth, const vec3_t mins, const vec3_t maxs)
{
    areanode_t *anode;
    vec3_t size;
    vec3_t mins1, maxs1, mins2, maxs2;

    if (sv_numareanodes == AREA_NODES)
	SV_Error("%s: sv_numareanodes == AREA_NODES", __func__);

    anode = &sv_areanodes[sv_numareanodes];
    sv_numareanodes++;

    ClearLink(&anode->trigger_edicts);
    ClearLink(&anode->solid_edicts);

    if (depth == AREA_DEPTH) {
	anode->axis = -1;
	anode->children[0] = anode->children[1] = NULL;
	return anode;
    }

    VectorSubtract(maxs, mins, size);
    if (size[0] > size[1])
	anode->axis = 0;
    else
	anode->axis = 1;

    anode->dist = 0.5 * (maxs[anode->axis] + mins[anode->axis]);
    VectorCopy(mins, mins1);
    VectorCopy(mins, mins2);
    VectorCopy(maxs, maxs1);
    VectorCopy(maxs, maxs2);

    maxs1[anode->axis] = mins2[anode->axis] = anode->dist;

    anode->children[0] = SV_CreateAreaNode(depth + 1, mins2, maxs2);
    anode->children[1] = SV_CreateAreaNode(depth + 1, mins1, maxs1);

    return anode;
}

/*
===============
SV_ClearWorld

===============
*/
void
SV_ClearWorld(void)
{
    model_t *model = &sv.worldmodel->model;

    memset(sv_areanodes, 0, sizeof(sv_areanodes));
    sv_numareanodes = 0;
    SV_CreateAreaNode(0, model->mins, model->maxs);
}

static link_t **sv_link_next;
static link_t **sv_link_prev;

/*
===============
SV_UnlinkEdict

===============
*/
void
SV_UnlinkEdict(edict_t *ent)
{
    if (!ent->area.prev)
	return;			// not linked in anywhere
    RemoveLink(&ent->area);
    if (sv_link_next && *sv_link_next == &ent->area)
	*sv_link_next = ent->area.next;
    if (sv_link_prev && *sv_link_prev == &ent->area)
	*sv_link_prev = ent->area.prev;
    ent->area.prev = ent->area.next = NULL;
}


/*
====================
SV_TouchLinks
====================
*/
static void
SV_TouchLinks(edict_t *ent, const areanode_t *node)
{
    link_t *link, *next;
    const link_t *const triggers = &node->trigger_edicts;
    edict_t *touch;
    int old_self, old_other;

    /* touch linked edicts */
    for (link = triggers->next; link != triggers; link = next) {
	sv_link_next = &next;
	/*
	 * FIXME - Just paranoia? Check if this can really happen...
	 *         (I think it was related to the E2M2 drawbridge bug)
	 */
	if (!link) { /* my area got removed out from under me! */
	    Con_Printf ("%s: encountered NULL link\n", __func__);
	    break;
	}

	next = link->next;
	touch = container_of(link, edict_t, area);
	if (touch == ent)
	    continue;
	if (!touch->v.touch || touch->v.solid != SOLID_TRIGGER)
	    continue;
	if (ent->v.absmin[0] > touch->v.absmax[0]
	    || ent->v.absmin[1] > touch->v.absmax[1]
	    || ent->v.absmin[2] > touch->v.absmax[2]
	    || ent->v.absmax[0] < touch->v.absmin[0]
	    || ent->v.absmax[1] < touch->v.absmin[1]
	    || ent->v.absmax[2] < touch->v.absmin[2])
	    continue;

	old_self = pr_global_struct->self;
	old_other = pr_global_struct->other;

	pr_global_struct->self = EDICT_TO_PROG(touch);
	pr_global_struct->other = EDICT_TO_PROG(ent);
	pr_global_struct->time = sv.time;
	PR_ExecuteProgram(touch->v.touch);

	pr_global_struct->self = old_self;
	pr_global_struct->other = old_other;
    }

    sv_link_next = NULL;

    /* recurse down both sides */
    if (node->axis == -1)
	return;

    if (ent->v.absmax[node->axis] > node->dist)
	SV_TouchLinks(ent, node->children[0]);
    if (ent->v.absmin[node->axis] < node->dist)
	SV_TouchLinks(ent, node->children[1]);
}


/*
===============
SV_FindTouchedLeafs

===============
*/
static void
SV_FindTouchedLeafs(edict_t *ent, const mnode_t *node)
{
    const mplane_t *splitplane;
    const mleaf_t *leaf;
    int sides;
    int leafnum;

    if (node->contents == CONTENTS_SOLID)
	return;

    /* add an efrag if the node is a leaf */
    if (node->contents < 0) {
	if (ent->num_leafs == MAX_ENT_LEAFS)
	    return;

	leaf = (mleaf_t *)node;
	leafnum = leaf - sv.worldmodel->leafs - 1;
	ent->leafnums[ent->num_leafs] = leafnum;
	ent->num_leafs++;
	return;
    }

    /* recurse down the contacted sides */
    splitplane = node->plane;
    sides = BOX_ON_PLANE_SIDE(ent->v.absmin, ent->v.absmax, splitplane);
    if (sides & PSIDE_FRONT)
	SV_FindTouchedLeafs(ent, node->children[0]);
    if (sides & PSIDE_BACK)
	SV_FindTouchedLeafs(ent, node->children[1]);
}

/*
===============
SV_LinkEdict

===============
*/
void
SV_LinkEdict(edict_t *ent, qboolean touch_triggers)
{
    areanode_t *node;

    if (ent->area.prev)
	SV_UnlinkEdict(ent);	// unlink from old position

    if (ent == sv.edicts)
	return;			// don't add the world

    if (ent->free)
	return;

    /* set the abs box */
    VectorAdd(ent->v.origin, ent->v.mins, ent->v.absmin);
    VectorAdd(ent->v.origin, ent->v.maxs, ent->v.absmax);

    /*
     * To make items easier to pick up and allow them to be grabbed off of
     * shelves, the abs sizes are expanded
     */
    if ((int)ent->v.flags & FL_ITEM) {
	ent->v.absmin[0] -= 15;
	ent->v.absmin[1] -= 15;
	ent->v.absmax[0] += 15;
	ent->v.absmax[1] += 15;
    } else {
	/*
	 * because movement is clipped an epsilon away from an actual edge, we
	 * must fully check even when bounding boxes don't quite touch
	 */
	ent->v.absmin[0] -= 1;
	ent->v.absmin[1] -= 1;
	ent->v.absmin[2] -= 1;
	ent->v.absmax[0] += 1;
	ent->v.absmax[1] += 1;
	ent->v.absmax[2] += 1;
    }

    /* link to PVS leafs */
    ent->num_leafs = 0;
    if (ent->v.modelindex)
	SV_FindTouchedLeafs(ent, sv.worldmodel->nodes);

    if (ent->v.solid == SOLID_NOT)
	return;

    /* find the first node that the ent's box crosses */
    node = sv_areanodes;
    while (1) {
	if (node->axis == -1)
	    break;
	if (ent->v.absmin[node->axis] > node->dist)
	    node = node->children[0];
	else if (ent->v.absmax[node->axis] < node->dist)
	    node = node->children[1];
	else
	    break;		// crosses the node
    }

    /* link it in */
    if (ent->v.solid == SOLID_TRIGGER)
	InsertLinkBefore(&ent->area, &node->trigger_edicts);
    else
	InsertLinkBefore(&ent->area, &node->solid_edicts);

    if (touch_triggers)
	/* touch all entities at this node and decend for more */
	SV_TouchLinks(ent, sv_areanodes);
}

/*
==================
SV_PointContents
==================
*/
int
SV_PointContents(const vec3_t point)
{
#ifdef NQ_HACK
    int contents;

    contents = Mod_HullPointContents(&sv.worldmodel->hulls[0], 0, point);
    if (contents <= CONTENTS_CURRENT_0 && contents >= CONTENTS_CURRENT_DOWN)
	contents = CONTENTS_WATER;

    return contents;
#endif
#if defined(QW_HACK) && defined(SERVERONLY)
    return Mod_HullPointContents(&sv.worldmodel->hulls[0], 0, point);
#endif
}

//===========================================================================

/*
============
SV_TestEntityPosition

A small wrapper around SV_BoxInSolidEntity that never clips against the
supplied entity.  TODO: This could be a lot more efficient?
============
*/
edict_t *
SV_TestEntityPosition(const edict_t *ent)
{
    edict_t *ret = NULL;
    trace_t trace;

    SV_TraceMoveEntity(ent, ent->v.origin, ent->v.origin, MOVE_NORMAL, &trace);
    if (trace.startsolid)
	ret = sv.edicts;

    return ret;
}

/*
==================
SV_ClipToEntity

Handles selection or creation of a clipping hull, and offseting (and
eventually rotation) of the end points.  Returns true if the move was clipped
by the entity, false otherwise.
==================
*/
static void
SV_ClipToEntity(const edict_t *ent, const vec3_t start, const vec3_t mins,
		const vec3_t maxs, const vec3_t end, trace_t *trace)
{
    boxhull_t boxhull;
    const hull_t *hull;
    vec3_t offset;
    vec3_t start_l, end_l;

    /* fill in a default trace */
    memset(trace, 0, sizeof(trace_t));
    trace->fraction = 1;
    trace->allsolid = true;
    VectorCopy(end, trace->endpos);

    /* get the clipping hull */
    hull = SV_HullForEntity(ent, mins, maxs, offset, &boxhull);

    VectorSubtract(start, offset, start_l);
    VectorSubtract(end, offset, end_l);

    /* trace a line through the apropriate clipping hull */
    Mod_TraceHull(hull, hull->firstclipnode, start_l, end_l, trace);

    /* fix trace up by the offset */
    if (trace->fraction != 1)
	VectorAdd(trace->endpos, offset, trace->endpos);
}

//===========================================================================

/*
====================
SV_ClipToLinks

Mins and maxs enclose the entire area swept by the move.
Returns a pointer to the entity which clipped the move, NULL otherwise.
====================
*/
static const edict_t *
SV_ClipToLinks_r(const edict_t *clipent, const areanode_t *node,
		 const moveclip_t *clip, trace_t *trace)
{
    link_t *link, *next;
    const link_t *const solids = &node->solid_edicts;
    edict_t *touch;
    trace_t stacktrace;
    const bounds_t *clipbounds;

    /* touch linked edicts */
    for (link = solids->next; link != solids; link = next) {
	next = link->next;
	touch = container_of(link, edict_t, area);
	if (touch->v.solid == SOLID_NOT)
	    continue;
	if (touch == clip->passedict)
	    continue;
	if (touch->v.solid == SOLID_TRIGGER)
	    SV_Error("Trigger in clipping list");

	if (clip->type == MOVE_NOMONSTERS && touch->v.solid != SOLID_BSP)
	    continue;

	if (clip->move.mins[0] > touch->v.absmax[0]
	    || clip->move.mins[1] > touch->v.absmax[1]
	    || clip->move.mins[2] > touch->v.absmax[2]
	    || clip->move.maxs[0] < touch->v.absmin[0]
	    || clip->move.maxs[1] < touch->v.absmin[1]
	    || clip->move.maxs[2] < touch->v.absmin[2])
	    continue;

	if (clip->passedict && clip->passedict->v.size[0]
	    && !touch->v.size[0])
	    continue;		// points never interact

	/* might intersect, so do an exact clip */
	if (trace->allsolid)
	    return clipent;
	if (clip->passedict) {
	    /* don't clip against own missiles */
	    if (PROG_TO_EDICT(touch->v.owner) == clip->passedict)
		continue;
	    /* don't clip against owner */
	    if (PROG_TO_EDICT(clip->passedict->v.owner) == touch)
		continue;
	}

	if ((int)touch->v.flags & FL_MONSTER)
	    clipbounds = &clip->monster;
	else
	    clipbounds = &clip->object;
	SV_ClipToEntity(touch, clip->start, clipbounds->mins, clipbounds->maxs,
			clip->end, &stacktrace);

	if (stacktrace.allsolid || stacktrace.startsolid
	    || stacktrace.fraction < trace->fraction) {
	    clipent = touch;
	    if (trace->startsolid) {
		*trace = stacktrace;
		trace->startsolid = true;
	    } else
		*trace = stacktrace;
	} else if (stacktrace.startsolid)
	    trace->startsolid = true;
    }

    /* recurse down both sides */
    if (node->axis == -1)
	return clipent;

    if (clip->move.maxs[node->axis] > node->dist)
	clipent = SV_ClipToLinks_r(clipent, node->children[0], clip, trace);
    if (clip->move.mins[node->axis] < node->dist)
	clipent = SV_ClipToLinks_r(clipent, node->children[1], clip, trace);

    return clipent;
}

static const edict_t *
SV_ClipToLinks(const areanode_t *node, const moveclip_t *clip, trace_t *trace)
{
    return SV_ClipToLinks_r(NULL, node, clip, trace);
}


/*
==================
SV_MoveBounds
==================
*/
static void
SV_MoveBounds(const bounds_t *object, const vec3_t start, const vec3_t end,
	      bounds_t *move)
{
    int i;

    for (i = 0; i < 3; i++) {
	if (end[i] > start[i]) {
	    move->mins[i] = start[i] + object->mins[i] - 1;
	    move->maxs[i] = end[i] + object->maxs[i] + 1;
	} else {
	    move->mins[i] = end[i] + object->mins[i] - 1;
	    move->maxs[i] = start[i] + object->maxs[i] + 1;
	}
    }
}

/*
==================
SV_TraceMove

If the move was clipped, returns a pointer to the entity that clipped the
move, otherwise NULL.
==================
*/
const edict_t *
SV_TraceMove(const vec3_t start, const vec3_t mins, const vec3_t maxs,
	     const vec3_t end, const movetype_t type, const edict_t *passedict,
	     trace_t *trace)
{
    const edict_t *clipent;
    qboolean clipworld;
    moveclip_t clip;
    int i;

    memset(&clip, 0, sizeof(moveclip_t));

    VectorCopy(start, clip.start);
    VectorCopy(end, clip.end);
    VectorCopy(mins, clip.object.mins);
    VectorCopy(maxs, clip.object.maxs);
    clip.type = type;
    clip.passedict = passedict;

    /* clip to world */
    SV_ClipToEntity(sv.edicts, start, mins, maxs, end, trace);
    clipworld = (trace->fraction < 1 || trace->startsolid);

    if (type == MOVE_MISSILE) {
	for (i = 0; i < 3; i++) {
	    clip.monster.mins[i] = -15;
	    clip.monster.maxs[i] = 15;
	}
    } else {
	VectorCopy(mins, clip.monster.mins);
	VectorCopy(maxs, clip.monster.maxs);
    }

    /* create the bounding box of the entire move */
    SV_MoveBounds(&clip.monster, start, end, &clip.move);

    /* clip to entities */
    clipent = SV_ClipToLinks(sv_areanodes, &clip, trace);
    if (!clipent && clipworld)
	clipent = sv.edicts;

    return clipent;
}
