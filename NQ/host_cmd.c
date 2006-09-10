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

#include "client.h"
#include "cmd.h"
#include "console.h"
#include "host.h"
#include "keys.h"
#include "menu.h"
#include "model.h"
#include "net.h"
#include "protocol.h"
#include "quakedef.h"
#include "screen.h"
#include "server.h"
#include "sys.h"
#include "world.h"
#include "zone.h"

int current_skill;
qboolean noclip_anglehack;

/*
==================
Host_Quit_f
==================
*/

void
Host_Quit_f(void)
{
    if (key_dest != key_console && cls.state != ca_dedicated) {
	M_Menu_Quit_f();
	return;
    }
    CL_Disconnect();
    Host_ShutdownServer(false);

    Sys_Quit();
}


/*
===============================================================================

SERVER TRANSITIONS

===============================================================================
*/


/*
======================
Host_Map_f

handle a
map <servername>
command from the console.  Active clients are kicked off.
======================
*/
static void
Host_Map_f(void)
{
    int i;
    char name[MAX_QPATH];

    if (cmd_source != src_command)
	return;

    if (Cmd_Argc() < 2) {	// no map name given
	Con_Printf ("map <levelname>: start a new server\n");
	if (cls.state == ca_dedicated) {
	    if (sv.active)
		Con_Printf ("Currently on: %s\n", sv.name);
	    else
		Con_Printf ("Server not active\n");
	} else if (cls.state >= ca_connected) {
	    Con_Printf ("Currently on: %s ( %s )\n", cl.levelname, cl.mapname);
	}
	return;
    }

    cls.demonum = -1;		// stop demo loop in case this fails

    CL_Disconnect();
    Host_ShutdownServer(false);

    key_dest = key_game;	// remove console or menu
    SCR_BeginLoadingPlaque();

    svs.serverflags = 0;	// haven't completed an episode yet
    strcpy(name, Cmd_Argv(1));

    SV_SpawnServer(name);

    if (!sv.active)
	return;

    if (cls.state != ca_dedicated) {
	strcpy(cls.spawnparms, "");

	for (i = 2; i < Cmd_Argc(); i++) {
	    strcat(cls.spawnparms, Cmd_Argv(i));
	    strcat(cls.spawnparms, " ");
	}
	Cmd_ExecuteString("connect local", src_command);
    }
}

static struct stree_root *
Host_Map_Arg_f(const char *arg)
{
    struct stree_root *root;

    root = Z_Malloc(sizeof(struct stree_root));
    if (root) {
	*root = STREE_ROOT;

	STree_AllocInit();
	COM_ScanDir(root, "maps", arg, ".bsp", true);
    }
    return root;
}

/*
==================
Host_Changelevel_f

Goes to a new map, taking all clients along
==================
*/
static void
Host_Changelevel_f(void)
{
    char level[MAX_QPATH];

    if (Cmd_Argc() != 2) {
	Con_Printf
	    ("changelevel <levelname> : continue game on a new level\n");
	return;
    }
    if (!sv.active || cls.demoplayback) {
	Con_Printf("Only the server may changelevel\n");
	return;
    }
    SV_SaveSpawnparms();
    strcpy(level, Cmd_Argv(1));
    SV_SpawnServer(level);
}

/*
==================
Host_Restart_f

Restarts the current server for a dead player
==================
*/
static void
Host_Restart_f(void)
{
    char mapname[MAX_QPATH];

    if (cls.demoplayback || !sv.active)
	return;

    if (cmd_source != src_command)
	return;
    strcpy(mapname, sv.name);	// must copy out, because it gets cleared
    // in sv_spawnserver
    SV_SpawnServer(mapname);
}

/*
==================
Host_Reconnect_f

This command causes the client to wait for the signon messages again.
This is sent just before a server changes levels
==================
*/
static void
Host_Reconnect_f(void)
{
    SCR_BeginLoadingPlaque();
    cls.signon = 0;		// need new connection messages

    // FIXME - this check is just paranoia until I understand it better
    if (cls.state < ca_connected)
	Host_Error("Host_Reconnect_f: cls.state < ca_connected");

    cls.state = ca_connected;
}

/*
=====================
Host_Connect_f

User command to connect to server
=====================
*/
static void
Host_Connect_f(void)
{
    char name[MAX_QPATH];

    cls.demonum = -1;		// stop demo loop in case this fails
    if (cls.demoplayback) {
	CL_StopPlayback();
	CL_Disconnect();
    }
    strcpy(name, Cmd_Argv(1));
    CL_EstablishConnection(name);
    Host_Reconnect_f();
}


/*
===============================================================================

LOAD / SAVE GAME

===============================================================================
*/

#define	SAVEGAME_VERSION	5

/*
===============
Host_SavegameComment

Writes a SAVEGAME_COMMENT_LENGTH character comment describing the current
===============
*/
static void
Host_SavegameComment(char *text)
{
    int i;
    char kills[20];

    for (i = 0; i < SAVEGAME_COMMENT_LENGTH; i++)
	text[i] = ' ';
    memcpy(text, cl.levelname, strlen(cl.levelname));
    sprintf(kills, "kills:%3i/%3i", cl.stats[STAT_MONSTERS],
	    cl.stats[STAT_TOTALMONSTERS]);
    memcpy(text + 22, kills, strlen(kills));
// convert space to _ to make stdio happy
    for (i = 0; i < SAVEGAME_COMMENT_LENGTH; i++)
	if (text[i] == ' ')
	    text[i] = '_';
    text[SAVEGAME_COMMENT_LENGTH] = '\0';
}


/*
===============
Host_Savegame_f
===============
*/
static void
Host_Savegame_f(void)
{
    char name[MAX_OSPATH];
    FILE *f;
    int i, length, err;
    char comment[SAVEGAME_COMMENT_LENGTH + 1];

    if (cmd_source != src_command)
	return;

    if (!sv.active) {
	Con_Printf("Not playing a local game.\n");
	return;
    }

    if (cl.intermission) {
	Con_Printf("Can't save in intermission.\n");
	return;
    }

    if (svs.maxclients != 1) {
	Con_Printf("Can't save multiplayer games.\n");
	return;
    }

    if (Cmd_Argc() != 2) {
	Con_Printf("save <savename> : save a game\n");
	return;
    }

    if (strstr(Cmd_Argv(1), "..")) {
	Con_Printf("Relative pathnames are not allowed.\n");
	return;
    }

    for (i = 0; i < svs.maxclients; i++) {
	if (svs.clients[i].active && (svs.clients[i].edict->v.health <= 0)) {
	    Con_Printf("Can't savegame with a dead player\n");
	    return;
	}
    }

    length = snprintf(name, sizeof(name), "%s/%s", com_gamedir, Cmd_Argv(1));
    err = COM_DefaultExtension(name, ".sav", name, sizeof(name));
    if (length >= sizeof(name) || err) {
	Con_Printf("ERROR: couldn't save, filename too long.\n");
	return;
    }

    Con_Printf("Saving game to %s...\n", name);
    f = fopen(name, "w");
    if (!f) {
	Con_Printf("ERROR: couldn't open.\n");
	return;
    }

    fprintf(f, "%i\n", SAVEGAME_VERSION);
    Host_SavegameComment(comment);
    fprintf(f, "%s\n", comment);
    for (i = 0; i < NUM_SPAWN_PARMS; i++)
	fprintf(f, "%f\n", svs.clients->spawn_parms[i]);
    fprintf(f, "%d\n", current_skill);
    fprintf(f, "%s\n", sv.name);
    fprintf(f, "%f\n", sv.time);

// write the light styles

    for (i = 0; i < MAX_LIGHTSTYLES; i++) {
	if (sv.lightstyles[i])
	    fprintf(f, "%s\n", sv.lightstyles[i]);
	else
	    fprintf(f, "m\n");
    }


    ED_WriteGlobals(f);
    for (i = 0; i < sv.num_edicts; i++) {
	ED_Write(f, EDICT_NUM(i));
	fflush(f);
    }
    fclose(f);
    Con_Printf("done.\n");
}


/*
===============
Host_Loadgame_f
===============
*/
static void
Host_Loadgame_f(void)
{
    char name[MAX_OSPATH];
    char mapname[MAX_QPATH];
    FILE *f;
    float time, tfloat;
    char str[32768];
    char *lightstyle;
    const char *start;
    int i, r, length, err;
    edict_t *ent;
    int entnum;
    int version;
    float spawn_parms[NUM_SPAWN_PARMS];

    if (cmd_source != src_command)
	return;

    if (Cmd_Argc() != 2) {
	Con_Printf("load <savename> : load a game\n");
	return;
    }

    cls.demonum = -1;		// stop demo loop in case this fails

    length = snprintf(name, sizeof(name), "%s/%s", com_gamedir, Cmd_Argv(1));
    err = COM_DefaultExtension(name, ".sav", name, sizeof(name));
    if (length >= sizeof(name) || err) {
	Con_Printf("ERROR: couldn't open save game, filename too long.\n");
	return;
    }

// we can't call SCR_BeginLoadingPlaque, because too much stack space has
// been used.  The menu calls it before stuffing loadgame command
//      SCR_BeginLoadingPlaque();

    Con_Printf("Loading game from %s...\n", name);
    f = fopen(name, "r");
    if (!f) {
	Con_Printf("ERROR: couldn't open.\n");
	return;
    }

    fscanf(f, "%i\n", &version);
    if (version != SAVEGAME_VERSION) {
	fclose(f);
	Con_Printf("Savegame is version %i, not %i\n", version,
		   SAVEGAME_VERSION);
	return;
    }
    fscanf(f, "%s\n", str);
    for (i = 0; i < NUM_SPAWN_PARMS; i++)
	fscanf(f, "%f\n", &spawn_parms[i]);

    /*
     * This silliness is so we can load 1.06 save files, which have float
     * skill values
     */
    fscanf(f, "%f\n", &tfloat);
    current_skill = (int)(tfloat + 0.1);
    Cvar_SetValue("skill", (float)current_skill);

    fscanf(f, "%s\n", mapname);
    fscanf(f, "%f\n", &time);

    CL_Disconnect_f();

    SV_SpawnServer(mapname);

    if (!sv.active) {
	Con_Printf("Couldn't load map\n");
	return;
    }
    sv.paused = true;		// pause until all clients connect
    sv.loadgame = true;

// load the light styles

    for (i = 0; i < MAX_LIGHTSTYLES; i++) {
	fscanf(f, "%s\n", str);
	lightstyle = Hunk_Alloc(strlen(str) + 1);
	strcpy(lightstyle, str);
	sv.lightstyles[i] = lightstyle;
    }

// load the edicts out of the savegame file
    entnum = -1;		// -1 is the globals
    while (!feof(f)) {
	for (i = 0; i < sizeof(str) - 1; i++) {
	    r = fgetc(f);
	    if (r == EOF || !r)
		break;
	    str[i] = r;
	    if (r == '}') {
		i++;
		break;
	    }
	}
	if (i == sizeof(str) - 1)
	    Sys_Error("Loadgame buffer overflow");
	str[i] = 0;
	start = COM_Parse(str);
	if (!com_token[0])
	    break;		// end of file
	if (strcmp(com_token, "{"))
	    Sys_Error("First token isn't a brace");

	if (entnum == -1) {	// parse the global vars
	    ED_ParseGlobals(start);
	} else {		// parse an edict

	    ent = EDICT_NUM(entnum);
	    memset(&ent->v, 0, progs->entityfields * 4);
	    ent->free = false;
	    ED_ParseEdict(start, ent);

	    // link it into the bsp tree
	    if (!ent->free)
		SV_LinkEdict(ent, false);
	}

	entnum++;
    }

    sv.num_edicts = entnum;
    sv.time = time;

    fclose(f);

    for (i = 0; i < NUM_SPAWN_PARMS; i++)
	svs.clients->spawn_parms[i] = spawn_parms[i];

    if (cls.state != ca_dedicated) {
	CL_EstablishConnection("local");
	Host_Reconnect_f();
    }
}

//============================================================================

static void
Host_Version_f(void)
{
    Con_Printf("Version TyrQuake-%s\n", stringify(TYR_VERSION));
    Con_Printf("Exe: " __TIME__ " " __DATE__ "\n");
}

/*
===============================================================================

DEBUGGING TOOLS

===============================================================================
*/

static edict_t *
FindViewthing(void)
{
    int i;
    edict_t *e;

    for (i = 0; i < sv.num_edicts; i++) {
	e = EDICT_NUM(i);
	if (!strcmp(PR_GetString(e->v.classname), "viewthing"))
	    return e;
    }
    Con_Printf("No viewthing on map\n");
    return NULL;
}

/*
==================
Host_Viewmodel_f
==================
*/
static void
Host_Viewmodel_f(void)
{
    edict_t *e;
    model_t *m;

    e = FindViewthing();
    if (!e)
	return;

    m = Mod_ForName(Cmd_Argv(1), false);
    if (!m) {
	Con_Printf("Can't load %s\n", Cmd_Argv(1));
	return;
    }

    e->v.frame = 0;
    cl.model_precache[(int)e->v.modelindex] = m;
}

/*
==================
Host_Viewframe_f
==================
*/
static void
Host_Viewframe_f(void)
{
    edict_t *e;
    int f;
    model_t *m;

    e = FindViewthing();
    if (!e)
	return;
    m = cl.model_precache[(int)e->v.modelindex];

    f = atoi(Cmd_Argv(1));
    if (f >= m->numframes)
	f = m->numframes - 1;

    e->v.frame = f;
}


static void
PrintFrameName(model_t *m, int frame)
{
    aliashdr_t *hdr;
    maliasframedesc_t *pframedesc;

    hdr = Mod_Extradata(m);
    if (!hdr)
	return;
    pframedesc = &hdr->frames[frame];

    Con_Printf("frame %i: %s\n", frame, pframedesc->name);
}

/*
==================
Host_Viewnext_f
==================
*/
static void
Host_Viewnext_f(void)
{
    edict_t *e;
    model_t *m;

    e = FindViewthing();
    if (!e)
	return;
    m = cl.model_precache[(int)e->v.modelindex];

    e->v.frame = e->v.frame + 1;
    if (e->v.frame >= m->numframes)
	e->v.frame = m->numframes - 1;

    PrintFrameName(m, e->v.frame);
}

/*
==================
Host_Viewprev_f
==================
*/
static void
Host_Viewprev_f(void)
{
    edict_t *e;
    model_t *m;

    e = FindViewthing();
    if (!e)
	return;

    m = cl.model_precache[(int)e->v.modelindex];

    e->v.frame = e->v.frame - 1;
    if (e->v.frame < 0)
	e->v.frame = 0;

    PrintFrameName(m, e->v.frame);
}

/*
===============================================================================

DEMO LOOP CONTROL

===============================================================================
*/


/*
==================
Host_Startdemos_f
==================
*/
static void
Host_Startdemos_f(void)
{
    int i, c;

    if (cls.state == ca_dedicated) {
	if (!sv.active)
	    Cbuf_AddText("map start\n");
	return;
    }

    c = Cmd_Argc() - 1;
    if (c > MAX_DEMOS) {
	Con_Printf("Max %i demos in demoloop\n", MAX_DEMOS);
	c = MAX_DEMOS;
    }
    Con_Printf("%i demo(s) in loop\n", c);

    for (i = 1; i < c + 1; i++)
	strncpy(cls.demos[i - 1], Cmd_Argv(i), sizeof(cls.demos[0]) - 1);

    if (!sv.active && cls.demonum != -1 && !cls.demoplayback) {
	cls.demonum = 0;
	CL_NextDemo();
    } else
	cls.demonum = -1;
}


/*
==================
Host_Demos_f

Return to looping demos
==================
*/
static void
Host_Demos_f(void)
{
    if (cls.state == ca_dedicated)
	return;
    if (cls.demonum == -1)
	cls.demonum = 1;
    CL_Disconnect_f();
    CL_NextDemo();
}

/*
==================
Host_Stopdemo_f

Return to looping demos
==================
*/
static void
Host_Stopdemo_f(void)
{
    if (cls.state == ca_dedicated)
	return;
    if (!cls.demoplayback)
	return;
    CL_StopPlayback();
    CL_Disconnect();
}

//=============================================================================

/*
==================
Host_InitCommands
==================
*/
void
Host_InitCommands(void)
{
    Cmd_AddCommand("quit", Host_Quit_f);
    Cmd_AddCommand("restart", Host_Restart_f);
    Cmd_AddCommand("map", Host_Map_f);
    Cmd_AddCommand("changelevel", Host_Changelevel_f);
    Cmd_SetCompletion("map", Host_Map_Arg_f);
    Cmd_SetCompletion("changelevel", Host_Map_Arg_f);

    Cmd_AddCommand("connect", Host_Connect_f);
    Cmd_AddCommand("reconnect", Host_Reconnect_f);
    Cmd_AddCommand("version", Host_Version_f);

    Cmd_AddCommand("load", Host_Loadgame_f);
    Cmd_AddCommand("save", Host_Savegame_f);

    Cmd_AddCommand("startdemos", Host_Startdemos_f);
    Cmd_AddCommand("demos", Host_Demos_f);
    Cmd_AddCommand("stopdemo", Host_Stopdemo_f);

    Cmd_AddCommand("viewmodel", Host_Viewmodel_f);
    Cmd_AddCommand("viewframe", Host_Viewframe_f);
    Cmd_AddCommand("viewnext", Host_Viewnext_f);
    Cmd_AddCommand("viewprev", Host_Viewprev_f);
}
