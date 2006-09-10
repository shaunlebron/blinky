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
// sv_main.c -- server main program

#include "bspfile.h"
#include "cmd.h"
#include "console.h"
#include "cvar.h"
#include "host.h"
#include "model.h"
#include "net.h"
#include "protocol.h"
#include "quakedef.h"
#include "screen.h"
#include "server.h"
#include "sound.h"
#include "sys.h"
#include "world.h"

server_t sv;
server_static_t svs;

/* inline model names for precache */
#define MODSTRLEN (sizeof("*" stringify(MAX_MODELS)) / sizeof(char))
static char localmodels[MAX_MODELS][MODSTRLEN];

//============================================================================

typedef struct {
    int version;
    const char *name;
    const char *description;
} sv_protocol_t;

#define PROT(v, n, d) { .version = v, .name = n, .description = d }
static sv_protocol_t sv_protocols[] = {
    PROT(PROTOCOL_VERSION_NQ,   "nq",   "Standard NetQuake protocol"),
    PROT(PROTOCOL_VERSION_FITZ, "fitz", "FitzQuake protocol"),
    PROT(PROTOCOL_VERSION_BJP,  "bjp",  "BJP protocol (v1)"),
    PROT(PROTOCOL_VERSION_BJP2, "bjp2", "BJP protocol (v2)"),
    PROT(PROTOCOL_VERSION_BJP3, "bjp3", "BJP protocol (v3)"),
};

static int sv_protocol = PROTOCOL_VERSION_NQ;

static void
SV_Protocol_f(void)
{
    const char *name = "unknown";
    int i;

    if (Cmd_Argc() == 1) {
	for (i = 0; i < ARRAY_SIZE(sv_protocols); i++) {
	    if (sv_protocols[i].version == sv_protocol) {
		name = sv_protocols[i].name;
		break;
	    }
	}
	Con_Printf("sv_protocol is %d (%s)\n"
		   "    use 'sv_protocol list' to list available protocols\n",
		   sv_protocol, name);
    } else if (Cmd_Argc() == 2) {
	if (!strcasecmp(Cmd_Argv(1), "list")) {
	    Con_Printf("Version  Name  Description\n"
		       "-------  ----  -----------\n");
	    for (i = 0; i < ARRAY_SIZE(sv_protocols); i++) {
		Con_Printf("%7d  %-4s  %s\n", sv_protocols[i].version,
			   sv_protocols[i].name, sv_protocols[i].description);
	    }
	} else {
	    int v = Q_atoi(Cmd_Argv(1));
	    for (i = 0; i < ARRAY_SIZE(sv_protocols); i++) {
		if (sv_protocols[i].version == v)
		    break;
		if (!strcasecmp(sv_protocols[i].name, Cmd_Argv(1)))
		    break;
	    }
	    if (i == ARRAY_SIZE(sv_protocols)) {
		Con_Printf("sv_protocol: unknown protocol version\n");
		return;
	    }
	    if (sv_protocol != sv_protocols[i].version) {
		sv_protocol = sv_protocols[i].version;
		if (sv.active)
		    Con_Printf("change will not take effect until the next "
			       "level load.\n");
	    }
	}
    } else {
	Con_Printf("Usage: sv_protocol [<version> | <name> | 'list']\n");
    }
}

static struct stree_root *
SV_Protocol_Arg_f(const char *arg)
{
    int i, arg_len;
    char digits[10];
    struct stree_root *root;

    root = Z_Malloc(sizeof(struct stree_root));
    if (root) {
	*root = STREE_ROOT;
	STree_AllocInit();
	arg_len = arg ? strlen(arg) : 0;
	for (i = 0; i < ARRAY_SIZE(sv_protocols); i++) {
	    if (!arg || !strncasecmp(sv_protocols[i].name, arg, arg_len))
		STree_InsertAlloc(root, sv_protocols[i].name, false);
	    snprintf(digits, sizeof(digits), "%d", sv_protocols[i].version);
	    if (arg_len && !strncmp(digits, arg, arg_len))
		STree_InsertAlloc(root, digits, true);
	}
    }
    return root;
}

/*
===============
SV_Init
===============
*/
void
SV_Init(void)
{
    int i;

    Cvar_RegisterVariable(&sv_maxvelocity);
    Cvar_RegisterVariable(&sv_gravity);
    Cvar_RegisterVariable(&sv_friction);
    Cvar_RegisterVariable(&sv_edgefriction);
    Cvar_RegisterVariable(&sv_stopspeed);
    Cvar_RegisterVariable(&sv_maxspeed);
    Cvar_RegisterVariable(&sv_accelerate);
    Cvar_RegisterVariable(&sv_idealpitchscale);
    Cvar_RegisterVariable(&sv_aim);
    Cvar_RegisterVariable(&sv_nostep);

    Cmd_AddCommand("sv_protocol", SV_Protocol_f);
    Cmd_SetCompletion("sv_protocol", SV_Protocol_Arg_f);

    for (i = 0; i < MAX_MODELS; i++)
	sprintf(localmodels[i], "*%i", i);
}

/*
=============================================================================

EVENT MESSAGES

=============================================================================
*/

/*
==================
SV_StartParticle

Make sure the event gets sent to all clients
==================
*/
void
SV_StartParticle(vec3_t org, vec3_t dir, int color, int count)
{
    int i, v;

    /*
     * Drop silently if there is no room
     * FIXME - does not take into account MTU...
     */
    if (sv.datagram.cursize > MAX_DATAGRAM - 12)
	return;

    MSG_WriteByte(&sv.datagram, svc_particle);
    MSG_WriteCoord(&sv.datagram, org[0]);
    MSG_WriteCoord(&sv.datagram, org[1]);
    MSG_WriteCoord(&sv.datagram, org[2]);
    for (i = 0; i < 3; i++) {
	v = dir[i] * 16;
	if (v > 127)
	    v = 127;
	else if (v < -128)
	    v = -128;
	MSG_WriteChar(&sv.datagram, v);
    }
    MSG_WriteByte(&sv.datagram, count);
    MSG_WriteByte(&sv.datagram, color);
}

static void
SV_WriteSoundNum(sizebuf_t *sb, int c, unsigned int bits)
{
    switch (sv.protocol) {
    case PROTOCOL_VERSION_NQ:
    case PROTOCOL_VERSION_BJP:
	MSG_WriteByte(sb, c);
	break;
    case PROTOCOL_VERSION_BJP2:
    case PROTOCOL_VERSION_BJP3:
	MSG_WriteShort(sb, c);
	break;
    case PROTOCOL_VERSION_FITZ:
	if (bits & SND_FITZ_LARGESOUND)
	    MSG_WriteShort(sb, c);
	else
	    MSG_WriteByte(sb, c);
	break;
    default:
	Host_Error("%s: Unknown protocol version (%d)\n", __func__,
		   sv.protocol);
    }
}

/*
==================
SV_StartSound

Each entity can have eight independant sound sources, like voice,
weapon, feet, etc.

Channel 0 is an auto-allocate channel, the others override anything
already running on that entity/channel pair.

An attenuation of 0 will play full volume everywhere in the level.
Larger attenuations will drop off.  (max 4 attenuation)

==================
*/
void
SV_StartSound(edict_t *entity, int channel, const char *sample, int volume,
	      float attenuation)
{
    int sound_num;
    int field_mask;
    int i;
    int ent;
    float coord;

    if (volume < 0 || volume > 255)
	Sys_Error("%s: volume = %i", __func__, volume);

    if (attenuation < 0 || attenuation > 4)
	Sys_Error("%s: attenuation = %f", __func__, attenuation);

    if (channel < 0 || channel > 7)
	Sys_Error("%s: channel = %i", __func__, channel);

    /*
     * Drop silently if there is no room
     * FIXME - does not take into account MTU...
     */
    if (sv.datagram.cursize > MAX_DATAGRAM - 14)
	return;

// find precache number for sound
    for (sound_num = 1; sound_num < MAX_SOUNDS
	 && sv.sound_precache[sound_num]; sound_num++)
	if (!strcmp(sample, sv.sound_precache[sound_num]))
	    break;

    if (sound_num == MAX_SOUNDS || !sv.sound_precache[sound_num]) {
	Con_Printf("%s: %s not precacheed\n", __func__, sample);
	return;
    }

    ent = NUM_FOR_EDICT(entity);

    field_mask = 0;
    if (volume != DEFAULT_SOUND_PACKET_VOLUME)
	field_mask |= SND_VOLUME;
    if (attenuation != DEFAULT_SOUND_PACKET_ATTENUATION)
	field_mask |= SND_ATTENUATION;

    if (ent >= 8192) {
	if (sv.protocol != PROTOCOL_VERSION_FITZ)
	    return; /* currently no other protocols can encode these */
	field_mask |= SND_FITZ_LARGEENTITY;
    }
    if (sound_num >= 256 || channel >= 8) {
	if (sv.protocol != PROTOCOL_VERSION_FITZ)
	    return; /* currently no other protocols can encode these */
	field_mask |= SND_FITZ_LARGESOUND;
    }

// directed messages go only to the entity the are targeted on
    MSG_WriteByte(&sv.datagram, svc_sound);
    MSG_WriteByte(&sv.datagram, field_mask);
    if (field_mask & SND_VOLUME)
	MSG_WriteByte(&sv.datagram, volume);
    if (field_mask & SND_ATTENUATION)
	MSG_WriteByte(&sv.datagram, attenuation * 64);
    if (field_mask & SND_FITZ_LARGEENTITY) {
	MSG_WriteShort(&sv.datagram, ent);
	MSG_WriteByte(&sv.datagram, channel);
    } else {
	MSG_WriteShort(&sv.datagram, (ent << 3) | channel);
    }
    SV_WriteSoundNum(&sv.datagram, sound_num, field_mask);
    for (i = 0; i < 3; i++) {
	coord = entity->v.origin[i];
	coord += 0.5 * (entity->v.mins[i] + entity->v.maxs[i]);
	MSG_WriteCoord(&sv.datagram, coord);
    }
}

/*
==============================================================================

CLIENT SPAWNING

==============================================================================
*/

/*
================
SV_SendServerinfo

Sends the first message from the server to a connected client.
This will be sent on the initial connection and upon each server load.
================
*/
void
SV_SendServerinfo(client_t *client)
{
    const char **s;

    MSG_WriteByte(&client->message, svc_print);
    MSG_WriteStringf(&client->message,
		     "%c\nVERSION TyrQuake-%s SERVER (%i CRC)",
		     2, stringify(TYR_VERSION), pr_crc);

    MSG_WriteByte(&client->message, svc_serverinfo);
    MSG_WriteLong(&client->message, sv.protocol);
    MSG_WriteByte(&client->message, svs.maxclients);

    if (!coop.value && deathmatch.value)
	MSG_WriteByte(&client->message, GAME_DEATHMATCH);
    else
	MSG_WriteByte(&client->message, GAME_COOP);

    MSG_WriteString(&client->message, PR_GetString(sv.edicts->v.message));

    for (s = sv.model_precache + 1; *s; s++)
	MSG_WriteString(&client->message, *s);
    MSG_WriteByte(&client->message, 0);

    for (s = sv.sound_precache + 1; *s; s++)
	MSG_WriteString(&client->message, *s);
    MSG_WriteByte(&client->message, 0);

// send music
    MSG_WriteByte(&client->message, svc_cdtrack);
    MSG_WriteByte(&client->message, sv.edicts->v.sounds);
    MSG_WriteByte(&client->message, sv.edicts->v.sounds);

// set view
    MSG_WriteByte(&client->message, svc_setview);
    MSG_WriteShort(&client->message, NUM_FOR_EDICT(client->edict));

    MSG_WriteByte(&client->message, svc_signonnum);
    MSG_WriteByte(&client->message, 1);

    client->sendsignon = true;
    client->spawned = false;	// need prespawn, spawn, etc
}

/*
================
SV_ConnectClient

Initializes a client_t for a new net connection.  This will only be called
once for a player each game, not once for each level change.
================
*/
void
SV_ConnectClient(int clientnum)
{
    edict_t *ent;
    client_t *client;
    int edictnum;
    struct qsocket_s *netconnection;
    int i;
    float spawn_parms[NUM_SPAWN_PARMS];

    client = svs.clients + clientnum;

    Con_DPrintf("Client %s connected\n", client->netconnection->address);

    edictnum = clientnum + 1;

    ent = EDICT_NUM(edictnum);

// set up the client_t
    netconnection = client->netconnection;

    if (sv.loadgame)
	memcpy(spawn_parms, client->spawn_parms, sizeof(spawn_parms));
    memset(client, 0, sizeof(*client));
    client->netconnection = netconnection;

    strcpy(client->name, "unconnected");
    client->active = true;
    client->spawned = false;
    client->edict = ent;
    client->message.data = client->msgbuf;
    client->message.maxsize = sizeof(client->msgbuf);
    client->message.allowoverflow = true;	// we can catch it

    if (sv.loadgame) {
	memcpy(client->spawn_parms, spawn_parms, sizeof(spawn_parms));
    } else {
	// call the progs to get default spawn parms for the new client
	PR_ExecuteProgram(pr_global_struct->SetNewParms);
	for (i = 0; i < NUM_SPAWN_PARMS; i++)
	    client->spawn_parms[i] = (&pr_global_struct->parm1)[i];
    }

    SV_SendServerinfo(client);
}


/*
===================
SV_CheckForNewClients

===================
*/
void
SV_CheckForNewClients(void)
{
    struct qsocket_s *sock;
    int i;

//
// check for new connections
//
    while (1) {
	sock = NET_CheckNewConnections();
	if (!sock)
	    break;

	//
	// init a new client structure
	//
	for (i = 0; i < svs.maxclients; i++)
	    if (!svs.clients[i].active)
		break;
	if (i == svs.maxclients)
	    Sys_Error("%s: no free clients", __func__);

	svs.clients[i].netconnection = sock;
	SV_ConnectClient(i);

	net_activeconnections++;
    }
}



/*
===============================================================================

FRAME UPDATES

===============================================================================
*/

/*
==================
SV_ClearDatagram

==================
*/
void
SV_ClearDatagram(void)
{
    SZ_Clear(&sv.datagram);
}

//=============================================================================

void
SV_WriteModelIndex(sizebuf_t *sb, int c, unsigned int bits)
{
    switch (sv.protocol) {
    case PROTOCOL_VERSION_NQ:
	MSG_WriteByte(sb, c);
	break;
    case PROTOCOL_VERSION_BJP:
    case PROTOCOL_VERSION_BJP2:
    case PROTOCOL_VERSION_BJP3:
	MSG_WriteShort(sb, c);
	break;
    case PROTOCOL_VERSION_FITZ:
	if (bits & B_FITZ_LARGEMODEL)
	    MSG_WriteShort(sb, c);
	else
	    MSG_WriteByte(sb, c);
	break;
    default:
	Host_Error("%s: Unknown protocol version (%d)\n", __func__,
		   sv.protocol);
    }
}

/*
=============
SV_WriteEntitiesToClient

=============
*/
void
SV_WriteEntitiesToClient(edict_t *clent, sizebuf_t *msg)
{
    int e, i;
    int bits;
    const leafbits_t *pvs;
    vec3_t org;
    float miss;
    edict_t *ent;

// find the client's PVS
    VectorAdd(clent->v.origin, clent->v.view_ofs, org);
    pvs = Mod_FatPVS(sv.worldmodel, org);

// send over all entities (excpet the client) that touch the pvs
    ent = NEXT_EDICT(sv.edicts);
    for (e = 1; e < sv.num_edicts; e++, ent = NEXT_EDICT(ent)) {

// clent is ALWAYS sent
	if (ent != clent) {

// ignore ents without visible models
	    if (!ent->v.modelindex || !*PR_GetString(ent->v.model))
		continue;

// ignore if not touching a PV leaf
	    for (i = 0; i < ent->num_leafs; i++)
		if (Mod_TestLeafBit(pvs, ent->leafnums[i]))
		    break;

	    if (i == ent->num_leafs)
		continue;	// not visible
	}

	if (msg->maxsize - msg->cursize < 16) {
	    Con_Printf("packet overflow\n");
	    return;
	}
// send an update
	bits = 0;

	for (i = 0; i < 3; i++) {
	    miss = ent->v.origin[i] - ent->baseline.origin[i];
	    if (miss < -0.1 || miss > 0.1)
		bits |= U_ORIGIN1 << i;
	}

	if (ent->v.angles[0] != ent->baseline.angles[0])
	    bits |= U_ANGLE1;

	if (ent->v.angles[1] != ent->baseline.angles[1])
	    bits |= U_ANGLE2;

	if (ent->v.angles[2] != ent->baseline.angles[2])
	    bits |= U_ANGLE3;

	if (ent->v.movetype == MOVETYPE_STEP)
	    bits |= U_NOLERP;	// don't mess up the step animation

	if (ent->baseline.colormap != ent->v.colormap)
	    bits |= U_COLORMAP;

	if (ent->baseline.skinnum != ent->v.skin)
	    bits |= U_SKIN;

	if (ent->baseline.frame != ent->v.frame)
	    bits |= U_FRAME;

	if (ent->baseline.effects != ent->v.effects)
	    bits |= U_EFFECTS;

	if (ent->baseline.modelindex != ent->v.modelindex)
	    bits |= U_MODEL;

	/* FIXME - TODO: add alpha stuff here */

	if (sv.protocol == PROTOCOL_VERSION_FITZ) {
	    if ((bits & U_FRAME) && ((int)ent->v.frame & 0xff00))
		bits |= U_FITZ_FRAME2;
	    if ((bits & U_MODEL) && ((int)ent->v.modelindex & 0xff00))
		bits |= U_FITZ_MODEL2;
	    /* FIXME - Add the U_LERPFINISH bit */
	    if (bits & 0x00ff0000)
		bits |= U_FITZ_EXTEND1;
	    if (bits & 0xff000000)
		bits |= U_FITZ_EXTEND2;
	}

	if (e >= 256)
	    bits |= U_LONGENTITY;

	if (bits >= 256)
	    bits |= U_MOREBITS;

	//
	// write the message
	//
	MSG_WriteByte(msg, bits | U_SIGNAL);

	if (bits & U_MOREBITS)
	    MSG_WriteByte(msg, bits >> 8);
	if (bits & U_FITZ_EXTEND1)
	    MSG_WriteByte(msg, bits >> 16);
	if (bits & U_FITZ_EXTEND2)
	    MSG_WriteByte(msg, bits >> 24);

	if (bits & U_LONGENTITY)
	    MSG_WriteShort(msg, e);
	else
	    MSG_WriteByte(msg, e);

	if (bits & U_MODEL)
	    SV_WriteModelIndex(msg, ent->v.modelindex, 0);
	if (bits & U_FRAME)
	    MSG_WriteByte(msg, ent->v.frame);
	if (bits & U_COLORMAP)
	    MSG_WriteByte(msg, ent->v.colormap);
	if (bits & U_SKIN)
	    MSG_WriteByte(msg, ent->v.skin);
	if (bits & U_EFFECTS)
	    MSG_WriteByte(msg, ent->v.effects);
	if (bits & U_ORIGIN1)
	    MSG_WriteCoord(msg, ent->v.origin[0]);
	if (bits & U_ANGLE1)
	    MSG_WriteAngle(msg, ent->v.angles[0]);
	if (bits & U_ORIGIN2)
	    MSG_WriteCoord(msg, ent->v.origin[1]);
	if (bits & U_ANGLE2)
	    MSG_WriteAngle(msg, ent->v.angles[1]);
	if (bits & U_ORIGIN3)
	    MSG_WriteCoord(msg, ent->v.origin[2]);
	if (bits & U_ANGLE3)
	    MSG_WriteAngle(msg, ent->v.angles[2]);
#if 0 /* FIXME */
	if (bits & U_FITZ_ALPHA)
	    MSG_WriteByte(msg, ent->alpha);
#endif
	if (bits & U_FITZ_FRAME2)
	    MSG_WriteByte(msg, (int)ent->v.frame >> 8);
	if (bits & U_FITZ_MODEL2)
	    MSG_WriteByte(msg, (int)ent->v.modelindex >> 8);
#if 0 /* FIXME */
	if (bits & U_FITZ_LERPFINISH)
	    MSG_WriteByte(msg, (byte)floorf(((ent->v.nextthink - sv.time) * 255.0f) + 0.5f));
#endif
     }
}

/*
=============
SV_CleanupEnts

=============
*/
void
SV_CleanupEnts(void)
{
    int e;
    edict_t *ent;

    ent = NEXT_EDICT(sv.edicts);
    for (e = 1; e < sv.num_edicts; e++, ent = NEXT_EDICT(ent)) {
	ent->v.effects = (int)ent->v.effects & ~EF_MUZZLEFLASH;
    }

}

/*
==================
SV_WriteClientdataToMessage

==================
*/
void
SV_WriteClientdataToMessage(edict_t *player, sizebuf_t *msg)
{
    int bits;
    int i;
    edict_t *other;
    int items;
    eval_t *items2;
    float coord;

//
// send a damage message
//
    if (player->v.dmg_take || player->v.dmg_save) {
	other = PROG_TO_EDICT(player->v.dmg_inflictor);
	MSG_WriteByte(msg, svc_damage);
	MSG_WriteByte(msg, player->v.dmg_save);
	MSG_WriteByte(msg, player->v.dmg_take);
	for (i = 0; i < 3; i++) {
	    coord = other->v.origin[i];
	    coord += 0.5 * (other->v.mins[i] + other->v.maxs[i]);
	    MSG_WriteCoord(msg, coord);
	}
	player->v.dmg_take = 0;
	player->v.dmg_save = 0;
    }
//
// send the current viewpos offset from the view entity
//

    /* how much to look up / down ideally */
    SV_SetIdealPitch(player);

// a fixangle might get lost in a dropped packet.  Oh well.
    if (player->v.fixangle) {
	MSG_WriteByte(msg, svc_setangle);
	for (i = 0; i < 3; i++)
	    MSG_WriteAngle(msg, player->v.angles[i]);
	player->v.fixangle = 0;
    }

    bits = 0;

    if (player->v.view_ofs[2] != DEFAULT_VIEWHEIGHT)
	bits |= SU_VIEWHEIGHT;

    if (player->v.idealpitch)
	bits |= SU_IDEALPITCH;

// stuff the sigil bits into the high bits of items for sbar, or else
// mix in items2
    items = player->v.items;
    items2 = GetEdictFieldValue(player, "items2");
    if (items2)
	items |= (int)items2->_float << 23;
    else
	items |= (int)pr_global_struct->serverflags << 28;

    bits |= SU_ITEMS;

    if ((int)player->v.flags & FL_ONGROUND)
	bits |= SU_ONGROUND;

    if (player->v.waterlevel >= 2)
	bits |= SU_INWATER;

    for (i = 0; i < 3; i++) {
	if (player->v.punchangle[i])
	    bits |= (SU_PUNCH1 << i);
	if (player->v.velocity[i])
	    bits |= (SU_VELOCITY1 << i);
    }

    if (player->v.weaponframe)
	bits |= SU_WEAPONFRAME;

    if (player->v.armorvalue)
	bits |= SU_ARMOR;

//if (player->v.weapon)
    bits |= SU_WEAPON;

    if (sv.protocol == PROTOCOL_VERSION_FITZ) {
	if ((bits & SU_WEAPON) &&
	    (SV_ModelIndex(PR_GetString(player->v.weaponmodel)) & 0xff00))
	    bits |= SU_FITZ_WEAPON2;
	if ((int)player->v.armorvalue & 0xff00)
	    bits |= SU_FITZ_ARMOR2;
	if ((int)player->v.currentammo & 0xff00)
	    bits |= SU_FITZ_AMMO2;
	if ((int)player->v.ammo_shells & 0xff00)
	    bits |= SU_FITZ_SHELLS2;
	if ((int)player->v.ammo_nails & 0xff00)
	    bits |= SU_FITZ_NAILS2;
	if ((int)player->v.ammo_rockets & 0xff00)
	    bits |= SU_FITZ_ROCKETS2;
	if ((int)player->v.ammo_cells & 0xff00)
	    bits |= SU_FITZ_CELLS2;
	if ((bits & SU_WEAPONFRAME) &&
	    ((int)player->v.weaponframe & 0xff00))
	    bits |= SU_FITZ_WEAPONFRAME2;
#if 0 /* FIXME - TODO */
	if ((bits & SU_WEAPON) && player->alpha != ENTALPHA_DEFAULT)
	    // for now, weaponalpha = client entity alpha
	    bits |= SU_FITZ_WEAPONALPHA;
#endif
	if (bits & 0x00ff0000)
	    bits |= SU_FITZ_EXTEND1;
	if (bits & 0xff000000)
	    bits |= SU_FITZ_EXTEND2;
    }

// send the data

    MSG_WriteByte(msg, svc_clientdata);
    MSG_WriteShort(msg, bits);
    if (bits & SU_FITZ_EXTEND1)
	MSG_WriteByte(msg, bits >> 16);
    if (bits & SU_FITZ_EXTEND2)
	MSG_WriteByte(msg, bits >> 24);

    if (bits & SU_VIEWHEIGHT)
	MSG_WriteChar(msg, player->v.view_ofs[2]);

    if (bits & SU_IDEALPITCH)
	MSG_WriteChar(msg, player->v.idealpitch);

    for (i = 0; i < 3; i++) {
	if (bits & (SU_PUNCH1 << i))
	    MSG_WriteChar(msg, player->v.punchangle[i]);
	if (bits & (SU_VELOCITY1 << i))
	    MSG_WriteChar(msg, player->v.velocity[i] / 16);
    }

// [always sent]        if (bits & SU_ITEMS)
    MSG_WriteLong(msg, items);

    if (bits & SU_WEAPONFRAME)
	MSG_WriteByte(msg, player->v.weaponframe);
    if (bits & SU_ARMOR)
	MSG_WriteByte(msg, player->v.armorvalue);
    if (bits & SU_WEAPON)
	SV_WriteModelIndex(msg, SV_ModelIndex(PR_GetString(player->v.weaponmodel)), 0);

    MSG_WriteShort(msg, player->v.health);
    MSG_WriteByte(msg, player->v.currentammo);
    MSG_WriteByte(msg, player->v.ammo_shells);
    MSG_WriteByte(msg, player->v.ammo_nails);
    MSG_WriteByte(msg, player->v.ammo_rockets);
    MSG_WriteByte(msg, player->v.ammo_cells);

    if (standard_quake) {
	MSG_WriteByte(msg, player->v.weapon);
    } else {
	for (i = 0; i < 32; i++) {
	    if (((int)player->v.weapon) & (1 << i)) {
		MSG_WriteByte(msg, i);
		break;
	    }
	}
    }

    /* FITZ protocol stuff */
    if (bits & SU_FITZ_WEAPON2)
	MSG_WriteByte(msg, SV_ModelIndex(PR_GetString(player->v.weaponmodel)) >> 8);
    if (bits & SU_FITZ_ARMOR2)
	MSG_WriteByte(msg, (int)player->v.armorvalue >> 8);
    if (bits & SU_FITZ_AMMO2)
	MSG_WriteByte(msg, (int)player->v.currentammo >> 8);
    if (bits & SU_FITZ_SHELLS2)
	MSG_WriteByte(msg, (int)player->v.ammo_shells >> 8);
    if (bits & SU_FITZ_NAILS2)
	MSG_WriteByte(msg, (int)player->v.ammo_nails >> 8);
    if (bits & SU_FITZ_ROCKETS2)
	MSG_WriteByte(msg, (int)player->v.ammo_rockets >> 8);
    if (bits & SU_FITZ_CELLS2)
	MSG_WriteByte(msg, (int)player->v.ammo_cells >> 8);
    if (bits & SU_FITZ_WEAPONFRAME2)
	MSG_WriteByte(msg, (int)player->v.weaponframe >> 8);
#if 0 /* FIXME - TODO */
    if (bits & SU_FITZ_WEAPONALPHA)
	// for now, weaponalpha = client entity alpha
	MSG_WriteByte(msg, player->alpha);
#endif
}

/*
=======================
SV_SendClientDatagram
=======================
*/
qboolean
SV_SendClientDatagram(client_t *client)
{
    byte buf[MAX_DATAGRAM];
    sizebuf_t msg;
    int err;

    msg.data = buf;
    msg.maxsize = qmin(MAX_DATAGRAM, client->netconnection->mtu);
    msg.cursize = 0;

    MSG_WriteByte(&msg, svc_time);
    MSG_WriteFloat(&msg, sv.time);

// add the client specific data to the datagram
    SV_WriteClientdataToMessage(client->edict, &msg);
    SV_WriteEntitiesToClient(client->edict, &msg);

// copy the server datagram if there is space
    if (msg.cursize + sv.datagram.cursize < msg.maxsize)
	SZ_Write(&msg, sv.datagram.data, sv.datagram.cursize);

// send the datagram
    err = NET_SendUnreliableMessage(client->netconnection, &msg);
    /* if the message couldn't send, kick the client off */
    if (err == -1) {
	SV_DropClient(client, true);
	return false;
    }

    return true;
}

/*
=======================
SV_UpdateToReliableMessages
=======================
*/
void
SV_UpdateToReliableMessages(void)
{
    int i, j;
    client_t *client, *recipient;

    /* check for changes to be sent over the reliable streams */
    client = svs.clients;
    for (i = 0; i < svs.maxclients; i++, client++) {
	if (client->old_frags != client->edict->v.frags) {
	    recipient = svs.clients;
	    for (j = 0; j < svs.maxclients; j++, recipient++) {
		if (!recipient->active)
		    continue;
		MSG_WriteByte(&recipient->message, svc_updatefrags);
		MSG_WriteByte(&recipient->message, i);
		MSG_WriteShort(&recipient->message, client->edict->v.frags);
	    }
	    client->old_frags = client->edict->v.frags;
	}
    }

    recipient = svs.clients;
    for (i = 0; i < svs.maxclients; i++, recipient++) {
	if (!recipient->active)
	    continue;
	SZ_Write(&recipient->message, sv.reliable_datagram.data,
		 sv.reliable_datagram.cursize);
    }
    SZ_Clear(&sv.reliable_datagram);
}


/*
=======================
SV_SendNop

Send a nop message without trashing or sending the accumulated client
message buffer
=======================
*/
void
SV_SendNop(client_t *client)
{
    sizebuf_t msg;
    byte buf[4];
    int err;

    msg.data = buf;
    msg.maxsize = sizeof(buf);
    msg.cursize = 0;

    MSG_WriteChar(&msg, svc_nop);

    err = NET_SendUnreliableMessage(client->netconnection, &msg);
    /* if the message couldn't send, kick the client off */
    if (err == -1)
	SV_DropClient(client, true);
    client->last_message = realtime;
}

/*
=======================
SV_SendClientMessages
=======================
*/
void
SV_SendClientMessages(void)
{
    int i, err;
    client_t *client;

    /* update frags, names, etc */
    SV_UpdateToReliableMessages();

    /* build individual updates */
    client = svs.clients;
    for (i = 0; i < svs.maxclients; i++, client++) {
	if (!client->active)
	    continue;

	if (client->spawned) {
	    if (!SV_SendClientDatagram(client))
		continue;
	} else {
	    /*
	     * The player isn't totally in the game yet.  Send small keepalive
	     * messages if too much time has passed.  Send a full message when
	     * the next signon stage has been requested.  Some other message
	     * data (name changes, etc) may accumulate between signon stages.
	     */
	    if (!client->sendsignon) {
		if (realtime - client->last_message > 5)
		    SV_SendNop(client);
		/* don't send out non-signon messages */
		continue;
	    }
	}

	/*
	 * Check for an overflowed message.  Should only happen on a very
	 * fucked up connection that backs up a lot, then changes level.
	 */
	if (client->message.overflowed) {
	    SV_DropClient(client, true);
	    client->message.overflowed = false;
	    continue;
	}

	if (!client->message.cursize && !client->dropasap)
	    continue;
	if (!NET_CanSendMessage(client->netconnection))
	    continue;

	if (client->dropasap) {
	    /* went to another level */
	    SV_DropClient(client, false);
	    continue;
	}

	err = NET_SendMessage(client->netconnection, &client->message);
	if (err == -1)
	    SV_DropClient(client, true);

	SZ_Clear(&client->message);
	client->last_message = realtime;
	client->sendsignon = false;
    }

    /* clear muzzle flashes */
    SV_CleanupEnts();
}


/*
==============================================================================

SERVER SPAWNING

==============================================================================
*/

/*
================
SV_ModelIndex

================
*/
int
SV_ModelIndex(const char *name)
{
    int i;

    if (!name || !name[0])
	return 0;

    for (i = 0; i < MAX_MODELS && sv.model_precache[i]; i++)
	if (!strcmp(sv.model_precache[i], name))
	    return i;
    if (i == MAX_MODELS || !sv.model_precache[i])
	Sys_Error("%s: model %s not precached", __func__, name);
    return i;
}

/*
================
SV_CreateBaseline

================
*/
void
SV_CreateBaseline(void)
{
    int i;
    edict_t *svent;
    int entnum;
    unsigned int bits;

    for (entnum = 0; entnum < sv.num_edicts; entnum++) {
	// get the current server version
	svent = EDICT_NUM(entnum);
	if (svent->free)
	    continue;
	if (entnum > svs.maxclients && !svent->v.modelindex)
	    continue;

	//
	// create entity baseline
	//
	VectorCopy(svent->v.origin, svent->baseline.origin);
	VectorCopy(svent->v.angles, svent->baseline.angles);
	svent->baseline.frame = svent->v.frame;
	svent->baseline.skinnum = svent->v.skin;
	if (entnum > 0 && entnum <= svs.maxclients) {
	    svent->baseline.colormap = entnum;
	    svent->baseline.modelindex = SV_ModelIndex("progs/player.mdl");
	} else {
	    svent->baseline.colormap = 0;
	    svent->baseline.modelindex =
		SV_ModelIndex(PR_GetString(svent->v.model));
	}

	bits = 0;
	if (sv.protocol == PROTOCOL_VERSION_FITZ) {
	    if (svent->baseline.modelindex & 0xff00)
		bits |= B_FITZ_LARGEMODEL;
	    if (svent->baseline.frame & 0xff00)
		bits |= B_FITZ_LARGEFRAME;
#if 0 /* FIXME - TODO */
	    if (svent->baseline.alpha != ENTALPHA_DEFAULT)
		bits |= B_FITZ_ALPHA
#endif
	}

	//
	// add to the message
	//
	if (bits) {
	    MSG_WriteByte(&sv.signon, svc_fitz_spawnbaseline2);
	    MSG_WriteShort(&sv.signon, entnum);
	    MSG_WriteByte(&sv.signon, bits);
	} else {
	    MSG_WriteByte(&sv.signon, svc_spawnbaseline);
	    MSG_WriteShort(&sv.signon, entnum);
	}

	SV_WriteModelIndex(&sv.signon, svent->baseline.modelindex, bits);
	if (bits & B_FITZ_LARGEFRAME)
	    MSG_WriteShort(&sv.signon, svent->baseline.frame);
	else
	    MSG_WriteByte(&sv.signon, svent->baseline.frame);
	MSG_WriteByte(&sv.signon, svent->baseline.colormap);
	MSG_WriteByte(&sv.signon, svent->baseline.skinnum);
	for (i = 0; i < 3; i++) {
	    MSG_WriteCoord(&sv.signon, svent->baseline.origin[i]);
	    MSG_WriteAngle(&sv.signon, svent->baseline.angles[i]);
	}

#if 0 /* FIXME - TODO */
	if (bits & B_FITZ_ALPHA)
	    MSG_WriteByte(&sv.signon, svent->baseline.alpha);
#endif
    }
}


/*
================
SV_SendReconnect

Tell all the clients that the server is changing levels
================
*/
void
SV_SendReconnect(void)
{
    byte data[128];
    sizebuf_t msg;

    msg.data = data;
    msg.cursize = 0;
    msg.maxsize = sizeof(data);

    MSG_WriteChar(&msg, svc_stufftext);
    MSG_WriteString(&msg, "reconnect\n");
    NET_SendToAll(&msg, 5);

    if (cls.state != ca_dedicated)
	Cmd_ExecuteString("reconnect\n", src_command);
}


/*
================
SV_SaveSpawnparms

Grabs the current state of each client for saving across the
transition to another level
================
*/
void
SV_SaveSpawnparms(void)
{
    int i, j;
    client_t *client;

    svs.serverflags = pr_global_struct->serverflags;
    client = svs.clients;
    for (i = 0; i < svs.maxclients; i++, client++) {
	if (!client->active)
	    continue;
	/* call the progs to get default spawn parms for the new client */
	pr_global_struct->self = EDICT_TO_PROG(client->edict);
	PR_ExecuteProgram(pr_global_struct->SetChangeParms);
	for (j = 0; j < NUM_SPAWN_PARMS; j++)
	    client->spawn_parms[j] = (&pr_global_struct->parm1)[j];
    }
}


/*
================
SV_SpawnServer

This is called at the start of each level
================
*/
void
SV_SpawnServer(char *server)
{
    model_t *model;
    client_t *client;
    edict_t *ent;
    int i;

    // let's not have any servers with no name
    if (hostname.string[0] == 0)
	Cvar_Set("hostname", "UNNAMED");
    scr_centertime_off = 0;

    Con_DPrintf("SpawnServer: %s\n", server);
    svs.changelevel_issued = false;	// now safe to issue another

//
// tell all connected clients that we are going to a new level
//
    if (sv.active) {
	SV_SendReconnect();
    }
//
// make cvars consistant
//
    if (coop.value)
	Cvar_SetValue("deathmatch", 0);
    current_skill = (int)(skill.value + 0.5);
    if (current_skill < 0)
	current_skill = 0;
    if (current_skill > 3)
	current_skill = 3;

    Cvar_SetValue("skill", (float)current_skill);

//
// set up the new server
//
    Host_ClearMemory();

    memset(&sv, 0, sizeof(sv));

    strcpy(sv.name, server);

    sv.protocol = sv_protocol;

// load progs to get entity field count
    PR_LoadProgs();

// allocate server memory
    sv.max_edicts = MAX_EDICTS;
    sv.edicts = Hunk_AllocName(sv.max_edicts * pr_edict_size, "edicts");

    sv.datagram.maxsize = sizeof(sv.datagram_buf);
    sv.datagram.cursize = 0;
    sv.datagram.data = sv.datagram_buf;

    sv.reliable_datagram.maxsize = sizeof(sv.reliable_datagram_buf);
    sv.reliable_datagram.cursize = 0;
    sv.reliable_datagram.data = sv.reliable_datagram_buf;

    sv.signon.maxsize = sizeof(sv.signon_buf);
    sv.signon.cursize = 0;
    sv.signon.data = sv.signon_buf;

// leave slots at start for clients only
    sv.num_edicts = svs.maxclients + 1;
    for (i = 0; i < svs.maxclients; i++) {
	ent = EDICT_NUM(i + 1);
	svs.clients[i].edict = ent;
    }

    sv.state = ss_loading;
    sv.paused = false;

    sv.time = 1.0;

    strcpy(sv.name, server);
    sprintf(sv.modelname, "maps/%s.bsp", server);
    model = Mod_ForName(sv.modelname, false);
    if (!model) {
	Con_Printf("Couldn't spawn server %s\n", sv.modelname);
	sv.worldmodel = NULL;
	sv.active = false;
	return;
    }

    sv.worldmodel = BrushModel(model);
    sv.models[1] = model;
    if (sv.worldmodel->numsubmodels >= MAX_MODELS)
	Host_Error("Total models (%d) exceeds MAX_MODELS (%d)\n",
		   sv.worldmodel->numsubmodels, MAX_MODELS - 1);

//
// clear world interaction links
//
    SV_ClearWorld();

    sv.sound_precache[0] = pr_strings;

    sv.model_precache[0] = pr_strings;
    sv.model_precache[1] = sv.modelname;
    for (i = 1; i < sv.worldmodel->numsubmodels; i++) {
	sv.model_precache[1 + i] = localmodels[i];
	sv.models[i + 1] = Mod_ForName(localmodels[i], false);
    }

//
// load the rest of the entities
//
    ent = EDICT_NUM(0);
    memset(&ent->v, 0, progs->entityfields * 4);
    ent->free = false;
    ent->v.model = PR_SetString(sv.worldmodel->model.name);
    ent->v.modelindex = 1;	// world model
    ent->v.solid = SOLID_BSP;
    ent->v.movetype = MOVETYPE_PUSH;

    if (coop.value)
	pr_global_struct->coop = coop.value;
    else
	pr_global_struct->deathmatch = deathmatch.value;

    pr_global_struct->mapname = PR_SetString(sv.name);

// serverflags are for cross level information (sigils)
    pr_global_struct->serverflags = svs.serverflags;

    ED_LoadFromFile(sv.worldmodel->entities);

    sv.active = true;

// all setup is completed, any further precache statements are errors
    sv.state = ss_active;

// run two frames to allow everything to settle
    /* FIXME - don't do this when loading a saved game */
    host_frametime = 0.1;
    SV_Physics();
    SV_Physics();

// create a baseline for more efficient communications
    SV_CreateBaseline();

// send serverinfo to all connected clients
    client = svs.clients;
    for (i = 0; i < svs.maxclients; i++, client++)
	if (client->active)
	    SV_SendServerinfo(client);

    Con_DPrintf("Server spawned.\n");
}
