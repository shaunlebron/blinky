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

#ifndef GLQUAKE_H
#define GLQUAKE_H

#ifdef _WIN32
#include <windows.h>
#endif

#include <GL/gl.h>
#ifdef _WIN32
#include <GL/glext.h>
#endif

#include "client.h"
#include "gl_model.h"
#include "protocol.h"

#ifndef APIENTRY
#define APIENTRY
#endif

void GL_BeginRendering(int *x, int *y, int *width, int *height);
void GL_EndRendering(void);

extern unsigned char d_15to8table[65536];

#ifdef _WIN32
// Function prototypes for the Texture Object Extension routines
typedef GLboolean (APIENTRY *ARETEXRESFUNCPTR) (GLsizei, const GLuint *,
						const GLboolean *);
typedef void (APIENTRY *BINDTEXFUNCPTR) (GLenum, GLuint);
typedef void (APIENTRY *DELTEXFUNCPTR) (GLsizei, const GLuint *);
typedef void (APIENTRY *GENTEXFUNCPTR) (GLsizei, GLuint *);
typedef GLboolean (APIENTRY *ISTEXFUNCPTR) (GLuint);
typedef void (APIENTRY *PRIORTEXFUNCPTR) (GLsizei, const GLuint *,
					  const GLclampf *);
typedef void (APIENTRY *TEXSUBIMAGEPTR) (int, int, int, int, int, int, int,
					 int, void *);

extern BINDTEXFUNCPTR bindTexFunc;
extern DELTEXFUNCPTR delTexFunc;
extern TEXSUBIMAGEPTR TexSubImage2DFunc;

// ARB Multitexture
#ifndef GL_VERSION_1_2
# define   GL_TEXTURE0_ARB  0x84C0
# define   GL_TEXTURE1_ARB  0x84C1
#endif

#endif /* _WIN32 */

#ifndef GL_VERSION_1_3
#define GL_MAX_TEXTURE_UNITS GL_MAX_TEXTURE_UNITS_ARB
#endif

extern int texture_mode;

extern float gldepthmin, gldepthmax;

void GL_Upload32(const unsigned *data, int width, int height,
		 qboolean mipmap, qboolean alpha);
void GL_Upload8(const byte *data, int width, int height,
		qboolean mipmap, qboolean alpha);
void GL_Upload8_EXT(const byte *data, int width, int height,
		    qboolean mipmap, qboolean alpha);
int GL_LoadTexture(const char *identifier, int width, int height,
		   const byte *data, qboolean mipmap, qboolean alpha);
int GL_FindTexture(const char *identifier);

void GL_SelectTexture(GLenum);

extern int glx, gly, glwidth, glheight;

// r_local.h -- private refresh defs

#define ALIAS_BASE_SIZE_RATIO	(1.0 / 11.0)
				// normalizing factor so player model works
				// out to about 1 pixel per triangle
#define	MAX_LBM_HEIGHT	480

#define SKYSHIFT	7
#define	SKYSIZE		(1 << SKYSHIFT)
#define SKYMASK		(SKYSIZE - 1)

#define BACKFACE_EPSILON	0.01

void R_TimeRefresh_f(void);
void R_ReadPointFile_f(void);

typedef struct surfcache_s {
    struct surfcache_s *next;
    struct surfcache_s **owner;	// NULL is an empty chunk of memory
    int lightadj[MAXLIGHTMAPS];	// checked for strobe flush
    int dlight;
    int size;			// including header
    unsigned width;
    unsigned height;		// DEBUG only needed for debug
    float mipscale;
    struct texture_s *texture;	// checked for animating textures
    byte data[4];		// width*height elements
} surfcache_t;


typedef struct {
    pixel_t *surfdat;		// destination for generated surface
    int rowbytes;		// destination logical width in bytes
    msurface_t *surf;		// description for surface to generate
    fixed8_t lightadj[MAXLIGHTMAPS];
    // adjust for lightmap levels for dynamic lighting
    texture_t *texture;		// corrected for animating textures
    int surfmip;		// mipmapped ratio of surface texels / world pixels
    int surfwidth;		// in mipmapped texels
    int surfheight;		// in mipmapped texels
} drawsurf_t;


typedef enum {
    pt_static, pt_grav, pt_slowgrav, pt_fire, pt_explode, pt_explode2,
    pt_blob, pt_blob2
} ptype_t;

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
typedef struct particle_s {
// driver-usable fields
    vec3_t org;
    float color;
// drivers never touch the following fields
    struct particle_s *next;
    vec3_t vel;
    float ramp;
    float die;
    ptype_t type;
} particle_t;


//====================================================


extern entity_t r_worldentity;
extern qboolean r_cache_thrash;	// compatability
extern vec3_t modelorg, r_entorigin;
extern entity_t *currententity;
extern int r_visframecount;	// ??? what difs?
extern int r_framecount;
extern int c_brush_polys;
extern int c_lightmaps_uploaded;

//
// view origin
//
extern vec3_t vup;
extern vec3_t vpn;
extern vec3_t vright;
extern vec3_t r_origin;

//
// screen size info
//
extern refdef_t r_refdef;
extern mleaf_t *r_viewleaf, *r_oldviewleaf;
extern texture_t *r_notexture_mip;
extern int d_lightstylevalue[256];	// 8.8 fraction of base light value

extern qboolean envmap;
extern GLuint currenttexture;
extern GLuint particletexture;
extern GLuint playertextures[MAX_CLIENTS];

extern cvar_t r_norefresh;
extern cvar_t r_drawentities;
extern cvar_t r_drawworld;
extern cvar_t r_drawviewmodel;
extern cvar_t r_speeds;
extern cvar_t r_waterwarp;
extern cvar_t r_fullbright;
extern cvar_t r_lightmap;
extern cvar_t r_shadows;
extern cvar_t r_mirroralpha;
extern cvar_t r_wateralpha;
extern cvar_t r_dynamic;
extern cvar_t r_novis;

extern cvar_t gl_clear;
extern cvar_t gl_cull;
extern cvar_t gl_poly;
extern cvar_t gl_texsort;
extern cvar_t gl_smoothmodels;
extern cvar_t gl_affinemodels;
extern cvar_t gl_polyblend;
extern cvar_t gl_keeptjunctions;
extern cvar_t gl_reporttjunctions;
extern cvar_t gl_flashblend;
extern cvar_t gl_nocolors;
extern cvar_t gl_finish;

extern cvar_t _gl_allowgammafallback;
extern cvar_t _gl_drawhull;

#ifdef NQ_HACK
extern cvar_t gl_doubleeyes;
#endif

#ifdef QW_HACK
extern GLuint netgraphtexture;	// netgraph texture
extern cvar_t r_netgraph;
void R_NetGraph(void);
#endif

extern int gl_lightmap_format;
extern int gl_solid_format;
extern int gl_alpha_format;

extern cvar_t gl_max_size;
extern cvar_t gl_playermip;

extern int mirrortexturenum;	// quake texturenum, not gltexturenum
extern qboolean mirror;
extern mplane_t *mirror_plane;

extern float r_world_matrix[16];

extern const char *gl_renderer;

void R_TranslatePlayerSkin(int playernum);
void GL_Bind(int texnum);

// ARB multitexture function pointers...
// FIXME - Find out what the APIENTRY stuff is (WIN32 obviously)
typedef void (APIENTRY *lpMultiTexFUNC) (GLenum, GLfloat, GLfloat);
typedef void (APIENTRY *lpActiveTextureFUNC) (GLenum);

extern lpMultiTexFUNC qglMultiTexCoord2fARB;
extern lpActiveTextureFUNC qglActiveTextureARB;

extern qboolean gl_mtexable;

void GL_DisableMultitexture(void);
void GL_EnableMultitexture(void);

//
// gl_warp.c
//
void GL_SubdivideSurface(msurface_t *fa);
void EmitBothSkyLayers(msurface_t *fa);
void EmitWaterPolys(msurface_t *fa);
void EmitSkyPolys(msurface_t *fa);
void R_DrawSkyChain(msurface_t *s);

//
// gl_draw.c
//
void GL_Set2D(void);

//
// gl_rmain.c
//
qboolean R_CullBox(vec3_t mins, vec3_t maxs);
void R_RotateForEntity(entity_t *e);

//
// gl_rlight.c
//
void R_MarkLights(dlight_t *light, int bit, mnode_t *node);
void R_AnimateLight(void);
void R_RenderDlights(void);
int R_LightPoint(vec3_t p);

//
// gl_refrag.c
//
void R_StoreEfrags(efrag_t **ppefrag);

//
// gl_mesh.c
//
void GL_MakeAliasModelDisplayLists(model_t *m, aliashdr_t *hdr);

//
// gl_rmisc.c
//
void R_InitBubble(void);

//
// gl_rsurf.c
//
void R_DrawBrushModel(entity_t *e);
void R_DrawWorld(void);
void R_DrawWorldHull(void); /* Quick hack for now... */
void R_DrawWaterSurfaces(void);
void R_RenderBrushPoly(msurface_t *fa); // only gl_rmain.c:R_Mirror
void GL_BuildLightmaps(void);

//
// Used only for r_shadows 1 (remove?)
//
extern vec3_t lightspot;

//
// gl_rmain.c, external only because it's registered elsewhere... broken?
//
extern cvar_t gl_ztrick;

//
// r_part.c
//
extern float r_avertexnormals[][3];


#endif /* GLQUAKE_H */
