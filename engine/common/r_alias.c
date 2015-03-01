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
// r_alias.c: routines for setting up to draw alias models

#include "console.h"
#include "cvar.h"
#include "model.h"
#include "quakedef.h"
#include "r_local.h"
#include "sys.h"

/* FIXME: shouldn't be needed (is needed for patch right now, but that should
          move) */
#include "d_local.h"

/* lowest light value we'll allow, to avoid the need for inner-loop light
   clamping */
#define LIGHT_MIN 5

affinetridesc_t r_affinetridesc;
trivertx_t *r_apverts;

void *acolormap;		// FIXME: should go away

// TODO: these probably will go away with optimized rasterization
vec3_t r_plightvec;
int r_ambientlight;
float r_shadelight;
static float ziscale;
static model_t *pmodel;

static vec3_t alias_forward, alias_right, alias_up;

int r_amodels_drawn;
int a_skinwidth;
int r_anumverts;

float aliastransform[3][4];

typedef struct {
    int index0;
    int index1;
} aedge_t;

#ifdef NQ_HACK
/*
 * incomplete model interpolation support
 * -> default to off and don't save to config for now
 */
cvar_t r_lerpmodels = { "r_lerpmodels", "0", false };
cvar_t r_lerpmove = { "r_lerpmove", "0", false };
#endif

static aedge_t aedges[12] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 5}, {1, 4}, {2, 7}, {3, 6}
};

#define NUMVERTEXNORMALS	162

float r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

static void R_AliasSetUpTransform(const entity_t *e, aliashdr_t *pahdr,
				  int trivial_accept);
static void R_AliasTransformVector(const vec3_t in, vec3_t out);
static void R_AliasTransformFinalVert(finalvert_t *fv, auxvert_t *av,
				      trivertx_t *pverts, stvert_t *pstverts);

void R_AliasTransformAndProjectFinalVerts(finalvert_t *fv, stvert_t *pstverts);
void R_AliasProjectFinalVert(finalvert_t *fv, auxvert_t *av);

/*
 * Model Loader Functions
 */
static int SW_Aliashdr_Padding(void) { return offsetof(sw_aliashdr_t, ahdr); }

static void
SW_LoadSkinData(model_t *model, aliashdr_t *ahdr,
		const alias_skindata_t *skindata)
{
    int i, j, skinsize;
    byte *pixels;

    skinsize = ahdr->skinwidth * ahdr->skinheight;
    pixels = Hunk_Alloc(skindata->numskins * skinsize * r_pixbytes);
    ahdr->skindata = (byte *)pixels - (byte *)ahdr;

    for (i = 0; i < skindata->numskins; i++) {
	if (r_pixbytes == 1) {
	    memcpy(pixels, skindata->data[i], skinsize);
	} else if (r_pixbytes == 2) {
	    uint16_t *skin16 = (uint16_t *)pixels;
	    for (j = 0; j < skinsize; j++)
		skin16[j] = d_8to16table[skindata->data[i][j]];
	} else {
	    Sys_Error("%s: driver set invalid r_pixbytes: %d", __func__,
		      r_pixbytes);
	}
	pixels += skinsize * r_pixbytes;
    }
}

static void
SW_LoadMeshData(const model_t *model, aliashdr_t *hdr,
		const alias_meshdata_t *meshdata,
		const alias_posedata_t *posedata)
{
    int i;
    trivertx_t *verts;
    stvert_t *stverts;
    mtriangle_t *triangles;

    /*
     * Save the pose vertex data
     */
    verts = Hunk_Alloc(hdr->numposes * hdr->numverts * sizeof(*verts));
    hdr->posedata = (byte *)verts - (byte *)hdr;
    for (i = 0; i < hdr->numposes; i++) {
	memcpy(verts, posedata->verts[i], hdr->numverts * sizeof(*verts));
	verts += hdr->numverts;
    }

    /*
     * Save the s/t verts
     * => put s and t in 16.16 format
     */
    stverts = Hunk_Alloc(hdr->numverts * sizeof(*stverts));
    SW_Aliashdr(hdr)->stverts = (byte *)stverts - (byte *)hdr;
    for (i = 0; i < hdr->numverts; i++) {
	stverts[i].onseam = meshdata->stverts[i].onseam;
	stverts[i].s = meshdata->stverts[i].s << 16;
	stverts[i].t = meshdata->stverts[i].t << 16;
    }

    /*
     * Save the triangle data
     */
    triangles = Hunk_Alloc(hdr->numtris * sizeof(*triangles));
    SW_Aliashdr(hdr)->triangles = (byte *)triangles - (byte *)hdr;
    memcpy(triangles, meshdata->triangles, hdr->numtris * sizeof(*triangles));
}

static model_loader_t SW_Model_Loader = {
    .Aliashdr_Padding = SW_Aliashdr_Padding,
    .LoadSkinData = SW_LoadSkinData,
    .LoadMeshData = SW_LoadMeshData,
    .CacheDestructor = NULL,
};

const model_loader_t *
R_ModelLoader(void)
{
    return &SW_Model_Loader;
}

/*
================
R_AliasCheckBBox
================
*/
qboolean
R_AliasCheckBBox(entity_t *e)
{
    int i, flags, frame, numv;
    aliashdr_t *pahdr;
    float zi, basepts[8][3], v0, v1, frac;
    finalvert_t *pv0, *pv1, viewpts[16];
    auxvert_t *pa0, *pa1, viewaux[16];
    maliasframedesc_t *pframedesc;
    qboolean zclipped, zfullyclipped;
    unsigned anyclip, allclip;
    int minz;

// expand, rotate, and translate points into worldspace

    e->trivial_accept = 0;
    pmodel = e->model;
    pahdr = Mod_Extradata(pmodel);

    R_AliasSetUpTransform(e, pahdr, 0);

// construct the base bounding box for this frame
    frame = e->frame;
// TODO: don't repeat this check when drawing?
    if ((frame >= pahdr->numframes) || (frame < 0)) {
	Con_DPrintf("No such frame %d %s\n", frame, pmodel->name);
	frame = 0;
    }

    pframedesc = &pahdr->frames[frame];

// x worldspace coordinates
    basepts[0][0] = basepts[1][0] = basepts[2][0] = basepts[3][0] =
	(float)pframedesc->bboxmin.v[0];
    basepts[4][0] = basepts[5][0] = basepts[6][0] = basepts[7][0] =
	(float)pframedesc->bboxmax.v[0];

// y worldspace coordinates
    basepts[0][1] = basepts[3][1] = basepts[5][1] = basepts[6][1] =
	(float)pframedesc->bboxmin.v[1];
    basepts[1][1] = basepts[2][1] = basepts[4][1] = basepts[7][1] =
	(float)pframedesc->bboxmax.v[1];

// z worldspace coordinates
    basepts[0][2] = basepts[1][2] = basepts[4][2] = basepts[5][2] =
	(float)pframedesc->bboxmin.v[2];
    basepts[2][2] = basepts[3][2] = basepts[6][2] = basepts[7][2] =
	(float)pframedesc->bboxmax.v[2];

    zclipped = false;
    zfullyclipped = true;

    minz = 9999;
    for (i = 0; i < 8; i++) {
	R_AliasTransformVector(&basepts[i][0], &viewaux[i].fv[0]);

	if (viewaux[i].fv[2] < ALIAS_Z_CLIP_PLANE) {
	    // we must clip points that are closer than the near clip plane
	    viewpts[i].flags = ALIAS_Z_CLIP;
	    zclipped = true;
	} else {
	    if (viewaux[i].fv[2] < minz)
		minz = viewaux[i].fv[2];
	    viewpts[i].flags = 0;
	    zfullyclipped = false;
	}
    }


    if (zfullyclipped) {
	return false;		// everything was near-z-clipped
    }

    numv = 8;

    if (zclipped) {
	// organize points by edges, use edges to get new points (possible trivial
	// reject)
	for (i = 0; i < 12; i++) {
	    // edge endpoints
	    pv0 = &viewpts[aedges[i].index0];
	    pv1 = &viewpts[aedges[i].index1];
	    pa0 = &viewaux[aedges[i].index0];
	    pa1 = &viewaux[aedges[i].index1];

	    // if one end is clipped and the other isn't, make a new point
	    if (pv0->flags ^ pv1->flags) {
		frac = (ALIAS_Z_CLIP_PLANE - pa0->fv[2]) /
		    (pa1->fv[2] - pa0->fv[2]);
		viewaux[numv].fv[0] = pa0->fv[0] +
		    (pa1->fv[0] - pa0->fv[0]) * frac;
		viewaux[numv].fv[1] = pa0->fv[1] +
		    (pa1->fv[1] - pa0->fv[1]) * frac;
		viewaux[numv].fv[2] = ALIAS_Z_CLIP_PLANE;
		viewpts[numv].flags = 0;
		numv++;
	    }
	}
    }
// project the vertices that remain after clipping
    anyclip = 0;
    allclip = ALIAS_XY_CLIP_MASK;

// TODO: probably should do this loop in ASM, especially if we use floats
    for (i = 0; i < numv; i++) {
	// we don't need to bother with vertices that were z-clipped
	if (viewpts[i].flags & ALIAS_Z_CLIP)
	    continue;

	zi = 1.0 / viewaux[i].fv[2];

	// FIXME: do with chop mode in ASM, or convert to float
	v0 = (viewaux[i].fv[0] * xscale * zi) + xcenter;
	v1 = (viewaux[i].fv[1] * yscale * zi) + ycenter;

	flags = 0;

	if (v0 < r_refdef.fvrectx)
	    flags |= ALIAS_LEFT_CLIP;
	if (v1 < r_refdef.fvrecty)
	    flags |= ALIAS_TOP_CLIP;
	if (v0 > r_refdef.fvrectright)
	    flags |= ALIAS_RIGHT_CLIP;
	if (v1 > r_refdef.fvrectbottom)
	    flags |= ALIAS_BOTTOM_CLIP;

	anyclip |= flags;
	allclip &= flags;
    }

    if (allclip)
	return false;		// trivial reject off one side

#ifdef NQ_HACK
    /*
     * FIXME - Trivial accept not safe while lerping unless we check
     *         the bbox of both src and dst frames
     */
    if (r_lerpmodels.value)
	return true;
#endif

    e->trivial_accept = !anyclip & !zclipped;
    if (e->trivial_accept) {
	if (minz > (r_aliastransition + (pahdr->size * r_resfudge))) {
	    e->trivial_accept |= 2;
	}
    }

    return true;
}


/*
================
R_AliasTransformVector
================
*/
static void
R_AliasTransformVector(const vec3_t in, vec3_t out)
{
    out[0] = DotProduct(in, aliastransform[0]) + aliastransform[0][3];
    out[1] = DotProduct(in, aliastransform[1]) + aliastransform[1][3];
    out[2] = DotProduct(in, aliastransform[2]) + aliastransform[2][3];
}


/*
================
R_AliasPreparePoints

General clipped case
================
*/
static void
R_AliasPreparePoints(aliashdr_t *pahdr, finalvert_t *pfinalverts,
		     auxvert_t *pauxverts)
{
    int i;
    stvert_t *pstverts;
    finalvert_t *fv;
    auxvert_t *av;
    mtriangle_t *ptri;
    finalvert_t *pfv[3];

    pstverts = (stvert_t *)((byte *)pahdr + SW_Aliashdr(pahdr)->stverts);
    r_anumverts = pahdr->numverts;
    fv = pfinalverts;
    av = pauxverts;

    for (i = 0; i < r_anumverts; i++, fv++, av++, r_apverts++, pstverts++) {
	R_AliasTransformFinalVert(fv, av, r_apverts, pstverts);
	if (av->fv[2] < ALIAS_Z_CLIP_PLANE)
	    fv->flags |= ALIAS_Z_CLIP;
	else {
	    R_AliasProjectFinalVert(fv, av);
	    if (fv->v[0] < r_refdef.aliasvrect.x)
		fv->flags |= ALIAS_LEFT_CLIP;
	    if (fv->v[1] < r_refdef.aliasvrect.y)
		fv->flags |= ALIAS_TOP_CLIP;
	    if (fv->v[0] > r_refdef.aliasvrectright)
		fv->flags |= ALIAS_RIGHT_CLIP;
	    if (fv->v[1] > r_refdef.aliasvrectbottom)
		fv->flags |= ALIAS_BOTTOM_CLIP;
	}
    }

//
// clip and draw all triangles
//
    r_affinetridesc.numtriangles = 1;

    ptri = (mtriangle_t *)((byte *)pahdr + SW_Aliashdr(pahdr)->triangles);
    for (i = 0; i < pahdr->numtris; i++, ptri++) {
	pfv[0] = &pfinalverts[ptri->vertindex[0]];
	pfv[1] = &pfinalverts[ptri->vertindex[1]];
	pfv[2] = &pfinalverts[ptri->vertindex[2]];

	if (pfv[0]->flags & pfv[1]->flags & pfv[2]->
	    flags & (ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP))
	    continue;		// completely clipped

	if (!((pfv[0]->flags | pfv[1]->flags | pfv[2]->flags) & (ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP))) {	// totally unclipped
	    r_affinetridesc.pfinalverts = pfinalverts;
	    r_affinetridesc.ptriangles = ptri;
	    D_PolysetDraw();
	} else {		// partially clipped
	    R_AliasClipTriangle(ptri, pfinalverts, pauxverts);
	}
    }
}


/*
================
R_AliasSetUpTransform
================
*/
static void
R_AliasSetUpTransform(const entity_t *e, aliashdr_t *pahdr, int trivial_accept)
{
    int i;
    float rotationmatrix[3][4], t2matrix[3][4];
    static float tmatrix[3][4];
    static float viewmatrix[3][4];
    vec3_t angles;

// TODO: should really be stored with the entity instead of being reconstructed
// TODO: should use a look-up table
// TODO: could cache lazily, stored in the entity

#ifdef NQ_HACK
    if (r_lerpmove.value && e->previousanglestime != e->currentanglestime) {
	float delta = e->currentanglestime - e->previousanglestime;
	float frac = qclamp((cl.time - e->currentanglestime) / delta, 0.0, 1.0);
	vec3_t lerpvec;

	/* FIXME - hack to skip the viewent (weapon) */
	if (e == &cl.viewent)
	    goto nolerp;

	VectorSubtract(e->currentangles, e->previousangles, lerpvec);
	for (i = 0; i < 3; i++) {
	    if (lerpvec[i] >= 180.0f)
		lerpvec[i] -= 360.0f;
	    else if (lerpvec[i] < -180.0f)
		lerpvec[i] += 360.0f;
	}
	VectorMA(e->previousangles, frac, lerpvec, angles);
	angles[PITCH] = -angles[PITCH];
    } else
    nolerp:
#endif
    {
	angles[ROLL] = e->angles[ROLL];
	angles[PITCH] = -e->angles[PITCH];
	angles[YAW] = e->angles[YAW];
    }
    AngleVectors(angles, alias_forward, alias_right, alias_up);

    tmatrix[0][0] = pahdr->scale[0];
    tmatrix[1][1] = pahdr->scale[1];
    tmatrix[2][2] = pahdr->scale[2];

    tmatrix[0][3] = pahdr->scale_origin[0];
    tmatrix[1][3] = pahdr->scale_origin[1];
    tmatrix[2][3] = pahdr->scale_origin[2];

// TODO: can do this with simple matrix rearrangement

    for (i = 0; i < 3; i++) {
	t2matrix[i][0] = alias_forward[i];
	t2matrix[i][1] = -alias_right[i];
	t2matrix[i][2] = alias_up[i];
    }

    t2matrix[0][3] = -modelorg[0];
    t2matrix[1][3] = -modelorg[1];
    t2matrix[2][3] = -modelorg[2];

// FIXME: can do more efficiently than full concatenation
    R_ConcatTransforms(t2matrix, tmatrix, rotationmatrix);

// TODO: should be global, set when vright, etc., set
    VectorCopy(vright, viewmatrix[0]);
    VectorCopy(vup, viewmatrix[1]);
    VectorInverse(viewmatrix[1]);
    VectorCopy(vpn, viewmatrix[2]);

//      viewmatrix[0][3] = 0;
//      viewmatrix[1][3] = 0;
//      viewmatrix[2][3] = 0;

    R_ConcatTransforms(viewmatrix, rotationmatrix, aliastransform);

// do the scaling up of x and y to screen coordinates as part of the transform
// for the unclipped case (it would mess up clipping in the clipped case).
// Also scale down z, so 1/z is scaled 31 bits for free, and scale down x and y
// correspondingly so the projected x and y come out right
// FIXME: make this work for clipped case too?
    if (trivial_accept) {
	for (i = 0; i < 4; i++) {
	    aliastransform[0][i] *= aliasxscale *
		(1.0 / ((float)0x8000 * 0x10000));
	    aliastransform[1][i] *= aliasyscale *
		(1.0 / ((float)0x8000 * 0x10000));
	    aliastransform[2][i] *= 1.0 / ((float)0x8000 * 0x10000);

	}
    }
}


/*
================
R_AliasTransformFinalVert
================
*/
static void
R_AliasTransformFinalVert(finalvert_t *fv, auxvert_t *av,
			  trivertx_t *pverts, stvert_t *pstverts)
{
    int temp;
    float lightcos, *plightnormal;

    av->fv[0] = DotProduct(pverts->v, aliastransform[0]) +
	aliastransform[0][3];
    av->fv[1] = DotProduct(pverts->v, aliastransform[1]) +
	aliastransform[1][3];
    av->fv[2] = DotProduct(pverts->v, aliastransform[2]) +
	aliastransform[2][3];

    fv->v[2] = pstverts->s;
    fv->v[3] = pstverts->t;

    fv->flags = pstverts->onseam;

// lighting
    plightnormal = r_avertexnormals[pverts->lightnormalindex];
    lightcos = DotProduct(plightnormal, r_plightvec);
    temp = r_ambientlight;

    if (lightcos < 0) {
	temp += (int)(r_shadelight * lightcos);

	// clamp; because we limited the minimum ambient and shading light, we
	// don't have to clamp low light, just bright
	if (temp < 0)
	    temp = 0;
    }

    fv->v[4] = temp;
}

#ifndef USE_X86_ASM

/*
================
R_AliasTransformAndProjectFinalVerts
================
*/
void
R_AliasTransformAndProjectFinalVerts(finalvert_t *fv, stvert_t *pstverts)
{
    int i, temp;
    float lightcos, *plightnormal, zi;
    trivertx_t *pverts;

    pverts = r_apverts;

    for (i = 0; i < r_anumverts; i++, fv++, pverts++, pstverts++) {
	// transform and project
	zi = 1.0 / (DotProduct(pverts->v, aliastransform[2]) +
		    aliastransform[2][3]);

	// x, y, and z are scaled down by 1/2**31 in the transform, so 1/z is
	// scaled up by 1/2**31, and the scaling cancels out for x and y in the
	// projection
	fv->v[5] = zi;

	fv->v[0] = ((DotProduct(pverts->v, aliastransform[0]) +
		     aliastransform[0][3]) * zi) + aliasxcenter;
	fv->v[1] = ((DotProduct(pverts->v, aliastransform[1]) +
		     aliastransform[1][3]) * zi) + aliasycenter;

	fv->v[2] = pstverts->s;
	fv->v[3] = pstverts->t;
	fv->flags = pstverts->onseam;

	// lighting
	plightnormal = r_avertexnormals[pverts->lightnormalindex];
	lightcos = DotProduct(plightnormal, r_plightvec);
	temp = r_ambientlight;

	if (lightcos < 0) {
	    temp += (int)(r_shadelight * lightcos);

	    // clamp; because we limited the minimum ambient and shading light, we
	    // don't have to clamp low light, just bright
	    if (temp < 0)
		temp = 0;
	}

	fv->v[4] = temp;
    }
}

#endif


/*
================
R_AliasProjectFinalVert
================
*/
void
R_AliasProjectFinalVert(finalvert_t *fv, auxvert_t *av)
{
    float zi;

// project points
    zi = 1.0 / av->fv[2];

    fv->v[5] = zi * ziscale;

    fv->v[0] = (av->fv[0] * aliasxscale * zi) + aliasxcenter;
    fv->v[1] = (av->fv[1] * aliasyscale * zi) + aliasycenter;
}


/*
================
R_AliasPrepareUnclippedPoints
================
*/
static void
R_AliasPrepareUnclippedPoints(aliashdr_t *pahdr, finalvert_t *pfinalverts)
{
    stvert_t *pstverts;

    pstverts = (stvert_t *)((byte *)pahdr + SW_Aliashdr(pahdr)->stverts);
    r_anumverts = pahdr->numverts;

    R_AliasTransformAndProjectFinalVerts(pfinalverts, pstverts);

    if (r_affinetridesc.drawtype)
	D_PolysetDrawFinalVerts(pfinalverts, r_anumverts);

    r_affinetridesc.pfinalverts = pfinalverts;
    r_affinetridesc.ptriangles = (mtriangle_t *)((byte *)pahdr +
						 SW_Aliashdr(pahdr)->triangles);
    r_affinetridesc.numtriangles = pahdr->numtris;

    D_PolysetDraw();
}

/*
===============
R_AliasSetupSkin
===============
*/
static void
R_AliasSetupSkin(const entity_t *entity, aliashdr_t *aliashdr)
{
    const maliasskindesc_t *pskindesc;
    const float *intervals;
    int skinnum, numframes, frame;
    int skinbytes;
    byte *pdata;

    skinnum = entity->skinnum;
    if ((skinnum >= aliashdr->numskins) || (skinnum < 0)) {
	Con_DPrintf("%s: %s has no such skin (%d)\n",
		    __func__, entity->model->name, skinnum);
	skinnum = 0;
    }

    pskindesc = ((maliasskindesc_t *)((byte *)aliashdr + aliashdr->skindesc));
    pskindesc += skinnum;
    a_skinwidth = aliashdr->skinwidth;

    frame = pskindesc->firstframe;
    numframes = pskindesc->numframes;

    if (numframes > 1) {
	const float frametime = cl.time + entity->syncbase;
	intervals = (float *)((byte *)aliashdr + aliashdr->skinintervals);
	frame += Mod_FindInterval(intervals + frame, numframes, frametime);
    }

    skinbytes = aliashdr->skinwidth * aliashdr->skinheight * r_pixbytes;
    pdata = (byte *)aliashdr + aliashdr->skindata;
    pdata += frame * skinbytes;

    r_affinetridesc.pskin = pdata;
    r_affinetridesc.skinwidth = a_skinwidth;
    r_affinetridesc.seamfixupX16 = (a_skinwidth >> 1) << 16;
    r_affinetridesc.skinheight = aliashdr->skinheight;

#ifdef QW_HACK
    if (entity->scoreboard) {
	byte *base;

	if (!entity->scoreboard->skin)
	    Skin_Find(entity->scoreboard);
	base = Skin_Cache(entity->scoreboard->skin);
	if (base) {
	    r_affinetridesc.pskin = base;
	    r_affinetridesc.skinwidth = 320;
	    r_affinetridesc.skinheight = 200;
	}
    }
#endif
}

/*
================
R_AliasSetupLighting
================
*/
void
R_AliasSetupLighting(alight_t *plighting)
{

// guarantee that no vertex will ever be lit below LIGHT_MIN, so we don't have
// to clamp off the bottom
    r_ambientlight = plighting->ambientlight;

    if (r_ambientlight < LIGHT_MIN)
	r_ambientlight = LIGHT_MIN;

    r_ambientlight = (255 - r_ambientlight) << VID_CBITS;

    if (r_ambientlight < LIGHT_MIN)
	r_ambientlight = LIGHT_MIN;

    r_shadelight = plighting->shadelight;

    if (r_shadelight < 0)
	r_shadelight = 0;

    r_shadelight *= VID_GRADES;

// rotate the lighting vector into the model's frame of reference
    r_plightvec[0] = DotProduct(plighting->plightvec, alias_forward);
    r_plightvec[1] = -DotProduct(plighting->plightvec, alias_right);
    r_plightvec[2] = DotProduct(plighting->plightvec, alias_up);
}

#ifdef NQ_HACK
static trivertx_t *
R_AliasBlendPoseVerts(const entity_t *e, aliashdr_t *hdr, float blend)
{
    static trivertx_t blendverts[MAXALIASVERTS];
    trivertx_t *poseverts, *pv1, *pv2, *light;
    int i, blend0, blend1;

#define SHIFT 22
    blend1 = blend * (1 << SHIFT);
    blend0 = (1 << SHIFT) - blend1;

    poseverts = (trivertx_t *)((byte *)hdr + hdr->posedata);
    pv1 = poseverts + e->previouspose * hdr->numverts;
    pv2 = poseverts + e->currentpose * hdr->numverts;
    light = (blend < 0.5f) ? pv1 : pv2;
    poseverts = blendverts;

    for (i = 0; i < hdr->numverts; i++, poseverts++, pv1++, pv2++, light++) {
	poseverts->v[0] = (pv1->v[0] * blend0 + pv2->v[0] * blend1) >> SHIFT;
	poseverts->v[1] = (pv1->v[1] * blend0 + pv2->v[1] * blend1) >> SHIFT;
	poseverts->v[2] = (pv1->v[2] * blend0 + pv2->v[2] * blend1) >> SHIFT;
	poseverts->lightnormalindex = light->lightnormalindex;
    }
#undef SHIFT

    return blendverts;
}
#endif

/*
=================
R_AliasSetupFrame

set r_apverts
=================
*/
static void
R_AliasSetupFrame(entity_t *e, aliashdr_t *pahdr)
{
    int frame, pose, numposes;
    float *intervals;

    frame = e->frame;
    if ((frame >= pahdr->numframes) || (frame < 0)) {
	Con_DPrintf("%s: no such frame %d\n", __func__, frame);
	frame = 0;
    }

    pose = pahdr->frames[frame].firstpose;
    numposes = pahdr->frames[frame].numposes;

    if (numposes > 1) {
	intervals = (float *)((byte *)pahdr + pahdr->poseintervals) + pose;
	pose += Mod_FindInterval(intervals, numposes, cl.time + e->syncbase);
    }

#ifdef NQ_HACK
    if (r_lerpmodels.value) {
	float delta, time, blend;

	/* A few quick sanity checks to abort lerping */
	if (e->currentframetime < e->previousframetime)
	    goto nolerp;
	if (e->currentframetime - e->previousframetime > 1.0f)
	    goto nolerp;
	/* FIXME - hack to skip the viewent (weapon) */
	if (e == &cl.viewent)
	    goto nolerp;

	if (numposes > 1) {
	    /* FIXME - merge with Mod_FindInterval? */
	    int i;
	    float fullinterval, targettime;
	    fullinterval = intervals[numposes - 1];
	    time = cl.time + e->syncbase;
	    targettime = time - (int)(time / fullinterval) * fullinterval;
	    for (i = 0; i < numposes - 1; i++)
		if (intervals[i] > targettime)
		    break;

	    e->currentpose = pahdr->frames[e->currentframe].firstpose + i;
	    if (i == 0) {
		e->previouspose = pahdr->frames[e->currentframe].firstpose;
		e->previouspose += numposes - 1;
		time = targettime;
		delta = intervals[0];
	    } else {
		e->previouspose = e->currentpose - 1;
		time = targettime - intervals[i - 1];
		delta = intervals[i] - intervals[i - 1];
	    }
	} else {
	    e->currentpose = pahdr->frames[e->currentframe].firstpose;
	    e->previouspose = pahdr->frames[e->previousframe].firstpose;
	    time = cl.time - e->currentframetime;
	    delta = e->currentframetime - e->previousframetime;
	}
	blend = qclamp(time / delta, 0.0f, 1.0f);
	r_apverts = R_AliasBlendPoseVerts(e, pahdr, blend);

	return;
    }
 nolerp:
#endif
    r_apverts = (trivertx_t *)((byte *)pahdr + pahdr->posedata);
    r_apverts += pose * pahdr->numverts;
}


/*
================
R_AliasDrawModel
================
*/
void
R_AliasDrawModel(entity_t *e, alight_t *plighting)
{
    aliashdr_t *pahdr;
    finalvert_t *pfinalverts;
    finalvert_t finalverts[CACHE_PAD_ARRAY(MAXALIASVERTS, finalvert_t)];
    auxvert_t *pauxverts;
    auxvert_t auxverts[MAXALIASVERTS];

    r_amodels_drawn++;

// cache align
    pfinalverts = CACHE_ALIGN_PTR(finalverts);
    pauxverts = &auxverts[0];

    pahdr = Mod_Extradata(e->model);

    R_AliasSetupSkin(e, pahdr);
    R_AliasSetUpTransform(e, pahdr, e->trivial_accept);
    R_AliasSetupLighting(plighting);
    R_AliasSetupFrame(e, pahdr);

    if (!e->colormap)
	Sys_Error("%s: !e->colormap", __func__);

    r_affinetridesc.drawtype = (e->trivial_accept == 3) &&
	r_recursiveaffinetriangles;

    if (r_affinetridesc.drawtype) {
	D_PolysetUpdateTables();	// FIXME: precalc...
    } else {
#ifdef USE_X86_ASM
	D_Aff8Patch(e->colormap);
#endif
    }

    acolormap = e->colormap;

    if (e != &cl.viewent)
	ziscale = ((float)0x8000) * ((float)0x10000);
    else
	ziscale = ((float)0x8000) * ((float)0x10000) * 3.0;

    if (e->trivial_accept)
	R_AliasPrepareUnclippedPoints(pahdr, pfinalverts);
    else
	R_AliasPreparePoints(pahdr, pfinalverts, pauxverts);
}
