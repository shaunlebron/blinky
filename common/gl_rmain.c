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
// r_main.c

#include "console.h"
#include "glquake.h"
#include "mathlib.h"
#include "model.h"
#include "quakedef.h"
#include "render.h"
#include "screen.h"
#include "sound.h"
#include "sys.h"
#include "vid.h"
#include "view.h"

entity_t r_worldentity;
qboolean r_cache_thrash;	// compatability

vec3_t r_entorigin;
int r_visframecount;		// bumped when going to a new PVS
int r_framecount;		// used for dlight push checking

static mplane_t frustum[4];

int c_lightmaps_uploaded;
int c_brush_polys;
static int c_alias_polys;

qboolean envmap;		// true during envmap command capture

GLuint currenttexture = -1;	// to avoid unnecessary texture sets
GLuint playertextures[MAX_CLIENTS];// up to 16 color translated skins

int mirrortexturenum;		// quake texturenum, not gltexturenum
qboolean mirror;
mplane_t *mirror_plane;

//
// view origin
//
vec3_t vup;
vec3_t vpn;
vec3_t vright;
vec3_t r_origin;

float r_world_matrix[16];

#ifdef NQ_HACK /* Mirrors disabled for now in QW */
static float r_base_world_matrix[16];
#endif

//
// screen size info
//
refdef_t r_refdef;

mleaf_t *r_viewleaf, *r_oldviewleaf;
texture_t *r_notexture_mip;
int d_lightstylevalue[256];	// 8.8 fraction of base light value

cvar_t r_norefresh = { "r_norefresh", "0" };
cvar_t r_drawentities = { "r_drawentities", "1" };
cvar_t r_drawviewmodel = { "r_drawviewmodel", "1" };
cvar_t r_speeds = { "r_speeds", "0" };
cvar_t r_lightmap = { "r_lightmap", "0" };
cvar_t r_shadows = { "r_shadows", "0" };
cvar_t r_mirroralpha = { "r_mirroralpha", "1" };
cvar_t r_wateralpha = { "r_wateralpha", "1", true };
cvar_t r_dynamic = { "r_dynamic", "1" };
cvar_t r_novis = { "r_novis", "0" };
#ifdef QW_HACK
cvar_t r_netgraph = { "r_netgraph", "0" };
#endif
cvar_t r_waterwarp = { "r_waterwarp", "1" };

cvar_t r_fullbright = {
    .name = "r_fullbright",
    .string = "0",
    .flags = CVAR_DEVELOPER
};
cvar_t gl_keeptjunctions = {
    .name = "gl_keeptjunctions",
    .string = "1",
    .flags = CVAR_OBSOLETE
};
cvar_t gl_reporttjunctions = {
    .name = "gl_reporttjunctions",
    .string = "0",
    .flags = CVAR_OBSOLETE
};
cvar_t gl_texsort = {
    .name = "gl_texsort",
    .string = "1",
    .flags = CVAR_OBSOLETE
};

cvar_t gl_finish = { "gl_finish", "0" };
cvar_t gl_clear = { "gl_clear", "0" };
cvar_t gl_cull = { "gl_cull", "1" };
cvar_t gl_smoothmodels = { "gl_smoothmodels", "1" };
cvar_t gl_affinemodels = { "gl_affinemodels", "0" };
cvar_t gl_polyblend = { "gl_polyblend", "1" };
cvar_t gl_flashblend = { "gl_flashblend", "1" };
cvar_t gl_playermip = { "gl_playermip", "0" };
cvar_t gl_nocolors = { "gl_nocolors", "0" };
cvar_t gl_zfix = { "gl_zfix", "0" };
#ifdef NQ_HACK
cvar_t gl_doubleeyes = { "gl_doubleeyes", "1" };
#endif

cvar_t _gl_allowgammafallback = { "_gl_allowgammafallback", "1" };

#ifdef NQ_HACK
/*
 * incomplete model interpolation support
 * -> default to off and don't save to config for now
 */
cvar_t r_lerpmodels = { "r_lerpmodels", "0", false };
cvar_t r_lerpmove = { "r_lerpmove", "0", false };
#endif

/*
=================
R_CullBox

Returns true if the box is completely outside the frustum
=================
*/
qboolean
R_CullBox(const vec3_t mins, const vec3_t maxs)
{
    int i;

    for (i = 0; i < 4; i++)
	/* Not using macro since frustum planes generally not axis aligned */
	if (BoxOnPlaneSide(mins, maxs, &frustum[i]) == 2)
	    return true;
    return false;
}


void
R_RotateForEntity(const vec3_t origin, const vec3_t angles)
{
    glTranslatef(origin[0], origin[1], origin[2]);

    glRotatef(angles[1], 0, 0, 1);
    glRotatef(-angles[0], 0, 1, 0);
    glRotatef(angles[2], 1, 0, 0);
}

/*
=============================================================

  SPRITE MODELS

=============================================================
*/

typedef struct {
    GLuint texture;
    byte pixels[];
} gl_spritedata_t;

int R_SpriteDataSize(int numpixels)
{
    return offsetof(gl_spritedata_t, pixels[numpixels]);
}

void R_SpriteDataStore(mspriteframe_t *frame, const char *modelname,
		       int framenum, byte *pixels)
{
    char name[MAX_QPATH];
    gl_spritedata_t *spritedata;
    qpic8_t pic;

    spritedata = (gl_spritedata_t *)frame->rdata;
    memcpy(spritedata->pixels, pixels, frame->width * frame->height);

    pic.width = pic.stride = frame->width;
    pic.height = frame->height;
    pic.pixels = pixels;

    snprintf(name, sizeof(name), "%s_%i", modelname, framenum);
    spritedata->texture = GL_LoadTexture_Alpha(name, &pic, true, 255);
}

/*
=================
R_DrawSpriteModel

=================
*/
static void
R_DrawSpriteModel(const entity_t *entity)
{
    const msprite_t *sprite;
    const mspriteframe_t *frame;
    const float *up, *right;
    const gl_spritedata_t *spritedata;
    vec3_t point, v_forward, v_right, v_up;

    sprite = entity->model->cache.data;
    frame = Mod_GetSpriteFrame(entity, sprite, cl.time + entity->syncbase);
    spritedata = (gl_spritedata_t *)frame->rdata;

    /*
     * Don't even bother culling, because it's just a single polygon
     * without a surface cache
     */
    if (sprite->type == SPR_ORIENTED) {
	/* bullet marks on walls */
	AngleVectors(entity->angles, v_forward, v_right, v_up);
	up = v_up;
	right = v_right;
    } else {
	/* normal sprite */
	up = vup;
	right = vright;
    }

    glColor3f(1, 1, 1);

    GL_DisableMultitexture();
    GL_Bind(spritedata->texture);

    glEnable(GL_ALPHA_TEST);
    glBegin(GL_QUADS);

    glTexCoord2f(0, 1);
    VectorMA(entity->origin, frame->down, up, point);
    VectorMA(point, frame->left, right, point);
    glVertex3fv(point);

    glTexCoord2f(0, 0);
    VectorMA(entity->origin, frame->up, up, point);
    VectorMA(point, frame->left, right, point);
    glVertex3fv(point);

    glTexCoord2f(1, 0);
    VectorMA(entity->origin, frame->up, up, point);
    VectorMA(point, frame->right, right, point);
    glVertex3fv(point);

    glTexCoord2f(1, 1);
    VectorMA(entity->origin, frame->down, up, point);
    VectorMA(point, frame->right, right, point);
    glVertex3fv(point);

    glEnd();

    glDisable(GL_ALPHA_TEST);
}

/*
=============================================================

  ALIAS MODELS

=============================================================
*/

#define NUMVERTEXNORMALS 162

float r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

static vec3_t shadevector;
static float shadelight, ambientlight;

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
static float r_avertexnormal_dots[SHADEDOT_QUANT][256] =
#include "anorm_dots.h"
     ;

static float *shadedots = r_avertexnormal_dots[0];

static int lastposenum;


/*
 * Model Loader Functions
 */
static int GL_Aliashdr_Padding(void) { return offsetof(gl_aliashdr_t, ahdr); }

/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes - Ed
=================
*/

typedef struct {
    short x, y;
} floodfill_t;

// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP( off, dx, dy ) \
{ \
	if (pos[off] == fillcolor) \
	{ \
		pos[off] = 255; \
		fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
		inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
	} \
	else if (pos[off] != 255) fdc = pos[off]; \
}

static void
GL_FloodFillSkin(byte *skin, int skinwidth, int skinheight)
{
    byte fillcolor = *skin;	// assume this is the pixel to fill
    floodfill_t fifo[FLOODFILL_FIFO_SIZE];
    int inpt = 0, outpt = 0;
    int filledcolor = -1;
    int i;

    if (filledcolor == -1) {
	filledcolor = 0;
	// attempt to find opaque black
	for (i = 0; i < 256; ++i)
	    if (d_8to24table[i] == (255 << 0))	// alpha 1.0
	    {
		filledcolor = i;
		break;
	    }
    }
    // can't fill to filled color or to transparent color (used as visited marker)
    if ((fillcolor == filledcolor) || (fillcolor == 255)) {
	//printf( "not filling skin from %d to %d\n", fillcolor, filledcolor );
	return;
    }

    fifo[inpt].x = 0, fifo[inpt].y = 0;
    inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

    while (outpt != inpt) {
	int x = fifo[outpt].x, y = fifo[outpt].y;
	int fdc = filledcolor;
	byte *pos = &skin[x + skinwidth * y];

	outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

	if (x > 0)
	    FLOODFILL_STEP(-1, -1, 0);
	if (x < skinwidth - 1)
	    FLOODFILL_STEP(1, 1, 0);
	if (y > 0)
	    FLOODFILL_STEP(-skinwidth, 0, -1);
	if (y < skinheight - 1)
	    FLOODFILL_STEP(skinwidth, 0, 1);
	skin[x + skinwidth * y] = fdc;
    }
}

static void
GL_LoadSkinData(model_t *model, aliashdr_t *aliashdr,
		const alias_skindata_t *skindata)
{
    char hunkname[HUNK_NAMELEN + 1];
    int i, skinsize;
    GLuint *textures;
    byte *pixels;
    qpic8_t pic;

    skinsize = aliashdr->skinwidth * aliashdr->skinheight;
    pixels = Mod_AllocName(skindata->numskins * skinsize, model->name);
    aliashdr->skindata = (byte *)pixels - (byte *)aliashdr;
    textures = Mod_AllocName(skindata->numskins * sizeof(GLuint), model->name);
    GL_Aliashdr(aliashdr)->textures = (byte *)textures - (byte *)aliashdr;

    for (i = 0; i < skindata->numskins; i++) {
	GL_FloodFillSkin(skindata->data[i], aliashdr->skinwidth, aliashdr->skinheight);
	memcpy(pixels, skindata->data[i], skinsize);
	pixels += skinsize;

	pic.width = pic.stride = aliashdr->skinwidth;
	pic.height = aliashdr->skinheight;
	pic.pixels = skindata->data[i];

	COM_FileBase(model->name, hunkname, sizeof(hunkname));
	textures[i] = GL_LoadTexture(va("%s_%i", hunkname, i), &pic, true);
    }
}

static model_loader_t GL_Model_Loader = {
    .Aliashdr_Padding = GL_Aliashdr_Padding,
    .LoadSkinData = GL_LoadSkinData,
    .LoadMeshData = GL_LoadMeshData,
    .CacheDestructor = NULL,
};

const model_loader_t *
R_ModelLoader(void)
{
    return &GL_Model_Loader;
}

/*
=============
GL_AliasDrawModel
=============
*/
static void
GL_AliasDrawModel(const entity_t *entity, float blend)
{
    aliashdr_t *aliashdr;
    const trivertx_t *vertbase, *verts1;
    const int *order;
    int count;

    lastposenum = entity->currentpose;

    aliashdr = Mod_Extradata(entity->model);
    vertbase = (trivertx_t *)((byte *)aliashdr + aliashdr->posedata);
    verts1 = vertbase + entity->currentpose * aliashdr->numverts;
    order = (int *)((byte *)aliashdr + GL_Aliashdr(aliashdr)->commands);

#ifdef NQ_HACK
    if (r_lerpmodels.value && blend != 1.0f) {
	const trivertx_t *lightvert;
	const trivertx_t *verts0;
	const float blend0 = 1.0f - blend;

	verts0 = vertbase + entity->previouspose * aliashdr->numverts;
	lightvert = (blend < 0.5f) ? verts0 : verts1;

	while (1) {
	    /* get the vertex count and primitive type */
	    count = *order++;
	    if (!count)
		break;
	    if (count < 0) {
		count = -count;
		glBegin(GL_TRIANGLE_FAN);
	    } else {
		glBegin(GL_TRIANGLE_STRIP);
	    }

	    while (count--) {
		vec3_t glvertex;
		float light;

		/* texture coordinates come from the draw list */
		glTexCoord2f(((float *)order)[0], ((float *)order)[1]);
		order += 2;

		/* normals and vertexes come from the frame list */
		light = shadedots[lightvert->lightnormalindex] * shadelight;
		if (r_fullbright.value)
		    light = 255.0f;
		glColor3f(light, light, light);
		glvertex[0] = verts0->v[0] * blend0 + verts1->v[0] * blend;
		glvertex[1] = verts0->v[1] * blend0 + verts1->v[1] * blend;
		glvertex[2] = verts0->v[2] * blend0 + verts1->v[2] * blend;
		glVertex3f(glvertex[0], glvertex[1], glvertex[2]);
		verts0++;
		verts1++;
		lightvert++;
	    }
	    glEnd();
	}
	return;
    }
#endif
    while (1) {
	/* get the vertex count and primitive type */
	count = *order++;
	if (!count)
	    break;
	if (count < 0) {
	    count = -count;
	    glBegin(GL_TRIANGLE_FAN);
	} else {
	    glBegin(GL_TRIANGLE_STRIP);
	}

	while (count--) {
	    float light;

	    /* texture coordinates come from the draw list */
	    glTexCoord2f(((float *)order)[0], ((float *)order)[1]);
	    order += 2;

	    /* normals and vertexes come from the frame list */
	    light = shadedots[verts1->lightnormalindex] * shadelight;
	    if (r_fullbright.value)
		light = 255.0f;
	    glColor3f(light, light, light);
	    glVertex3f(verts1->v[0], verts1->v[1], verts1->v[2]);
	    verts1++;
	}
	glEnd();
    }
}

/*
=============
GL_AliasDrawShadow
=============
*/
static void
GL_AliasDrawShadow(const entity_t *entity, aliashdr_t *paliashdr, int posenum)
{
    trivertx_t *verts;
    int *order;
    vec3_t point;
    float height, lheight;
    int count;

    lheight = entity->origin[2] - lightspot[2];
    height = -lheight + 1.0;

    verts = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
    verts += posenum * paliashdr->numverts;
    order = (int *)((byte *)paliashdr + GL_Aliashdr(paliashdr)->commands);

    while (1) {
	// get the vertex count and primitive type
	count = *order++;
	if (!count)
	    break;		// done
	if (count < 0) {
	    count = -count;
	    glBegin(GL_TRIANGLE_FAN);
	} else
	    glBegin(GL_TRIANGLE_STRIP);

	do {
	    // texture coordinates come from the draw list
	    // (skipped for shadows) glTexCoord2fv ((float *)order);
	    order += 2;

	    // normals and vertexes come from the frame list
	    point[0] =
		verts->v[0] * paliashdr->scale[0] +
		paliashdr->scale_origin[0];
	    point[1] =
		verts->v[1] * paliashdr->scale[1] +
		paliashdr->scale_origin[1];
	    point[2] =
		verts->v[2] * paliashdr->scale[2] +
		paliashdr->scale_origin[2];

	    point[0] -= shadevector[0] * (point[2] + lheight);
	    point[1] -= shadevector[1] * (point[2] + lheight);
	    point[2] = height;
//                      height -= 0.001;
	    glVertex3fv(point);

	    verts++;
	} while (--count);

	glEnd();
    }
}


/*
===============
R_AliasSetupSkin
===============
*/
static void
R_AliasSetupSkin(const entity_t *entity, aliashdr_t *aliashdr)
{
    const maliasskindesc_t *skindesc;
    const float *intervals;
    int skinnum, numframes, frame;
    GLuint *textures;

    skinnum = entity->skinnum;
    if ((skinnum >= aliashdr->numskins) || (skinnum < 0)) {
	Con_DPrintf("%s: %s has no such skin (%d)\n",
		    __func__, entity->model->name, skinnum);
	skinnum = 0;
    }

    skindesc = (maliasskindesc_t *)((byte *)aliashdr + aliashdr->skindesc);
    skindesc += skinnum;
    frame = skindesc->firstframe;
    numframes = skindesc->numframes;

    if (numframes > 1) {
	const float frametime = cl.time + entity->syncbase;
	intervals = (float *)((byte *)aliashdr + aliashdr->skinintervals);
	frame += Mod_FindInterval(intervals + frame, numframes, frametime);
    }

    textures = (GLuint *)((byte *)aliashdr + GL_Aliashdr(aliashdr)->textures);
    GL_Bind(textures[frame]);
}

/*
=================
R_AliasSetupFrame

=================
*/
static void
R_AliasSetupFrame(entity_t *entity, aliashdr_t *aliashdr)
{
    int framenum, pose, numposes;
    const float *intervals;

    framenum = entity->frame;
    if ((framenum >= aliashdr->numframes) || (framenum < 0)) {
	Con_DPrintf("%s: no such frame %d\n", __func__, framenum);
	framenum = 0;
    }

    pose = aliashdr->frames[framenum].firstpose;
    numposes = aliashdr->frames[framenum].numposes;

    if (numposes > 1) {
	const float frametime = cl.time + entity->syncbase;
	intervals = (float *)((byte *)aliashdr + aliashdr->poseintervals);
	pose += Mod_FindInterval(intervals + pose, numposes, frametime);
    }

#ifdef NQ_HACK
    if (r_lerpmodels.value) {
	const maliasframedesc_t *currentframe, *previousframe;
	float delta, time, blend;

	/* A few quick sanity checks to abort lerping */
	if (entity->currentframetime < entity->previousframetime)
	    goto nolerp;
	if (entity->currentframetime - entity->previousframetime > 1.0f)
	    goto nolerp;
	/* FIXME - hack to skip the viewent (weapon) */
	if (entity == &cl.viewent)
	    goto nolerp;

	currentframe = &aliashdr->frames[entity->currentframe];
	if (numposes > 1) {
	    /* FIXME - merge with Mod_FindInterval? */
	    int i;
	    float fullinterval, targettime;
	    fullinterval = intervals[numposes - 1];
	    time = cl.time + entity->syncbase;
	    targettime = time - (int)(time / fullinterval) * fullinterval;
	    for (i = 0; i < numposes - 1; i++)
		if (intervals[i] > targettime)
		    break;

	    entity->currentpose = currentframe->firstpose + i;
	    if (i == 0) {
		entity->previouspose = currentframe->firstpose + numposes - 1;
		time = targettime;
		delta = intervals[0];
	    } else {
		entity->previouspose = entity->currentpose - 1;
		time = targettime - intervals[i - 1];
		delta = intervals[i] - intervals[i - 1];
	    }
	} else {
	    previousframe = &aliashdr->frames[entity->previousframe];
	    entity->currentpose = currentframe->firstpose;
	    entity->previouspose = previousframe->firstpose;
	    time = cl.time - entity->currentframetime;
	    delta = entity->currentframetime - entity->previousframetime;
	}
	blend = qclamp(time / delta, 0.0f, 1.0f);

	GL_AliasDrawModel(entity, blend);

	return;
    }
 nolerp:
#endif
    entity->currentpose = pose;
    entity->previouspose = pose;

    GL_AliasDrawModel(entity, 1.0f);
}


/*
=================
R_AliasCalcLerp
=================
*/
static void
R_AliasCalcLerp(entity_t *entity, vec3_t origin, vec3_t angles)
{
#ifdef NQ_HACK
    /* FIXME - hack to skip viewent (weapon) lerp */
    if (entity == &cl.viewent)
	goto nolerp_origin;

    /* Origin LERP */
    if (r_lerpmove.value) {
	float delta, frac;
	vec3_t lerpvec;

	delta = entity->currentorigintime - entity->previousorigintime;
	frac = qclamp((cl.time - entity->currentorigintime) / delta, 0.0, 1.0);
	VectorSubtract(entity->currentorigin, entity->previousorigin, lerpvec);
	VectorMA(entity->previousorigin, frac, lerpvec, origin);
    } else {
 nolerp_origin:
	VectorCopy(entity->origin, origin);
    }

    /* FIXME - hack to skip the viewent (weapon) */
    if (entity == &cl.viewent)
	goto nolerp_angles;

    /* Angles lerp */
    if (entity->previousanglestime == entity->currentanglestime)
	goto nolerp_angles;
    if (r_lerpmove.value) {
	float delta, frac;
	vec3_t lerpvec;
	int i;

	delta = entity->currentanglestime - entity->previousanglestime;
	frac = qclamp((cl.time - entity->currentanglestime) / delta, 0.0, 1.0);
	VectorSubtract(entity->currentangles, entity->previousangles, lerpvec);
	for (i = 0; i < 3; i++) {
	    if (lerpvec[i] > 180.0f)
		lerpvec[i] -= 360.0f;
	    else if (lerpvec[i] < -180.0f)
		lerpvec[i] += 360.0f;
	}
	VectorMA(entity->previousangles, frac, lerpvec, angles);
	//angles[PITCH] = -angles[PITCH];
    } else {
    nolerp_angles:
	VectorCopy(entity->angles, angles);
    }
#endif
#ifdef QW_HACK
    VectorCopy(entity->origin, origin);
    VectorCopy(entity->angles, angles);
#endif
}

static void
R_AliasCalcLight(const entity_t *entity,
		 const vec3_t origin, const vec3_t angles)
{
    const model_t *model = entity->model;
    const dlight_t *dlight;
    int i, shadequant;
    float angle;

    ambientlight = shadelight = R_LightPoint(origin);

    /* always give the viewmodel some light */
    if (entity == &cl.viewent && ambientlight < 24)
	ambientlight = shadelight = 24;

    dlight = cl_dlights;
    for (i = 0; i < MAX_DLIGHTS; i++, dlight++) {
	if (dlight->die >= cl.time) {
	    vec3_t lightvec;
	    vec_t add;

	    VectorSubtract(origin, dlight->origin, lightvec);
	    add = dlight->radius - Length(lightvec);
	    if (add > 0) {
		ambientlight += add;
		shadelight += add;
	    }
	}
    }

    // clamp lighting so it doesn't overbright as much
    if (ambientlight > 128)
	ambientlight = 128;
    if (ambientlight + shadelight > 192)
	shadelight = 192 - ambientlight;

    // ZOID: never allow players to go totally black
#ifdef NQ_HACK
    if (CL_PlayerEntity(entity)) {
#endif
#ifdef QW_HACK
    if (!strcmp(model->name, "progs/player.mdl")) {
#endif
	if (ambientlight < 8)
	    ambientlight = shadelight = 8;
    } else if (!strcmp(model->name, "progs/flame.mdl")
	       || !strcmp(model->name, "progs/flame2.mdl")) {
	// HACK HACK HACK -- no fullbright colors, so make torches full light
	ambientlight = shadelight = 256;
    }

    shadequant = (int)(angles[1] * (SHADEDOT_QUANT / 360.0));
    shadedots =	r_avertexnormal_dots[shadequant & (SHADEDOT_QUANT - 1)];
    shadelight /= 200.0;

    angle = angles[1] / 180 * M_PI;
    shadevector[0] = cos(-angle);
    shadevector[1] = sin(-angle);
    shadevector[2] = 1;
    VectorNormalize(shadevector);
}

/*
=================
R_AliasDrawModel
=================
*/
static void
R_AliasDrawModel(entity_t *entity)
{
    const model_t *model = entity->model;
    vec3_t origin, angles, mins, maxs;
    aliashdr_t *aliashdr;

    /* Calculate position and cull if out of view */
    R_AliasCalcLerp(entity, origin, angles);
    VectorAdd(origin, model->mins, mins);
    VectorAdd(origin, model->maxs, maxs);
    if (R_CullBox(mins, maxs))
	return;

    /* Calculate lighting at the lerp origin */
    R_AliasCalcLight(entity, origin, angles);

    /* locate the proper data */
    aliashdr = Mod_Extradata(entity->model);

    /* draw all the triangles */
    c_alias_polys += aliashdr->numtris;
    VectorCopy(origin, r_entorigin);

    GL_DisableMultitexture();

    glPushMatrix();
    R_RotateForEntity(origin, angles);

    /* double size of eyes, since they are really hard to see in gl */
#ifdef NQ_HACK
    if (!strcmp(model->name, "progs/eyes.mdl") && gl_doubleeyes.value) {
#endif
#ifdef QW_HACK
    if (!strcmp(model->name, "progs/eyes.mdl")) {
#endif
	glTranslatef(aliashdr->scale_origin[0], aliashdr->scale_origin[1],
		     aliashdr->scale_origin[2] - (22 + 8));
	glScalef(aliashdr->scale[0] * 2, aliashdr->scale[1] * 2,
		 aliashdr->scale[2] * 2);
    } else {
	glTranslatef(aliashdr->scale_origin[0], aliashdr->scale_origin[1],
		     aliashdr->scale_origin[2]);
	glScalef(aliashdr->scale[0], aliashdr->scale[1],
		 aliashdr->scale[2]);
    }

    R_AliasSetupSkin(entity, aliashdr);

    /*
     * We can't dynamically colormap textures, so they are cached
     * seperately for the players.  Heads are just uncolored.
     */
#ifdef NQ_HACK
    if (entity->colormap != vid.colormap && !gl_nocolors.value) {
	const int playernum = CL_PlayerEntity(entity);
	if (playernum)
	    GL_Bind(playertextures[playernum - 1]);
    }
#endif
#ifdef QW_HACK
    if (entity->scoreboard && !gl_nocolors.value) {
	const int playernum = entity->scoreboard - cl.players;
	if (!entity->scoreboard->skin) {
	    Skin_Find(entity->scoreboard);
	    R_TranslatePlayerSkin(playernum);
	}
	if (playernum >= 0 && playernum < MAX_CLIENTS)
	    GL_Bind(playertextures[playernum]);
    }
#endif

    if (gl_smoothmodels.value)
	glShadeModel(GL_SMOOTH);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    if (gl_affinemodels.value)
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);

    R_AliasSetupFrame(entity, aliashdr);

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    glShadeModel(GL_FLAT);
    if (gl_affinemodels.value)
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

    glPopMatrix();

    if (r_shadows.value) {
	glPushMatrix();
	R_RotateForEntity(origin, angles);
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glColor4f(0, 0, 0, 0.5);
	GL_AliasDrawShadow(entity, aliashdr, lastposenum);
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
	glColor4f(1, 1, 1, 1);
	glPopMatrix();
    }
}

//=============================================================================

/*
===============
R_MarkLeaves
===============
*/
void
R_MarkLeaves(void)
{
    const leafbits_t *pvs;
    leafblock_t check;
    int leafnum;
    mnode_t *node;
    mleaf_t *leaf;

    if (r_oldviewleaf == r_viewleaf && !r_novis.value)
	return;

    if (mirror)
	return;

    r_visframecount++;
    r_oldviewleaf = r_viewleaf;

    /* Pass the zero leaf to get the all visible set */
    leaf = r_novis.value ? cl.worldmodel->leafs : r_viewleaf;

    pvs = Mod_LeafPVS(cl.worldmodel, leaf);
    foreach_leafbit(pvs, leafnum, check) {
	node = (mnode_t *)&cl.worldmodel->leafs[leafnum + 1];
	do {
	    if (node->visframe == r_visframecount)
		break;
	    node->visframe = r_visframecount;
	    node = node->parent;
	} while (node);
    }
}

/*
====================
R_DrawEntitiesOnList
====================
*/
static void
R_DrawEntitiesOnList(void)
{
    entity_t *e;
    int i;

    if (!r_drawentities.value)
	return;

    // draw sprites seperately, because of alpha blending
    for (i = 0; i < cl_numvisedicts; i++) {
	e = &cl_visedicts[i];
	switch (e->model->type) {
	case mod_alias:
	    R_AliasDrawModel(e);
	    break;
	case mod_brush:
	    R_DrawBrushModel(e);
	    break;
	default:
	    break;
	}
    }

    for (i = 0; i < cl_numvisedicts; i++) {
	e = &cl_visedicts[i];
	switch (e->model->type) {
	case mod_sprite:
	    R_DrawSpriteModel(e);
	    break;
	default:
	    break;
	}
    }
}

/*
=============
R_DrawViewModel
=============
*/
static void
R_DrawViewModel(void)
{
    float ambient[4], diffuse[4];
    int j;
    int lnum;
    vec3_t dist;
    float add;
    dlight_t *dl;
    int ambientlight, shadelight;
    entity_t *e;

#ifdef NQ_HACK
    if (!r_drawviewmodel.value)
	return;

    if (chase_active.value)
	return;
#endif
#ifdef QW_HACK
    if (!r_drawviewmodel.value || !Cam_DrawViewModel())
	return;
#endif

    if (envmap)
	return;

    if (!r_drawentities.value)
	return;

    if (cl.stats[STAT_ITEMS] & IT_INVISIBILITY)
	return;

    if (cl.stats[STAT_HEALTH] <= 0)
	return;

    e = &cl.viewent;
    if (!e->model)
	return;

    j = R_LightPoint(e->origin);

    if (j < 24)
	j = 24;			// always give some light on gun
    ambientlight = j;
    shadelight = j;

// add dynamic lights
    for (lnum = 0; lnum < MAX_DLIGHTS; lnum++) {
	dl = &cl_dlights[lnum];
	if (!dl->radius)
	    continue;
	if (!dl->radius)
	    continue;
	if (dl->die < cl.time)
	    continue;

	VectorSubtract(e->origin, dl->origin, dist);
	add = dl->radius - Length(dist);
	if (add > 0)
	    ambientlight += add;
    }

    ambient[0] = ambient[1] = ambient[2] = ambient[3] =
	(float)ambientlight / 128;
    diffuse[0] = diffuse[1] = diffuse[2] = diffuse[3] =
	(float)shadelight / 128;

    // hack the depth range to prevent view model from poking into walls
    glDepthRange(gldepthmin, gldepthmin + 0.3 * (gldepthmax - gldepthmin));
    R_AliasDrawModel(e);
    glDepthRange(gldepthmin, gldepthmax);
}

/*
 * GL_DrawBlendPoly
 * - Render a polygon covering the whole screen
 * - Used for full-screen color blending and approximated gamma correction
 */
static void
GL_DrawBlendPoly(void)
{
    glBegin(GL_QUADS);
    glVertex3f(10, 100, 100);
    glVertex3f(10, -100, 100);
    glVertex3f(10, -100, -100);
    glVertex3f(10, 100, -100);
    glEnd();
}

/*
============
R_PolyBlend
============
*/
static void
R_PolyBlend(void)
{
    float gamma = 1.0;

    if (!VID_IsFullScreen() || (!VID_SetGammaRamp &&
				_gl_allowgammafallback.value)) {
	gamma = v_gamma.value * v_gamma.value;
	if (gamma < 0.25)
	    gamma = 0.25;
	else if (gamma > 1.0)
	    gamma = 1.0;
    }

    if ((gl_polyblend.value && v_blend[3]) || gamma < 1.0) {
	GL_DisableMultitexture();

	glDisable(GL_ALPHA_TEST);
	glEnable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);

	glLoadIdentity();
	glRotatef(-90, 1, 0, 0);	// put Z going up
	glRotatef(90, 0, 0, 1);		// put Z going up

	if (gl_polyblend.value && v_blend[3]) {
	    glColor4fv(v_blend);
	    GL_DrawBlendPoly();
	}
	if (gamma < 1.0) {
	    glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
	    glColor4f(1, 1, 1, gamma);
	    GL_DrawBlendPoly();
	    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	glDisable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_ALPHA_TEST);
    }
}

static void
R_SetFrustum(void)
{
    int i;

    // FIXME - organise better?
    if (r_lockfrustum.value)
	return;

    if (r_refdef.fov_x == 90) {
	// front side is visible

	VectorAdd(vpn, vright, frustum[0].normal);
	VectorSubtract(vpn, vright, frustum[1].normal);

	VectorAdd(vpn, vup, frustum[2].normal);
	VectorSubtract(vpn, vup, frustum[3].normal);
    } else {
	// rotate VPN right by FOV_X/2 degrees
	RotatePointAroundVector(frustum[0].normal, vup, vpn,
				-(90 - r_refdef.fov_x / 2));
	// rotate VPN left by FOV_X/2 degrees
	RotatePointAroundVector(frustum[1].normal, vup, vpn,
				90 - r_refdef.fov_x / 2);
	// rotate VPN up by FOV_X/2 degrees
	RotatePointAroundVector(frustum[2].normal, vright, vpn,
				90 - r_refdef.fov_y / 2);
	// rotate VPN down by FOV_X/2 degrees
	RotatePointAroundVector(frustum[3].normal, vright, vpn,
				-(90 - r_refdef.fov_y / 2));
    }

    for (i = 0; i < 4; i++) {
	frustum[i].type = PLANE_ANYZ;	// FIXME - true for all angles?
	frustum[i].dist = DotProduct(r_origin, frustum[i].normal);
	frustum[i].signbits = SignbitsForPlane(&frustum[i]);
    }
}

/*
===============
R_SetupFrame
===============
*/
void
R_SetupFrame(void)
{
// don't allow cheats in multiplayer
#ifdef NQ_HACK
    if (cl.maxclients > 1)
	Cvar_Set("r_fullbright", "0");
#endif
#ifdef QW_HACK
    r_fullbright.value = 0;
    r_lightmap.value = 0;
    if (!atoi(Info_ValueForKey(cl.serverinfo, "watervis")))
	r_wateralpha.value = 1;
#endif

    R_AnimateLight();

    r_framecount++;

// build the transformation matrix for the given view angles
    VectorCopy(r_refdef.vieworg, r_origin);

    AngleVectors(r_refdef.viewangles, vpn, vright, vup);

// current viewleaf
    r_oldviewleaf = r_viewleaf;
    if (!r_viewleaf || !r_lockpvs.value)
	r_viewleaf = Mod_PointInLeaf(cl.worldmodel, r_origin);

// color shifting for water, etc.
    V_SetContentsColor(r_viewleaf->contents);
    V_CalcBlend();

// surface cache isn't thrashing (don't have one in GL?)
    r_cache_thrash = false;

// reset count of polys for this frame
    c_brush_polys = 0;
    c_alias_polys = 0;
    c_lightmaps_uploaded = 0;
}


static void
MYgluPerspective(GLdouble fovy, GLdouble aspect,
		 GLdouble zNear, GLdouble zFar)
{
    GLdouble xmin, xmax, ymin, ymax;

    ymax = zNear * tan(fovy * M_PI / 360.0);
    ymin = -ymax;

    xmin = ymin * aspect;
    xmax = ymax * aspect;

    glFrustum(xmin, xmax, ymin, ymax, zNear, zFar);
}


/*
=============
R_SetupGL
=============
*/
static void
R_SetupGL(void)
{
    float screenaspect;
    int x, x2, y2, y, w, h;

    //
    // set up viewpoint
    //
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    x = r_refdef.vrect.x * glwidth / vid.width;
    x2 = (r_refdef.vrect.x + r_refdef.vrect.width) * glwidth / vid.width;
    y = (vid.height - r_refdef.vrect.y) * glheight / vid.height;
    y2 = (vid.height -
	  (r_refdef.vrect.y + r_refdef.vrect.height)) * glheight / vid.height;

    // fudge around because of frac screen scale
    // FIXME: well not fix, but figure out why this is done...
    if (x > 0)
	x--;
    if (x2 < glwidth)
	x2++;
    if (y2 < 0)
	y2--;
    if (y < glheight)
	y++;

    w = x2 - x;
    h = y - y2;

    // FIXME: Skybox? Regular Quake sky?
    if (envmap) {
	x = y2 = 0;
	w = h = 256;
    }

    glViewport(glx + x, gly + y2, w, h);
    screenaspect = (float)r_refdef.vrect.width / r_refdef.vrect.height;

    // FIXME - wtf is all this about?
    //yfov = 2*atan((float)r_refdef.vrect.height/r_refdef.vrect.width)*180/M_PI;
    //yfov = (2.0 * tan (scr_fov.value/360*M_PI)) / screenaspect;
    //yfov = 2*atan((float)r_refdef.vrect.height/r_refdef.vrect.width)*(scr_fov.value*2)/M_PI;
    //MYgluPerspective (yfov,  screenaspect,  4,  4096);

    // FIXME - set depth dynamically for improved depth precision in smaller
    //         spaces
    //MYgluPerspective (r_refdef.fov_y, screenaspect, 4, 4096);
    MYgluPerspective(r_refdef.fov_y, screenaspect, 4, 6144);

    if (mirror) {
	if (mirror_plane->normal[2])
	    glScalef(1, -1, 1);
	else
	    glScalef(-1, 1, 1);
	glCullFace(GL_BACK);
    } else
	glCullFace(GL_FRONT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glRotatef(-90, 1, 0, 0);	// put Z going up
    glRotatef(90, 0, 0, 1);	// put Z going up
    glRotatef(-r_refdef.viewangles[2], 1, 0, 0);
    glRotatef(-r_refdef.viewangles[0], 0, 1, 0);
    glRotatef(-r_refdef.viewangles[1], 0, 0, 1);
    glTranslatef(-r_refdef.vieworg[0], -r_refdef.vieworg[1],
		 -r_refdef.vieworg[2]);

    glGetFloatv(GL_MODELVIEW_MATRIX, r_world_matrix);

    //
    // set drawing parms
    //
    if (gl_cull.value)
	glEnable(GL_CULL_FACE);
    else
	glDisable(GL_CULL_FACE);

    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_DEPTH_TEST);
}

/*
================
R_RenderScene

r_refdef must be set before the first call
================
*/
static void
R_RenderScene(void)
{
    R_SetupFrame();
    R_SetFrustum();
    R_SetupGL();
    R_MarkLeaves();		// done here so we know if we're in water
    R_DrawWorld();		// adds static entities to the list
    S_ExtraUpdate();		// don't let sound get messed up if going slow
    R_DrawEntitiesOnList();
    GL_DisableMultitexture();
    R_RenderDlights();
    R_DrawParticles();
}


/*
=============
R_Clear
=============
*/
static void
R_Clear(void)
{
    if (r_mirroralpha.value != 1.0) {
	if (gl_clear.value)
	    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	else
	    glClear(GL_DEPTH_BUFFER_BIT);
	gldepthmin = 0;
	gldepthmax = 0.5;
	glDepthFunc(GL_LEQUAL);
    } else if (gl_ztrick.value) {
	static unsigned int trickframe = 0;

	if (gl_clear.value)
	    glClear(GL_COLOR_BUFFER_BIT);

	trickframe++;
	if (trickframe & 1) {
	    gldepthmin = 0;
	    gldepthmax = 0.49999;
	    glDepthFunc(GL_LEQUAL);
	} else {
	    gldepthmin = 1;
	    gldepthmax = 0.5;
	    glDepthFunc(GL_GEQUAL);
	}
    } else {
	if (gl_clear.value)
	    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	else
	    glClear(GL_DEPTH_BUFFER_BIT);
	gldepthmin = 0;
	gldepthmax = 1;
	glDepthFunc(GL_LEQUAL);
    }

    glDepthRange(gldepthmin, gldepthmax);

    if (gl_zfix.value) {
	if (gldepthmax > gldepthmin)
	    glPolygonOffset(1, 1);
	else
	    glPolygonOffset(-1, -1);
    }
}

#ifdef NQ_HACK /* Mirrors disabled for now in QW */
/*
=============
R_Mirror
=============
*/
static void
R_Mirror(void)
{
    float d;
    msurface_t *s;
    entity_t *ent;

    if (!mirror)
	return;

    memcpy(r_base_world_matrix, r_world_matrix, sizeof(r_base_world_matrix));

    d = DotProduct(r_refdef.vieworg,
		   mirror_plane->normal) - mirror_plane->dist;
    VectorMA(r_refdef.vieworg, -2 * d, mirror_plane->normal,
	     r_refdef.vieworg);

    d = DotProduct(vpn, mirror_plane->normal);
    VectorMA(vpn, -2 * d, mirror_plane->normal, vpn);

    r_refdef.viewangles[0] = -asin(vpn[2]) / M_PI * 180;
    r_refdef.viewangles[1] = atan2(vpn[1], vpn[0]) / M_PI * 180;
    r_refdef.viewangles[2] = -r_refdef.viewangles[2];

    /* Add the player to visedicts they can see their reflection */
    ent = &cl_entities[cl.viewentity];
    if (cl_numvisedicts < MAX_VISEDICTS) {
	cl_visedicts[cl_numvisedicts] = *ent;
	cl_numvisedicts++;
    }

    gldepthmin = 0.5;
    gldepthmax = 1;
    glDepthRange(gldepthmin, gldepthmax);
    glDepthFunc(GL_LEQUAL);

    R_RenderScene();
    R_DrawWaterSurfaces();

    gldepthmin = 0;
    gldepthmax = 0.5;
    glDepthRange(gldepthmin, gldepthmax);
    glDepthFunc(GL_LEQUAL);

    // blend on top
    glEnable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);
    if (mirror_plane->normal[2])
	glScalef(1, -1, 1);
    else
	glScalef(-1, 1, 1);
    glCullFace(GL_FRONT);
    glMatrixMode(GL_MODELVIEW);

    glLoadMatrixf(r_base_world_matrix);

    glColor4f(1, 1, 1, r_mirroralpha.value);
    s = cl.worldmodel->textures[mirrortexturenum]->texturechain;
    for (; s; s = s->texturechain)
	R_RenderBrushPoly(&r_worldentity, s);
    cl.worldmodel->textures[mirrortexturenum]->texturechain = NULL;
    glDisable(GL_BLEND);
    glColor4f(1, 1, 1, 1);
}
#endif

/*
================
R_RenderView

r_refdef must be set before the first call
================
*/
void
R_RenderView(void)
{
    double time1 = 0, time2;

    if (r_norefresh.value)
	return;

    if (!r_worldentity.model || !cl.worldmodel)
	Sys_Error("%s: NULL worldmodel", __func__);

    if (gl_finish.value || r_speeds.value)
	glFinish();

    if (r_speeds.value) {
	time1 = Sys_DoubleTime();
	c_brush_polys = 0;
	c_alias_polys = 0;
	c_lightmaps_uploaded = 0;
    }

    mirror = false;

    R_Clear();

    // render normal view
    R_RenderScene();
    R_DrawViewModel();
    R_DrawWaterSurfaces();

#ifdef NQ_HACK /* Mirrors disabled for now in QW */
    // render mirror view
    R_Mirror();
#endif

    R_PolyBlend();

    if (r_speeds.value) {
//              glFinish ();
	time2 = Sys_DoubleTime();
	Con_Printf("%3i ms  %4i wpoly %4i epoly %4i dlit\n",
		   (int)((time2 - time1) * 1000), c_brush_polys,
		   c_alias_polys, c_lightmaps_uploaded);
    }
}
