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
// gl_warp.c -- sky and water polygons

#include <float.h>

#include "console.h"
#include "gl_model.h"
#include "glquake.h"
#include "quakedef.h"
#include "sys.h"

// FIXME - header hacks
extern model_t *loadmodel;
extern cvar_t gl_subdivide_size;

static float speedscale;	// for top sky and bottom sky
static float speedscale2;	// for sky alpha layer using multitexture

msurface_t *warpface;

void
BoundPoly(int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
    int i, j;
    float *v;

    mins[0] = mins[1] = mins[2] = FLT_MAX;
    maxs[0] = maxs[1] = maxs[2] = -FLT_MAX;
    v = verts;
    for (i = 0; i < numverts; i++)
	for (j = 0; j < 3; j++, v++) {
	    if (*v < mins[j])
		mins[j] = *v;
	    if (*v > maxs[j])
		maxs[j] = *v;
	}
}

void
SubdividePolygon(int numverts, float *verts)
{
    int i, j, k;
    vec3_t mins, maxs;
    float m;
    float *v;
    vec3_t front[64], back[64];
    int f, b;
    float dist[64];
    float frac;
    glpoly_t *poly;
    float s, t;

    if (numverts > 60)
	Sys_Error("numverts = %i", numverts);

    BoundPoly(numverts, verts, mins, maxs);

    for (i = 0; i < 3; i++) {
	m = (mins[i] + maxs[i]) * 0.5;
	m = gl_subdivide_size.value * floor(m / gl_subdivide_size.value +
					    0.5);
	if (maxs[i] - m < 8)
	    continue;
	if (m - mins[i] < 8)
	    continue;

	// cut it
	v = verts + i;
	for (j = 0; j < numverts; j++, v += 3)
	    dist[j] = *v - m;

	// wrap cases
	dist[j] = dist[0];
	v -= i;
	VectorCopy(verts, v);

	f = b = 0;
	v = verts;
	for (j = 0; j < numverts; j++, v += 3) {
	    if (dist[j] >= 0) {
		VectorCopy(v, front[f]);
		f++;
	    }
	    if (dist[j] <= 0) {
		VectorCopy(v, back[b]);
		b++;
	    }
	    if (dist[j] == 0 || dist[j + 1] == 0)
		continue;
	    if ((dist[j] > 0) != (dist[j + 1] > 0)) {
		// clip point
		frac = dist[j] / (dist[j] - dist[j + 1]);
		for (k = 0; k < 3; k++)
		    front[f][k] = back[b][k] =
			v[k] + frac * (v[3 + k] - v[k]);
		f++;
		b++;
	    }
	}

	SubdividePolygon(f, front[0]);
	SubdividePolygon(b, back[0]);
	return;
    }

    poly =
	Hunk_Alloc(sizeof(glpoly_t) + numverts * VERTEXSIZE * sizeof(float));
    poly->next = warpface->polys;
    warpface->polys = poly;
    poly->numverts = numverts;
    for (i = 0; i < numverts; i++, verts += 3) {
	VectorCopy(verts, poly->verts[i]);
	s = DotProduct(verts, warpface->texinfo->vecs[0]);
	t = DotProduct(verts, warpface->texinfo->vecs[1]);
	poly->verts[i][3] = s;
	poly->verts[i][4] = t;
    }
}

/*
================
GL_SubdivideSurface

Breaks a polygon up along axial 64 unit
boundaries so that turbulent and sky warps
can be done reasonably.
================
*/
void
GL_SubdivideSurface(msurface_t *fa)
{
    vec3_t verts[64];
    int numverts;
    int i;
    int lindex;
    float *vec;

    warpface = fa;

    //
    // convert edges back to a normal polygon
    //
    numverts = 0;
    for (i = 0; i < fa->numedges; i++) {
	lindex = loadmodel->surfedges[fa->firstedge + i];

	if (lindex > 0)
	    vec = loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position;
	else
	    vec =
		loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position;
	VectorCopy(vec, verts[numverts]);
	numverts++;
    }
    SubdividePolygon(numverts, verts[0]);
}

//=========================================================


// speed up sin calculations - Ed
// FIXME - calculate this at (GL) starup
float turbsin[256] = {
#include "gl_warp_sin.h"
};

#define TURBSCALE (256.0 / (2 * M_PI))

/*
=============
EmitWaterPolys

Does a water warp on the pre-fragmented glpoly_t chain
=============
*/
void
EmitWaterPolys(msurface_t *fa)
{
    glpoly_t *p;
    float *v;
    int i;
    float s, t, os, ot;

    for (p = fa->polys; p; p = p->next) {
	glBegin(GL_POLYGON);
	for (i = 0, v = p->verts[0]; i < p->numverts; i++, v += VERTEXSIZE) {
	    os = v[3];
	    ot = v[4];

	    s = os + turbsin[(int)((ot * 0.125 + realtime) * TURBSCALE) & 255];
	    s *= (1.0 / 64);

	    t = ot + turbsin[(int)((os * 0.125 + realtime) * TURBSCALE) & 255];
	    t *= (1.0 / 64);

	    glTexCoord2f(s, t);
	    glVertex3fv(v);
	}
	glEnd();
    }
}


/*
=============
EmitSkyPolys
=============
*/
void
EmitSkyPolys(msurface_t *fa)
{
    glpoly_t *p;
    float *v;
    int i;
    float s, t;
    vec3_t dir;
    float length;

    for (p = fa->polys; p; p = p->next) {
	glBegin(GL_POLYGON);
	for (i = 0, v = p->verts[0]; i < p->numverts; i++, v += VERTEXSIZE) {
	    VectorSubtract(v, r_origin, dir);
	    dir[2] *= 3;	// flatten the sphere

	    length = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
	    length = sqrt(length);
	    length = 6 * 63 / length;

	    dir[0] *= length;
	    dir[1] *= length;

	    s = (speedscale + dir[0]) * (1.0 / 128);
	    t = (speedscale + dir[1]) * (1.0 / 128);

	    glTexCoord2f(s, t);
	    glVertex3fv(v);
	}
	glEnd();
    }
}

/*
=================
EmitSkyPolysMtex
  EXPERIMENTAL: use multitexture on sky...
=================
*/
void
EmitSkyPolysMtex(msurface_t *fa)
{
    glpoly_t *p;
    float *v;
    int i;
    float s, t;
    vec3_t dir;
    float length;

    for (p = fa->polys; p; p = p->next) {
	glBegin(GL_POLYGON);
	for (i = 0, v = p->verts[0]; i < p->numverts; i++, v += VERTEXSIZE) {
	    VectorSubtract(v, r_origin, dir);
	    dir[2] *= 3;	// flatten the sphere

	    length = DotProduct(dir, dir);
	    length = sqrt(length);
	    length = 6 * 63 / length;

	    dir[0] *= length;
	    dir[1] *= length;

	    s = (speedscale + dir[0]) * (1.0 / 128);
	    t = (speedscale + dir[1]) * (1.0 / 128);
	    qglMultiTexCoord2fARB(GL_TEXTURE0_ARB, s, t);

	    s = (speedscale2 + dir[0]) * (1.0 / 128);
	    t = (speedscale2 + dir[1]) * (1.0 / 128);
	    qglMultiTexCoord2fARB(GL_TEXTURE1_ARB, s, t);

	    glVertex3fv(v);
	}
	glEnd();
    }
}

/*
===============
EmitBothSkyLayers

Does a sky warp on the pre-fragmented glpoly_t chain
This will be called for brushmodels, the world
will have them chained together.
===============
*/
void
EmitBothSkyLayers(msurface_t *fa)
{
    texture_t *t = fa->texinfo->texture;

    GL_DisableMultitexture();

    GL_Bind(t->gl_texturenum);
    speedscale = realtime * 8;
    speedscale -= (int)speedscale & ~127;

    EmitSkyPolys(fa);

    glEnable(GL_BLEND);
    GL_Bind(t->gl_texturenum_alpha);
    speedscale = realtime * 16;
    speedscale -= (int)speedscale & ~127;

    EmitSkyPolys(fa);

    glDisable(GL_BLEND);
}

/*
=================
R_DrawSkyChain
=================
*/
void
R_DrawSkyChain(msurface_t *s)
{
    msurface_t *fa;
    texture_t *t = s->texinfo->texture;

    if (gl_mtexable) {
	GL_SelectTexture(GL_TEXTURE0_ARB);
	GL_Bind(t->gl_texturenum);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

	GL_EnableMultitexture();
	GL_Bind(t->gl_texturenum_alpha);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);

	speedscale = realtime * 8;
	speedscale -= (int)speedscale & ~127;

	speedscale2 = realtime * 16;
	speedscale2 -= (int)speedscale2 & ~127;

	for (fa = s; fa; fa = fa->texturechain)
	    EmitSkyPolysMtex(fa);

	GL_DisableMultitexture();

    } else {
	GL_DisableMultitexture();

	GL_Bind(t->gl_texturenum);
	speedscale = realtime * 8;
	speedscale -= (int)speedscale & ~127;

	for (fa = s; fa; fa = fa->texturechain)
	    EmitSkyPolys(fa);

	glEnable(GL_BLEND);
	GL_Bind(t->gl_texturenum_alpha);
	speedscale = realtime * 16;
	speedscale -= (int)speedscale & ~127;

	for (fa = s; fa; fa = fa->texturechain)
	    EmitSkyPolys(fa);

	glDisable(GL_BLEND);
    }
}

//===============================================================

/*
=============
R_InitSky

A sky texture is 256*128, with the right side being a masked overlay
==============
*/
void
R_InitSky(texture_t *mt)
{
    int i, j, p;
    byte *src;
    unsigned trans[128 * 128];
    unsigned transpix;
    int r, g, b;
    unsigned *rgba;

    src = (byte *)mt + mt->offsets[0];

    // make an average value for the back to avoid
    // a fringe on the top level

    r = g = b = 0;
    for (i = 0; i < 128; i++)
	for (j = 0; j < 128; j++) {
	    p = src[i * 256 + j + 128];
	    rgba = &d_8to24table[p];
	    trans[(i * 128) + j] = *rgba;
	    r += ((byte *)rgba)[0];
	    g += ((byte *)rgba)[1];
	    b += ((byte *)rgba)[2];
	}

    ((byte *)&transpix)[0] = r / (128 * 128);
    ((byte *)&transpix)[1] = g / (128 * 128);
    ((byte *)&transpix)[2] = b / (128 * 128);
    ((byte *)&transpix)[3] = 0;

    // FIXME - move this to some init function
    glGenTextures(1, &mt->gl_texturenum);
    GL_Bind(mt->gl_texturenum);
    glTexImage2D(GL_TEXTURE_2D, 0, gl_solid_format, 128, 128, 0, GL_RGBA,
		 GL_UNSIGNED_BYTE, trans);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    for (i = 0; i < 128; i++)
	for (j = 0; j < 128; j++) {
	    p = src[i * 256 + j];
	    if (p == 0)
		trans[(i * 128) + j] = transpix;
	    else
		trans[(i * 128) + j] = d_8to24table[p];
	}

    // FIXME - move this to an init function...
    glGenTextures(1, &mt->gl_texturenum_alpha);
    GL_Bind(mt->gl_texturenum_alpha);
    glTexImage2D(GL_TEXTURE_2D, 0, gl_alpha_format, 128, 128, 0, GL_RGBA,
		 GL_UNSIGNED_BYTE, trans);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}
