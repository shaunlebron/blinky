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

#include "console.h"
#include "crc.h"
#include "model.h"
#include "qwsvdef.h"
#include "server.h"
#include "world.h"

server_static_t svs;		// persistant server info
server_t sv;			// local server

/* inline model names for precache */
#define MODSTRLEN (sizeof("*" stringify(MAX_MODELS)) / sizeof(char))
static char localmodels[MAX_MODELS][MODSTRLEN];

char localinfo[MAX_LOCALINFO_STRING + 1];	// local game info

void
SV_ModelInit(void)
{
    int i;

    for (i = 0; i < MAX_MODELS; i++)
	snprintf(localmodels[i], sizeof(localmodels[i]), "*%d", i);
}

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
	SV_Error("SV_ModelIndex: model %s not precached", name);
    return i;
}

/*
================
SV_FlushSignon

Moves to the next signon buffer if needed
================
*/
void
SV_FlushSignon(void)
{
    if (sv.signon.cursize < sv.signon.maxsize - 512)
	return;

    if (sv.num_signon_buffers == MAX_SIGNON_BUFFERS - 1)
	SV_Error("sv.num_signon_buffers == MAX_SIGNON_BUFFERS-1");

    sv.signon_buffer_size[sv.num_signon_buffers - 1] = sv.signon.cursize;
    sv.signon.data = sv.signon_buffers[sv.num_signon_buffers];
    sv.num_signon_buffers++;
    sv.signon.cursize = 0;
}

/*
================
SV_CreateBaseline

Entity baselines are used to compress the update messages
to the clients -- only the fields that differ from the
baseline will be transmitted
================
*/
void
SV_CreateBaseline(void)
{
    int i;
    edict_t *svent;
    int entnum;

    for (entnum = 0; entnum < sv.num_edicts; entnum++) {
	svent = EDICT_NUM(entnum);
	if (svent->free)
	    continue;
	// create baselines for all player slots,
	// and any other edict that has a visible model
	if (entnum > MAX_CLIENTS && !svent->v.modelindex)
	    continue;

	//
	// create entity baseline
	//
	VectorCopy(svent->v.origin, svent->baseline.origin);
	VectorCopy(svent->v.angles, svent->baseline.angles);
	svent->baseline.frame = svent->v.frame;
	svent->baseline.skinnum = svent->v.skin;
	if (entnum > 0 && entnum <= MAX_CLIENTS) {
	    svent->baseline.colormap = entnum;
	    svent->baseline.modelindex = SV_ModelIndex("progs/player.mdl");
	} else {
	    svent->baseline.colormap = 0;
	    svent->baseline.modelindex =
		SV_ModelIndex(PR_GetString(svent->v.model));
	}

	//
	// flush the signon message out to a seperate buffer if
	// nearly full
	//
	SV_FlushSignon();

	//
	// add to the message
	//
	MSG_WriteByte(&sv.signon, svc_spawnbaseline);
	MSG_WriteShort(&sv.signon, entnum);

	MSG_WriteByte(&sv.signon, svent->baseline.modelindex);
	MSG_WriteByte(&sv.signon, svent->baseline.frame);
	MSG_WriteByte(&sv.signon, svent->baseline.colormap);
	MSG_WriteByte(&sv.signon, svent->baseline.skinnum);
	for (i = 0; i < 3; i++) {
	    MSG_WriteCoord(&sv.signon, svent->baseline.origin[i]);
	    MSG_WriteAngle(&sv.signon, svent->baseline.angles[i]);
	}
    }
}


/*
================
SV_SaveSpawnparms

Grabs the current state of the progs serverinfo flags
and each client for saving across the
transition to another level
================
*/
static void
SV_SaveSpawnparms(void)
{
    client_t *client;
    int i, j;

    /* only run if progs loaded */
    if (!sv.state)
	return;

    /* serverflags is the only game related thing maintained */
    svs.serverflags = pr_global_struct->serverflags;

    client = svs.clients;
    for (i = 0; i < MAX_CLIENTS; i++, client++) {
	if (client->state != cs_spawned)
	    continue;

	/* client needs to reconnect */
	client->state = cs_connected;

	/* call the progs to get default spawn parms for the new client */
	pr_global_struct->self = EDICT_TO_PROG(client->edict);
	PR_ExecuteProgram(pr_global_struct->SetChangeParms);
	for (j = 0; j < NUM_SPAWN_PARMS; j++)
	    client->spawn_parms[j] = (&pr_global_struct->parm1)[j];
    }
}

/*
================
SV_CalcPHS

Expands the PVS and calculates the PHS
(Potentially Hearable Set)
================
*/
void
SV_CalcPHS(void)
{
    int numleafs;
    size_t leafmem, pvsmem, phsmem;
    int i, leafnum;
    int vcount, hcount;
    const leafbits_t *leafbits;
    leafbits_t *pvs, *phs;
    leafblock_t check;

    Con_Printf("Building PHS...\n");

    numleafs = sv.worldmodel->numleafs;
    leafmem = Mod_LeafbitsSize(sv.worldmodel->numleafs);

    /*
     * Allocate space for an array of pointers, followed by the data.
     * PVS is 1 based, so allocate one extra row
     */
    pvsmem = (sizeof(leafbits_t *) + leafmem) * (numleafs + 1);
    phsmem = (sizeof(leafbits_t *) + leafmem) * numleafs;
    sv.pvs = Hunk_AllocName(pvsmem, "pvs");
    sv.phs = Hunk_AllocName(phsmem, "phs");
    for (i = 0; i < numleafs + 1; i++) {
	int offset = sizeof(leafbits_t *) * (numleafs + 1) + leafmem * i;
	sv.pvs[i] = (leafbits_t *)((byte *)sv.pvs + offset);
    }
    for (i = 0; i < numleafs; i++) {
	int offset = sizeof(leafbits_t *) * numleafs + leafmem * i;
	sv.phs[i] = (leafbits_t *)((byte *)sv.phs + offset);
    }

    vcount = 0;
    for (i = 0; i < numleafs + 1; i++) {
	pvs = sv.pvs[i];
	leafbits = Mod_LeafPVS(sv.worldmodel, sv.worldmodel->leafs + i);
	memcpy(pvs, leafbits, leafmem);
	if (!i)
	    continue;

	vcount += Mod_CountLeafBits(pvs);
    }

    hcount = 0;
    for (i = 0; i < numleafs; i++) {
	pvs = sv.pvs[i];
	phs = sv.phs[i];
	memcpy(phs, pvs, leafmem);
	if (!i)
	    continue;

	foreach_leafbit(pvs, leafnum, check) {
	    /*
	     * OR each visible pvs row into the phs
	     * index is +1 because pvs is 1 based
	     */
	    if (leafnum + 1 >= numleafs)
		continue;
	    leafbits = sv.pvs[leafnum + 1];
	    Mod_AddLeafBits(phs, leafbits);
	}
	hcount += Mod_CountLeafBits(phs);
    }

    Con_Printf("Average leafs visible / hearable / total: %i / %i / %i\n",
	       vcount / numleafs, hcount / numleafs, numleafs);
}

static unsigned
SV_CheckModel(const char *mdl)
{
    byte stackbuf[1024];	// avoid dirtying the cache heap
    const byte *buf;
    unsigned short crc;

    buf = COM_LoadStackFile(mdl, stackbuf, sizeof(stackbuf), NULL);
    crc = CRC_Block(buf, com_filesize);

    return crc;
}

/*
================
SV_SpawnServer

Change the server to a new map, taking all connected
clients along with it.

This is only called from the SV_Map_f() function.
================
*/
void
SV_SpawnServer(const char *server)
{
    model_t *model;
    edict_t *ent;
    int i;

    Con_DPrintf("SpawnServer: %s\n", server);

    SV_SaveSpawnparms();

    svs.spawncount++;		// any partially connected client will be
    // restarted

    sv.state = ss_dead;

    Mod_ClearAll();
    Hunk_FreeToLowMark(host_hunklevel);

    // wipe the entire per-level structure
    memset(&sv, 0, sizeof(sv));

    sv.datagram.maxsize = sizeof(sv.datagram_buf);
    sv.datagram.data = sv.datagram_buf;
    sv.datagram.allowoverflow = true;

    sv.reliable_datagram.maxsize = sizeof(sv.reliable_datagram_buf);
    sv.reliable_datagram.data = sv.reliable_datagram_buf;

    sv.multicast.maxsize = sizeof(sv.multicast_buf);
    sv.multicast.data = sv.multicast_buf;

    sv.master.maxsize = sizeof(sv.master_buf);
    sv.master.data = sv.master_buf;

    sv.signon.maxsize = sizeof(sv.signon_buffers[0]);
    sv.signon.data = sv.signon_buffers[0];
    sv.num_signon_buffers = 1;

    strcpy(sv.name, server);

    // load progs to get entity field count
    // which determines how big each edict is
    PR_LoadProgs();

    // allocate edicts
    sv.edicts = Hunk_AllocName(MAX_EDICTS * pr_edict_size, "edicts");

    // leave slots at start for clients only
    sv.num_edicts = MAX_CLIENTS + 1;
    for (i = 0; i < MAX_CLIENTS; i++) {
	ent = EDICT_NUM(i + 1);
	svs.clients[i].edict = ent;
//ZOID - make sure we update frags right
	svs.clients[i].old_frags = 0;
    }

    sv.time = 1.0;

    snprintf(sv.name, sizeof(sv.name), "%s", server);
    snprintf(sv.modelname, sizeof(sv.modelname), "maps/%s.bsp", server);
    model = Mod_ForName(sv.modelname, true);
    sv.worldmodel = BrushModel(model);
    SV_CalcPHS();

    //
    // clear physics interaction links
    //
    SV_ClearWorld();

    sv.sound_precache[0] = pr_strings;

    sv.model_precache[0] = pr_strings;
    sv.model_precache[1] = sv.modelname;
    sv.models[1] = &sv.worldmodel->model;
    for (i = 1; i < sv.worldmodel->numsubmodels; i++) {
	sv.model_precache[1 + i] = localmodels[i];
	sv.models[i + 1] = Mod_ForName(localmodels[i], false);
    }

    //check player/eyes models for hacks
    sv.model_player_checksum = SV_CheckModel("progs/player.mdl");
    sv.eyes_player_checksum = SV_CheckModel("progs/eyes.mdl");

    //
    // spawn the rest of the entities on the map
    //

    // precache and static commands can be issued during
    // map initialization
    sv.state = ss_loading;

    ent = EDICT_NUM(0);
    ent->free = false;
    ent->v.model = PR_SetString(sv.worldmodel->model.name);
    ent->v.modelindex = 1;	// world model
    ent->v.solid = SOLID_BSP;
    ent->v.movetype = MOVETYPE_PUSH;

    pr_global_struct->mapname = PR_SetString(sv.name);
    // serverflags are for cross level information (sigils)
    pr_global_struct->serverflags = svs.serverflags;

    // run the frame start qc function to let progs check cvars
    SV_ProgStartFrame();

    // load and spawn all other entities
    ED_LoadFromFile(sv.worldmodel->entities);

    // look up some model indexes for specialized message compression
    SV_FindModelNumbers();

    // all spawning is completed, any further precache statements
    // or prog writes to the signon message are errors
    sv.state = ss_active;

    // run two frames to allow everything to settle
    host_frametime = 0.1;
    SV_Physics();
    SV_Physics();

    // save movement vars
    SV_SetMoveVars();

    // create a baseline for more efficient communications
    SV_CreateBaseline();
    sv.signon_buffer_size[sv.num_signon_buffers - 1] = sv.signon.cursize;

    Info_SetValueForKey(svs.info, "map", sv.name, MAX_SERVERINFO_STRING);
    Con_DPrintf("Server spawned.\n");
}
