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
// cl_ents.c -- entity parsing and management

#include "client.h"
#include "console.h"
#include "cvar.h"
#include "mathlib.h"
#include "model.h"
#include "pmove.h"
#include "protocol.h"
#include "quakedef.h"
#include "sys.h"
#include "view.h"

#ifdef GLQUAKE
#include "glquake.h"
#else
#include "d_iface.h"
#endif

// refresh list
int cl_numvisedicts;
entity_t cl_visedicts[MAX_VISEDICTS];

static struct predicted_player {
    int flags;
    qboolean active;
    vec3_t origin;		// predicted origin
} predicted_players[MAX_CLIENTS];

typedef struct visedict_info_s {
    int keynum;
    vec3_t origin;
} visedict_info_t;

/*
 * This containse saved visedicts from the last frame so it
 * can be scanned for old origins of trailing objects
 */
static visedict_info_t saved_visedicts[MAX_VISEDICTS];
static int num_saved_visedicts;

//============================================================

const float dl_colors[4][4] = {
    { 0.2, 0.1, 0.05, 0.7 },	/* FLASH */
    { 0.05, 0.05, 0.3, 0.7 },	/* BLUE */
    { 0.5, 0.05, 0.05, 0.7 },	/* RED */
    { 0.5, 0.05, 0.4, 0.7 }	/* PURPLE */
};

/*
===============
CL_AllocDlight
===============
*/
dlight_t *
CL_AllocDlight(int key)
{
    int i;
    dlight_t *dl;

// first look for an exact key match
    if (key) {
	dl = cl_dlights;
	for (i = 0; i < MAX_DLIGHTS; i++, dl++) {
	    if (dl->key == key) {
		memset(dl, 0, sizeof(*dl));
		dl->color = dl_colors[DLIGHT_FLASH];
		dl->key = key;
		return dl;
	    }
	}
    }
// then look for anything else
    dl = cl_dlights;
    for (i = 0; i < MAX_DLIGHTS; i++, dl++) {
	if (dl->die < cl.time) {
	    memset(dl, 0, sizeof(*dl));
	    dl->color = dl_colors[DLIGHT_FLASH];
	    dl->key = key;
	    return dl;
	}
    }

    dl = &cl_dlights[0];
    memset(dl, 0, sizeof(*dl));
    dl->color = dl_colors[DLIGHT_FLASH];
    dl->key = key;
    return dl;
}

/*
===============
CL_NewDlight
===============
*/
static void
CL_NewDlight(int key, float x, float y, float z, float radius, float time,
	     int color)
{
    dlight_t *dl;

    dl = CL_AllocDlight(key);
    dl->origin[0] = x;
    dl->origin[1] = y;
    dl->origin[2] = z;
    dl->radius = radius;
    dl->die = cl.time + time;
    dl->color = dl_colors[color];
}


/*
===============
CL_DecayLights

===============
*/
void
CL_DecayLights(void)
{
    int i;
    dlight_t *dl;

    dl = cl_dlights;
    for (i = 0; i < MAX_DLIGHTS; i++, dl++) {
	if (dl->die < cl.time || !dl->radius)
	    continue;

	dl->radius -= host_frametime * dl->decay;
	if (dl->radius < 0)
	    dl->radius = 0;
    }
}


/*
=========================================================================

PACKET ENTITY PARSING / LINKING

=========================================================================
*/

/*
==================
CL_ParseDelta

Can go from either a baseline or a previous packet_entity
==================
*/
static void
CL_ParseDelta(entity_state_t *from, entity_state_t *to, int bits)
{
    int i;

    // set everything to the state we are delta'ing from
    *to = *from;

    to->number = bits & 511;
    bits &= ~511;

    if (bits & U_MOREBITS) {	// read in the low order bits
	i = MSG_ReadByte();
	bits |= i;
    }
    to->flags = bits;

    if (bits & U_MODEL)
	to->modelindex = MSG_ReadByte();

    if (bits & U_FRAME)
	to->frame = MSG_ReadByte();

    if (bits & U_COLORMAP)
	to->colormap = MSG_ReadByte();

    if (bits & U_SKIN)
	to->skinnum = MSG_ReadByte();

    if (bits & U_EFFECTS)
	to->effects = MSG_ReadByte();

    if (bits & U_ORIGIN1)
	to->origin[0] = MSG_ReadCoord();

    if (bits & U_ANGLE1)
	to->angles[0] = MSG_ReadAngle();

    if (bits & U_ORIGIN2)
	to->origin[1] = MSG_ReadCoord();

    if (bits & U_ANGLE2)
	to->angles[1] = MSG_ReadAngle();

    if (bits & U_ORIGIN3)
	to->origin[2] = MSG_ReadCoord();

    if (bits & U_ANGLE3)
	to->angles[2] = MSG_ReadAngle();

    if (bits & U_SOLID) {
	// FIXME
    }
}


/*
=================
FlushEntityPacket
=================
*/
static void
FlushEntityPacket(void)
{
    int word;
    entity_state_t olde, newe;

    Con_DPrintf("FlushEntityPacket\n");

    memset(&olde, 0, sizeof(olde));

    cl.validsequence = 0;	// can't render a frame
    cl.frames[cls.netchan.incoming_sequence & UPDATE_MASK].invalid = true;

    // read it all, but ignore it
    while (1) {
	word = (unsigned short)MSG_ReadShort();
	if (msg_badread)
	    Host_EndGame("msg_badread in packetentities");

	if (!word)
	    break;		// done

	CL_ParseDelta(&olde, &newe, word);
    }
}

/*
==================
CL_ParsePacketEntities

An svc_packetentities has just been parsed, deal with the
rest of the data stream.
==================
*/
void
CL_ParsePacketEntities(qboolean delta)
{
    int oldpacket, newpacket;
    packet_entities_t *oldp, *newp, dummy;
    int oldindex, newindex;
    int word, newnum, oldnum;
    qboolean full, old_ok;
    byte from;

    newpacket = cls.netchan.incoming_sequence & UPDATE_MASK;
    newp = &cl.frames[newpacket].packet_entities;
    cl.frames[newpacket].invalid = false;

    if (delta) {
	from = MSG_ReadByte();

	oldpacket = cl.frames[newpacket].delta_sequence;

	if ((from & UPDATE_MASK) != (oldpacket & UPDATE_MASK))
	    Con_DPrintf("WARNING: from mismatch\n");
    } else
	oldpacket = -1;

    full = false;
    if (oldpacket != -1) {
	if (cls.netchan.outgoing_sequence - oldpacket >= UPDATE_BACKUP - 1) {
	    /* we can't use this, it is too old */
	    FlushEntityPacket();
	    return;
	}
	cl.validsequence = cls.netchan.incoming_sequence;
	oldp = &cl.frames[oldpacket & UPDATE_MASK].packet_entities;
    } else {
	/* this is a full update that we can start delta compressing from now */
	oldp = &dummy;
	dummy.num_entities = 0;
	cl.validsequence = cls.netchan.incoming_sequence;
	full = true;
    }

    oldindex = 0;
    newindex = 0;
    newp->num_entities = 0;

    while (1) {
	word = (unsigned short)MSG_ReadShort();
	if (msg_badread)
	    Host_EndGame("msg_badread in packetentities");

	if (!word) {
	    while (oldindex < oldp->num_entities) {
		/* copy all the rest of the entities from the old packet */
		if (newindex >= MAX_PACKET_ENTITIES)
		    Host_EndGame("%s: newindex == MAX_PACKET_ENTITIES",
				 __func__);
		newp->entities[newindex] = oldp->entities[oldindex];
		newindex++;
		oldindex++;
	    }
	    break;
	}

	newnum = word & 511;
	old_ok = oldindex < oldp->num_entities;
	oldnum = old_ok ? oldp->entities[oldindex].number : 9999;

	while (newnum > oldnum) {
	    if (full) {
		Con_Printf("WARNING: oldcopy on full update");
		FlushEntityPacket();
		return;
	    }
	    /* copy one of the old entities over to the new packet unchanged */
	    if (newindex >= MAX_PACKET_ENTITIES)
		Host_EndGame("%s: newindex == MAX_PACKET_ENTITIES", __func__);
	    newp->entities[newindex] = oldp->entities[oldindex];
	    newindex++;
	    oldindex++;
	    old_ok = oldindex < oldp->num_entities;
	    oldnum = old_ok ? oldp->entities[oldindex].number : 9999;
	}

	/* new from baseline */
	if (newnum < oldnum) {
	    if (word & U_REMOVE) {
		if (full) {
		    cl.validsequence = 0;
		    Con_Printf("WARNING: U_REMOVE on full update\n");
		    FlushEntityPacket();
		    return;
		}
		continue;
	    }
	    if (newindex >= MAX_PACKET_ENTITIES)
		Host_EndGame("%s: newindex == MAX_PACKET_ENTITIES", __func__);
	    CL_ParseDelta(&cl_baselines[newnum], &newp->entities[newindex],
			  word);
	    newindex++;
	    continue;
	}

	/* delta from previous */
	if (newnum == oldnum) {
	    if (full) {
		cl.validsequence = 0;
		Con_Printf("WARNING: delta on full update");
	    }
	    if (word & U_REMOVE) {
		oldindex++;
		continue;
	    }
	    CL_ParseDelta(&oldp->entities[oldindex],
			  &newp->entities[newindex], word);
	    newindex++;
	    oldindex++;
	}

    }
    newp->num_entities = newindex;
}


/*
===============
CL_LinkPacketEntities

===============
*/
static void
CL_LinkPacketEntities(void)
{
    entity_t *ent;
    packet_entities_t *pack;
    entity_state_t *s1, *s2;
    float f;
    model_t *model;
    vec3_t old_origin;
    float autorotate;
    int i;
    int pnum;
    dlight_t *dl;

    pack =
	&cl.frames[cls.netchan.incoming_sequence & UPDATE_MASK].
	packet_entities;

    autorotate = anglemod(100 * cl.time);

    f = 0;			// FIXME: no interpolation right now

    for (pnum = 0; pnum < pack->num_entities; pnum++) {
	s1 = &pack->entities[pnum];
	s2 = s1;		// FIXME: no interpolation right now

	// spawn light flashes, even ones coming from invisible objects
	if ((s1->effects & (EF_BLUE | EF_RED)) == (EF_BLUE | EF_RED))
	    CL_NewDlight(s1->number, s1->origin[0], s1->origin[1],
			 s1->origin[2], 200 + (rand() & 31), 0.1, DLIGHT_PURPLE);
	else if (s1->effects & EF_BLUE)
	    CL_NewDlight(s1->number, s1->origin[0], s1->origin[1],
			 s1->origin[2], 200 + (rand() & 31), 0.1, DLIGHT_BLUE);
	else if (s1->effects & EF_RED)
	    CL_NewDlight(s1->number, s1->origin[0], s1->origin[1],
			 s1->origin[2], 200 + (rand() & 31), 0.1, DLIGHT_RED);
	else if (s1->effects & EF_BRIGHTLIGHT)
	    CL_NewDlight(s1->number, s1->origin[0], s1->origin[1],
			 s1->origin[2] + 16, 400 + (rand() & 31), 0.1, DLIGHT_FLASH);
	else if (s1->effects & EF_DIMLIGHT)
	    CL_NewDlight(s1->number, s1->origin[0], s1->origin[1],
			 s1->origin[2], 200 + (rand() & 31), 0.1, DLIGHT_FLASH);

	// if set to invisible, skip
	if (!s1->modelindex)
	    continue;

	// create a new entity
	if (cl_numvisedicts == MAX_VISEDICTS)
	    break;		// object list is full

	ent = &cl_visedicts[cl_numvisedicts];
	cl_numvisedicts++;

	ent->keynum = s1->number;
	ent->model = model = cl.model_precache[s1->modelindex];

	// set colormap
	if (s1->colormap && (s1->colormap < MAX_CLIENTS)
	    && !strcmp(ent->model->name, "progs/player.mdl")) {
	    ent->colormap = cl.players[s1->colormap - 1].translations;
	    ent->scoreboard = &cl.players[s1->colormap - 1];
	} else {
	    ent->colormap = vid.colormap;
	    ent->scoreboard = NULL;
	}

	// set skin
	ent->skinnum = s1->skinnum;

	// set frame
	ent->frame = s1->frame;

	// rotate binary objects locally
	if (model->flags & EF_ROTATE) {
	    ent->angles[0] = 0;
	    ent->angles[1] = autorotate;
	    ent->angles[2] = 0;
	} else {
	    float a1, a2;

	    for (i = 0; i < 3; i++) {
		a1 = s1->angles[i];
		a2 = s2->angles[i];
		if (a1 - a2 >= 180)
		    a1 -= 360;
		if (a1 - a2 < -180)
		    a1 += 360;
		ent->angles[i] = a2 + f * (a1 - a2);
	    }
	}

	// calculate origin
	for (i = 0; i < 3; i++)
	    ent->origin[i] = s2->origin[i] +
		f * (s1->origin[i] - s2->origin[i]);

	// add automatic particle trails
	if (!model->flags)
	    continue;

	// scan the old entity display list for a matching
	for (i = 0; i < num_saved_visedicts; i++) {
	    if (saved_visedicts[i].keynum == ent->keynum) {
		VectorCopy(saved_visedicts[i].origin, old_origin);
		break;
	    }
	}
	if (i == num_saved_visedicts)
	    continue;		// not in last message

	for (i = 0; i < 3; i++)
	    if (abs(old_origin[i] - ent->origin[i]) > 128) {	// no trail if too far
		VectorCopy(ent->origin, old_origin);
		break;
	    }
	if (model->flags & EF_ROCKET) {
	    R_RocketTrail(old_origin, ent->origin, 0);
	    dl = CL_AllocDlight(s1->number);
	    VectorCopy(ent->origin, dl->origin);
	    dl->radius = 200;
	    dl->die = cl.time + 0.1;
	} else if (model->flags & EF_GRENADE)
	    R_RocketTrail(old_origin, ent->origin, 1);
	else if (model->flags & EF_GIB)
	    R_RocketTrail(old_origin, ent->origin, 2);
	else if (model->flags & EF_ZOMGIB)
	    R_RocketTrail(old_origin, ent->origin, 4);
	else if (model->flags & EF_TRACER)
	    R_RocketTrail(old_origin, ent->origin, 3);
	else if (model->flags & EF_TRACER2)
	    R_RocketTrail(old_origin, ent->origin, 5);
	else if (model->flags & EF_TRACER3)
	    R_RocketTrail(old_origin, ent->origin, 6);
    }
}


/*
=========================================================================

PROJECTILE PARSING / LINKING

=========================================================================
*/

typedef struct {
    int modelindex;
    vec3_t origin;
    vec3_t angles;
} projectile_t;

#define	MAX_PROJECTILES	32
static projectile_t cl_projectiles[MAX_PROJECTILES];
static int cl_num_projectiles;

void
CL_ClearProjectiles(void)
{
    cl_num_projectiles = 0;
}

/*
=====================
CL_ParseProjectiles

Nails are passed as efficient temporary entities
=====================
*/
void
CL_ParseProjectiles(void)
{
    int i, c, j;
    byte bits[6];
    projectile_t *pr;

    c = MSG_ReadByte();
    for (i = 0; i < c; i++) {
	for (j = 0; j < 6; j++)
	    bits[j] = MSG_ReadByte();

	if (cl_num_projectiles == MAX_PROJECTILES)
	    continue;

	pr = &cl_projectiles[cl_num_projectiles];
	cl_num_projectiles++;

	pr->modelindex = cl_spikeindex;
	pr->origin[0] = ((bits[0] + ((bits[1] & 15) << 8)) << 1) - 4096;
	pr->origin[1] = (((bits[1] >> 4) + (bits[2] << 4)) << 1) - 4096;
	pr->origin[2] = ((bits[3] + ((bits[4] & 15) << 8)) << 1) - 4096;
	pr->angles[0] = 360 * (bits[4] >> 4) / 16;
	pr->angles[1] = 360 * bits[5] / 256;
    }
}

/*
=============
CL_LinkProjectiles

=============
*/
static void
CL_LinkProjectiles(void)
{
    int i;
    projectile_t *pr;
    entity_t *ent;

    for (i = 0, pr = cl_projectiles; i < cl_num_projectiles; i++, pr++) {
	// grab an entity to fill in
	if (cl_numvisedicts == MAX_VISEDICTS)
	    break;		// object list is full
	ent = &cl_visedicts[cl_numvisedicts];
	cl_numvisedicts++;
	ent->keynum = 0;

	if (pr->modelindex < 1)
	    continue;
	ent->model = cl.model_precache[pr->modelindex];
	ent->skinnum = 0;
	ent->frame = 0;
	ent->colormap = vid.colormap;
	ent->scoreboard = NULL;
	VectorCopy(pr->origin, ent->origin);
	VectorCopy(pr->angles, ent->angles);
    }
}

//========================================

/*
===================
CL_ParsePlayerinfo
===================
*/
void
CL_ParsePlayerinfo(void)
{
    int msec;
    int flags;
    player_state_t *state;
    int num;
    int i;

    num = MSG_ReadByte();
    if (num > MAX_CLIENTS)
	Sys_Error("CL_ParsePlayerinfo: bad num");

    state = &cl.frames[parsecountmod].playerstate[num];
    flags = state->flags = MSG_ReadShort();

    state->messagenum = cl.parsecount;
    state->origin[0] = MSG_ReadCoord();
    state->origin[1] = MSG_ReadCoord();
    state->origin[2] = MSG_ReadCoord();

    state->frame = MSG_ReadByte();

    // the other player's last move was likely some time
    // before the packet was sent out, so accurately track
    // the exact time it was valid at
    if (flags & PF_MSEC) {
	msec = MSG_ReadByte();
	state->state_time = parsecounttime - msec * 0.001;
    } else
	state->state_time = parsecounttime;

    if (flags & PF_COMMAND)
	MSG_ReadDeltaUsercmd(&nullcmd, &state->command);

    for (i = 0; i < 3; i++) {
	if (flags & (PF_VELOCITY1 << i))
	    state->velocity[i] = MSG_ReadShort();
	else
	    state->velocity[i] = 0;
    }
    if (flags & PF_MODEL)
	state->modelindex = MSG_ReadByte();
    else
	state->modelindex = cl_playerindex;

    if (flags & PF_SKINNUM)
	state->skinnum = MSG_ReadByte();
    else
	state->skinnum = 0;

    if (flags & PF_EFFECTS)
	state->effects = MSG_ReadByte();
    else
	state->effects = 0;

    if (flags & PF_WEAPONFRAME)
	state->weaponframe = MSG_ReadByte();
    else
	state->weaponframe = 0;

    VectorCopy(state->command.angles, state->viewangles);
}


/*
================
CL_AddFlagModels

Called when the CTF flags are set
================
*/
static void
CL_AddFlagModels(entity_t *ent, int team)
{
    int i;
    float f;
    vec3_t v_forward, v_right, v_up;
    entity_t *newent;

    if (cl_flagindex == -1)
	return;

    f = 14;
    if (ent->frame >= 29 && ent->frame <= 40) {
	if (ent->frame >= 29 && ent->frame <= 34) {	//axpain
	    if (ent->frame == 29)
		f = f + 2;
	    else if (ent->frame == 30)
		f = f + 8;
	    else if (ent->frame == 31)
		f = f + 12;
	    else if (ent->frame == 32)
		f = f + 11;
	    else if (ent->frame == 33)
		f = f + 10;
	    else if (ent->frame == 34)
		f = f + 4;
	} else if (ent->frame >= 35 && ent->frame <= 40) {	// pain
	    if (ent->frame == 35)
		f = f + 2;
	    else if (ent->frame == 36)
		f = f + 10;
	    else if (ent->frame == 37)
		f = f + 10;
	    else if (ent->frame == 38)
		f = f + 8;
	    else if (ent->frame == 39)
		f = f + 4;
	    else if (ent->frame == 40)
		f = f + 2;
	}
    } else if (ent->frame >= 103 && ent->frame <= 118) {
	if (ent->frame >= 103 && ent->frame <= 104)
	    f = f + 6;		//nailattack
	else if (ent->frame >= 105 && ent->frame <= 106)
	    f = f + 6;		//light
	else if (ent->frame >= 107 && ent->frame <= 112)
	    f = f + 7;		//rocketattack
	else if (ent->frame >= 112 && ent->frame <= 118)
	    f = f + 7;		//shotattack
    }

    newent = CL_NewTempEntity();
    newent->model = cl.model_precache[cl_flagindex];
    newent->skinnum = team;

    AngleVectors(ent->angles, v_forward, v_right, v_up);
    v_forward[2] = -v_forward[2];	// reverse z component
    for (i = 0; i < 3; i++)
	newent->origin[i] =
	    ent->origin[i] - f * v_forward[i] + 22 * v_right[i];
    newent->origin[2] -= 16;

    VectorCopy(ent->angles, newent->angles);
    newent->angles[2] -= 45;
}

/*
=============
CL_LinkPlayers

Create visible entities in the correct position
for all current players
=============
*/
static void
CL_LinkPlayers(physent_stack_t *pestack)
{
    int playernum;
    player_info_t *info;
    player_state_t *state;
    player_state_t exact;
    double playertime;
    entity_t *ent;
    int msec;
    frame_t *frame;
    int oldphysent;

    playertime = realtime - cls.latency + 0.02;
    if (playertime > realtime)
	playertime = realtime;

    frame = &cl.frames[cl.parsecount & UPDATE_MASK];

    info = cl.players;
    state = frame->playerstate;
    for (playernum = 0; playernum < MAX_CLIENTS; playernum++, info++, state++) {
	if (state->messagenum != cl.parsecount)
	    continue;		// not present this frame

	// spawn light flashes, even ones coming from invisible objects
#ifdef GLQUAKE
	if (!gl_flashblend.value || playernum != cl.playernum) {
#endif
	    if ((state->effects & (EF_BLUE | EF_RED)) == (EF_BLUE | EF_RED))
		CL_NewDlight(playernum, state->origin[0], state->origin[1],
			     state->origin[2], 200 + (rand() & 31), 0.1, DLIGHT_PURPLE);
	    else if (state->effects & EF_BLUE)
		CL_NewDlight(playernum, state->origin[0], state->origin[1],
			     state->origin[2], 200 + (rand() & 31), 0.1, DLIGHT_BLUE);
	    else if (state->effects & EF_RED)
		CL_NewDlight(playernum, state->origin[0], state->origin[1],
			     state->origin[2], 200 + (rand() & 31), 0.1, DLIGHT_RED);
	    else if (state->effects & EF_BRIGHTLIGHT)
		CL_NewDlight(playernum, state->origin[0], state->origin[1],
			     state->origin[2] + 16, 400 + (rand() & 31),
			     0.1, DLIGHT_FLASH);
	    else if (state->effects & EF_DIMLIGHT)
		CL_NewDlight(playernum, state->origin[0], state->origin[1],
			     state->origin[2], 200 + (rand() & 31), 0.1, DLIGHT_FLASH);
#ifdef GLQUAKE
	}
#endif

	// the player object never gets added
	if (playernum == cl.playernum)
	    continue;

	if (!state->modelindex)
	    continue;

	if (!Cam_DrawPlayer(playernum))
	    continue;

	// grab an entity to fill in
	if (cl_numvisedicts == MAX_VISEDICTS)
	    break;		// object list is full
	ent = &cl_visedicts[cl_numvisedicts];
	cl_numvisedicts++;
	ent->keynum = 0;

	ent->model = cl.model_precache[state->modelindex];
	ent->skinnum = state->skinnum;
	ent->frame = state->frame;
	ent->colormap = info->translations;
	if (state->modelindex == cl_playerindex)
	    ent->scoreboard = info;	// use custom skin
	else
	    ent->scoreboard = NULL;

	//
	// angles
	//
	ent->angles[PITCH] = -state->viewangles[PITCH] / 3;
	ent->angles[YAW] = state->viewangles[YAW];
	ent->angles[ROLL] = 0;
	ent->angles[ROLL] = V_CalcRoll(ent->angles, state->velocity) * 4;

	// only predict half the move to minimize overruns
	msec = 500 * (playertime - state->state_time);
	if (msec <= 0 || (!cl_predict_players.value && !cl_predict_players2.value)) {
	    VectorCopy(state->origin, ent->origin);
//Con_DPrintf ("nopredict\n");
	} else {
	    // predict players movement
	    if (msec > 255)
		msec = 255;
	    state->command.msec = msec;
//Con_DPrintf ("predict: %i\n", msec);

	    oldphysent = pestack->numphysent;
	    CL_SetSolidPlayers(pestack, playernum);
	    CL_PredictUsercmd(state, &exact, &state->command, pestack, false);
	    pestack->numphysent = oldphysent;
	    VectorCopy(exact.origin, ent->origin);
	}

	if (state->effects & EF_FLAG1)
	    CL_AddFlagModels(ent, 0);
	else if (state->effects & EF_FLAG2)
	    CL_AddFlagModels(ent, 1);

    }
}

//======================================================================

/*
===============
CL_SetSolid

Builds all the pmove physents for the current frame
===============
*/
void
CL_SetSolidEntities(physent_stack_t *pestack)
{
    const packet_entities_t *pak;
    const frame_t *frame;
    physent_t *physent;
    int i;

    if (!cl.worldmodel) {
	/* FIXME - Shouldn't be getting called without a world? */
	pestack->numphysent = 0;
	return;
    }

    physent = pestack->physents;
    physent->brushmodel = ConstBrushModel(&cl.worldmodel->model);
    VectorCopy(vec3_origin, physent->origin);
    physent++;

    frame = &cl.frames[parsecountmod];
    pak = &frame->packet_entities;

    for (i = 0; i < pak->num_entities; i++) {
	const entity_state_t *state = &pak->entities[i];
	const brushmodel_t *brushmodel;
	const model_t *model;

	if (!state->modelindex)
	    continue;

	model = cl.model_precache[state->modelindex];
	if (!model || model->type != mod_brush)
	    continue;

	brushmodel = ConstBrushModel(model);
	if (brushmodel->hulls[1].firstclipnode) {
	    physent->brushmodel = brushmodel;
	    VectorCopy(state->origin, physent->origin);
	    physent++;
	}
    }
    pestack->numphysent = physent - pestack->physents;
}

/*
===
Calculate the new position of players, without other player clipping

We do this to set up real player prediction.
Players are predicted twice, first without clipping other players,
then with clipping against them.
This sets up the first phase.
===
*/
void
CL_SetUpPlayerPrediction(const physent_stack_t *pestack, qboolean dopred)
{
    int j;
    player_state_t *state;
    player_state_t exact;
    double playertime;
    int msec;
    frame_t *frame;
    struct predicted_player *pplayer;

    playertime = realtime - cls.latency + 0.02;
    if (playertime > realtime)
	playertime = realtime;

    frame = &cl.frames[cl.parsecount & UPDATE_MASK];

    for (j = 0, pplayer = predicted_players, state = frame->playerstate;
	 j < MAX_CLIENTS; j++, pplayer++, state++) {

	pplayer->active = false;

	if (state->messagenum != cl.parsecount)
	    continue;		// not present this frame

	if (!state->modelindex)
	    continue;

	pplayer->active = true;
	pplayer->flags = state->flags;

	// note that the local player is special, since he moves locally
	// we use his last predicted postition
	if (j == cl.playernum) {
	    VectorCopy(cl.
		       frames[cls.netchan.outgoing_sequence & UPDATE_MASK].
		       playerstate[cl.playernum].origin, pplayer->origin);
	} else {
	    // only predict half the move to minimize overruns
	    msec = 500 * (playertime - state->state_time);
	    if (msec <= 0 ||
		(!cl_predict_players.value && !cl_predict_players2.value) ||
		!dopred) {
		VectorCopy(state->origin, pplayer->origin);
		//Con_DPrintf ("nopredict\n");
	    } else {
		// predict players movement
		if (msec > 255)
		    msec = 255;
		state->command.msec = msec;
		//Con_DPrintf ("predict: %i\n", msec);

		CL_PredictUsercmd(state, &exact, &state->command, pestack, false);
		VectorCopy(exact.origin, pplayer->origin);
	    }
	}
    }
}

/*
===============
CL_SetSolid

Builds all the pmove physents for the current frame
Note that CL_SetUpPlayerPrediction() must be called first!
pmove must be setup with world and solid entity hulls before calling
(via CL_PredictMove)
===============
*/
void
CL_SetSolidPlayers(physent_stack_t *pestack, int playernum)
{
    struct predicted_player *pplayer;
    physent_t *physent;
    int i;

    if (!cl_solid_players.value)
	return;

    physent = pestack->physents + pestack->numphysent;
    for (i = 0, pplayer = predicted_players; i < MAX_CLIENTS; i++, pplayer++) {
	/* check if active this frame */
	if (!pplayer->active)
	    continue;

	/* the player object never gets added */
	if (i == playernum)
	    continue;

	/* dead players aren't solid */
	if (pplayer->flags & PF_DEAD)
	    continue;

	physent->brushmodel = NULL;
	VectorCopy(pplayer->origin, physent->origin);
	VectorCopy(player_mins, physent->mins);
	VectorCopy(player_maxs, physent->maxs);
	physent++;
    }
    pestack->numphysent = physent - pestack->physents;
}


/*
===============
CL_EmitEntities

Builds the visedicts array for cl.time

Made up of: clients, packet_entities, nails, and tents
===============
*/
void
CL_EmitEntities(physent_stack_t *pestack)
{
    int i;

    if (cls.state != ca_active)
	return;
    if (!cl.validsequence)
	return;

    for (i = 0; i < cl_numvisedicts; i++) {
	saved_visedicts[i].keynum = cl_visedicts[i].keynum;
	VectorCopy(cl_visedicts[i].origin, saved_visedicts[i].origin);
    }
    num_saved_visedicts = cl_numvisedicts;
    cl_numvisedicts = 0;

    CL_LinkPlayers(pestack);
    CL_LinkPacketEntities();
    CL_LinkProjectiles();
    CL_UpdateTEnts();
}
