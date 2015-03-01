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
#include "console.h"
#include "cmd.h"
#include "cvar.h"
#include "quakedef.h"
#include "sys.h"

cvar_t baseskin = { "baseskin", "base" };
cvar_t noskins = { "noskins", "0" };

static char allskins[128];

#define	MAX_CACHED_SKINS		128
static skin_t skins[MAX_CACHED_SKINS];
static int numskins;

/*
================
Skin_Find

  Determines the best skin for the given scoreboard
  slot, and sets scoreboard->skin

================
*/
void
Skin_Find(player_info_t *player)
{
    skin_t *skin;
    int i;
    char name[MAX_QPATH];
    const char *skinname;

    skinname = allskins;
    if (!skinname[0]) {
	skinname = Info_ValueForKey(player->userinfo, "skin");
	if (!skinname || !skinname[0])
	    skinname = baseskin.string;
    }
    if (strstr(skinname, "..") || skinname[0] == '.')
	skinname = "base";

    COM_StripExtension(skinname, name, sizeof(name));

    for (i = 0; i < numskins; i++) {
	if (!strcmp(name, skins[i].name)) {
	    player->skin = &skins[i];
	    Skin_Cache(player->skin);
	    return;
	}
    }

    if (numskins == MAX_CACHED_SKINS) {	// ran out of spots, so flush everything
	Skin_Skins_f();
	return;
    }

    skin = &skins[numskins];
    player->skin = skin;
    numskins++;

    memset(skin, 0, sizeof(*skin));
    snprintf(skin->name, sizeof(skin->name), "%s", name);
}


/*
==========
Skin_Cache

Returns a pointer to the skin bitmap, or NULL to use the default
==========
*/
byte *
Skin_Cache(skin_t * skin)
{
    char name[MAX_QPATH];
    byte *raw, *out, *pix;
    pcx_t *pcx;
    int x, y;
    int dataByte;
    int runLength;

    if (cls.downloadtype == dl_skin)
	return NULL;		// use base until downloaded

    if (noskins.value == 1)	// JACK: So NOSKINS > 1 will show skins, but
	return NULL;		// not download new ones.

    if (skin->failedload)
	return NULL;

    out = Cache_Check(&skin->cache);
    if (out)
	return out;

//
// load the pic from disk
//
    snprintf(name, sizeof(name), "skins/%s.pcx", skin->name);
    pcx = COM_LoadTempFile(name);
    if (!pcx) {
	Con_Printf("Couldn't load skin %s\n", name);
	snprintf(name, sizeof(name), "skins/%s.pcx", baseskin.string);
	pcx = COM_LoadTempFile(name);
	if (!pcx)
	    goto Fail;
    }
//
// parse the PCX file
//
    if (pcx->manufacturer != 0x0a
	|| pcx->version != 5
	|| pcx->encoding != 1
	|| pcx->bits_per_pixel != 8 || pcx->xmax >= 320 || pcx->ymax >= 200) {
	Con_Printf("Bad skin %s\n", name);
	goto Fail;
    }

    out = Cache_Alloc(&skin->cache, 320 * 200, skin->name);
    if (!out)
	Sys_Error("Skin_Cache: couldn't allocate");

    raw = &pcx->data;
    pix = out;
    memset(out, 0, 320 * 200);

    for (y = 0; y < pcx->ymax; y++, pix += 320) {
	for (x = 0; x <= pcx->xmax;) {
	    if (raw - (byte *)pcx > com_filesize)
		goto Fail_Malformed;

	    dataByte = *raw++;

	    if ((dataByte & 0xC0) == 0xC0) {
		runLength = dataByte & 0x3F;
		if (raw - (byte *)pcx > com_filesize)
		    goto Fail_Malformed;

		dataByte = *raw++;
	    } else
		runLength = 1;

	    // skin sanity check
	    if (runLength + x > pcx->xmax + 2)
		goto Fail_Malformed;

	    while (runLength-- > 0)
		pix[x++] = dataByte;
	}

    }

    if (raw - (byte *)pcx > com_filesize)
	goto Fail_Malformed;

    skin->failedload = false;
    return out;

 Fail_Malformed:
    Con_Printf("Skin %s was malformed.  You should delete it.\n", name);
    Cache_Free(&skin->cache);
 Fail:
    skin->failedload = true;
    return NULL;
}


/*
=================
Skin_NextDownload
=================
*/
void
Skin_NextDownload(void)
{
    player_info_t *player;
    int i;

    if (cls.downloadnumber == 0)
	Con_Printf("Checking skins...\n");
    cls.downloadtype = dl_skin;

    for (; cls.downloadnumber != MAX_CLIENTS; cls.downloadnumber++) {
	player = &cl.players[cls.downloadnumber];
	if (!player->name[0])
	    continue;
	Skin_Find(player);
	if (noskins.value)
	    continue;
	if (!CL_CheckOrDownloadFile(va("skins/%s.pcx", player->skin->name)))
	    return;		// started a download
    }

    cls.downloadtype = dl_none;

    // now load them in for real
    for (i = 0; i < MAX_CLIENTS; i++) {
	player = &cl.players[i];
	if (!player->name[0])
	    continue;
	Skin_Cache(player->skin);
#ifdef GLQUAKE
	player->skin = NULL;
#endif
    }

    if (cls.state != ca_active) {	// get next signon phase
	MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
	MSG_WriteStringf(&cls.netchan.message, "begin %i", cl.servercount);
	Cache_Report();		// print remaining memory
    }
}


/*
==========
Skin_Skins_f

Refind all skins, downloading if needed.
==========
*/
void
Skin_Skins_f(void)
{
    int i;

    for (i = 0; i < numskins; i++) {
	if (skins[i].cache.data)
	    Cache_Free(&skins[i].cache);
    }
    numskins = 0;

    cls.downloadnumber = 0;
    cls.downloadtype = dl_skin;
    Skin_NextDownload();
}


/*
==========
Skin_AllSkins_f

Sets all skins to one specific one
==========
*/
void
Skin_AllSkins_f(void)
{
    strcpy(allskins, Cmd_Argv(1));
    Skin_Skins_f();
}
