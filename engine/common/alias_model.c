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
#include "crc.h"
#include "model.h"
#include "sys.h"

#ifdef GLQUAKE
#include "glquake.h"
#else
#include "r_local.h"
#endif

/*
=================
Mod_LoadAliasFrame
=================
*/
static void *
Mod_LoadAliasFrame(aliashdr_t *aliashdr, void *buffer,
		   maliasframedesc_t *frame, alias_posedata_t *posedata)
{
    const daliasframe_t *dframe = buffer;
    float *intervals = (float *)((byte *)aliashdr + aliashdr->poseintervals);
    int i;

    snprintf(frame->name, sizeof(frame->name), "%s", dframe->name);
    frame->firstpose = posedata->numposes;
    frame->numposes = 1;
    for (i = 0; i < 3; i++) {
	/* byte values only, no endian swapping */
	frame->bboxmin.v[i] = dframe->bboxmin.v[i];
	frame->bboxmax.v[i] = dframe->bboxmax.v[i];
    }
    /* interval is unused, but 999 should make problems obvious */
    intervals[posedata->numposes] = 999.0f;
    posedata->verts[posedata->numposes] = dframe->verts;
    posedata->numposes++;

    return (byte *)buffer + offsetof(daliasframe_t, verts[aliashdr->numverts]);
}


/*
=================
Mod_LoadAliasFrameGroup
=================
*/
static void *
Mod_LoadAliasFrameGroup(aliashdr_t *aliashdr, void *buffer,
			maliasframedesc_t *frame, alias_posedata_t *posedata)
{
    float *interval = (float *)((byte *)aliashdr + aliashdr->poseintervals);
    const daliasgroup_t *group = buffer;
    const daliasframe_t *dframe;
    const trivertx_t **vert;
    int i;

    frame->firstpose = posedata->numposes;
    frame->numposes = LittleLong(group->numframes);

    for (i = 0; i < 3; i++) {
	/* byte values only, no endian swapping */
	frame->bboxmin.v[i] = group->bboxmin.v[i];
	frame->bboxmax.v[i] = group->bboxmax.v[i];
    }

    /* Group frames start after the interval data */
    dframe = (const daliasframe_t *)&group->intervals[frame->numposes];
    snprintf(frame->name, sizeof(frame->name), "%s", dframe->name);

    vert = &posedata->verts[posedata->numposes];
    interval += posedata->numposes;
    for (i = 0; i < frame->numposes; i++) {
	*vert++ = dframe->verts;
	*interval = LittleFloat(group->intervals[i].interval);
	if (*interval++ <= 0)
	    Sys_Error("%s: interval <= 0", __func__);
	posedata->numposes++;
	dframe = (const daliasframe_t *)&dframe->verts[aliashdr->numverts];
    }

    /* Need to offset the buffer pointer to maintain const correctness */
    return (byte *)buffer + ((const byte *)dframe - (const byte *)buffer);
}

/*
=================
Mod_LoadAliasFrames
=================
*/
static void *
Mod_LoadAliasFrames(aliashdr_t *aliashdr, void *buffer,
		    alias_posedata_t *posedata)
{
    const int numframes = aliashdr->numframes;
    maliasframedesc_t *frame;
    int i;

    if (numframes < 1)
	Sys_Error("%s: Invalid # of frames: %d", __func__, numframes);

    posedata->numposes = 0;
    frame = aliashdr->frames;
    for (i = 0; i < numframes; i++, frame++) {
	const daliasframetype_t *const dframetype = buffer;
	const aliasframetype_t frametype = LittleLong(dframetype->type);
	buffer = (byte *)buffer + sizeof(daliasframetype_t);
	if (frametype == ALIAS_SINGLE)
	    buffer = Mod_LoadAliasFrame(aliashdr, buffer, frame, posedata);
	else
	    buffer = Mod_LoadAliasFrameGroup(aliashdr, buffer, frame, posedata);
    }

    return buffer;
}

/*
=================
Mod_LoadAliasSkin
=================
*/
static void *
Mod_LoadAliasSkin(aliashdr_t *aliashdr, void *buffer,
		  maliasskindesc_t *skin, alias_skindata_t *skindata)
{
    float *intervals = (float *)((byte *)aliashdr + aliashdr->skinintervals);
    const int skinsize = aliashdr->skinwidth * aliashdr->skinheight;

    skindata->data[skindata->numskins] = buffer;
    skin->firstframe = skindata->numskins;
    skin->numframes = 1;
    intervals[skindata->numskins] = 999.0f;
    skindata->numskins++;

    return (byte *)buffer + skinsize;
}

/*
=================
Mod_LoadAliasSkinGroup
=================
*/
static void *
Mod_LoadAliasSkinGroup(aliashdr_t *aliashdr, void *buffer,
		       maliasskindesc_t *skin, alias_skindata_t *skindata)
{
    float *interval = (float *)((byte *)aliashdr + aliashdr->skinintervals);
    const int skinsize = aliashdr->skinwidth * aliashdr->skinheight;
    const daliasskingroup_t *group = buffer;
    const daliasskininterval_t *dinterval;
    int i;

    skin->firstframe = skindata->numskins;
    skin->numframes = LittleLong(group->numskins);

    dinterval = (const daliasskininterval_t *)(group + 1);
    interval += skindata->numskins;
    for (i = 0; i < skin->numframes; i++, interval++, dinterval++) {
	*interval = LittleFloat(dinterval->interval);
	if (*interval <= 0)
	    Sys_Error("%s: interval <= 0", __func__);
	skindata->numskins++;
    }

    /* Advance the buffer pointer */
    buffer = (byte *)buffer + sizeof(*group);
    buffer = (byte *)buffer + sizeof(*dinterval) * skin->numframes;

    for (i = 0; i < skin->numframes; i++) {
	skindata->data[skin->firstframe + i] = buffer;
	buffer = (byte *)buffer + skinsize;
    }

    return buffer;
}

/*
===============
Mod_LoadAliasSkins
===============
*/
static void *
Mod_LoadAliasSkins(aliashdr_t *aliashdr, const model_loader_t *loader,
		   model_t *model, void *buffer,
		   alias_skindata_t *skindata)
{
    maliasskindesc_t *skin;
    int i;

    if (aliashdr->numskins < 1)
	Sys_Error("%s: Invalid # of skins: %d", __func__, aliashdr->numskins);
    if (aliashdr->skinwidth & 0x03)
	Sys_Error("%s: skinwidth not multiple of 4", __func__);

    skin = Mod_AllocName(aliashdr->numskins * sizeof(*skin), model->name);
    aliashdr->skindesc = (byte *)skin - (byte *)aliashdr;

    skindata->numskins = 0;
    for (i = 0; i < aliashdr->numskins; i++, skin++) {
	const daliasskintype_t *const dskintype = buffer;
	const aliasskintype_t skintype = LittleLong(dskintype->type);
	buffer = (byte *)buffer + sizeof(daliasskintype_t);
	if (skintype == ALIAS_SKIN_SINGLE)
	    buffer = Mod_LoadAliasSkin(aliashdr, buffer, skin, skindata);
	else
	    buffer = Mod_LoadAliasSkinGroup(aliashdr, buffer, skin, skindata);
    }

    /* Hand off saving the skin data to the loader */
    loader->LoadSkinData(model, aliashdr, skindata);

    return buffer;
}

static void *
Mod_LoadAliasSTVerts(const aliashdr_t *aliashdr, void *buffer,
		     alias_meshdata_t *meshdata)
{
    const stvert_t *in = buffer;
    stvert_t *out = meshdata->stverts;
    int i;

    for (i = 0; i < aliashdr->numverts; i++, in++, out++) {
	out->onseam = LittleLong(in->onseam);
	out->s = LittleLong(in->s);
	out->t = LittleLong(in->t);
    }

    return (byte *)buffer + aliashdr->numverts * sizeof(*in);
}

static void *
Mod_LoadAliasTriangles(const aliashdr_t *aliashdr, void *buffer,
		       alias_meshdata_t *meshdata, const model_t *model)
{
    const dtriangle_t *in = buffer;
    mtriangle_t *out = meshdata->triangles;
    int i, j;

    for (i = 0; i < aliashdr->numtris; i++, in++, out++) {
	out->facesfront = LittleLong(in->facesfront);
	for (j = 0; j < 3; j++) {
	    const int index = out->vertindex[j] = LittleLong(in->vertindex[j]);
	    if (index < 0 || index >= aliashdr->numverts)
		Sys_Error("%s: invalid vertex index (%d of %d) in %s\n",
			  __func__, index, aliashdr->numverts, model->name);
	}
    }

    return (byte *)buffer + aliashdr->numtris * sizeof(*in);
}

static void
Mod_AliasCRC(const model_t *model, const byte *buffer, int bufferlen)
{
#ifdef QW_HACK
    unsigned short crc;
    const char *crcmodel = NULL;

    if (!strcmp(model->name, "progs/player.mdl"))
	crcmodel = "pmodel";
    if (!strcmp(model->name, "progs/eyes.mdl"))
	crcmodel = "emodel";

    if (crcmodel) {
	crc = CRC_Block(buffer, bufferlen);
	Info_SetValueForKey(cls.userinfo, crcmodel, va("%d", (int)crc),
			    MAX_INFO_STRING);
	if (cls.state >= ca_connected) {
	    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
	    MSG_WriteStringf(&cls.netchan.message, "setinfo %s %d", crcmodel,
			     (int)crc);
	}
    }
#endif
}

/*
 * Make temporary space on the low hunk to save away various model
 * data for later processing by the driver-specific loader.
 */
void
Mod_AliasLoaderAlloc(const mdl_t *mdl, alias_meshdata_t *meshdata,
		     alias_posedata_t *posedata, alias_skindata_t *skindata)
{
    const void *buffer;
    int i, skinsize, numverts, numskins, numframes, count;

    /* Skin data follows the header */
    buffer = mdl + 1;

    /* Expand skin groups for total skin count */
    count = 0;
    numskins = LittleLong(mdl->numskins);
    skinsize = LittleLong(mdl->skinwidth) * LittleLong(mdl->skinheight);
    for (i = 0; i < numskins; i++) {
	const daliasskintype_t *const dskintype = buffer;
	const aliasskintype_t skintype = LittleLong(dskintype->type);
	buffer = (const byte *)buffer + sizeof(daliasskintype_t);
	if (skintype == ALIAS_SKIN_SINGLE) {
	    buffer = (const byte *)buffer + skinsize;
	    count++;
	} else {
	    /* skin group */
	    const daliasskingroup_t *const dskingroup = buffer;
	    const int groupskins = LittleLong(dskingroup->numskins);
	    buffer = (const byte *)buffer + sizeof(daliasskingroup_t);
	    buffer = (const byte *)buffer + groupskins * sizeof(daliasskininterval_t);
	    buffer = (const byte *)buffer + groupskins * skinsize;
	    count += groupskins;
	}
    }
    skindata->numskins = count; /* to be incremented as data is filled in */
    skindata->data = Hunk_Alloc(count * sizeof(byte *));

    /* Verticies and triangles are simple */
    numverts = LittleLong(mdl->numverts);
    buffer = (const byte *)buffer + numverts * sizeof(stvert_t);
    count = LittleLong(mdl->numtris);
    buffer = (const byte *)buffer + count * sizeof(dtriangle_t);
    meshdata->stverts = Hunk_Alloc(numverts * sizeof(*meshdata->stverts));
    meshdata->triangles = Hunk_Alloc(count * sizeof(*meshdata->triangles));

    /* Expand frame groups to get total pose count */
    count = 0;
    numframes = LittleLong(mdl->numframes);
    for (i = 0; i < numframes; i++) {
	const daliasframetype_t *const dframetype = buffer;
	const aliasframetype_t frametype = LittleLong(dframetype->type);
	buffer = (const byte *)buffer + sizeof(daliasframetype_t);
	if (frametype == ALIAS_SINGLE) {
	    buffer = &((const daliasframe_t *)buffer)->verts[numverts];
	    count++;
	} else {
	    const daliasgroup_t *const group = buffer;
	    const int groupframes = LittleLong(group->numframes);
	    const int framesize = offsetof(daliasframe_t, verts[numverts]);
	    buffer = &group->intervals[groupframes];
	    buffer = (const byte *)buffer + groupframes * framesize;
	    count += groupframes;
	}
    }
    posedata->numposes = count;
    posedata->verts = Hunk_Alloc(count * sizeof(trivertx_t *));
}

/*
=================
Mod_LoadAliasModel
=================
*/
void
Mod_LoadAliasModel(const model_loader_t *loader, model_t *model, void *buffer)
{
    const mdl_t *mdl = buffer;
    aliashdr_t *aliashdr;
    byte *membase;
    int i, version;
    int lowmark, start, end, memsize, pad;
    float *intervals;
    alias_meshdata_t meshdata;
    alias_posedata_t posedata;
    alias_skindata_t skindata;

    model->type = mod_alias;
    model->flags = LittleLong(mdl->flags);
    model->synctype = LittleLong(mdl->synctype);
    model->numframes = LittleLong(mdl->numframes);
    version = LittleLong(mdl->version);
    if (version != ALIAS_VERSION)
	Sys_Error("%s has wrong version number (%i should be %i)",
		  model->name, version, ALIAS_VERSION);

    /* Before any swapping, CRC models for QW client */
    Mod_AliasCRC(model, buffer, com_filesize);

    /* Allocate loader temporary space */
    lowmark = Hunk_LowMark();
    Mod_AliasLoaderAlloc(mdl, &meshdata, &posedata, &skindata);

    /*
     * Allocate space for the alias header, plus frame data.
     * Leave pad bytes above the header for driver specific data.
     */
    start = Hunk_LowMark();
    pad = loader->Aliashdr_Padding();
    memsize = pad + sizeof(aliashdr_t);
    memsize += LittleLong(mdl->numframes) * sizeof(aliashdr->frames[0]);
    membase = Mod_AllocName(memsize, model->name);
    aliashdr = (aliashdr_t *)(membase + pad);

    /* Space for the interval data can be allocated now */
    intervals = Mod_AllocName(posedata.numposes * sizeof(float), model->name);
    aliashdr->poseintervals = (byte *)intervals - (byte *)aliashdr;
    intervals = Mod_AllocName(skindata.numskins * sizeof(float), model->name);
    aliashdr->skinintervals = (byte *)intervals - (byte *)aliashdr;

    /* Copy and byte swap the header data */
    aliashdr->numposes = posedata.numposes;
    aliashdr->numskins = LittleLong(mdl->numskins);
    aliashdr->skinwidth = LittleLong(mdl->skinwidth);
    aliashdr->skinheight = LittleLong(mdl->skinheight);
    aliashdr->numverts = LittleLong(mdl->numverts);
    aliashdr->numtris = LittleLong(mdl->numtris);
    aliashdr->numframes = LittleLong(mdl->numframes);
    aliashdr->size = LittleFloat(mdl->size) * ALIAS_BASE_SIZE_RATIO;
    for (i = 0; i < 3; i++) {
	aliashdr->scale[i] = LittleFloat(mdl->scale[i]);
	aliashdr->scale_origin[i] = LittleFloat(mdl->scale_origin[i]);
    }

    /* Some sanity checks */
    if (aliashdr->skinheight > MAX_LBM_HEIGHT)
	Sys_Error("model %s has a skin taller than %d", model->name,
		  MAX_LBM_HEIGHT);
    if (aliashdr->numverts <= 0)
	Sys_Error("model %s has no vertices", model->name);
    if (aliashdr->numverts > MAXALIASVERTS)
	Sys_Error("model %s has too many vertices", model->name);
    if (aliashdr->numtris <= 0)
	Sys_Error("model %s has no triangles", model->name);

    /* Load the rest of the data */
    buffer = (byte *)buffer + sizeof(*mdl);
    buffer = Mod_LoadAliasSkins(aliashdr, loader, model, buffer, &skindata);
    buffer = Mod_LoadAliasSTVerts(aliashdr, buffer, &meshdata);
    buffer = Mod_LoadAliasTriangles(aliashdr, buffer, &meshdata, model);
    buffer = Mod_LoadAliasFrames(aliashdr, buffer, &posedata);

// FIXME: do this right
    model->mins[0] = model->mins[1] = model->mins[2] = -16;
    model->maxs[0] = model->maxs[1] = model->maxs[2] = 16;

    /* Get the driver to save the mesh data */
    loader->LoadMeshData(model, aliashdr, &meshdata, &posedata);

    /* move the complete, relocatable alias model to the cache */
    end = Hunk_LowMark();
    memsize = end - start;
    Cache_AllocPadded(&model->cache, pad, memsize - pad, model->name);
    if (!model->cache.data)
	return;
    memcpy((byte *)model->cache.data - pad, membase, memsize);
    model->cache.destructor = loader->CacheDestructor;

    Hunk_FreeToLowMark(lowmark);
}

/* Alias model cache */
static struct {
    model_t free;
    model_t used;
    model_t overflow;
} mcache;

void
Mod_InitAliasCache(void)
{
#define MAX_MCACHE 512 /* TODO: cvar controlled */
    int i;
    model_t *model;

    /*
     * To be allocated below host_hunklevel, so as to persist across
     * level loads. If it fills up, put extras on the overflow list...
     */
    mcache.used.next = mcache.overflow.next = NULL;
    mcache.free.next = Hunk_AllocName(MAX_MCACHE * sizeof(model_t), "mcache");

    model = mcache.free.next;
    for (i = 0; i < MAX_MCACHE - 1; i++, model++)
	model->next = model + 1;
    model->next = NULL;
}

model_t *
Mod_FindAliasName(const char *name)
{
    model_t *model;

    for (model = mcache.used.next; model; model = model->next)
	if (!strcmp(model->name, name))
	    return model;

    for (model = mcache.overflow.next; model; model = model->next)
	if (!strcmp(model->name, name))
	    return model;

    return model;
}

model_t *
Mod_NewAliasModel(void)
{
    model_t *model;

    model = mcache.free.next;
    if (model) {
	mcache.free.next = model->next;
	model->next = mcache.used.next;
	mcache.used.next = model;
    } else {
	/* TODO: warn on overflow; maybe automatically resize somehow? */
	model = Hunk_AllocName(sizeof(*model), "mcache+");
	model->next = mcache.overflow.next;
	mcache.overflow.next = model;
    }

    return model;
}

void
Mod_ClearAlias(void)
{
    model_t *model;

    /*
     * For now, only need to worry about overflow above the host
     * hunklevel which will disappear.
     */
    for (model = mcache.overflow.next; model; model = model->next)
	if (model->cache.data)
	    Cache_Free(&model->cache);
    mcache.overflow.next = NULL;
}

const model_t *
Mod_AliasCache(void)
{
    return mcache.used.next;
}

const model_t *
Mod_AliasOverflow(void)
{
    return mcache.overflow.next;
}
