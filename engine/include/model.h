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

#ifndef MODEL_H
#define MODEL_H

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Use the GCC builtin ffsl function */
#ifndef ffsl
#define ffsl __builtin_ffsl
#endif

#ifdef GLQUAKE
#ifdef APPLE_OPENGL
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#include "bspfile.h"
#include "modelgen.h"
#include "spritegn.h"
#include "zone.h"

#ifdef NQ_HACK
#include "quakedef.h"
#endif
#ifdef QW_HACK
#include "bothdefs.h"
#endif

/*

d*_t structures are on-disk representations
m*_t structures are in-memory

*/

// entity effects
#define EF_BRIGHTFIELD	1
#define EF_MUZZLEFLASH 	2
#define EF_BRIGHTLIGHT 	4
#define EF_DIMLIGHT 	8
#define EF_FLAG1	16
#define EF_FLAG2	32
#define EF_BLUE		64
#define EF_RED		128

/*
==============================================================================

BRUSH MODELS

==============================================================================
*/


//
// in memory representation
//
// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct {
    vec3_t position;
} mvertex_t;

typedef struct texture_s {
    char name[16];
    unsigned width, height;
#ifdef GLQUAKE
    GLuint gl_texturenum;
    GLuint gl_texturenum_alpha;	// for sky texture
    struct msurface_s *texturechain;
#endif
    int anim_total;		// total tenths in sequence ( 0 = no)
    int anim_min, anim_max;	// time for this frame min <=time< max
    struct texture_s *anim_next;	// in the animation sequence
    struct texture_s *alternate_anims;	// bmodels in frmae 1 use these
    unsigned offsets[MIPLEVELS];	// four mip maps stored
} texture_t;


#define	SURF_PLANEBACK		(1 << 1)
#define	SURF_DRAWSKY		(1 << 2)
#define SURF_DRAWSPRITE		(1 << 3)
#define SURF_DRAWTURB		(1 << 4)
#define SURF_DRAWTILED		(1 << 5)
#define SURF_DRAWBACKGROUND	(1 << 6)
#ifdef GLQUAKE
#define SURF_UNDERWATER		(1 << 7)
#define SURF_DONTWARP		(1 << 8)
#endif

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct {
    unsigned int v[2];
    unsigned int cachededgeoffset;
} medge_t;

typedef struct {
    float vecs[2][4];
    float mipadjust;
    texture_t *texture;
    int flags;
} mtexinfo_t;

#ifdef GLQUAKE
#define	VERTEXSIZE	7
typedef struct glpoly_s {
    struct glpoly_s *next;
    struct glpoly_s *chain;
    int numverts;
    int flags;			// for SURF_UNDERWATER
    float verts[0][VERTEXSIZE];	// variable sized (xyz s1t1 s2t2)
} glpoly_t;
#endif

typedef struct msurface_s {
    int visframe;	// should be drawn when node is crossed
    int clipflags;	// flags for clipping against frustum
    vec3_t mins;	// bounding box for frustum culling
    vec3_t maxs;

    mplane_t *plane;
    int flags;

    int firstedge;	// look up in model->surfedges[], negative numbers
    int numedges;	// are backwards edges

#ifdef GLQUAKE
    int light_s;	// gl lightmap coordinates
    int light_t;
    int lightmaptexturenum;
    int cached_light[MAXLIGHTMAPS];	// values currently used in lightmap
    qboolean cached_dlight;	// true if dynamic light in cache
    glpoly_t *polys;	// multiple if warped
    struct msurface_s *texturechain;
#else
// surface generation data
    struct surfcache_s *cachespots[MIPLEVELS];
#endif

    short texturemins[2];
    short extents[2];

    mtexinfo_t *texinfo;

// lighting info
    int dlightframe;
    unsigned dlightbits;

    byte styles[MAXLIGHTMAPS];
    byte *samples;		// [numstyles*surfsize]
} msurface_t;

/*
 * foreach_surf_lightstyle()
 *   Iterator for lightmaps on a surface
 *     msurface_t *s => the surface
 *     int n         => lightmap number
 */
#define foreach_surf_lightstyle(s, n) \
	for ((n) = 0; (n) < MAXLIGHTMAPS && (s)->styles[n] != 255; (n)++)

typedef struct mnode_s {
// common with leaf
    int contents;		// 0, to differentiate from leafs
    int visframe;		// node needs to be traversed if current
    int clipflags;		// frustum plane clip flags

    vec3_t mins;		// for bounding box culling
    vec3_t maxs;

    struct mnode_s *parent;

// node specific
    mplane_t *plane;
    struct mnode_s *children[2];

    unsigned int firstsurface;
    unsigned int numsurfaces;
} mnode_t;

/* forward decls; can't include render.h/glquake.h */
struct efrag_s;
struct entity_s;

typedef struct mleaf_s {
// common with node
    int contents;		// wil be a negative contents number
    int visframe;		// node needs to be traversed if current
    int clipflags;		// frustum plane clip flags

    vec3_t mins;		// for bounding box culling
    vec3_t maxs;

    struct mnode_s *parent;

// leaf specific
    byte *compressed_vis;
    struct efrag_s *efrags;

    msurface_t **firstmarksurface;
    int nummarksurfaces;
    int key;			// BSP sequence number for leaf's contents
    byte ambient_sound_level[NUM_AMBIENTS];
} mleaf_t;

// !!! if this is changed, it must be changed in asm_i386.h too !!!
typedef struct {
    const mclipnode_t *clipnodes;
    const mplane_t *planes;
    int firstclipnode;
    int lastclipnode;
    vec3_t clip_mins;
    vec3_t clip_maxs;
} hull_t;

/*
==============================================================================

SPRITE MODELS

==============================================================================
*/


// FIXME: shorten these?
typedef struct mspriteframe_s {
    int width;
    int height;
    float up, down, left, right;
    byte rdata[];	/* Renderer data, variable sized */
} mspriteframe_t;

/*
 * Renderer provides this function to specify the amount of space it needs for
 * a sprite frame with given pixel count
 */
int R_SpriteDataSize(int numpixels);

/*
 * Renderer provides this function to translate and store the raw sprite data
 * from the model file as needed.
 */
void R_SpriteDataStore(mspriteframe_t *frame, const char *modelname,
		       int framenum, byte *pixels);

typedef struct {
    int numframes;
    float *intervals;
    mspriteframe_t *frames[];	/* variable sized */
} mspritegroup_t;

typedef struct {
    spriteframetype_t type;
    union {
	mspriteframe_t *frame;
	mspritegroup_t *group;
    } frame;
} mspriteframedesc_t;

typedef struct {
    int type;
    int maxwidth;
    int maxheight;
    int numframes;
    float beamlength;		// remove?
    mspriteframedesc_t frames[];	/* variable sized */
} msprite_t;

/*
==============================================================================

ALIAS MODELS

Alias models are position independent, so the cache manager can move them.
==============================================================================
*/

typedef struct {
    int firstpose;
    int numposes;
    trivertx_t bboxmin;
    trivertx_t bboxmax;
    int frame;
    char name[16];
} maliasframedesc_t;

typedef struct {
    int firstframe;
    int numframes;
} maliasskindesc_t;

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct mtriangle_s {
    int facesfront;
    int vertindex[3];
} mtriangle_t;

typedef struct {
    vec3_t scale;
    vec3_t scale_origin;
    int numskins;
    int skindesc;
    int skinintervals;
    int skindata;
    int skinwidth;
    int skinheight;
    int numverts;
    int numtris;
    int numframes;
    float size;
    int numposes;
    int poseintervals;
    int posedata;	// (numposes * numverts) trivertx_t
    maliasframedesc_t frames[];	// variable sized
} aliashdr_t;

#ifdef GLQUAKE

typedef struct {
    int commands;	// gl command list with embedded s/t
    int textures;	/* Offset to GLuint texture names */
    aliashdr_t ahdr;
} gl_aliashdr_t;

static inline gl_aliashdr_t *
GL_Aliashdr(aliashdr_t *h)
{
    return container_of(h, gl_aliashdr_t, ahdr);
}

#else

typedef struct {
    int stverts;
    int triangles;
    aliashdr_t ahdr;
} sw_aliashdr_t;

static inline sw_aliashdr_t *
SW_Aliashdr(aliashdr_t *h)
{
    return container_of(h, sw_aliashdr_t, ahdr);
}

#endif

#define	MAXALIASVERTS	2048
#define	MAXALIASFRAMES	512
#define	MAXALIASTRIS	4096

//===================================================================

//
// Whole model
//

typedef enum { mod_brush, mod_sprite, mod_alias } modtype_t;

#define	EF_ROCKET	1	// leave a trail
#define	EF_GRENADE	2	// leave a trail
#define	EF_GIB		4	// leave a trail
#define	EF_ROTATE	8	// rotate (bonus items)
#define	EF_TRACER	16	// green split trail
#define	EF_ZOMGIB	32	// small blood trail
#define	EF_TRACER2	64	// orange split trail + rotate
#define	EF_TRACER3	128	// purple trail

typedef struct model_s {
    char name[MAX_QPATH];
    struct model_s *next;

    modtype_t type;
    int numframes;
    synctype_t synctype;

    int flags;

//
// volume occupied by the model graphics
//
    vec3_t mins, maxs;
    float radius;

//
// additional model data
//
    cache_user_t cache;		// only access through Mod_Extradata

} model_t;

/*
 * Brush (BSP) model
 */
typedef struct brushmodel_s {
    struct brushmodel_s *next;
    model_t model;

    int firstmodelsurface;
    int nummodelsurfaces;

    int numsubmodels;
    dmodel_t *submodels;

    int numplanes;
    mplane_t *planes;

    int numleafs;		// number of visible leafs, not counting 0
    mleaf_t *leafs;

    int numvertexes;
    mvertex_t *vertexes;

    int numedges;
    medge_t *edges;

    int numnodes;
    mnode_t *nodes;

    int numtexinfo;
    mtexinfo_t *texinfo;

    int numsurfaces;
    msurface_t *surfaces;

    int numsurfedges;
    int *surfedges;

    int numclipnodes;
    mclipnode_t *clipnodes;

    int nummarksurfaces;
    msurface_t **marksurfaces;

    hull_t hulls[MAX_MAP_HULLS];

    int numtextures;
    texture_t **textures;

#ifdef QW_HACK
    unsigned checksum;		// for world models only
    unsigned checksum2;		// for world models only
#endif

    byte *visdata;
    byte *lightdata;
    char *entities;
} brushmodel_t;

static inline const brushmodel_t *
ConstBrushModel(const model_t *model)
{
    assert(model && model->type == mod_brush);
    return const_container_of(model, brushmodel_t, model);
}

static inline brushmodel_t *
BrushModel(model_t *model)
{
    assert(model && model->type == mod_brush);
    return container_of(model, brushmodel_t, model);
}

/* Alias model loader structures */
typedef struct {
    mtriangle_t *triangles;
    stvert_t *stverts;
} alias_meshdata_t;

typedef struct {
    int numposes;
    const trivertx_t **verts;
} alias_posedata_t;

typedef struct {
    int numskins;
    byte **data;
} alias_skindata_t;

typedef struct model_loader {
    int (*Aliashdr_Padding)(void);
    void (*LoadSkinData)(model_t *, aliashdr_t *, const alias_skindata_t *);
    void (*LoadMeshData)(const model_t *, aliashdr_t *hdr,
			 const alias_meshdata_t *, const alias_posedata_t *);
    void (*CacheDestructor)(cache_user_t *);
} model_loader_t;

//============================================================================

void Mod_Init(const model_loader_t *loader);
void *Mod_AllocName(int size, const char *name); /* Internal helper */
#ifndef SERVERONLY
void Mod_InitAliasCache(void);
void Mod_ClearAlias(void);
model_t *Mod_NewAliasModel(void);
model_t *Mod_FindAliasName(const char *name);
const model_t *Mod_AliasCache(void);
const model_t *Mod_AliasOverflow(void);
#endif
void Mod_ClearAll(void);
model_t *Mod_ForName(const char *name, qboolean crash);
void *Mod_Extradata(model_t *model);	// handles caching
void Mod_TouchModel(const char *name);
void Mod_Print(void);

/*
 * PVS/PHS information
 */
typedef unsigned long leafblock_t;
typedef struct {
    int numleafs;
    leafblock_t bits[]; /* Variable Sized */
} leafbits_t;

mleaf_t *Mod_PointInLeaf(const brushmodel_t *model, const vec3_t point);
const leafbits_t *Mod_LeafPVS(const brushmodel_t *model, const mleaf_t *leaf);
const leafbits_t *Mod_FatPVS(const brushmodel_t *model, const vec3_t point);

int __ERRORLONGSIZE(void); /* to generate an error at link time */
#define QBYTESHIFT(x) ((x) == 8 ? 6 : ((x) == 4 ? 5 : __ERRORLONGSIZE() ))
#define LEAFSHIFT QBYTESHIFT(sizeof(leafblock_t))
#define LEAFMASK  ((sizeof(leafblock_t) << 3) - 1UL)

static inline qboolean
Mod_TestLeafBit(const leafbits_t *bits, int leafnum)
{
    return !!(bits->bits[leafnum >> LEAFSHIFT] & (1UL << (leafnum & LEAFMASK)));
}

static inline size_t
Mod_LeafbitsSize(int numleafs)
{
    return offsetof(leafbits_t, bits[(numleafs + LEAFMASK) >> LEAFSHIFT]);
}

static inline int
Mod_NextLeafBit(const leafbits_t *leafbits, int leafnum, leafblock_t *check)
{
    int bit;

    if (!*check) {
	leafnum += (1 << LEAFSHIFT);
	leafnum &= ~LEAFMASK;
	if (leafnum < leafbits->numleafs)
	    *check = leafbits->bits[leafnum >> LEAFSHIFT];
	while (!*check) {
	    leafnum += (1 << LEAFSHIFT);
	    if (leafnum < leafbits->numleafs)
		*check = leafbits->bits[leafnum >> LEAFSHIFT];
	    else
		return leafbits->numleafs;
	}
    }

    bit = ffsl(*check) - 1;
    leafnum = (leafnum & ~LEAFMASK) + bit;
    *check &= ~(1UL << bit);

    return leafnum;
}

/*
 * Macro to iterate over just the ones in the leaf bit array
 */
#define foreach_leafbit(leafbits, leafnum, check) \
    for (	check = 0, leafnum = Mod_NextLeafBit(leafbits, -1, &check); \
		leafnum < leafbits->numleafs;				    \
		leafnum = Mod_NextLeafBit(leafbits, leafnum, &check) )

/* 'OR' the bits of src into dst */
void Mod_AddLeafBits(leafbits_t *dst, const leafbits_t *src);

#ifdef SERVERONLY
/* Slightly faster counting of sparse sets for QWSV */
int Mod_CountLeafBits(const leafbits_t *leafbits);
#endif

// FIXME - surely this doesn't belong here?
texture_t *R_TextureAnimation(const struct entity_s *e, texture_t *base);

void Mod_LoadAliasModel(const model_loader_t *loader, model_t *model,
			void *buffer);
void Mod_LoadSpriteModel(model_t *model, const void *buffer);

const mspriteframe_t *Mod_GetSpriteFrame(const struct entity_s *entity,
					 const msprite_t *sprite, float time);

int Mod_FindInterval(const float *intervals, int numintervals, float time);

/*
 * Create a tiny hull structure for a given bounding box
 */
typedef struct {
    hull_t hull;
    mplane_t planes[6];
} boxhull_t;

void Mod_CreateBoxhull(const vec3_t mins, const vec3_t maxs,
		       boxhull_t *boxhull);


int Mod_HullPointContents(const hull_t *hull, int nodenum, const vec3_t point);


typedef struct {
    qboolean allsolid;		// if true, plane is not valid
    qboolean startsolid;	// if true, the initial point was in a solid area
    qboolean inopen, inwater;
    float fraction;		// time completed, 1.0 = didn't hit anything
    vec3_t endpos;		// final position
    mplane_t plane;		// surface normal at impact
} trace_t;

qboolean Mod_TraceHull(const hull_t *hull, int nodenum,
		       const vec3_t p1, const vec3_t p2,
		       trace_t *trace);

#endif /* MODEL_H */
