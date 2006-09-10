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
// sv_user.c -- server code for moving users

#include "cmd.h"
#include "console.h"
#include "pmove.h"
#include "qwsvdef.h"
#include "server.h"
#include "sys.h"
#include "world.h"

static cvar_t cl_rollspeed = { "cl_rollspeed", "200" };
static cvar_t cl_rollangle = { "cl_rollangle", "2.0" };
static cvar_t sv_spectalk = { "sv_spectalk", "1" };
static cvar_t sv_mapcheck = { "sv_mapcheck", "1" };

/*
============================================================

USER STRINGCMD EXECUTION

============================================================
*/

/*
================
SV_New_f

Sends the first message from the server to a connected client.
This will be sent on the initial connection and upon each server load.
================
*/
static void
SV_New_f(client_t *client)
{
    const char *gamedir;
    int playernum;

    if (client->state == cs_spawned)
	return;

    client->state = cs_connected;
    client->connection_started = realtime;

    // send the info about the new client to all connected clients
//      SV_FullClientUpdate (client, &sv.reliable_datagram);
//      client->sendinfo = true;

    gamedir = Info_ValueForKey(svs.info, "*gamedir");
    if (!gamedir[0])
	gamedir = "qw";

//NOTE:  This doesn't go through ClientReliableWrite since it's before the user
//spawns.  These functions are written to not overflow
    if (client->num_backbuf) {
	Con_Printf("WARNING %s: [SV_New] Back buffered (%d0, clearing",
		   client->name, client->netchan.message.cursize);
	client->num_backbuf = 0;
	SZ_Clear(&client->netchan.message);
    }
    // send the serverdata
    MSG_WriteByte(&client->netchan.message, svc_serverdata);
    MSG_WriteLong(&client->netchan.message, PROTOCOL_VERSION);
    MSG_WriteLong(&client->netchan.message, svs.spawncount);
    MSG_WriteString(&client->netchan.message, gamedir);

    playernum = NUM_FOR_EDICT(client->edict) - 1;
    if (client->spectator)
	playernum |= 128;
    MSG_WriteByte(&client->netchan.message, playernum);

    // send full levelname
    MSG_WriteString(&client->netchan.message,
		    PR_GetString(sv.edicts->v.message));

    // send the movevars
    MSG_WriteFloat(&client->netchan.message, movevars.gravity);
    MSG_WriteFloat(&client->netchan.message, movevars.stopspeed);
    MSG_WriteFloat(&client->netchan.message, movevars.maxspeed);
    MSG_WriteFloat(&client->netchan.message, movevars.spectatormaxspeed);
    MSG_WriteFloat(&client->netchan.message, movevars.accelerate);
    MSG_WriteFloat(&client->netchan.message, movevars.airaccelerate);
    MSG_WriteFloat(&client->netchan.message, movevars.wateraccelerate);
    MSG_WriteFloat(&client->netchan.message, movevars.friction);
    MSG_WriteFloat(&client->netchan.message, movevars.waterfriction);
    MSG_WriteFloat(&client->netchan.message, movevars.entgravity);

    // send music
    MSG_WriteByte(&client->netchan.message, svc_cdtrack);
    MSG_WriteByte(&client->netchan.message, sv.edicts->v.sounds);

    // send server info string
    MSG_WriteByte(&client->netchan.message, svc_stufftext);
    MSG_WriteStringf(&client->netchan.message, "fullserverinfo \"%s\"\n",
		     svs.info);
}

/*
==================
SV_Soundlist_f
==================
*/
static void
SV_Soundlist_f(client_t *client)
{
    const char **soundlist;
    int nextsound;

    if (client->state != cs_connected) {
	Con_Printf("soundlist not valid -- already spawned\n");
	return;
    }
    // handle the case of a level changing while a client was connecting
    if (atoi(Cmd_Argv(1)) != svs.spawncount) {
	Con_Printf("SV_Soundlist_f from different level\n");
	SV_New_f(client);
	return;
    }

    nextsound = atoi(Cmd_Argv(2));

//NOTE:  This doesn't go through ClientReliableWrite since it's before the user
//spawns.  These functions are written to not overflow
    if (client->num_backbuf) {
	Con_Printf("WARNING %s: [SV_Soundlist] Back buffered (%d0, clearing",
		   client->name, client->netchan.message.cursize);
	client->num_backbuf = 0;
	SZ_Clear(&client->netchan.message);
    }

    MSG_WriteByte(&client->netchan.message, svc_soundlist);
    MSG_WriteByte(&client->netchan.message, nextsound);

    soundlist = sv.sound_precache + 1 + nextsound;
    while (*soundlist) {
	if (client->netchan.message.cursize >= (MAX_MSGLEN / 2))
	    break;
	MSG_WriteString(&client->netchan.message, *soundlist);
	soundlist++;
	nextsound++;
    }
    MSG_WriteByte(&client->netchan.message, 0);

    /* next msg */
    if (*soundlist)
	MSG_WriteByte(&client->netchan.message, nextsound);
    else
	MSG_WriteByte(&client->netchan.message, 0);
}

/*
==================
SV_Modellist_f
==================
*/
static void
SV_Modellist_f(client_t *client)
{
    const char **modellist;
    int nextmodel;

    if (client->state != cs_connected) {
	Con_Printf("modellist not valid -- already spawned\n");
	return;
    }
    // handle the case of a level changing while a client was connecting
    if (atoi(Cmd_Argv(1)) != svs.spawncount) {
	Con_Printf("SV_Modellist_f from different level\n");
	SV_New_f(client);
	return;
    }

    nextmodel = atoi(Cmd_Argv(2));

//NOTE:  This doesn't go through ClientReliableWrite since it's before the user
//spawns.  These functions are written to not overflow
    if (client->num_backbuf) {
	Con_Printf("WARNING %s: [SV_Modellist] Back buffered (%d0, clearing",
		   client->name, client->netchan.message.cursize);
	client->num_backbuf = 0;
	SZ_Clear(&client->netchan.message);
    }

    MSG_WriteByte(&client->netchan.message, svc_modellist);
    MSG_WriteByte(&client->netchan.message, nextmodel);

    modellist = sv.model_precache + 1 + nextmodel;
    while (*modellist) {
	if (client->netchan.message.cursize >= (MAX_MSGLEN / 2))
	    break;
	MSG_WriteString(&client->netchan.message, *modellist);
	modellist++;
	nextmodel++;
    }
    MSG_WriteByte(&client->netchan.message, 0);

    /* next msg */
    if (*modellist)
	MSG_WriteByte(&client->netchan.message, nextmodel);
    else
	MSG_WriteByte(&client->netchan.message, 0);
}

/*
==================
SV_PreSpawn_f
==================
*/
static void
SV_PreSpawn_f(client_t *client)
{
    unsigned buf;
    unsigned check;

    if (client->state != cs_connected) {
	Con_Printf("prespawn not valid -- already spawned\n");
	return;
    }
    // handle the case of a level changing while a client was connecting
    if (atoi(Cmd_Argv(1)) != svs.spawncount) {
	Con_Printf("SV_PreSpawn_f from different level\n");
	SV_New_f(client);
	return;
    }

    buf = atoi(Cmd_Argv(2));
    if (buf >= sv.num_signon_buffers)
	buf = 0;

    if (!buf) {
	// should be three numbers following containing checksums
	check = atoi(Cmd_Argv(3));

//              Con_DPrintf("Client check = %d\n", check);

	if (sv_mapcheck.value && check != sv.worldmodel->checksum &&
	    check != sv.worldmodel->checksum2) {
	    SV_ClientPrintf(client, PRINT_HIGH,
			    "Map model file does not match (%s), %i != %i/%i.\n"
			    "You may need a new version of the map, or the proper install files.\n",
			    sv.modelname, check, sv.worldmodel->checksum,
			    sv.worldmodel->checksum2);
	    SV_DropClient(client);
	    return;
	}
	client->checksum = check;
    }
//NOTE:  This doesn't go through ClientReliableWrite since it's before the user
//spawns.  These functions are written to not overflow
    if (client->num_backbuf) {
	Con_Printf("WARNING %s: [SV_PreSpawn] Back buffered (%d0, clearing",
		   client->name, client->netchan.message.cursize);
	client->num_backbuf = 0;
	SZ_Clear(&client->netchan.message);
    }

    SZ_Write(&client->netchan.message,
	     sv.signon_buffers[buf], sv.signon_buffer_size[buf]);

    buf++;
    if (buf == sv.num_signon_buffers) {	// all done prespawning
	MSG_WriteByte(&client->netchan.message, svc_stufftext);
	MSG_WriteStringf(&client->netchan.message, "cmd spawn %i 0\n",
			 svs.spawncount);
    } else {			// need to prespawn more
	MSG_WriteByte(&client->netchan.message, svc_stufftext);
	MSG_WriteStringf(&client->netchan.message, "cmd prespawn %i %i\n",
			 svs.spawncount, buf);
    }
}

/*
==================
SV_Spawn_f
==================
*/
static void
SV_Spawn_f(client_t *client)
{
    int i, length;
    client_t *recipient;
    edict_t *player;
    eval_t *val;
    int n;

    if (client->state != cs_connected) {
	Con_Printf("Spawn not valid -- already spawned\n");
	return;
    }
// handle the case of a level changing while a client was connecting
    if (atoi(Cmd_Argv(1)) != svs.spawncount) {
	Con_Printf("SV_Spawn_f from different level\n");
	SV_New_f(client);
	return;
    }

    n = atoi(Cmd_Argv(2));

    // make sure n is valid
    if (n < 0 || n > MAX_CLIENTS) {
	Con_Printf("SV_Spawn_f invalid client start\n");
	SV_New_f(client);
	return;
    }
// send all current names, colors, and frag counts
    // FIXME: is this a good thing?
    SZ_Clear(&client->netchan.message);

// send current status of all other players

    // normally this could overflow, but no need to check due to backbuf
    recipient = svs.clients + n;
    for (i = n; i < MAX_CLIENTS; i++, recipient++)
	SV_FullClientUpdateToClient(recipient, client);

// send all current light styles
    for (i = 0; i < MAX_LIGHTSTYLES; i++) {
	length = 3 + (sv.lightstyles[i] ? strlen(sv.lightstyles[i]) : 1);
	ClientReliableWrite_Begin(client, svc_lightstyle, length);
	ClientReliableWrite_Byte(client, (char)i);
	ClientReliableWrite_String(client, sv.lightstyles[i]);
    }

    // set up the edict
    player = client->edict;

    memset(&player->v, 0, progs->entityfields * 4);
    player->v.colormap = NUM_FOR_EDICT(player);
    player->v.team = 0;		// FIXME
    player->v.netname = PR_SetString(client->name);

    client->entgravity = 1.0;
    val = GetEdictFieldValue(player, "gravity");
    if (val)
	val->_float = 1.0;
    client->maxspeed = sv_maxspeed.value;
    val = GetEdictFieldValue(player, "maxspeed");
    if (val)
	val->_float = sv_maxspeed.value;

//
// force stats to be updated
//
    memset(client->stats, 0, sizeof(client->stats));

    ClientReliableWrite_Begin(client, svc_updatestatlong, 6);
    ClientReliableWrite_Byte(client, STAT_TOTALSECRETS);
    ClientReliableWrite_Long(client, pr_global_struct->total_secrets);

    ClientReliableWrite_Begin(client, svc_updatestatlong, 6);
    ClientReliableWrite_Byte(client, STAT_TOTALMONSTERS);
    ClientReliableWrite_Long(client, pr_global_struct->total_monsters);

    ClientReliableWrite_Begin(client, svc_updatestatlong, 6);
    ClientReliableWrite_Byte(client, STAT_SECRETS);
    ClientReliableWrite_Long(client, pr_global_struct->found_secrets);

    ClientReliableWrite_Begin(client, svc_updatestatlong, 6);
    ClientReliableWrite_Byte(client, STAT_MONSTERS);
    ClientReliableWrite_Long(client, pr_global_struct->killed_monsters);

    // get the client to check and download skins
    // when that is completed, a begin command will be issued
    ClientReliableWrite_Begin(client, svc_stufftext, 8);
    ClientReliableWrite_String(client, "skins\n");
}

/*
==================
SV_SpawnSpectator
==================
*/
static void
SV_SpawnSpectator(edict_t *player)
{
    int i;
    edict_t *spawn;

    VectorCopy(vec3_origin, player->v.origin);
    VectorCopy(vec3_origin, player->v.view_ofs);
    player->v.view_ofs[2] = 22;

    /* search for an info_playerstart to spawn the spectator at */
    for (i = MAX_CLIENTS - 1; i < sv.num_edicts; i++) {
	spawn = EDICT_NUM(i);
	if (!strcmp(PR_GetString(spawn->v.classname), "info_player_start")) {
	    VectorCopy(spawn->v.origin, player->v.origin);
	    return;
	}
    }
}

/*
==================
SV_Begin_f
==================
*/
static void
SV_Begin_f(client_t *client)
{
    edict_t *player = client->edict;
    unsigned pmodel = 0, emodel = 0;
    int i;

    if (client->state == cs_spawned)
	return;			// don't begin again

    client->state = cs_spawned;

    // handle the case of a level changing while a client was connecting
    if (atoi(Cmd_Argv(1)) != svs.spawncount) {
	Con_Printf("SV_Begin_f from different level\n");
	SV_New_f(client);
	return;
    }

    if (client->spectator) {
	SV_SpawnSpectator(player);

	if (SpectatorConnect) {
	    // copy spawn parms out of the client_t
	    for (i = 0; i < NUM_SPAWN_PARMS; i++)
		(&pr_global_struct->parm1)[i] = client->spawn_parms[i];

	    // call the spawn function
	    pr_global_struct->time = sv.time;
	    pr_global_struct->self = EDICT_TO_PROG(player);
	    PR_ExecuteProgram(SpectatorConnect);
	}
    } else {
	// copy spawn parms out of the client_t
	for (i = 0; i < NUM_SPAWN_PARMS; i++)
	    (&pr_global_struct->parm1)[i] = client->spawn_parms[i];

	// call the spawn function
	pr_global_struct->time = sv.time;
	pr_global_struct->self = EDICT_TO_PROG(player);
	PR_ExecuteProgram(pr_global_struct->ClientConnect);

	// actually spawn the player
	pr_global_struct->time = sv.time;
	pr_global_struct->self = EDICT_TO_PROG(player);
	PR_ExecuteProgram(pr_global_struct->PutClientInServer);
    }

    // clear the net statistics, because connecting gives a bogus picture
    client->netchan.frame_latency = 0;
    client->netchan.frame_rate = 0;
    client->netchan.drop_count = 0;
    client->netchan.good_count = 0;

    //check he's not cheating

    pmodel = atoi(Info_ValueForKey(client->userinfo, "pmodel"));
    emodel = atoi(Info_ValueForKey(client->userinfo, "emodel"));

    if (pmodel != sv.model_player_checksum || emodel != sv.eyes_player_checksum)
	SV_BroadcastPrintf(PRINT_HIGH, "%s WARNING: non standard player/eyes "
			   "model detected\n", client->name);

    // if we are paused, tell the client
    if (sv.paused) {
	ClientReliableWrite_Begin(client, svc_setpause, 2);
	ClientReliableWrite_Byte(client, sv.paused);
	SV_ClientPrintf(client, PRINT_HIGH, "Server is paused.\n");
    }
#if 0
//
// send a fixangle over the reliable channel to make sure it gets there
// Never send a roll angle, because savegames can catch the server
// in a state where it is expecting the client to correct the angle
// and it won't happen if the game was just loaded, so you wind up
// with a permanent head tilt
    ent = EDICT_NUM(1 + (client - svs.clients));
    MSG_WriteByte(&client->netchan.message, svc_setangle);
    for (i = 0; i < 2; i++)
	MSG_WriteAngle(&client->netchan.message, ent->v.angles[i]);
    MSG_WriteAngle(&client->netchan.message, 0);
#endif
}

//=============================================================================

/*
==================
SV_NextDownload_f
==================
*/
static void
SV_NextDownload_f(client_t *client)
{
    byte buffer[1024];
    int r;
    int percent;
    int size;

    if (!client->download)
	return;

    r = client->downloadsize - client->downloadcount;
    if (r > 768)
	r = 768;
    r = fread(buffer, 1, r, client->download);
    ClientReliableWrite_Begin(client, svc_download, 6 + r);
    ClientReliableWrite_Short(client, r);

    client->downloadcount += r;
    size = client->downloadsize;
    if (!size)
	size = 1;
    percent = client->downloadcount * 100 / size;
    ClientReliableWrite_Byte(client, percent);
    ClientReliableWrite_SZ(client, buffer, r);

    if (client->downloadcount != client->downloadsize)
	return;

    fclose(client->download);
    client->download = NULL;

}

static void
OutofBandPrintf(netadr_t where, const char *fmt, ...)
{
    va_list argptr;
    char send[MAX_PRINTMSG];

    send[0] = 0xff;
    send[1] = 0xff;
    send[2] = 0xff;
    send[3] = 0xff;
    send[4] = A2C_PRINT;
    va_start(argptr, fmt);
    vsnprintf(send + 5, sizeof(send) - 5, fmt, argptr);
    va_end(argptr);

    NET_SendPacket(strlen(send) + 1, send, where);
}

/*
==================
SV_NextUpload
==================
*/
static void
SV_NextUpload(client_t *client)
{
    int percent;
    int size;

    if (!*client->uploadfn) {
	SV_ClientPrintf(client, PRINT_HIGH, "Upload denied\n");
	ClientReliableWrite_Begin(client, svc_stufftext, 8);
	ClientReliableWrite_String(client, "stopul");

	/* suck out rest of packet */
	size = MSG_ReadShort();
	MSG_ReadByte();
	msg_readcount += size;
	return;
    }

    size = MSG_ReadShort();
    percent = MSG_ReadByte();

    if (!client->upload) {
	client->upload = fopen(client->uploadfn, "wb");
	if (!client->upload) {
	    Sys_Printf("Can't create %s\n", client->uploadfn);
	    ClientReliableWrite_Begin(client, svc_stufftext, 8);
	    ClientReliableWrite_String(client, "stopul");
	    *client->uploadfn = 0;
	    return;
	}
	Sys_Printf("Receiving %s from %d...\n", client->uploadfn,
		   client->userid);
	if (client->remote_snap)
	    OutofBandPrintf(client->snap_from,
			    "Server receiving %s from %d...\n",
			    client->uploadfn, client->userid);
    }

    fwrite(net_message.data + msg_readcount, 1, size, client->upload);
    msg_readcount += size;

    Con_DPrintf("UPLOAD: %d received\n", size);

    if (percent != 100) {
	ClientReliableWrite_Begin(client, svc_stufftext, 8);
	ClientReliableWrite_String(client, "nextul\n");
    } else {
	fclose(client->upload);
	client->upload = NULL;
	Sys_Printf("%s upload completed.\n", client->uploadfn);

	if (client->remote_snap) {
	    char *path = strchr(client->uploadfn, '/');
	    path = path ? path + 1 : client->uploadfn;
	    OutofBandPrintf(client->snap_from,
			    "%s upload completed.\n"
			    "To download, enter:\n"
			    "download %s\n",
			    client->uploadfn, path);
	}
    }
}

/*
==================
SV_BeginDownload_f
==================
*/
static void
SV_BeginDownload_f(client_t *client)
{
    char name[MAX_OSPATH], *p;

    /* Lowercase name (needed for casesen file systems) */
    snprintf(name, sizeof(name), "%s", Cmd_Argv(1));
    for (p = name; *p; p++)
	*p = tolower(*p);

    // first off, no .. or global allow check
    if (strstr(name, "..") || !allow_download.value
	// leading dot is no good
	|| *name == '.'
	// leading slash bad as well, must be in subdir
	|| *name == '/'
	// next up, skin check
	|| (strncmp(name, "skins/", 6) == 0 && !allow_download_skins.value)
	// now models
	|| (strncmp(name, "progs/", 6) == 0 && !allow_download_models.value)
	// now sounds
	|| (strncmp(name, "sound/", 6) == 0 && !allow_download_sounds.value)
	// now maps (note special case for maps, must not be in pak)
	|| (strncmp(name, "maps/", 6) == 0 && !allow_download_maps.value)
	// MUST be in a subdirectory
	|| !strstr(name, "/")) {	// don't allow anything with .. path
	ClientReliableWrite_Begin(client, svc_download, 4);
	ClientReliableWrite_Short(client, -1);
	ClientReliableWrite_Byte(client, 0);
	return;
    }

    if (client->download) {
	fclose(client->download);
	client->download = NULL;
    }

    client->downloadsize = COM_FOpenFile(name, &client->download);
    client->downloadcount = 0;

    if (!client->download
	// special check for maps, if it came from a pak file, don't allow
	// download  ZOID
	|| (strncmp(name, "maps/", 5) == 0 && file_from_pak)) {
	if (client->download) {
	    fclose(client->download);
	    client->download = NULL;
	}

	Sys_Printf("Couldn't upload %s to %s\n", name, client->name);
	ClientReliableWrite_Begin(client, svc_download, 4);
	ClientReliableWrite_Short(client, -1);
	ClientReliableWrite_Byte(client, 0);
	return;
    }

    SV_NextDownload_f(client);
    Sys_Printf("Uploading %s to %s\n", name, client->name);
}

//=============================================================================

/*
==================
SV_Say
==================
*/
static void
SV_Say(client_t *client, qboolean team)
{
    client_t *recipient;
    int i, tmp;
    size_t len, space;
    const char *p;
    char text[2048];
    char t1[32], *t2;

    if (Cmd_Argc() < 2)
	return;

    if (team) {
	strncpy(t1, Info_ValueForKey(client->userinfo, "team"), 31);
	t1[31] = 0;
    }

    if (client->spectator && (!sv_spectalk.value || team))
	sprintf(text, "[SPEC] %s: ", client->name);
    else if (team)
	sprintf(text, "(%s): ", client->name);
    else {
	sprintf(text, "%s: ", client->name);
    }

    if (fp_messages) {
	if (!sv.paused && realtime < client->lockedtill) {
	    SV_ClientPrintf(client, PRINT_CHAT,
			    "You can't talk for %d more seconds\n",
			    (int)(client->lockedtill - realtime));
	    return;
	}
	tmp = client->whensaidhead - fp_messages + 1;
	if (tmp < 0)
	    tmp = 10 + tmp;
	if (!sv.paused && client->whensaid[tmp]
	    && (realtime - client->whensaid[tmp] < fp_persecond)) {
	    client->lockedtill = realtime + fp_secondsdead;
	    if (fp_msg[0])
		SV_ClientPrintf(client, PRINT_CHAT,
				"FloodProt: %s\n", fp_msg);
	    else
		SV_ClientPrintf(client, PRINT_CHAT,
				"FloodProt: You can't talk for %d seconds.\n",
				fp_secondsdead);
	    return;
	}
	client->whensaidhead++;
	if (client->whensaidhead > 9)
	    client->whensaidhead = 0;
	client->whensaid[client->whensaidhead] = realtime;
    }

    len = strlen(text);
    space = sizeof(text) - len - 2; // -2 for \n and null terminator
    p = Cmd_Args();
    if (*p == '"') {
	/* remove quotes */
	strncat(text, p + 1, qmin(strlen(p) - 2, space));
	text[len + qmin(strlen(p) - 2, space)] = 0;
    } else {
	strncat(text, p, space);
	text[len + qmin(strlen(p), space)] = 0;
    }
    strcat(text, "\n");

    Sys_Printf("%s", text);

    recipient = svs.clients;
    for (i = 0; i < MAX_CLIENTS; i++, recipient++) {
	if (recipient->state != cs_spawned)
	    continue;
	if (client->spectator && !sv_spectalk.value)
	    if (!recipient->spectator)
		continue;

	if (team) {
	    // the spectator team
	    if (client->spectator) {
		if (!recipient->spectator)
		    continue;
	    } else {
		t2 = Info_ValueForKey(client->userinfo, "team");
		if (strcmp(t1, t2) || client->spectator)
		    continue;	// on different teams
	    }
	}
	SV_ClientPrintf(recipient, PRINT_CHAT, "%s", text);
    }
}


/*
==================
SV_Say_f
==================
*/
static void
SV_Say_f(client_t *client)
{
    SV_Say(client, false);
}

/*
==================
SV_Say_Team_f
==================
*/
static void
SV_Say_Team_f(client_t *client)
{
    SV_Say(client, true);
}



//============================================================================

/*
=================
SV_Pings_f

The client is showing the scoreboard, so send new ping times for all
clients
=================
*/
static void
SV_Pings_f(client_t *client)
{
    client_t *pingclient;
    int i;

    pingclient = svs.clients;
    for (i = 0; i < MAX_CLIENTS; i++, pingclient++) {
	if (pingclient->state != cs_spawned)
	    continue;

	ClientReliableWrite_Begin(client, svc_updateping, 4);
	ClientReliableWrite_Byte(client, i);
	ClientReliableWrite_Short(client, SV_CalcPing(pingclient));
	ClientReliableWrite_Begin(client, svc_updatepl, 4);
	ClientReliableWrite_Byte(client, i);
	ClientReliableWrite_Byte(client, pingclient->lossage);
    }
}



/*
==================
SV_Kill_f
==================
*/
static void
SV_Kill_f(client_t *client)
{
    edict_t *player = client->edict;

    if (player->v.health <= 0) {
	SV_ClientPrintf(client, PRINT_HIGH,
			"Can't suicide -- already dead!\n");
	return;
    }

    pr_global_struct->time = sv.time;
    pr_global_struct->self = EDICT_TO_PROG(player);
    PR_ExecuteProgram(pr_global_struct->ClientKill);
}

/*
==================
SV_TogglePause
==================
*/
void
SV_TogglePause(const char *msg)
{
    int i;
    client_t *cl;

    sv.paused ^= 1;

    if (msg)
	SV_BroadcastPrintf(PRINT_HIGH, "%s", msg);

    // send notification to all clients
    for (i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++) {
	if (!cl->state)
	    continue;
	ClientReliableWrite_Begin(cl, svc_setpause, 2);
	ClientReliableWrite_Byte(cl, sv.paused);
    }
}


/*
==================
SV_Pause_f
==================
*/
static void
SV_Pause_f(client_t *client)
{
    char st[sizeof(client->name) + 32];

    if (!pausable.value) {
	SV_ClientPrintf(client, PRINT_HIGH, "Pause not allowed.\n");
	return;
    }

    if (client->spectator) {
	SV_ClientPrintf(client, PRINT_HIGH,
			"Spectators can not pause.\n");
	return;
    }

    if (sv.paused)
	sprintf(st, "%s paused the game\n", client->name);
    else
	sprintf(st, "%s unpaused the game\n", client->name);

    SV_TogglePause(st);
}


/*
=================
SV_Drop_f

The client is going to disconnect, so remove the connection immediately
=================
*/
static void
SV_Drop_f(client_t *client)
{
    SV_EndRedirect();
    if (!client->spectator)
	SV_BroadcastPrintf(PRINT_HIGH, "%s dropped\n", client->name);
    SV_DropClient(client);
}

/*
=================
SV_PTrack_f

Change the bandwidth estimate for a client
=================
*/
static void
SV_PTrack_f(client_t *client)
{
    int i;
    edict_t *ent, *tent;

    if (!client->spectator)
	return;

    if (Cmd_Argc() != 2) {
	// turn off tracking
	client->spec_track = 0;
	ent = EDICT_NUM(client - svs.clients + 1);
	tent = EDICT_NUM(0);
	ent->v.goalentity = EDICT_TO_PROG(tent);
	return;
    }

    i = atoi(Cmd_Argv(1));
    if (i < 0 || i >= MAX_CLIENTS || svs.clients[i].state != cs_spawned ||
	svs.clients[i].spectator) {
	SV_ClientPrintf(client, PRINT_HIGH, "Invalid client to track\n");
	client->spec_track = 0;
	ent = EDICT_NUM(client - svs.clients + 1);
	tent = EDICT_NUM(0);
	ent->v.goalentity = EDICT_TO_PROG(tent);
	return;
    }
    client->spec_track = i + 1;	// now tracking

    ent = EDICT_NUM(client - svs.clients + 1);
    tent = EDICT_NUM(i + 1);
    ent->v.goalentity = EDICT_TO_PROG(tent);
}


/*
=================
SV_Rate_f

Change the bandwidth estimate for a client
=================
*/
static void
SV_Rate_f(client_t *client)
{
    int rate;

    if (Cmd_Argc() != 2) {
	SV_ClientPrintf(client, PRINT_HIGH, "Current rate is %i\n",
			(int)(1.0 / client->netchan.rate + 0.5));
	return;
    }

    rate = atoi(Cmd_Argv(1));
    if (rate < 500)
	rate = 500;
    if (rate > 10000)
	rate = 10000;

    SV_ClientPrintf(client, PRINT_HIGH, "Net rate set to %i\n", rate);
    client->netchan.rate = 1.0 / rate;
}


/*
=================
SV_Msg_f

Change the message level for a client
=================
*/
static void
SV_Msg_f(client_t *client)
{
    if (Cmd_Argc() != 2) {
	SV_ClientPrintf(client, PRINT_HIGH, "Current msg level is %i\n",
			client->messagelevel);
	return;
    }

    client->messagelevel = atoi(Cmd_Argv(1));

    SV_ClientPrintf(client, PRINT_HIGH, "Msg level set to %i\n",
		    client->messagelevel);
}

/*
==================
SV_SetInfo_f

Allow clients to change userinfo
==================
*/
static void
SV_SetInfo_f(client_t *client)
{
    int i;
    char oldval[MAX_INFO_STRING];

    if (Cmd_Argc() == 1) {
	Con_Printf("User info settings:\n");
	Info_Print(client->userinfo);
	return;
    }

    if (Cmd_Argc() != 3) {
	Con_Printf("usage: setinfo [ <key> <value> ]\n");
	return;
    }

    if (Cmd_Argv(1)[0] == '*')
	return;			// don't set priveledged values

    strcpy(oldval, Info_ValueForKey(client->userinfo, Cmd_Argv(1)));

    Info_SetValueForKey(client->userinfo, Cmd_Argv(1), Cmd_Argv(2),
			MAX_INFO_STRING);
// name is extracted below in ExtractFromUserInfo
//      strncpy (client->name, Info_ValueForKey (client->userinfo, "name")
//              , sizeof(client->name)-1);
//      SV_FullClientUpdate (client, &sv.reliable_datagram);
//      client->sendinfo = true;

    if (!strcmp(Info_ValueForKey(client->userinfo, Cmd_Argv(1)), oldval))
	return;			// key hasn't changed

    // process any changed values
    SV_ExtractFromUserinfo(client);

    i = client - svs.clients;
    MSG_WriteByte(&sv.reliable_datagram, svc_setinfo);
    MSG_WriteByte(&sv.reliable_datagram, i);
    MSG_WriteString(&sv.reliable_datagram, Cmd_Argv(1));
    MSG_WriteString(&sv.reliable_datagram,
		    Info_ValueForKey(client->userinfo, Cmd_Argv(1)));
}

/*
==================
SV_ShowServerinfo_f

Dumps the serverinfo info string
==================
*/
static void
SV_ShowServerinfo_f(client_t *client)
{
    Info_Print(svs.info);
}

static void
SV_NoSnap_f(client_t *client)
{
    if (*client->uploadfn) {
	*client->uploadfn = 0;
	SV_BroadcastPrintf(PRINT_HIGH, "%s refused remote screenshot\n",
			   client->name);
    }
}

typedef struct {
    const char *name;
    void (*func)(client_t *client);
} ucmd_t;

static ucmd_t ucmds[] = {
    { "new", SV_New_f },
    { "modellist", SV_Modellist_f },
    { "soundlist", SV_Soundlist_f },
    { "prespawn", SV_PreSpawn_f },
    { "spawn", SV_Spawn_f },
    { "begin", SV_Begin_f },

    { "drop", SV_Drop_f },
    { "pings", SV_Pings_f },

// issued by hand at client consoles
    { "rate", SV_Rate_f },
    { "kill", SV_Kill_f },
    { "pause", SV_Pause_f },
    { "msg", SV_Msg_f },

    { "say", SV_Say_f },
    { "say_team", SV_Say_Team_f },

    { "setinfo", SV_SetInfo_f },

    { "serverinfo", SV_ShowServerinfo_f },

    { "download", SV_BeginDownload_f },
    { "nextdl", SV_NextDownload_f },

    { "ptrack", SV_PTrack_f },	//ZOID - used with autocam

    { "snap", SV_NoSnap_f },

    { NULL, NULL }
};

/*
==================
SV_ExecuteUserCommand
==================
*/
static void
SV_ExecuteUserCommand(const char *cmdstring, client_t *client)
{
    ucmd_t *command;

    Cmd_TokenizeString(cmdstring);

    SV_BeginRedirect(RD_CLIENT, client);

    for (command = ucmds; command->name; command++)
	if (!strcmp(Cmd_Argv(0), command->name)) {
	    command->func(client);
	    break;
	}
    if (!command->name)
	Con_Printf("Bad user command: %s\n", Cmd_Argv(0));

    SV_EndRedirect();
}

/*
===========================================================================

USER CMD EXECUTION

===========================================================================
*/

/*
===============
V_CalcRoll

Used by view and sv_user
===============
*/
static float
V_CalcRoll(vec3_t angles, vec3_t velocity)
{
    vec3_t forward, right, up;
    float sign;
    float side;
    float value;

    AngleVectors(angles, forward, right, up);
    side = DotProduct(velocity, right);
    sign = side < 0 ? -1 : 1;
    side = fabs(side);

    value = cl_rollangle.value;

    if (side < cl_rollspeed.value)
	side = side * value / cl_rollspeed.value;
    else
	side = value;

    return side * sign;

}




//============================================================================

/*
================
AddAllEntsToPhysents

For debugging
================
*/
#if 0
static void
AddAllEntsToPhysents(const edict_t *player,
		     const vec3_t mins, const vec3_t maxs,
		     physent_stack_t *pestack)
{
    int i, entity, playernum;
    edict_t *check, *next;
    physent_t *physent;

    playernum = EDICT_TO_PROG(player);
    check = NEXT_EDICT(sv.edicts);
    physent = pestack->physents + pestack->numphysents;
    for (entity = 1; entity < sv.num_edicts; entity++, check = next) {
	next = NEXT_EDICT(check);
	if (check->free)
	    continue;
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

	    if (physent - pestack->physents == MAX_PHYSENTS)
		break;
	    VectorCopy(check->v.origin, physent->origin);
	    physent->info = entity;
	    if (check->v.solid == SOLID_BSP)
		physent->model = sv.models[(int)(check->v.modelindex)];
	    else {
		physent->model = NULL;
		VectorCopy(check->v.mins, physent->mins);
		VectorCopy(check->v.maxs, physent->maxs);
	    }
	    physent++;
	}
    }
    pestack->numphysents = physent - pestack->physents;
}
#endif

/*
===========
SV_PreRunCmd
===========
Done before running a player command.  Clears the touch array
*/
static byte playertouch[(MAX_EDICTS + 7) / 8];

static void
SV_PreRunCmd(void)
{
    memset(playertouch, 0, sizeof(playertouch));
}

static void
SV_PlayerMove(client_t *client, const usercmd_t *cmd)
{
    edict_t *player = client->edict;
    playermove_t pmove;
    physent_stack_t pestack;
    vec3_t mins, maxs;
    edict_t *entity;
    int i;

    if (!player->v.fixangle)
	VectorCopy(cmd->angles, player->v.v_angle);

    player->v.button0 = cmd->buttons & 1;
    player->v.button2 = (cmd->buttons & 2) >> 1;
    if (cmd->impulse)
	player->v.impulse = cmd->impulse;

//
// angles
// show 1/3 the pitch angle and all the roll angle
    if (player->v.health > 0) {
	if (!player->v.fixangle) {
	    player->v.angles[PITCH] = -player->v.v_angle[PITCH] / 3;
	    player->v.angles[YAW] = player->v.v_angle[YAW];
	}
	player->v.angles[ROLL] =
	    V_CalcRoll(player->v.angles, player->v.velocity) * 4;
    }

    host_frametime = cmd->msec * 0.001;
    if (host_frametime > 0.1)
	host_frametime = 0.1;

    if (!client->spectator) {
	pr_global_struct->frametime = host_frametime;

	pr_global_struct->time = sv.time;
	pr_global_struct->self = EDICT_TO_PROG(player);
	PR_ExecuteProgram(pr_global_struct->PlayerPreThink);

	SV_RunThink(player);
    }

    for (i = 0; i < 3; i++)
	pmove.origin[i] =
	    player->v.origin[i] + (player->v.mins[i] - player_mins[i]);
    VectorCopy(player->v.velocity, pmove.velocity);
    VectorCopy(player->v.v_angle, pmove.angles);

    pmove.spectator = client->spectator;
    pmove.waterjumptime = player->v.teleport_time;
    pmove.cmd = cmd;
    pmove.dead = player->v.health <= 0;
    pmove.oldbuttons = client->oldbuttons;

    movevars.entgravity = client->entgravity;
    movevars.maxspeed = client->maxspeed;

    /* Init the world's physent */
    memset(&pestack.physents[0], 0, sizeof(pestack.physents[0]));
    pestack.physents[0].brushmodel = ConstBrushModel(&sv.worldmodel->model);
    pestack.numphysent = 1;

    for (i = 0; i < 3; i++) {
	mins[i] = pmove.origin[i] - 256;
	maxs[i] = pmove.origin[i] + 256;
    }
#if 1
    SV_AddLinksToPhysents(player, mins, maxs, &pestack);
#else
    AddAllEntsToPmove(player, mins, maxs, &pestack);
#endif

#if 0
    {
	int before, after;

	before = PM_TestPlayerPosition(pmove.origin);
	PlayerMove(&pmove, &pestack);
	after = PM_TestPlayerPosition(pmove.origin);

	if (player->v.health > 0 && before && !after)
	    Con_Printf("player %s got stuck in playermove!!!!\n",
		       client->name);
    }
#else
    PlayerMove(&pmove, &pestack);
#endif

    client->oldbuttons = pmove.oldbuttons;
    player->v.teleport_time = pmove.waterjumptime;
    player->v.waterlevel = pmove.waterlevel;
    player->v.watertype = pmove.watertype;
    if (pmove.onground) {
	const int entitynum = pmove.onground->entitynum;
	player->v.groundentity = EDICT_TO_PROG(EDICT_NUM(entitynum));
	player->v.flags = (int)player->v.flags | FL_ONGROUND;
    } else
	player->v.flags = (int)player->v.flags & ~FL_ONGROUND;
    for (i = 0; i < 3; i++)
	player->v.origin[i] =
	    pmove.origin[i] - (player->v.mins[i] - player_mins[i]);

#if 0
    // truncate velocity the same way the net protocol will
    for (i = 0; i < 3; i++)
	player->v.velocity[i] = (int)pmove.velocity[i];
#else
    VectorCopy(pmove.velocity, player->v.velocity);
#endif

    VectorCopy(pmove.angles, player->v.v_angle);

    if (!client->spectator) {
	// link into place and touch triggers
	SV_LinkEdict(player, true);

	// touch other objects
	for (i = 0; i < pmove.numtouch; i++) {
	    const int entitynum = pmove.touch[i]->entitynum;
	    entity = EDICT_NUM(entitynum);
	    if (!entity->v.touch)
		continue;
	    if (playertouch[entitynum / 8] & (1 << (entitynum % 8)))
		continue;
	    pr_global_struct->self = EDICT_TO_PROG(entity);
	    pr_global_struct->other = EDICT_TO_PROG(player);
	    PR_ExecuteProgram(entity->v.touch);
	    playertouch[entitynum / 8] |= 1 << (entitynum % 8);
	}
    }
}

/*
===========
SV_RunCmd
===========
*/
static void
SV_RunCmd(client_t *client, const usercmd_t *cmd)
{
    /* split up very long moves */
    if (cmd->msec > 50) {
	usercmd_t split = *cmd;

	split.msec /= 2;
	SV_RunCmd(client, &split);
	split.impulse = 0;
	SV_RunCmd(client, &split);
	return;
    }
    SV_PlayerMove(client, cmd);
}

/*
===========
SV_PostRunCmd
===========
Done after running a player command.
*/
static void
SV_PostRunCmd(client_t *client)
{
    edict_t *player = client->edict;

    /* run post-think */
    if (!client->spectator) {
	pr_global_struct->time = sv.time;
	pr_global_struct->self = EDICT_TO_PROG(player);
	PR_ExecuteProgram(pr_global_struct->PlayerPostThink);
	SV_RunNewmis();
    } else if (SpectatorThink) {
	pr_global_struct->time = sv.time;
	pr_global_struct->self = EDICT_TO_PROG(player);
	PR_ExecuteProgram(SpectatorThink);
    }
}


/*
===================
SV_ExecuteClientMessage

The current net_message is parsed for the given client
===================
*/
void
SV_ExecuteClientMessage(client_t *client)
{
    edict_t *player = client->edict;
    qboolean move_issued = false;
    client_frame_t *frame;
    netchan_t *netchan;
    usercmd_t oldest, oldcmd, newcmd;
    int command, seq_hash, crc_offset, length;
    byte checksum, crc, *crc_buf;
    vec3_t origin;

    /* Calculate ping time */
    netchan = &client->netchan;
    frame = &client->frames[netchan->incoming_acknowledged & UPDATE_MASK];
    frame->ping_time = realtime - frame->senttime;

    /*
     * Make sure the reply sequence number matches the incoming
     * sequence number. If not, don't reply!
     */
    if (netchan->incoming_sequence >= netchan->outgoing_sequence)
	netchan->outgoing_sequence = netchan->incoming_sequence;
    else
	client->send_message = false;

    /* save time for ping calculations */
    frame = &client->frames[netchan->outgoing_sequence & UPDATE_MASK];
    frame->senttime = realtime;
    frame->ping_time = -1;

    seq_hash = netchan->incoming_sequence;

    /*
     * Mark time so clients will know how much to predict other players.
     * No delta unless requested.
     */
    client->localtime = sv.time;
    client->delta_sequence = -1;
    while (1) {
	if (msg_badread) {
	    Con_Printf("SV_ReadClientMessage: badread\n");
	    SV_DropClient(client);
	    return;
	}

	command = MSG_ReadByte();
	if (command == -1)
	    break;

	switch (command) {
	default:
	    Con_Printf("SV_ReadClientMessage: unknown command char\n");
	    SV_DropClient(client);
	    return;

	case clc_nop:
	    break;

	case clc_delta:
	    client->delta_sequence = MSG_ReadByte();
	    break;

	case clc_move:
	    /* Only one move allowed - no cheating! */
	    if (move_issued)
		return;
	    move_issued = true;

	    crc_offset = MSG_GetReadCount();
	    checksum = MSG_ReadByte();

	    /* read loss percentage */
	    client->lossage = MSG_ReadByte();

	    MSG_ReadDeltaUsercmd(&nullcmd, &oldest);
	    MSG_ReadDeltaUsercmd(&oldest, &oldcmd);
	    MSG_ReadDeltaUsercmd(&oldcmd, &newcmd);

	    if (client->state != cs_spawned)
		break;

	    /* if the checksum fails, ignore the rest of the packet */
	    crc_buf = net_message.data + crc_offset + 1;
	    length = MSG_GetReadCount() - crc_offset - 1;
	    crc = COM_BlockSequenceCRCByte(crc_buf, length, seq_hash);
	    if (crc != checksum) {
		Con_DPrintf("Failed command checksum for %s(%d) (%d != %d)\n",
			    client->name, netchan->incoming_sequence,
			    checksum, crc);
		return;
	    }

	    if (!sv.paused) {
		SV_PreRunCmd();

		if (net_drop < 20) {
		    while (net_drop > 2) {
			SV_RunCmd(client, &client->lastcmd);
			net_drop--;
		    }
		    if (net_drop > 1)
			SV_RunCmd(client, &oldest);
		    if (net_drop > 0)
			SV_RunCmd(client, &oldcmd);
		}
		SV_RunCmd(client, &newcmd);

		SV_PostRunCmd(client);
	    }

	    client->lastcmd = newcmd;
	    client->lastcmd.buttons = 0;	// avoid multiple fires on lag
	    break;

	case clc_stringcmd:
	    SV_ExecuteUserCommand(MSG_ReadString(), client);
	    break;

	case clc_tmove:
	    origin[0] = MSG_ReadCoord();
	    origin[1] = MSG_ReadCoord();
	    origin[2] = MSG_ReadCoord();
	    /* only allowed by spectators */
	    if (client->spectator) {
		VectorCopy(origin, player->v.origin);
		SV_LinkEdict(player, false);
	    }
	    break;

	case clc_upload:
	    SV_NextUpload(client);
	    break;
	}
    }
}

/*
==============
SV_UserInit
==============
*/
void
SV_UserInit(void)
{
    Cvar_RegisterVariable(&cl_rollspeed);
    Cvar_RegisterVariable(&cl_rollangle);
    Cvar_RegisterVariable(&sv_spectalk);
    Cvar_RegisterVariable(&sv_mapcheck);
}
