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

#include <string.h>

#include "common.h"
#include "console.h"
#include "model.h"
#include "sys.h"
#include "zone.h"

#ifdef GLQUAKE
#include "glquake.h"
#else
#include "d_iface.h"
#include "render.h"
#endif

/*
=================
Mod_LoadSpriteFrame
=================
*/
static const void *
Mod_LoadSpriteFrame(const void *buffer, mspriteframe_t **ppframe,
		    const char *loadname, int framenum)
{
    const dspriteframe_t *dframe;
    mspriteframe_t *frame;
    int width, height, numpixels, memsize, origin[2];

    dframe = buffer;

    width = LittleLong(dframe->width);
    height = LittleLong(dframe->height);
    numpixels = width * height;
    memsize = sizeof(*frame) + R_SpriteDataSize(numpixels);

    frame = Hunk_AllocName(memsize, loadname);
    memset(frame, 0, memsize);
    *ppframe = frame;

    frame->width = width;
    frame->height = height;
    origin[0] = LittleLong(dframe->origin[0]);
    origin[1] = LittleLong(dframe->origin[1]);

    frame->up = origin[1];
    frame->down = origin[1] - height;
    frame->left = origin[0];
    frame->right = width + origin[0];

    /* Let the renderer process the pixel data as needed */
    R_SpriteDataStore(frame, loadname, framenum, (byte *)(dframe + 1));

    return (byte *)buffer + sizeof(*dframe) + numpixels;
}


/*
=================
Mod_LoadSpriteGroup
=================
*/
static const void *
Mod_LoadSpriteGroup(const void *buffer, mspritegroup_t **ppgroup,
		    const char *loadname, int framenum)
{
    const dspritegroup_t *dgroup;
    const dspriteinterval_t *dintervals;
    float *intervals;
    mspritegroup_t *group;
    int i, numframes, memsize;

    dgroup = buffer;
    buffer = dgroup + 1;

    numframes = LittleLong(dgroup->numframes);
    memsize = sizeof(*group) + numframes * sizeof(group->frames[0]);
    group = Hunk_AllocName(memsize, loadname);

    group->numframes = numframes;
    *ppgroup = group;

    dintervals = buffer;
    intervals = Hunk_AllocName(numframes * sizeof(float), loadname);
    group->intervals = intervals;

    for (i = 0; i < numframes; i++, intervals++, dintervals++) {
	*intervals = LittleFloat(dintervals->interval);
	if (*intervals <= 0.0)
	    Sys_Error("%s: interval <= 0", __func__);
    }
    buffer = dintervals;

    for (i = 0; i < numframes; i++)
	buffer = Mod_LoadSpriteFrame(buffer, &group->frames[i],
				     loadname, framenum * 100 + i);

    return buffer;
}


/*
=================
Mod_LoadSpriteModel
=================
*/
void
Mod_LoadSpriteModel(model_t *model, const void *buffer)
{
    char hunkname[HUNK_NAMELEN + 1];
    const dsprite_t *dsprite;
    msprite_t *sprite;
    int i, version, numframes, memsize;

    dsprite = buffer;
    version = LittleLong(dsprite->version);
    if (version != SPRITE_VERSION)
	Sys_Error("%s: %s has wrong version number (%i should be %i)",
		  __func__, model->name, version, SPRITE_VERSION);

    numframes = LittleLong(dsprite->numframes);
    if (numframes < 1)
	Sys_Error("%s: Invalid # of frames: %d", __func__, numframes);

    memsize = sizeof(*sprite) + numframes * sizeof(sprite->frames[0]);
    COM_FileBase(model->name, hunkname, sizeof(hunkname));
    sprite = Hunk_AllocName(memsize, hunkname);

    sprite->numframes = numframes;
    sprite->type = LittleLong(dsprite->type);
    sprite->maxwidth = LittleLong(dsprite->width);
    sprite->maxheight = LittleLong(dsprite->height);
    sprite->beamlength = LittleFloat(dsprite->beamlength);

    model->type = mod_sprite;
    model->numframes = numframes;
    model->synctype = LittleLong(dsprite->synctype);
    model->flags = 0;
    model->mins[0] = model->mins[1] = -sprite->maxwidth / 2;
    model->maxs[0] = model->maxs[1] = sprite->maxwidth / 2;
    model->mins[2] = -sprite->maxheight / 2;
    model->maxs[2] = sprite->maxheight / 2;
    model->cache.data = sprite;

    /* load the frames */
    buffer = dsprite + 1;
    for (i = 0; i < numframes; i++) {
	const dspriteframetype_t *const dframetype = buffer;
	const spriteframetype_t frametype = LittleLong(dframetype->type);
	sprite->frames[i].type = frametype;
	buffer = (byte *)buffer + sizeof(dspriteframetype_t);
	if (frametype == SPR_SINGLE) {
	    mspriteframe_t **ppframe = &sprite->frames[i].frame.frame;
	    buffer = Mod_LoadSpriteFrame(buffer, ppframe, hunkname, i);
	} else {
	    mspritegroup_t **ppgroup = &sprite->frames[i].frame.group;
	    buffer = Mod_LoadSpriteGroup(buffer, ppgroup, hunkname, i);
	}
    }
}

/*
==================
Mod_GetSpriteFrame
==================
*/
const mspriteframe_t *
Mod_GetSpriteFrame(const entity_t *entity, const msprite_t *sprite, float time)
{
    const mspriteframedesc_t *framedesc;
    const mspritegroup_t *group;
    const float *intervals;
    float fullinterval, targettime;
    int numframes, framenum;

    framenum = entity->frame;
    if (framenum >= sprite->numframes || framenum < 0) {
	Con_Printf("R_DrawSprite: no such frame %d\n", framenum);
	framenum = 0;
    }

    framedesc = &sprite->frames[framenum];
    if (framedesc->type == SPR_SINGLE)
	return framedesc->frame.frame;

    group = framedesc->frame.group;
    intervals = group->intervals;
    numframes = group->numframes;
    fullinterval = intervals[numframes - 1];

    /*
     * when loading in Mod_LoadSpriteGroup, we guaranteed all interval
     * values are positive, so we don't have to worry about division by 0
     */
    targettime = time - ((int)(time / fullinterval)) * fullinterval;
    for (framenum = 0; framenum < numframes - 1; framenum++)
	if (intervals[framenum] > targettime)
	    break;

    return group->frames[framenum];
}
