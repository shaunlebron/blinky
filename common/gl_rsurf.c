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
// gl_rsurf.c: surface-related refresh code

#include "console.h"
#include "glquake.h"
#include "quakedef.h"
#include "sys.h"

#ifdef NQ_HACK
#include "host.h"
#endif

/*
 * Can't decide on water warp strategy yet
 * - Warp stuff on the other side of the water surface
 * - or just always warp underwater surfaces...
 * - issues with moving bsp models...
 */
//#define WATER_WARP_THROUGH_SURFACE

#ifdef WATER_WARP_THROUGH_SURFACE
/* FIXME - this version of the test is broken somehow... */
#define WATER_WARP_TEST(surf)					\
    (r_waterwarp.value && !((surf)->flags & SURF_DONTWARP) &&	\
	((r_viewleaf->contents == CONTENTS_EMPTY &&		\
	 ((surf)->flags & SURF_UNDERWATER))			\
    ||								\
	 (r_viewleaf->contents != CONTENTS_EMPTY &&		\
         !((surf)->flags & SURF_UNDERWATER))))
#else
#define WATER_WARP_TEST(surf)		\
    (r_waterwarp.value && ((surf)->flags & SURF_UNDERWATER))
#endif

#define	MAX_LIGHTMAPS	80

static int lightmap_bytes;		// 1, 2, or 4
static int lightmap_textures_initialised = 0;
static GLuint lightmap_textures[MAX_LIGHTMAPS];

static unsigned blocklights[18 * 18];

#define	BLOCK_WIDTH	128
#define	BLOCK_HEIGHT	128


typedef struct glRect_s {
    unsigned char l, t, w, h;
} glRect_t;

static glpoly_t *lightmap_polys[MAX_LIGHTMAPS];
static qboolean lightmap_modified[MAX_LIGHTMAPS];
static glRect_t lightmap_rectchange[MAX_LIGHTMAPS];

static int allocated[MAX_LIGHTMAPS][BLOCK_WIDTH];
static int lm_used;

// the lightmap texture data needs to be kept in
// main memory so texsubimage can update properly
static byte lightmaps[4 * MAX_LIGHTMAPS * BLOCK_WIDTH * BLOCK_HEIGHT];

#if 0
/*
 * Bring the lightmap data under a common structure?
 */
struct lightmap_info {
    glpoly_t *polys;
    qboolean modified;
    glRect_t rect;
    int allocated[BLOCK_WIDTH];
    byte data[4 * BLOCK_WIDTH * BLOCK_HEIGHT]; /* lightmaps */
};

static struct lightmap_info lightmaps[MAX_LIGHTMAPS];

#endif

/*
 * ===================
 * R_AddDynamicLights
 * ===================
 * Check all dynamic lights against this surface
 */
static void
R_AddDynamicLights(msurface_t *surf)
{
    int lnum;
    int sd, td;
    float dist, rad, minlight;
    vec3_t impact, local;
    int s, t;
    int i;
    int smax, tmax;
    mtexinfo_t *tex;
    dlight_t *dl;

    smax = (surf->extents[0] >> 4) + 1;
    tmax = (surf->extents[1] >> 4) + 1;
    tex = surf->texinfo;

    for (lnum = 0; lnum < MAX_DLIGHTS; lnum++) {
	if (!(surf->dlightbits & (1U << lnum)))
	    continue;		// not lit by this light

	dl = &cl_dlights[lnum];

	rad = dl->radius;
	dist = DotProduct(dl->origin, surf->plane->normal) - surf->plane->dist;
	rad -= fabs(dist);
	minlight = dl->minlight;
	if (rad < minlight)
	    continue;
	minlight = rad - minlight;

	for (i = 0; i < 3; i++)
	    impact[i] = dl->origin[i] -	surf->plane->normal[i] * dist;

	local[0] = DotProduct(impact, tex->vecs[0]) + tex->vecs[0][3];
	local[0] -= surf->texturemins[0];
	local[1] = DotProduct(impact, tex->vecs[1]) + tex->vecs[1][3];
	local[1] -= surf->texturemins[1];

	for (t = 0; t < tmax; t++) {
	    td = local[1] - t * 16;
	    if (td < 0)
		td = -td;
	    for (s = 0; s < smax; s++) {
		sd = local[0] - s * 16;
		if (sd < 0)
		    sd = -sd;
		if (sd > td)
		    dist = sd + (td >> 1);
		else
		    dist = td + (sd >> 1);
		if (dist < minlight)
		    blocklights[t * smax + s] += (rad - dist) * 256;
	    }
	}
    }
}

/*
 * Iterate through each lightmap for a surface
 * msurface_t *s => the surface
 * int n         => lightmap number
 */
#define for_each_surf_lightstyle(s, n) \
	for ((n) = 0; (n) < MAXLIGHTMAPS && (s)->styles[n] != 255; (n)++)

/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the correct format in blocklights
===============
*/
static void
R_BuildLightMap(msurface_t *surf, byte *dest, int stride)
{
    int smax, tmax;
    int t;
    int i, j, size;
    byte *lightmap;
    unsigned scale;
    int map;
    unsigned *bl;

    surf->cached_dlight = (surf->dlightframe == r_framecount);

    smax = (surf->extents[0] >> 4) + 1;
    tmax = (surf->extents[1] >> 4) + 1;
    size = smax * tmax;
    lightmap = surf->samples;

// set to full bright if no light data
    if (r_fullbright.value || !cl.worldmodel->lightdata) {
	for (i = 0; i < size; i++)
	    blocklights[i] = 255 * 256;
	goto store;
    }
// clear to no light
    for (i = 0; i < size; i++)
	blocklights[i] = 0;

// add all the lightmaps
    if (lightmap) {
	for_each_surf_lightstyle(surf, map) {
	    scale = d_lightstylevalue[surf->styles[map]];
	    surf->cached_light[map] = scale;	// 8.8 fraction
	    for (i = 0; i < size; i++)
		blocklights[i] += lightmap[i] * scale;
	    lightmap += size;	// skip to next lightmap
	}
    }

// add all the dynamic lights
    if (surf->dlightframe == r_framecount)
	R_AddDynamicLights(surf);

// bound, invert, and shift
  store:
    switch (gl_lightmap_format) {
    case GL_RGBA:
	stride -= (smax << 2);
	bl = blocklights;
	for (i = 0; i < tmax; i++, dest += stride) {
	    for (j = 0; j < smax; j++) {
		t = *bl++;
		t >>= 7;
		if (t > 255)
		    t = 255;
		dest[3] = 255 - t;
		dest += 4;
	    }
	}
	break;
    case GL_ALPHA:
    case GL_LUMINANCE:
    case GL_INTENSITY:
	bl = blocklights;
	for (i = 0; i < tmax; i++, dest += stride) {
	    for (j = 0; j < smax; j++) {
		t = *bl++;
		t >>= 7;
		if (t > 255)
		    t = 255;
		dest[j] = 255 - t;
	    }
	}
	break;
    default:
	Sys_Error("Bad lightmap format");
    }
}


/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
texture_t *
R_TextureAnimation(texture_t *base)
{
    int reletive;
    int count;

    if (currententity->frame) {
	if (base->alternate_anims)
	    base = base->alternate_anims;
    }

    if (!base->anim_total)
	return base;

    reletive = (int)(cl.time * 10) % base->anim_total;

    count = 0;
    while (base->anim_min > reletive || base->anim_max <= reletive) {
	base = base->anim_next;
	if (!base)
	    Sys_Error("%s: broken cycle", __func__);
	if (++count > 100)
	    Sys_Error("%s: infinite cycle", __func__);
    }

    return base;
}


/*
=============================================================

	BRUSH MODELS

=============================================================
*/

lpMultiTexFUNC qglMultiTexCoord2fARB = NULL;
lpActiveTextureFUNC qglActiveTextureARB = NULL;

static qboolean mtexenabled = false;
static GLenum oldtarget = GL_TEXTURE0_ARB;
static int cnttextures[2] = { -1, -1 };	// cached

/*
 * Makes the given texture unit active
 * FIXME: only aware of two texture units...
 */
void
GL_SelectTexture(GLenum target)
{
    if (!gl_mtexable || target == oldtarget)
	return;

    /*
     * Save the current texture unit's texture handle, select the new texture
     * unit and update currenttexture
     */
    qglActiveTextureARB(target);
    cnttextures[oldtarget - GL_TEXTURE0_ARB] = currenttexture;
    currenttexture = cnttextures[target - GL_TEXTURE0_ARB];
    oldtarget = target;
}

void
GL_DisableMultitexture(void)
{
    if (mtexenabled) {
	glDisable(GL_TEXTURE_2D);
	GL_SelectTexture(GL_TEXTURE0_ARB);
	mtexenabled = false;
    }
}

void
GL_EnableMultitexture(void)
{
    if (gl_mtexable) {
	GL_SelectTexture(GL_TEXTURE1_ARB);
	glEnable(GL_TEXTURE_2D);
	mtexenabled = true;
    }
}

/*
================
DrawGLPoly
================
*/
static void
DrawGLPoly(glpoly_t *p)
{
    int i;
    float *v;

    glBegin(GL_POLYGON);
    v = p->verts[0];
    for (i = 0; i < p->numverts; i++, v += VERTEXSIZE) {
	glTexCoord2f(v[3], v[4]);
	glVertex3fv(v);
    }
    glEnd();
}

static void
DrawGLPolyLM(glpoly_t *p)
{
    int i;
    float *v;

    glBegin(GL_POLYGON);
    v = p->verts[0];
    for (i = 0; i < p->numverts; i++, v += VERTEXSIZE) {
	glTexCoord2f(v[5], v[6]);
	glVertex3fv(v);
    }
    glEnd();
}

static void
DrawGLPoly_2Ply(glpoly_t *p)
{
    int i;
    float *v;

    glBegin(GL_TRIANGLE_FAN);
    v = p->verts[0];
    for (i = 0; i < p->numverts; i++, v+= VERTEXSIZE) {
	qglMultiTexCoord2fARB(GL_TEXTURE0_ARB, v[3], v[4]);
	qglMultiTexCoord2fARB(GL_TEXTURE1_ARB, v[5], v[6]);
	glVertex3fv(v);
    }
    glEnd();
}

static void
WaterWarpCoord(vec3_t in, vec3_t out)
{
    /* Produce the warped coord */
    out[0] = sin(in[1] * 0.05 + realtime) * sin(in[2] * 0.05 + realtime);
    out[0] = out[0] * 8 + in[0];
    out[1] = sin(in[0] * 0.05 + realtime) * sin(in[2] * 0.05 + realtime);
    out[1] = out[1] * 8 + in[1];
    out[2] = in[2];
}

/*
================
DrawGLWaterPoly

Warp the vertex coordinates
================
*/
static void
DrawGLWaterPoly(glpoly_t *p)
{
    int i;
    float *v;
    vec3_t nv;

    GL_DisableMultitexture();

    glBegin(GL_TRIANGLE_FAN);
    v = p->verts[0];
    for (i = 0; i < p->numverts; i++, v += VERTEXSIZE) {
	glTexCoord2f(v[3], v[4]);
	WaterWarpCoord(v, nv);
	glVertex3fv(nv);
    }
    glEnd();
}

static void
DrawGLWaterPolyLightmap(glpoly_t *p)
{
    int i;
    float *v;
    vec3_t nv;

    GL_DisableMultitexture();

    glBegin(GL_TRIANGLE_FAN);
    v = p->verts[0];
    for (i = 0; i < p->numverts; i++, v += VERTEXSIZE) {
	glTexCoord2f(v[5], v[6]);
	WaterWarpCoord(v, nv);
	glVertex3fv(nv);
    }
    glEnd();
}

static void
DrawFlatGLPoly(glpoly_t *p)
{
    int i;
    float *v;

    srand((unsigned long)p);
    glColor3f(rand() % 256 / 255.0, rand() % 256 / 255.0,
	      rand() % 256 / 255.0);
    glBegin(GL_POLYGON);
    v = p->verts[0];
    for (i = 0; i < p->numverts; i++, v += VERTEXSIZE)
	glVertex3fv(v);
    glEnd();
}

/*
 * R_UploadLightmapUpdate
 * Re-uploads the modified region of the given lightmap number
 */
static void
R_UploadLightmapUpdate(int map)
{
    glRect_t *rect;
    byte *pixels;
    unsigned offset;

    rect = &lightmap_rectchange[map];

    offset = map * BLOCK_HEIGHT + rect->t; /* number of rows */
    offset *= BLOCK_WIDTH;
    offset += rect->l; /* x-offset */
    offset *= lightmap_bytes;

    pixels = lightmaps + offset;

    /* set unpacking width to BLOCK_WIDTH, reset after */
    glPixelStorei(GL_UNPACK_ROW_LENGTH, BLOCK_WIDTH);
    glTexSubImage2D(GL_TEXTURE_2D,
		    0,
		    rect->l, /* x-offset */
		    rect->t, /* y-offset */
		    rect->w,
		    rect->h,
		    gl_lightmap_format,
		    GL_UNSIGNED_BYTE,
		    pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    rect->l = BLOCK_WIDTH;
    rect->t = BLOCK_HEIGHT;
    rect->h = 0;
    rect->w = 0;

    c_lightmaps_uploaded++;
}

/*
================
R_BlendLightmaps
================
*/
static void
R_BlendLightmaps(void)
{
    int i;
    glpoly_t *p;

    if (r_fullbright.value)
	return;
    if (r_drawflat.value)
	return;

    glDepthMask(0);		// don't bother writing Z

    if (gl_lightmap_format == GL_LUMINANCE)
	glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
    else if (gl_lightmap_format == GL_INTENSITY) {
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glColor4f(0, 0, 0, 1);
    }

    if (!r_lightmap.value) {
	glEnable(GL_BLEND);
    }

    for (i = 0; i < MAX_LIGHTMAPS; i++) {
	p = lightmap_polys[i];
	if (!p)
	    continue;
	GL_Bind(lightmap_textures[i]);
	if (lightmap_modified[i]) {
	    R_UploadLightmapUpdate(i);
	    lightmap_modified[i] = false;
	}
	for (; p; p = p->chain) {
	    if (WATER_WARP_TEST(p))
		DrawGLWaterPolyLightmap(p);
	    else
		DrawGLPolyLM(p);
	}
    }

    glDisable(GL_BLEND);
    if (gl_lightmap_format == GL_LUMINANCE)
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    else if (gl_lightmap_format == GL_INTENSITY) {
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glColor4f(1, 1, 1, 1);
    }
    glDepthMask(1);		// back to normal Z buffering
}

/*
 * R_UpdateLightmapBlockRect
 */
static void
R_UpdateLightmapBlockRect(msurface_t *fa)
{
    int map;
    glRect_t *theRect;
    byte *base;
    int smax, tmax;

    if (!r_dynamic.value)
	return;

    /* Check if any of this surface's lightmaps changed */
    for_each_surf_lightstyle(fa, map)
	if (d_lightstylevalue[fa->styles[map]] != fa->cached_light[map])
	    goto dynamic;

    /*
     * 	fa->dlightframe == r_framecount	=> dynamic this frame
     *  fa->cached_dlight		=> dynamic previously
     */
    if (fa->dlightframe == r_framecount || fa->cached_dlight) {
    dynamic:
	/*
	 * Record that the lightmap block for this surface has been
	 * modified. If necessary, increase the modified rectangle to include
	 * this surface's allocatied sub-area.
	 */
	lightmap_modified[fa->lightmaptexturenum] = true;
	theRect = &lightmap_rectchange[fa->lightmaptexturenum];
	if (fa->light_t < theRect->t) {
	    if (theRect->h)
		theRect->h += theRect->t - fa->light_t;
	    theRect->t = fa->light_t;
	}
	if (fa->light_s < theRect->l) {
	    if (theRect->w)
		theRect->w += theRect->l - fa->light_s;
	    theRect->l = fa->light_s;
	}
	smax = (fa->extents[0] >> 4) + 1;
	tmax = (fa->extents[1] >> 4) + 1;
	if ((theRect->w + theRect->l) < (fa->light_s + smax))
	    theRect->w = (fa->light_s - theRect->l) + smax;
	if ((theRect->h + theRect->t) < (fa->light_t + tmax))
	    theRect->h = (fa->light_t - theRect->t) + tmax;
	base =
	    lightmaps +
	    fa->lightmaptexturenum * lightmap_bytes * BLOCK_WIDTH *
	    BLOCK_HEIGHT;
	base +=
	    fa->light_t * BLOCK_WIDTH * lightmap_bytes +
	    fa->light_s * lightmap_bytes;
	R_BuildLightMap(fa, base, BLOCK_WIDTH * lightmap_bytes);
    }
}

/*
================
R_RenderBrushPoly
================
*/
void
R_RenderBrushPoly(msurface_t *fa)
{
    int i;
    texture_t *t;

    c_brush_polys++;

    if (fa->flags & SURF_DRAWSKY) {	// sky texture, no lightmaps
	EmitBothSkyLayers(fa);
	return;
    }

    if (gl_mtexable) {
	GL_SelectTexture(GL_TEXTURE0_ARB);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    }
    t = R_TextureAnimation(fa->texinfo->texture);
    GL_Bind(t->gl_texturenum);

    if (fa->flags & SURF_DRAWTURB) {	// warp texture, no lightmaps
	EmitWaterPolys(fa);
	return;
    }

    if (gl_mtexable) {
	/* All lightmaps are up to date... */
	i = fa->lightmaptexturenum;
	GL_EnableMultitexture();
	GL_Bind(lightmap_textures[i]);
	if (lightmap_modified[i]) {
	    R_UploadLightmapUpdate(i);
	    lightmap_modified[i] = false;
	}
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
	DrawGLPoly_2Ply(fa->polys);
	GL_DisableMultitexture();
    } else {
	if (WATER_WARP_TEST(fa))
	    DrawGLWaterPoly(fa->polys);
	else
	    DrawGLPoly(fa->polys);

	/* add the poly to the proper lightmap chain */
	fa->polys->chain = lightmap_polys[fa->lightmaptexturenum];
	lightmap_polys[fa->lightmaptexturenum] = fa->polys;

	R_UpdateLightmapBlockRect(fa);
    }
}


/*
================
R_MirrorChain
================
*/
static void
R_MirrorChain(msurface_t *s)
{
    if (mirror)
	return;
    mirror = true;
    mirror_plane = s->plane;
}

/*
================
R_DrawWaterSurfaces
================
*/
void
R_DrawWaterSurfaces(void)
{
    int i;
    msurface_t *s;
    texture_t *t;

    if (r_wateralpha.value == 1.0)
	return;

    //
    // go back to the world matrix
    //

    glLoadMatrixf(r_world_matrix);

    if (r_wateralpha.value < 1.0) {
	glEnable(GL_BLEND);
	glColor4f(1, 1, 1, r_wateralpha.value);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }

    for (i = 0; i < cl.worldmodel->numtextures; i++) {
	t = cl.worldmodel->textures[i];
	if (!t)
	    continue;
	s = t->texturechain;
	if (!s)
	    continue;
	if (!(s->flags & SURF_DRAWTURB))
	    continue;

	// set modulate mode explicitly

	GL_Bind(t->gl_texturenum);
	for (; s; s = s->texturechain)
	    EmitWaterPolys(s);

	t->texturechain = NULL;
    }

    if (r_wateralpha.value < 1.0) {
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

	glColor4f(1, 1, 1, 1);
	glDisable(GL_BLEND);
    }
}

/*
================
DrawTextureChains
================
*/
static void
DrawTextureChains(void)
{
    int i;
    msurface_t *s;
    texture_t *t;

    if (gl_mtexable) {
	/* Update all lightmaps */
	for (i = 0; i < cl.worldmodel->numtextures; i++) {
	    t = cl.worldmodel->textures[i];
	    if (!t)
		continue;
	    s = t->texturechain;
	    if (!s)
		continue;
	    if (s->flags & SURF_DRAWSKY)
		continue;
	    if (i == mirrortexturenum && !r_mirroralpha.value)
		continue;
	    if ((s->flags & SURF_DRAWTURB))
		continue;

	    for (; s; s = s->texturechain)
		R_UpdateLightmapBlockRect(s);
	}
    }

    for (i = 0; i < cl.worldmodel->numtextures; i++) {
	t = cl.worldmodel->textures[i];
	if (!t)
	    continue;
	s = t->texturechain;
	if (!s)
	    continue;
	if (s->flags & SURF_DRAWSKY) {
	    R_DrawSkyChain(s);
	} else if (i == mirrortexturenum && r_mirroralpha.value != 1.0) {
	    R_MirrorChain(s);
	    continue;
	} else {
	    if ((s->flags & SURF_DRAWTURB) && r_wateralpha.value != 1.0)
		continue;	// draw translucent water later
	    for (; s; s = s->texturechain)
		R_RenderBrushPoly(s);
	}
	t->texturechain = NULL;
    }
}

static void
DrawFlatTextureChains(void)
{
    int i;
    msurface_t *s;
    texture_t *t;

    GL_DisableMultitexture();
    glDisable(GL_TEXTURE_2D);

    for (i = 0; i < cl.worldmodel->numtextures; i++) {

	t = cl.worldmodel->textures[i];
	if (!t)
	    continue;

	s = t->texturechain;
	if (!s)
	    continue;

	/* sky and water polys are chained together! */
	if (s->flags & (SURF_DRAWSKY | SURF_DRAWTURB)) {
	    for (; s; s = s->texturechain) {
		glpoly_t *p;
		for (p = s->polys; p; p = p->next)
		    DrawFlatGLPoly(p);
	    }
	    t->texturechain = NULL;
	    continue;
	}

	/* FIXME - handle mirror texture? */

	for (; s; s = s->texturechain)
	    DrawFlatGLPoly(s->polys);

	t->texturechain = NULL;
    }

    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
}

/*
=================
R_DrawBrushModel
=================
*/
void
R_DrawBrushModel(entity_t *e)
{
    int i, k;
    vec3_t mins, maxs;
    msurface_t *psurf;
    float dot;
    mplane_t *pplane;
    model_t *clmodel;
    qboolean rotated;

    currententity = e;
    currenttexture = -1;

    clmodel = e->model;

    if (e->angles[0] || e->angles[1] || e->angles[2]) {
	rotated = true;
	for (i = 0; i < 3; i++) {
	    mins[i] = e->origin[i] - clmodel->radius;
	    maxs[i] = e->origin[i] + clmodel->radius;
	}
    } else {
	rotated = false;
	VectorAdd(e->origin, clmodel->mins, mins);
	VectorAdd(e->origin, clmodel->maxs, maxs);
    }

    if (R_CullBox(mins, maxs))
	return;

    VectorSubtract(r_refdef.vieworg, e->origin, modelorg);
    if (rotated) {
	vec3_t temp;
	vec3_t forward, right, up;

	VectorCopy(modelorg, temp);
	AngleVectors(e->angles, forward, right, up);
	modelorg[0] = DotProduct(temp, forward);
	modelorg[1] = -DotProduct(temp, right);
	modelorg[2] = DotProduct(temp, up);
    }

    psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

    glPushMatrix();
    e->angles[0] = -e->angles[0];	// stupid quake bug
    R_RotateForEntity(e);
    e->angles[0] = -e->angles[0];	// stupid quake bug

    if (!r_drawflat.value) {
	/*
	 * calculate dynamic lighting for bmodel if it's not an instanced model
	 */
	memset(lightmap_polys, 0, sizeof(lightmap_polys));

	if (clmodel->firstmodelsurface != 0 && !gl_flashblend.value) {
	    for (k = 0; k < MAX_DLIGHTS; k++) {
		if ((cl_dlights[k].die < cl.time) || (!cl_dlights[k].radius))
		    continue;

		R_MarkLights(&cl_dlights[k], 1 << k,
			     clmodel->nodes + clmodel->hulls[0].firstclipnode);
	    }
	}
    }

    /*
     * draw texture
     */
    if (r_drawflat.value)
	glDisable(GL_TEXTURE_2D);
    else
	glColor3f(1, 1, 1);

    for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++) {
	/* find which side of the node we are on */
	pplane = psurf->plane;
	dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

	/* draw the polygon */
	if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
	    (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON))) {
	    if (r_drawflat.value) {
		DrawFlatGLPoly(psurf->polys);
	    } else {
		/* FIXME - hack for dynamic lightmap updates... */
		qboolean real_mtexable = gl_mtexable;
		gl_mtexable = false;
		R_RenderBrushPoly(psurf);
		gl_mtexable = real_mtexable;
	    }
	}
    }

    if (r_drawflat.value) {
	glEnable(GL_TEXTURE_2D);
	glColor3f(1, 1, 1);
    } else {
	R_BlendLightmaps();
    }

    glPopMatrix();
}

/*
=============================================================

	WORLD MODEL

=============================================================
*/

/*
================
R_RecursiveWorldNode
================
*/
static void
R_RecursiveWorldNode(mnode_t *node)
{
    int c, side;
    mplane_t *plane;
    msurface_t *surf, **mark;
    mleaf_t *pleaf;
    double dot;

    if (node->contents == CONTENTS_SOLID)
	return;			// solid

    if (node->visframe != r_visframecount)
	return;
    if (R_CullBox(node->minmaxs, node->minmaxs + 3))
	return;			// Node outside frustum

// if a leaf node, draw stuff
    if (node->contents < 0) {
	pleaf = (mleaf_t *)node;

	mark = pleaf->firstmarksurface;
	c = pleaf->nummarksurfaces;

	if (c) {
	    do {
		(*mark)->visframe = r_framecount;
		mark++;
	    } while (--c);
	}
	// deal with model fragments in this leaf
	if (pleaf->efrags)
	    R_StoreEfrags(&pleaf->efrags);

	return;
    }
// node is just a decision point, so go down the apropriate sides

// find which side of the node we are on
    plane = node->plane;

    switch (plane->type) {
    case PLANE_X:
    case PLANE_Y:
    case PLANE_Z:
	dot = modelorg[plane->type - PLANE_X] - plane->dist;
	break;
    default:
	dot = DotProduct(modelorg, plane->normal) - plane->dist;
	break;
    }

    side = (dot >= 0) ? 0 : 1;

    /* recurse down the children, front side first */
    R_RecursiveWorldNode(node->children[side]);

    /* draw stuff */
    c = node->numsurfaces;

    if (c) {
	surf = cl.worldmodel->surfaces + node->firstsurface;

	if (dot < -BACKFACE_EPSILON)
	    side = SURF_PLANEBACK;
	else if (dot > BACKFACE_EPSILON)
	    side = 0;

	for (; c; c--, surf++) {
	    if (surf->visframe != r_framecount)
		continue;

	    // don't backface underwater surfaces, because they warp
	    if (!WATER_WARP_TEST(surf)
		&& ((dot < 0) ^ !!(surf->flags & SURF_PLANEBACK)))
		continue;	// wrong side

	    // just store it out
	    if (!mirror
		|| surf->texinfo->texture !=
		cl.worldmodel->textures[mirrortexturenum]) {
		surf->texturechain = surf->texinfo->texture->texturechain;
		surf->texinfo->texture->texturechain = surf;
	    }
	}
    }

    /* recurse down the back side */
    R_RecursiveWorldNode(node->children[side ? 0 : 1]);
}


/*
=============
R_DrawWorld
=============
*/
void
R_DrawWorld(void)
{
    entity_t ent;

    memset(&ent, 0, sizeof(ent));
    ent.model = cl.worldmodel;

    VectorCopy(r_refdef.vieworg, modelorg);

    currententity = &ent;
    currenttexture = -1;

    glColor3f(1, 1, 1);
    memset(lightmap_polys, 0, sizeof(lightmap_polys));

    if (r_drawflat.value || _gl_drawhull.value) {
	GL_DisableMultitexture();
	glDisable(GL_TEXTURE_2D);
    }

    if (_gl_drawhull.value) {
	switch ((int)_gl_drawhull.value) {
	case 1:
	case 2:
	    /* all preparation done when variable is set */
	    R_DrawWorldHull();
	    break;
	default:
	    /* FIXME: Error? should never happen... */
	    break;
	}
	glEnable(GL_TEXTURE_2D);
	glColor3f(1.0, 1.0, 1.0);
    } else {
	R_RecursiveWorldNode(cl.worldmodel->nodes);

	if (r_drawflat.value) {
	    DrawFlatTextureChains();
	} else {
	    DrawTextureChains();
	    R_BlendLightmaps();
	}
    }
}

/*
=============================================================================

  LIGHTMAP ALLOCATION

=============================================================================
*/

static float alloc_block_time = 0;

/*
 * AllocBlock
 * - returns a texture number and the position inside it
 */
static int
AllocBlock(int w, int h, int *x, int *y)
{
    int i, j;
    int best, best2;
    int texnum;
    float t1, t2;

    t1 = Sys_DoubleTime();

    /*
     * Only scan over the last four textures. Only negligable effects on the
     * packing efficiency, but much faster for maps with a lot of lightmaps.
     */
    texnum = (lm_used < 4) ? 0 : lm_used - 4;
    for ( ; texnum < MAX_LIGHTMAPS; texnum++) {

	if (texnum > lm_used)
	    lm_used = texnum;

	best = BLOCK_HEIGHT - h + 1;
	for (i = 0; i < BLOCK_WIDTH - w; i++) {

	    best2 = 0;
	    for (j = 0; j < w; j++) {
		/* If it's not going to fit, don't check again... */
		if (allocated[texnum][i + j] + h > BLOCK_HEIGHT) {
		    i += j + 1;
		    break;
		}
		if (allocated[texnum][i + j] >= best)
		    break;
		if (allocated[texnum][i + j] > best2)
		    best2 = allocated[texnum][i + j];
	    }
	    if (j == w) {	// this is a valid spot
		*x = i;
		*y = best = best2;
	    }
	}

	if (best + h > BLOCK_HEIGHT)
	    continue;

	for (i = 0; i < w; i++)
	    allocated[texnum][*x + i] = best + h;

	t2 = Sys_DoubleTime();
	alloc_block_time += t2 - t1;

	return texnum;
    }

    Sys_Error("%s: full", __func__);
}

mvertex_t *r_pcurrentvertbase;

static model_t *currentmodel;

/*
================
BuildSurfaceDisplayList
================
*/
static void
BuildSurfaceDisplayList(msurface_t *fa)
{
    int i, lindex, lnumverts;
    medge_t *pedges, *r_pedge;
    int vertpage;
    float *vec;
    float s, t;
    glpoly_t *poly;

// reconstruct the polygon
    pedges = currentmodel->edges;
    lnumverts = fa->numedges;
    vertpage = 0;

    //
    // draw texture
    //
    poly =
	Hunk_Alloc(sizeof(glpoly_t) + lnumverts * VERTEXSIZE * sizeof(float));
    poly->next = fa->polys;
    poly->flags = fa->flags;
    fa->polys = poly;
    poly->numverts = lnumverts;

    for (i = 0; i < lnumverts; i++) {
	lindex = currentmodel->surfedges[fa->firstedge + i];

	if (lindex > 0) {
	    r_pedge = &pedges[lindex];
	    vec = r_pcurrentvertbase[r_pedge->v[0]].position;
	} else {
	    r_pedge = &pedges[-lindex];
	    vec = r_pcurrentvertbase[r_pedge->v[1]].position;
	}
	s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
	s /= fa->texinfo->texture->width;

	t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
	t /= fa->texinfo->texture->height;

	VectorCopy(vec, poly->verts[i]);
	poly->verts[i][3] = s;
	poly->verts[i][4] = t;

	//
	// lightmap texture coordinates
	//
	s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
	s -= fa->texturemins[0];
	s += fa->light_s * 16;
	s += 8;
	s /= BLOCK_WIDTH * 16;	//fa->texinfo->texture->width;

	t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
	t -= fa->texturemins[1];
	t += fa->light_t * 16;
	t += 8;
	t /= BLOCK_HEIGHT * 16;	//fa->texinfo->texture->height;

	poly->verts[i][5] = s;
	poly->verts[i][6] = t;
    }
}

/*
========================
GL_CreateSurfaceLightmap
========================
*/
static void
GL_CreateSurfaceLightmap(msurface_t *surf)
{
    int smax, tmax;
    byte *base;

    if (surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB))
	return;

    smax = (surf->extents[0] >> 4) + 1;
    tmax = (surf->extents[1] >> 4) + 1;

    surf->lightmaptexturenum =
	AllocBlock(smax, tmax, &surf->light_s, &surf->light_t);
    base =
	lightmaps +
	surf->lightmaptexturenum * lightmap_bytes * BLOCK_WIDTH *
	BLOCK_HEIGHT;
    base += (surf->light_t * BLOCK_WIDTH + surf->light_s) * lightmap_bytes;
    R_BuildLightMap(surf, base, BLOCK_WIDTH * lightmap_bytes);
}


/*
==================
GL_BuildLightmaps

Builds the lightmap texture
with all the surfaces from all brush models
==================
*/
void
GL_BuildLightmaps(void)
{
    int i, j, cnt;
    float t1, t2;
    model_t *m;

    memset(allocated, 0, sizeof(allocated));
    lm_used = 0;
    alloc_block_time = 0;

    r_framecount = 1;		// no dlightcache

    // FIXME - move this to one of the init functions...
    if (!lightmap_textures_initialised) {
	glGenTextures(MAX_LIGHTMAPS, lightmap_textures);
	lightmap_textures_initialised = 1;
    }

    gl_lightmap_format = GL_LUMINANCE;
    if (COM_CheckParm("-lm_1"))
	gl_lightmap_format = GL_LUMINANCE;
    if (COM_CheckParm("-lm_a"))
	gl_lightmap_format = GL_ALPHA;
    if (COM_CheckParm("-lm_i"))
	gl_lightmap_format = GL_INTENSITY;
    if (COM_CheckParm("-lm_2"))
	gl_lightmap_format = GL_RGBA4;
    if (COM_CheckParm("-lm_4"))
	gl_lightmap_format = GL_RGBA;

    switch (gl_lightmap_format) {
    case GL_RGBA:
	lightmap_bytes = 4;
	break;
    case GL_RGBA4:
	lightmap_bytes = 2;
	break;
    case GL_LUMINANCE:
    case GL_INTENSITY:
    case GL_ALPHA:
	lightmap_bytes = 1;
	break;
    }

    t1 = Sys_DoubleTime();
    cnt = 0;

    for (j = 1; j < MAX_MODELS; j++) {
	m = cl.model_precache[j];
	if (!m)
	    break;
	if (m->name[0] == '*')
	    continue;

	r_pcurrentvertbase = m->vertexes;
	currentmodel = m;
	for (i = 0; i < m->numsurfaces; i++) {
	    cnt++;
	    GL_CreateSurfaceLightmap(m->surfaces + i);
	    if (m->surfaces[i].flags & SURF_DRAWTURB)
		continue;
	    if (m->surfaces[i].flags & SURF_DRAWSKY)
		continue;
	    BuildSurfaceDisplayList(m->surfaces + i);
	}
    }

    t2 = Sys_DoubleTime();
    Con_DPrintf("Built lightmaps in %f seconds.(%i surfs).\n", t2 - t1, cnt);
    Con_DPrintf("AllocBlock time spent: %f seconds.\n", alloc_block_time);

    //
    // upload all lightmaps that were filled
    //
    t1 = Sys_DoubleTime();
    for (i = 0; i < MAX_LIGHTMAPS; i++) {
	if (!allocated[i][0])
	    break;		// no more used
	lightmap_modified[i] = false;
	lightmap_rectchange[i].l = BLOCK_WIDTH;
	lightmap_rectchange[i].t = BLOCK_HEIGHT;
	lightmap_rectchange[i].w = 0;
	lightmap_rectchange[i].h = 0;
	GL_Bind(lightmap_textures[i]);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, lightmap_bytes, BLOCK_WIDTH,
		     BLOCK_HEIGHT, 0, gl_lightmap_format, GL_UNSIGNED_BYTE,
		     lightmaps +
		     i * BLOCK_WIDTH * BLOCK_HEIGHT * lightmap_bytes);
    }
    t2 = Sys_DoubleTime();
    Con_DPrintf("Uploaded %i lightmaps in %f seconds.\n", i, t2 - t1);
}
