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

#include <stdint.h>

#include "cmd.h"
#include "console.h"
#include "quakedef.h"
#include "r_local.h"
#include "screen.h"
#include "sound.h"
#include "sys.h"
#include "view.h"

void *colormap;
float r_time1;
int r_numallocatededges;

qboolean r_recursiveaffinetriangles = true;

int r_pixbytes = 1;
float r_aliasuvscale = 1.0;
int r_outofsurfaces;
int r_outofedges;

static vec3_t viewlightvec;
static alight_t r_viewlighting = { 128, 192, viewlightvec };

qboolean r_dowarp, r_dowarpold, r_viewchanged;

int c_surf;
int r_maxsurfsseen, r_maxedgesseen;

static int r_cnumsurfs;
static qboolean r_surfsonstack;

byte *r_warpbuffer;

static byte *r_stack_start;

entity_t r_worldentity;

//
// view origin
//
vec3_t vup, base_vup;
vec3_t vpn, base_vpn;
vec3_t vright, base_vright;
vec3_t r_origin;

//
// screen size info
//
refdef_t r_refdef;
float xcenter, ycenter;
float xscale, yscale;
float xscaleinv, yscaleinv;
float xscaleshrink, yscaleshrink;
float aliasxscale, aliasyscale, aliasxcenter, aliasycenter;

int screenwidth;

float pixelAspect;
static float screenAspect;
static float verticalFieldOfView;
static float xOrigin, yOrigin;

mplane_t screenedge[4];

//
// refresh flags
//
int r_framecount = 1;		// so frame counts initialized to 0 don't match
int r_visframecount;
int r_polycount;
int r_drawnpolycount;

mleaf_t *r_viewleaf, *r_oldviewleaf;

texture_t *r_notexture_mip;

float r_aliastransition, r_resfudge;

int d_lightstylevalue[256];	// 8.8 fraction of base light value

float dp_time1, dp_time2, db_time1, db_time2, rw_time1, rw_time2;
float se_time1, se_time2, de_time1, de_time2, dv_time1, dv_time2;

cvar_t r_draworder = { "r_draworder", "0" };
cvar_t r_speeds = { "r_speeds", "0" };
cvar_t r_graphheight = { "r_graphheight", "15" };
cvar_t r_clearcolor = { "r_clearcolor", "2" };
cvar_t r_waterwarp = { "r_waterwarp", "1" };
cvar_t r_drawentities = { "r_drawentities", "1" };
cvar_t r_drawviewmodel = { "r_drawviewmodel", "1" };
cvar_t r_drawflat = { "r_drawflat", "0" };
cvar_t r_ambient = { "r_ambient", "0" };
cvar_t r_numsurfs = { "r_numsurfs", "0" };
cvar_t r_numedges = { "r_numedges", "0" };

cvar_t r_lockpvs = { "r_lockpvs", "0" };
cvar_t r_lockfrustum = { "r_lockfrustum", "0" };

cvar_t r_fullbright = {
    .name = "r_fullbright",
    .string = "0",
    .flags = CVAR_DEVELOPER
};

#ifdef QW_HACK
cvar_t r_netgraph = { "r_netgraph", "0" };
static cvar_t r_zgraph = { "r_zgraph", "0" };
#endif

static cvar_t r_timegraph = { "r_timegraph", "0" };
static cvar_t r_aliasstats = { "r_polymodelstats", "0" };
static cvar_t r_dspeeds = { "r_dspeeds", "0" };
static cvar_t r_reportsurfout = { "r_reportsurfout", "0" };
static cvar_t r_maxsurfs = { "r_maxsurfs", "0" };
static cvar_t r_reportedgeout = { "r_reportedgeout", "0" };
static cvar_t r_maxedges = { "r_maxedges", "0" };
static cvar_t r_aliastransbase = { "r_aliastransbase", "200" };
static cvar_t r_aliastransadj = { "r_aliastransadj", "100" };

/*
==================
R_InitTextures
==================
*/
void
R_InitTextures(void)
{
    int x, y, m;
    byte *dest;

// create a simple checkerboard texture for the default
    r_notexture_mip =
	Hunk_AllocName(sizeof(texture_t) + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2,
		       "notexture");

    r_notexture_mip->width = r_notexture_mip->height = 16;
    r_notexture_mip->offsets[0] = sizeof(texture_t);
    r_notexture_mip->offsets[1] = r_notexture_mip->offsets[0] + 16 * 16;
    r_notexture_mip->offsets[2] = r_notexture_mip->offsets[1] + 8 * 8;
    r_notexture_mip->offsets[3] = r_notexture_mip->offsets[2] + 4 * 4;

    for (m = 0; m < 4; m++) {
	dest = (byte *)r_notexture_mip + r_notexture_mip->offsets[m];
	for (y = 0; y < (16 >> m); y++) {
	    for (x = 0; x < (16 >> m); x++) {
		if ((y < (8 >> m)) ^ (x < (8 >> m)))
		    *dest++ = 0;
		else
		    *dest++ = 0xff;
	    }
	}
    }
}


/*
================
R_InitTurb
================
*/
static void
R_InitTurb(void)
{
    int i;

    for (i = 0; i < TURB_TABLE_SIZE; ++i) {
	sintable[i] = TURB_SURF_AMP
	    + sin(i * 3.14159 * 2 / TURB_CYCLE) * TURB_SURF_AMP;
	intsintable[i] = TURB_SCREEN_AMP
	    + sin(i * 3.14159 * 2 / TURB_CYCLE) * TURB_SCREEN_AMP;
    }
}


/*
===============
R_Init
===============
*/
void
R_Init(void)
{
    int dummy;

    // get stack position so we can guess if we are going to overflow
    r_stack_start = (byte *)&dummy;

    R_InitTurb();

    Cmd_AddCommand("timerefresh", R_TimeRefresh_f);
    Cmd_AddCommand("pointfile", R_ReadPointFile_f);

    Cvar_RegisterVariable(&r_draworder);
    Cvar_RegisterVariable(&r_speeds);
    Cvar_RegisterVariable(&r_graphheight);
    Cvar_RegisterVariable(&r_clearcolor);
    Cvar_RegisterVariable(&r_waterwarp);
    Cvar_RegisterVariable(&r_drawentities);
    Cvar_RegisterVariable(&r_drawviewmodel);
    Cvar_RegisterVariable(&r_drawflat);
    Cvar_RegisterVariable(&r_ambient);
    Cvar_RegisterVariable(&r_numsurfs);
    Cvar_RegisterVariable(&r_numedges);
#ifdef NQ_HACK
    Cvar_RegisterVariable(&r_lerpmodels);
    Cvar_RegisterVariable(&r_lerpmove);
#endif
    Cvar_RegisterVariable(&r_lockpvs);
    Cvar_RegisterVariable(&r_lockfrustum);

    Cvar_RegisterVariable(&r_fullbright);

    Cvar_RegisterVariable(&r_timegraph);
    Cvar_RegisterVariable(&r_aliasstats);
    Cvar_RegisterVariable(&r_dspeeds);
    Cvar_RegisterVariable(&r_reportsurfout);
    Cvar_RegisterVariable(&r_maxsurfs);
    Cvar_RegisterVariable(&r_reportedgeout);
    Cvar_RegisterVariable(&r_maxedges);
    Cvar_RegisterVariable(&r_aliastransbase);
    Cvar_RegisterVariable(&r_aliastransadj);

#ifdef QW_HACK
    Cvar_RegisterVariable(&r_netgraph);
    Cvar_RegisterVariable(&r_zgraph);
#endif

    Cvar_SetValue("r_maxedges", (float)NUMSTACKEDGES);
    Cvar_SetValue("r_maxsurfs", (float)NUMSTACKSURFACES);

    view_clipplanes[0].leftedge = true;
    view_clipplanes[1].rightedge = true;
    view_clipplanes[1].leftedge = view_clipplanes[2].leftedge =
	view_clipplanes[3].leftedge = false;
    view_clipplanes[0].rightedge = view_clipplanes[2].rightedge =
	view_clipplanes[3].rightedge = false;

    r_refdef.xOrigin = XCENTERING;
    r_refdef.yOrigin = YCENTERING;

    R_InitParticles();

// TODO: collect 386-specific code in one place
#ifdef USE_X86_ASM
    Sys_MakeCodeWriteable(R_EdgeCodeStart, R_EdgeCodeEnd);
#endif

    D_Init();
}

/*
===============
R_NewMap
===============
*/
void
R_NewMap(void)
{
    int i;

    memset(&r_worldentity, 0, sizeof(r_worldentity));
    r_worldentity.model = &cl.worldmodel->model;

// clear out efrags in case the level hasn't been reloaded
// FIXME: is this one short?
    for (i = 0; i < cl.worldmodel->numleafs; i++)
	cl.worldmodel->leafs[i].efrags = NULL;

    r_viewleaf = NULL;
    R_ClearParticles();

    r_cnumsurfs = r_maxsurfs.value;

    if (r_cnumsurfs <= MINSURFACES)
	r_cnumsurfs = MINSURFACES;

    if (r_cnumsurfs > NUMSTACKSURFACES) {
	surfaces = Hunk_AllocName(r_cnumsurfs * sizeof(surf_t), "surfaces");
	surface_p = surfaces;
	surf_max = &surfaces[r_cnumsurfs];
	r_surfsonstack = false;
	// surface 0 doesn't really exist; it's just a dummy because index 0
	// is used to indicate no edge attached to surface
	surfaces--;
	R_SurfacePatch();
    } else {
	r_surfsonstack = true;
    }

    r_maxedgesseen = 0;
    r_maxsurfsseen = 0;

    r_numallocatededges = r_maxedges.value;

    if (r_numallocatededges < MINEDGES)
	r_numallocatededges = MINEDGES;

    if (r_numallocatededges <= NUMSTACKEDGES) {
	auxedges = NULL;
    } else {
	auxedges = Hunk_AllocName(r_numallocatededges * sizeof(edge_t),
				  "edges");
    }

    r_dowarpold = false;
    r_viewchanged = false;
}


/*
===============
R_SetVrect
===============
*/
void
R_SetVrect(const vrect_t *pvrectin, vrect_t *pvrect, int lineadj)
{
    int h;
    float size;
    qboolean full;

#ifdef NQ_HACK
    full = (scr_viewsize.value >= 120.0f);
#endif
#ifdef QW_HACK
    full = (!cl_sbar.value && scr_viewsize.value >= 100.0f);
#endif
    size = qmin(scr_viewsize.value, 100.0f);

    /* Hide the status bar during intermission */
    if (cl.intermission) {
	full = true;
	size = 100.0;
	lineadj = 0;
    }
    size /= 100.0;

    if (full)
	h = pvrectin->height;
    else
	h = pvrectin->height - lineadj;

    pvrect->width = pvrectin->width * size;
    if (pvrect->width < 96) {
	size = 96.0 / pvrectin->width;
	pvrect->width = 96;	// min for icons
    }
    pvrect->width &= ~7;

    pvrect->height = pvrectin->height * size;
    if (!full) {
	if (pvrect->height > pvrectin->height - lineadj)
	    pvrect->height = pvrectin->height - lineadj;
    } else if (pvrect->height > pvrectin->height)
	pvrect->height = pvrectin->height;
    pvrect->height &= ~1;

    pvrect->x = (pvrectin->width - pvrect->width) / 2;
    if (full)
	pvrect->y = 0;
    else
	pvrect->y = (h - pvrect->height) / 2;
}


/*
===============
R_ViewChanged

Called every time the vid structure or r_refdef changes.
Guaranteed to be called before the first refresh
===============
*/
void
R_ViewChanged(vrect_t *pvrect, int lineadj, float aspect)
{
    int i;
    float res_scale;

    r_viewchanged = true;

    R_SetVrect(pvrect, &r_refdef.vrect, lineadj);

    extern qboolean fisheye_enabled;
    if (fisheye_enabled) {

        // Make render size a square
        int minsize = r_refdef.vrect.width;
        if (r_refdef.vrect.height < minsize)
           minsize = r_refdef.vrect.height;
        r_refdef.vrect.width = r_refdef.vrect.height = minsize;

        // set fov
        extern double renderfov;
        r_refdef.horizontalFieldOfView = 2.0 * tan(renderfov / 2);
    }
    else {
        r_refdef.horizontalFieldOfView = 2.0 * tan(r_refdef.fov_x / 360 * M_PI);
    }

    r_refdef.fvrectx = (float)r_refdef.vrect.x;
    r_refdef.fvrectx_adj = (float)r_refdef.vrect.x - 0.5;
    r_refdef.vrect_x_adj_shift20 = (r_refdef.vrect.x << 20) + (1 << 19) - 1;
    r_refdef.fvrecty = (float)r_refdef.vrect.y;
    r_refdef.fvrecty_adj = (float)r_refdef.vrect.y - 0.5;
    r_refdef.vrectright = r_refdef.vrect.x + r_refdef.vrect.width;
    r_refdef.vrectright_adj_shift20 =
	(r_refdef.vrectright << 20) + (1 << 19) - 1;
    r_refdef.fvrectright = (float)r_refdef.vrectright;
    r_refdef.fvrectright_adj = (float)r_refdef.vrectright - 0.5;
    r_refdef.vrectrightedge = (float)r_refdef.vrectright - 0.99;
    r_refdef.vrectbottom = r_refdef.vrect.y + r_refdef.vrect.height;
    r_refdef.fvrectbottom = (float)r_refdef.vrectbottom;
    r_refdef.fvrectbottom_adj = (float)r_refdef.vrectbottom - 0.5;

    r_refdef.aliasvrect.x = (int)(r_refdef.vrect.x * r_aliasuvscale);
    r_refdef.aliasvrect.y = (int)(r_refdef.vrect.y * r_aliasuvscale);
    r_refdef.aliasvrect.width = (int)(r_refdef.vrect.width * r_aliasuvscale);
    r_refdef.aliasvrect.height =
	(int)(r_refdef.vrect.height * r_aliasuvscale);
    r_refdef.aliasvrectright =
	r_refdef.aliasvrect.x + r_refdef.aliasvrect.width;
    r_refdef.aliasvrectbottom =
	r_refdef.aliasvrect.y + r_refdef.aliasvrect.height;

    if (fisheye_enabled) {
        pixelAspect = (float)r_refdef.vrect.height / r_refdef.vrect.width;
    }
    else {
        pixelAspect = aspect;
    }
    xOrigin = r_refdef.xOrigin;
    yOrigin = r_refdef.yOrigin;

    screenAspect = r_refdef.vrect.width * pixelAspect / r_refdef.vrect.height;
// 320*200 1.0 pixelAspect = 1.6 screenAspect
// 320*240 1.0 pixelAspect = 1.3333 screenAspect
// proper 320*200 pixelAspect = 0.8333333

    verticalFieldOfView = r_refdef.horizontalFieldOfView / screenAspect;

// values for perspective projection
// if math were exact, the values would range from 0.5 to to range+0.5
// hopefully they wll be in the 0.000001 to range+.999999 and truncate
// the polygon rasterization will never render in the first row or column
// but will definately render in the [range] row and column, so adjust the
// buffer origin to get an exact edge to edge fill
    xcenter = ((float)r_refdef.vrect.width * XCENTERING) +
	r_refdef.vrect.x - 0.5;
    aliasxcenter = xcenter * r_aliasuvscale;
    ycenter = ((float)r_refdef.vrect.height * YCENTERING) +
	r_refdef.vrect.y - 0.5;
    aliasycenter = ycenter * r_aliasuvscale;

    xscale = r_refdef.vrect.width / r_refdef.horizontalFieldOfView;
    aliasxscale = xscale * r_aliasuvscale;
    xscaleinv = 1.0 / xscale;
    yscale = xscale * pixelAspect;
    aliasyscale = yscale * r_aliasuvscale;
    yscaleinv = 1.0 / yscale;
    xscaleshrink =
	(r_refdef.vrect.width - 6) / r_refdef.horizontalFieldOfView;
    yscaleshrink = xscaleshrink * pixelAspect;

// left side clip
    screenedge[0].normal[0] =
	-1.0 / (xOrigin * r_refdef.horizontalFieldOfView);
    screenedge[0].normal[1] = 0;
    screenedge[0].normal[2] = 1;
    screenedge[0].type = PLANE_ANYZ;

// right side clip
    screenedge[1].normal[0] =
	1.0 / ((1.0 - xOrigin) * r_refdef.horizontalFieldOfView);
    screenedge[1].normal[1] = 0;
    screenedge[1].normal[2] = 1;
    screenedge[1].type = PLANE_ANYZ;

// top side clip
    screenedge[2].normal[0] = 0;
    screenedge[2].normal[1] = -1.0 / (yOrigin * verticalFieldOfView);
    screenedge[2].normal[2] = 1;
    screenedge[2].type = PLANE_ANYZ;

// bottom side clip
    screenedge[3].normal[0] = 0;
    screenedge[3].normal[1] = 1.0 / ((1.0 - yOrigin) * verticalFieldOfView);
    screenedge[3].normal[2] = 1;
    screenedge[3].type = PLANE_ANYZ;

    for (i = 0; i < 4; i++)
	VectorNormalize(screenedge[i].normal);

    res_scale =
	sqrt((double)(r_refdef.vrect.width * r_refdef.vrect.height) /
	     (320.0 * 152.0)) * (2.0 / r_refdef.horizontalFieldOfView);
    r_aliastransition = r_aliastransbase.value * res_scale;
    r_resfudge = r_aliastransadj.value * res_scale;

// TODO: collect 386-specific code in one place
#ifdef USE_X86_ASM
    if (r_pixbytes == 1) {
	Sys_MakeCodeWriteable(R_Surf8Start, R_Surf8End);
	colormap = vid.colormap;
	R_Surf8Patch();
    } else {
	Sys_MakeCodeWriteable(R_Surf16Start, R_Surf16End);
	colormap = vid.colormap16;
	R_Surf16Patch();
    }
#endif

    D_ViewChanged();
}


/*
===============
R_MarkSurfaces
===============
*/
static void
R_MarkSurfaces(void)
{
    const leafbits_t *pvs;
    leafblock_t check;
    int leafnum, i;
    mleaf_t *leaf;
    mnode_t *node;
    msurface_t **mark;
    qboolean pvs_changed;

    /*
     * If the PVS hasn't changed, no need to update bsp visframes,
     * just store the efrags.
     */
    pvs_changed = (r_viewleaf != r_oldviewleaf && !r_lockpvs.value);
    if (pvs_changed) {
	r_visframecount++;
	r_oldviewleaf = r_viewleaf;
    }

    pvs = Mod_LeafPVS(cl.worldmodel, r_viewleaf);
    foreach_leafbit(pvs, leafnum, check) {
	leaf = &cl.worldmodel->leafs[leafnum + 1];
	if (leaf->efrags)
	    R_StoreEfrags(&leaf->efrags);
	if (!pvs_changed)
	    continue;

	/* Mark the surfaces */
	mark = leaf->firstmarksurface;
	for (i = 0; i < leaf->nummarksurfaces; i++) {
	    (*mark)->visframe = r_visframecount;
	    mark++;
	}

	/* Mark the leaf and all parent nodes */
	node = (mnode_t *)leaf;
	do {
	    if (node->visframe == r_visframecount)
		break;
	    node->visframe = r_visframecount;
	    node = node->parent;
	} while (node);
    }
}

/*
=============
R_CullSurfaces
=============
*/
static void
R_CullSurfaces(brushmodel_t *brushmodel, vec3_t vieworg)
{
    int i, j;
    int side;
    mnode_t *node;
    msurface_t *surf;
    mplane_t *plane;
    vec_t dist;

    node = brushmodel->nodes;
    node->clipflags = 15;

    for (;;) {
	if (node->visframe != r_visframecount)
	    goto NodeUp;

	if (node->clipflags) {
	    /* Clip the node against the frustum */
	    for (i = 0; i < 4; i++) {
		if (!(node->clipflags & (1 << i)))
		    continue;
		plane = &view_clipplanes[i].plane;
		side = BoxOnPlaneSide(node->mins, node->maxs, plane);
		if (side == PSIDE_BACK) {
		    node->clipflags = BMODEL_FULLY_CLIPPED;
		    goto NodeUp;
		}
		if (side == PSIDE_FRONT)
		    node->clipflags &= ~(1 << i);
	    }
	}

	if (node->contents < 0)
	    goto NodeUp;

	surf = brushmodel->surfaces + node->firstsurface;
	for (i = 0; i < node->numsurfaces; i++, surf++) {
	    /* Clip the surfaces against the frustum */
	    surf->clipflags = node->clipflags;
	    for (j = 0; j < 4; j++) {
		if (!(node->clipflags & (1 << j)))
		    continue;
		plane = &view_clipplanes[j].plane;
		side = BoxOnPlaneSide(surf->mins, surf->maxs, plane);
		if (side == PSIDE_BACK) {
		    surf->clipflags = BMODEL_FULLY_CLIPPED;
		    break;
		}
		if (side == PSIDE_FRONT)
		    surf->clipflags &= ~(1 << j);
	    }
	    if (j < 4)
		continue;

	    /* Cull backward facing surfs */
	    if (surf->plane->type < 3) {
		dist = vieworg[surf->plane->type] - surf->plane->dist;
	    } else {
		dist = DotProduct(vieworg, surf->plane->normal);
		dist -= surf->plane->dist;
	    }
	    if (surf->flags & SURF_PLANEBACK) {
		if (dist > -BACKFACE_EPSILON)
		    surf->clipflags = BMODEL_FULLY_CLIPPED;
	    } else {
		if (dist < BACKFACE_EPSILON)
		    surf->clipflags = BMODEL_FULLY_CLIPPED;
	    }
	}

//  DownLeft:
	/* Don't descend into solid leafs because parent links are broken */
	if (node->children[0]->contents == CONTENTS_SOLID)
	    goto DownRight;
	node->children[0]->clipflags = node->clipflags;
	node = node->children[0];
	continue;

    DownRight:
	/* Don't descent into solid leafs because parent links are broken */
	if (node->children[1]->contents == CONTENTS_SOLID)
	    goto NodeUp;
	node->children[1]->clipflags = node->clipflags;
	node = node->children[1];
	continue;

    NodeUp:
	/* If we just processed the left node, go right */
	if (node->parent && node == node->parent->children[0]) {
	    node = node->parent;
	    goto DownRight;
	}

	/* If we were on a right branch, backtrack */
	while (node->parent && node == node->parent->children[1])
	    node = node->parent;

	/* If we still have a parent, we need to cross to the right */
	if (node->parent) {
	    node = node->parent;
	    goto DownRight;
	}

	/* If no more parents, we are done */
	break;
    }
}

/*
=============
R_CullSubmodelSurfaces
=============
*/
static void
R_CullSubmodelSurfaces(const brushmodel_t *submodel, const vec3_t vieworg,
		       int clipflags)
{
    int i, j, side;
    msurface_t *surf;
    mplane_t *plane;
    vec_t dist;

    surf = submodel->surfaces + submodel->firstmodelsurface;
    for (i = 0; i < submodel->nummodelsurfaces; i++, surf++) {
	/* Clip the surface against the frustum */
	surf->clipflags = clipflags;
	for (j = 0; j < 4; j++) {
	    if (!(surf->clipflags & (1 << j)))
		continue;
	    plane = &view_clipplanes[j].plane;
	    side = BoxOnPlaneSide(surf->mins, surf->maxs, plane);
	    if (side == PSIDE_BACK) {
		surf->clipflags = BMODEL_FULLY_CLIPPED;
		break;
	    }
	    if (side == PSIDE_FRONT)
		surf->clipflags &= ~(1 << j);
	}
	if (j < 4)
	    continue;

	/* Cull backward facing surfs */
	if (surf->plane->type < 3) {
	    dist = vieworg[surf->plane->type] - surf->plane->dist;
	} else {
	    dist = DotProduct(vieworg, surf->plane->normal);
	    dist -= surf->plane->dist;
	}
	if (surf->flags & SURF_PLANEBACK) {
	    if (dist > -BACKFACE_EPSILON)
		surf->clipflags = BMODEL_FULLY_CLIPPED;
	} else {
	    if (dist < BACKFACE_EPSILON)
		surf->clipflags = BMODEL_FULLY_CLIPPED;
	}
    }
}

/*
=============
R_DrawEntitiesOnList
=============
*/
static void
R_DrawEntitiesOnList(void)
{
    entity_t *e;
    int i, j;
    int lnum;
    alight_t lighting;

// FIXME: remove and do real lighting
    float lightvec[3] = { -1, 0, 0 };
    vec3_t dist;
    float add;

    if (!r_drawentities.value)
	return;

    for (i = 0; i < cl_numvisedicts; i++) {
	e = &cl_visedicts[i];
#ifdef NQ_HACK
	if (e == &cl_entities[cl.viewentity])
	    continue;		// don't draw the player
#endif
	switch (e->model->type) {
	case mod_sprite:
	    VectorCopy(e->origin, r_entorigin);
	    VectorSubtract(r_origin, r_entorigin, modelorg);
	    R_DrawSprite(e);
	    break;

	case mod_alias:
#ifdef NQ_HACK
	    if (r_lerpmove.value) {
		float delta = e->currentorigintime - e->previousorigintime;
		float frac = qclamp((cl.time - e->currentorigintime) / delta, 0.0, 1.0);
		vec3_t lerpvec;

		/* FIXME - hack to skip the viewent (weapon) */
		if (e == &cl.viewent)
		    goto nolerp;

		VectorSubtract(e->currentorigin, e->previousorigin, lerpvec);
		VectorMA(e->previousorigin, frac, lerpvec, r_entorigin);
	    } else
	    nolerp:
#endif
	    {
		VectorCopy(e->origin, r_entorigin);
	    }
	    VectorSubtract(r_origin, r_entorigin, modelorg);

	    // see if the bounding box lets us trivially reject, also sets
	    // trivial accept status
	    if (R_AliasCheckBBox(e)) {
		j = R_LightPoint(e->origin);

		lighting.ambientlight = j;
		lighting.shadelight = j;

		lighting.plightvec = lightvec;

		for (lnum = 0; lnum < MAX_DLIGHTS; lnum++) {
		    if (cl_dlights[lnum].die >= cl.time) {
			VectorSubtract(e->origin, cl_dlights[lnum].origin,
				       dist);
			add = cl_dlights[lnum].radius - Length(dist);

			if (add > 0)
			    lighting.ambientlight += add;
		    }
		}

		// clamp lighting so it doesn't overbright as much
		if (lighting.ambientlight > 128)
		    lighting.ambientlight = 128;
		if (lighting.ambientlight + lighting.shadelight > 192)
		    lighting.shadelight = 192 - lighting.ambientlight;

		R_AliasDrawModel(e, &lighting);
	    }
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
    entity_t *e;
// FIXME: remove and do real lighting
    float lightvec[3] = { -1, 0, 0 };
    int j;
    int lnum;
    vec3_t dist;
    float add;
    dlight_t *dl;

#ifdef NQ_HACK
    if (!r_drawviewmodel.value)
	return;
#endif
#ifdef QW_HACK
    if (!r_drawviewmodel.value || !Cam_DrawViewModel())
	return;
#endif

    if (cl.stats[STAT_ITEMS] & IT_INVISIBILITY)
	return;

    if (cl.stats[STAT_HEALTH] <= 0)
	return;

    e = &cl.viewent;
    if (!e->model)
	return;

    VectorCopy(e->origin, r_entorigin);
    VectorSubtract(r_origin, r_entorigin, modelorg);

    VectorCopy(vup, viewlightvec);
    VectorInverse(viewlightvec);

    j = R_LightPoint(e->origin);

    if (j < 24)
	j = 24;			// always give some light on gun
    r_viewlighting.ambientlight = j;
    r_viewlighting.shadelight = j;

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
	    r_viewlighting.ambientlight += add;
    }

// clamp lighting so it doesn't overbright as much
    if (r_viewlighting.ambientlight > 128)
	r_viewlighting.ambientlight = 128;
    if (r_viewlighting.ambientlight + r_viewlighting.shadelight > 192)
	r_viewlighting.shadelight = 192 - r_viewlighting.ambientlight;

    r_viewlighting.plightvec = lightvec;

    R_AliasDrawModel(e, &r_viewlighting);
}


/*
=============
R_ModelCheckBBox
=============
*/
static int
R_ModelCheckBBox(const entity_t *e, model_t *model,
		  const vec3_t mins, const vec3_t maxs)
{
    int i, side, clipflags;
    vec_t dist;

    clipflags = 0;

    if (e->angles[0] || e->angles[1] || e->angles[2]) {
	for (i = 0; i < 4; i++) {
	    dist = DotProduct(e->origin, view_clipplanes[i].plane.normal);
	    dist -= view_clipplanes[i].plane.dist;

	    if (dist <= -model->radius)
		return BMODEL_FULLY_CLIPPED;

	    if (dist <= model->radius)
		clipflags |= (1 << i);
	}
    } else {
	for (i = 0; i < 4; i++) {
	    side = BoxOnPlaneSide(mins, maxs, &view_clipplanes[i].plane);
	    if (side == PSIDE_BACK)
		return BMODEL_FULLY_CLIPPED;
	    if (side == PSIDE_BOTH)
		clipflags |= (1 << i);
	}
    }

    return clipflags;
}


/*
=============
R_DrawBEntitiesOnList
=============
*/
static void
R_DrawBEntitiesOnList(void)
{
    entity_t *entity;
    int i, j, clipflags;
    vec3_t oldorigin;
    model_t *model;
    brushmodel_t *brushmodel;
    vec3_t mins, maxs;

    if (!r_drawentities.value)
	return;

    VectorCopy(modelorg, oldorigin);
    r_dlightframecount = r_framecount;

    for (i = 0; i < cl_numvisedicts; i++) {
	entity = &cl_visedicts[i];
	if (entity->model->type != mod_brush)
	    continue;

	model = entity->model;
	brushmodel = BrushModel(model);

	// see if the bounding box lets us trivially reject, also sets
	// trivial accept status
	VectorAdd(entity->origin, model->mins, mins);
	VectorAdd(entity->origin, model->maxs, maxs);
	clipflags = R_ModelCheckBBox(entity, model, mins, maxs);

	if (clipflags == BMODEL_FULLY_CLIPPED)
	    continue;

	VectorCopy(entity->origin, r_entorigin);
	VectorSubtract(r_origin, r_entorigin, modelorg);

	// FIXME: stop transforming twice
	R_RotateBmodel(entity);

	// calculate dynamic lighting for bmodel if it's not an
	// instanced model
	if (brushmodel->firstmodelsurface != 0) {
	    for (j = 0; j < MAX_DLIGHTS; j++) {
		if ((cl_dlights[j].die < cl.time) || (!cl_dlights[j].radius))
		    continue;
		R_MarkLights(&cl_dlights[j], 1 << j,
			     brushmodel->nodes + brushmodel->hulls[0].firstclipnode);
	    }
	}

	r_pefragtopnode = NULL;
	VectorCopy(mins, r_emins);
	VectorCopy(maxs, r_emaxs);
	R_SplitEntityOnNode2(cl.worldmodel->nodes);
	R_CullSubmodelSurfaces(brushmodel, modelorg, clipflags);

	if (r_pefragtopnode) {
	    entity->topnode = r_pefragtopnode;

	    if (r_pefragtopnode->contents >= 0) {
		// not a leaf; has to be clipped to the world BSP
		R_DrawSolidClippedSubmodelPolygons(entity);
	    } else {
		// falls entirely in one leaf, so we just put all
		// the edges in the edge list and let 1/z sorting
		// handle drawing order
		R_DrawSubmodelPolygons(entity, clipflags);
	    }
	    entity->topnode = NULL;
	}

	// put back world rotation and frustum clipping
	// FIXME: R_RotateBmodel should just work off base_vxx
	VectorCopy(base_vpn, vpn);
	VectorCopy(base_vup, vup);
	VectorCopy(base_vright, vright);
	VectorCopy(base_modelorg, modelorg);
	VectorCopy(oldorigin, modelorg);
	R_TransformFrustum();
    }
}


/*
================
R_EdgeDrawing
================
*/
 __attribute__((noinline))
static void
R_EdgeDrawing(void)
{
    edge_t ledges[CACHE_PAD_ARRAY(NUMSTACKEDGES, edge_t)];
    surf_t lsurfs[CACHE_PAD_ARRAY(NUMSTACKSURFACES, surf_t)];

    if (auxedges) {
	r_edges = auxedges;
    } else {
	r_edges = CACHE_ALIGN_PTR(ledges);
    }

    if (r_surfsonstack) {
	surfaces = CACHE_ALIGN_PTR(lsurfs);
	surf_max = &surfaces[r_cnumsurfs];
	// surface 0 doesn't really exist; it's just a dummy because index 0
	// is used to indicate no edge attached to surface
	surfaces--;
	R_SurfacePatch();
    }

    R_BeginEdgeFrame();

    if (r_dspeeds.value) {
	rw_time1 = Sys_DoubleTime();
    }

    R_RenderWorld();

    // only the world can be drawn back to front with no z reads or compares,
    // just z writes, so have the driver turn z compares on now
    D_TurnZOn();

    if (r_dspeeds.value) {
	rw_time2 = Sys_DoubleTime();
	db_time1 = rw_time2;
    }

    R_DrawBEntitiesOnList();

    if (r_dspeeds.value) {
	db_time2 = Sys_DoubleTime();
	se_time1 = db_time2;
    }

    if (!r_dspeeds.value) {
	VID_UnlockBuffer();
	S_ExtraUpdate();	// don't let sound get messed up if going slow
	VID_LockBuffer();
    }

    R_ScanEdges();
}


/*
================
R_RenderView

r_refdef must be set before the first call
================
*/
static void
R_RenderView_(void)
{
    byte warpbuffer[WARP_WIDTH * WARP_HEIGHT];

    r_warpbuffer = warpbuffer;

    if (r_timegraph.value || r_speeds.value || r_dspeeds.value)
	r_time1 = Sys_DoubleTime();

    R_SetupFrame();
    R_MarkSurfaces();		// done here so we know if we're in water
    R_CullSurfaces(BrushModel(r_worldentity.model), r_refdef.vieworg);

    // make FDIV fast. This reduces timing precision after we've been running
    // for a while, so we don't do it globally.  This also sets chop mode, and
    // we do it here so that setup stuff like the refresh area calculations
    // match what's done in screen.c
    Sys_LowFPPrecision();

    if (!r_worldentity.model || !cl.worldmodel)
	Sys_Error("%s: NULL worldmodel", __func__);

    if (!r_dspeeds.value) {
	VID_UnlockBuffer();
	S_ExtraUpdate();	// don't let sound get messed up if going slow
	VID_LockBuffer();
    }

    R_EdgeDrawing();

    if (!r_dspeeds.value) {
	VID_UnlockBuffer();
	S_ExtraUpdate();	// don't let sound get messed up if going slow
	VID_LockBuffer();
    }

    if (r_dspeeds.value) {
	se_time2 = Sys_DoubleTime();
	de_time1 = se_time2;
    }

    R_DrawEntitiesOnList();

    if (r_dspeeds.value) {
	de_time2 = Sys_DoubleTime();
	dv_time1 = de_time2;
    }

    R_DrawViewModel();

    if (r_dspeeds.value) {
	dv_time2 = Sys_DoubleTime();
	dp_time1 = Sys_DoubleTime();
    }

    R_DrawParticles();

    if (r_dspeeds.value)
	dp_time2 = Sys_DoubleTime();

    if (r_dowarp)
	D_WarpScreen();

    V_SetContentsColor(r_viewleaf->contents);

    if (r_timegraph.value)
	R_TimeGraph();

#ifdef QW_HACK
    if (r_netgraph.value)
	R_NetGraph();

    if (r_zgraph.value)
	R_ZGraph();
#endif

    if (r_aliasstats.value)
	R_PrintAliasStats();

    if (r_speeds.value)
	R_PrintTimes();

    if (r_dspeeds.value)
	R_PrintDSpeeds();

    if (r_reportsurfout.value && r_outofsurfaces)
	Con_Printf("Short %d surfaces\n", r_outofsurfaces);

    if (r_reportedgeout.value && r_outofedges)
	Con_Printf("Short roughly %d edges\n", r_outofedges * 2 / 3);

    // back to high floating-point precision
    Sys_HighFPPrecision();
}

void
R_RenderView(void)
{
    int dummy;

    if (Hunk_LowMark() & 3)
	Sys_Error("Hunk is missaligned");

    if ((intptr_t)(&dummy) & 3)
	Sys_Error("Stack is missaligned");

    if ((intptr_t)(&r_warpbuffer) & 3)
	Sys_Error("Globals are missaligned");

    R_RenderView_();
}
