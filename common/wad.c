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
// wad.c

#include "common.h"
#include "quakedef.h"
#include "sys.h"
#include "wad.h"

/*
==================
W_CleanupName

Lowercases name and pads with spaces and a terminating 0 to the length of
lumpinfo_t->name.
Used so lumpname lookups can proceed rapidly by comparing 4 chars at a time
Space padding is so names can be printed nicely in tables.
Can safely be performed in place.
==================
*/
void
W_CleanupName(const char *in, char *out)
{
    int i;
    int c;

    for (i = 0; i < LUMP_NAMELEN - 1; i++) {
	c = in[i];
	if (!c)
	    break;

	if (c >= 'A' && c <= 'Z')
	    c += ('a' - 'A');
	out[i] = c;
    }

    for (; i < LUMP_NAMELEN; i++)
	out[i] = 0;
}


/*
====================
W_LoadWadFile
====================
*/
void
W_LoadWadFile(wad_t *wad, const char *filename)
{
    lumpinfo_t *lump;
    wadinfo_t *header;
    unsigned i;
    int infotableofs;

    wad->base = COM_LoadHunkFile(filename);
    if (!wad->base)
	Sys_Error("%s: couldn't load %s", __func__, filename);

    header = (wadinfo_t *)wad->base;

    if (header->identification[0] != 'W'
	|| header->identification[1] != 'A'
	|| header->identification[2] != 'D'
	|| header->identification[3] != '2')
	Sys_Error("Wad file %s doesn't have WAD2 id", filename);

    wad->numlumps = LittleLong(header->numlumps);
    infotableofs = LittleLong(header->infotableofs);
    wad->lumps = (lumpinfo_t *)(wad->base + infotableofs);

    for (i = 0, lump = wad->lumps; i < wad->numlumps; i++, lump++) {
	lump->filepos = LittleLong(lump->filepos);
	lump->size = LittleLong(lump->size);
	W_CleanupName(lump->name, lump->name);
	if (lump->type == TYP_QPIC)
	    SwapDPic((dpic8_t *)(wad->base + lump->filepos));
    }
}


/*
=============
W_GetLumpinfo
=============
*/
lumpinfo_t *
W_GetLumpinfo(wad_t *wad, const char *name)
{
    int i;
    lumpinfo_t *lump;
    char clean[LUMP_NAMELEN];

    W_CleanupName(name, clean);

    for (lump = wad->lumps, i = 0; i < wad->numlumps; i++, lump++) {
	if (!strcmp(clean, lump->name))
	    return lump;
    }

    Sys_Error("%s: %s not found", __func__, name);
}

void *
W_GetLumpName(wad_t *wad, const char *name)
{
    lumpinfo_t *lump;

    lump = W_GetLumpinfo(wad, name);

    return wad->base + lump->filepos;
}

void *
W_GetLumpNum(wad_t *wad, int num)
{
    lumpinfo_t *lump;

    if (num < 0 || num >= wad->numlumps)
	Sys_Error("%s: bad number: %i", __func__, num);

    lump = wad->lumps + num;

    return wad->base + lump->filepos;
}

/*
=============================================================================

automatic byte swapping

=============================================================================
*/

void
SwapDPic(dpic8_t *dpic)
{
    dpic->width = LittleLong(dpic->width);
    dpic->height = LittleLong(dpic->height);
}
