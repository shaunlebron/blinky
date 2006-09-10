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

#include "cmd.h"
#include "console.h"
#include "crc.h"
#include "cvar.h"
#include "glquake.h"
#include "qpic.h"
#include "sys.h"

#ifdef NQ_HACK
#include "host.h"
#endif

// FIXME - should I let this get larger, with view to enhancements?
cvar_t gl_max_size = { "gl_max_size", "1024" };

static cvar_t gl_nobind = { "gl_nobind", "0" };
static cvar_t gl_picmip = { "gl_picmip", "0" };

int gl_lightmap_format = GL_RGBA;	// 4
int gl_solid_format = GL_RGB;	// 3
int gl_alpha_format = GL_RGBA;	// 4

typedef struct {
    GLuint texnum;
    char name[MAX_QPATH];
    int width, height;
    qboolean mipmap;
    unsigned short crc;		// CRC for texture cache matching
} gltexture_t;

#define	MAX_GLTEXTURES	4096
static gltexture_t gltextures[MAX_GLTEXTURES];
static int numgltextures;

void
GL_Bind(int texnum)
{
    if (gl_nobind.value)
	texnum = charset_texture;
    if (currenttexture == texnum)
	return;
    currenttexture = texnum;

    glBindTexture(GL_TEXTURE_2D, texnum);
}


typedef struct {
    const char *name;
    GLenum min_filter;
    GLenum mag_filter;
} glmode_t;

static glmode_t *glmode;

static glmode_t gl_texturemodes[] = {
    { "gl_nearest", GL_NEAREST, GL_NEAREST },
    { "gl_linear", GL_LINEAR, GL_LINEAR },
    { "gl_nearest_mipmap_nearest", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST },
    { "gl_linear_mipmap_nearest", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR },
    { "gl_nearest_mipmap_linear", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST },
    { "gl_linear_mipmap_linear", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR }
};


/*
===============
Draw_TextureMode_f
===============
*/
static void
GL_TextureMode_f(void)
{
    int i;
    gltexture_t *glt;

    if (Cmd_Argc() == 1) {
	Con_Printf("%s\n", glmode->name);
	return;
    }

    for (i = 0; i < ARRAY_SIZE(gl_texturemodes); i++) {
	if (!strcasecmp(gl_texturemodes[i].name, Cmd_Argv(1))) {
	    glmode = &gl_texturemodes[i];
	    break;
	}
    }
    if (i == ARRAY_SIZE(gl_texturemodes)) {
	Con_Printf("bad filter name\n");
	return;
    }

    /* change all the existing mipmap texture objects */
    for (i = 0, glt = gltextures; i < numgltextures; i++, glt++) {
	if (glt->mipmap) {
	    GL_Bind(glt->texnum);
	    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
			    glmode->min_filter);
	    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
			    glmode->mag_filter);
	}
    }
}

static struct stree_root *
GL_TextureMode_Arg_f(const char *arg)
{
    int i, arg_len;
    struct stree_root *root;

    root = Z_Malloc(sizeof(struct stree_root));
    if (root) {
	*root = STREE_ROOT;
	STree_AllocInit();
	arg_len = arg ? strlen(arg) : 0;
	for (i = 0; i < ARRAY_SIZE(gl_texturemodes); i++) {
	    if (!arg || !strncasecmp(gl_texturemodes[i].name, arg, arg_len))
		STree_InsertAlloc(root, gl_texturemodes[i].name, false);
	}
    }
    return root;
}

/*
================
GL_FindTexture
================
*/
int
GL_FindTexture(const char *name)
{
    int i;
    gltexture_t *glt;

    for (i = 0, glt = gltextures; i < numgltextures; i++, glt++) {
	if (!strcmp(name, glt->name))
	    return gltextures[i].texnum;
    }

    return -1;
}

/*
===============
GL_Upload32
===============
*/
static void
GL_Upload32(qpic32_t *pic, qboolean mipmap, qboolean alpha)
{
    const int format = alpha ? gl_alpha_format : gl_solid_format;
    qpic32_t *scaled;
    int width, height, mark;

    if (!gl_npotable || !gl_npot.value) {
	/* find the next power-of-two size up */
	width = 1;
	while (width < pic->width)
	    width <<= 1;
	height = 1;
	while (height < pic->height)
	    height <<= 1;
    } else {
	width = pic->width;
	height = pic->height;
    }

    width >>= (int)gl_picmip.value;
    width = qclamp(width, 1, (int)gl_max_size.value);
    height >>= (int)gl_picmip.value;
    height = qclamp(height, 1, (int)gl_max_size.value);

    mark = Hunk_LowMark();

    if (width != pic->width || height != pic->height) {
	scaled = QPic32_Alloc(width, height);
	QPic32_Stretch(pic, scaled);
    } else {
	scaled = pic;
    }

    if (mipmap) {
	int miplevel = 0;
	while (1) {
	    glTexImage2D(GL_TEXTURE_2D, miplevel, format,
			 scaled->width, scaled->height, 0,
			 GL_RGBA, GL_UNSIGNED_BYTE, scaled->pixels);
	    if (scaled->width == 1 && scaled->height == 1)
		break;

	    QPic32_MipMap(scaled);
	    miplevel++;
	}
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
			glmode->min_filter);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
			glmode->mag_filter);
    } else {
	glTexImage2D(GL_TEXTURE_2D, 0, format,
		     scaled->width, scaled->height, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, scaled->pixels);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
			glmode->mag_filter);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
			glmode->mag_filter);
    }

    Hunk_FreeToLowMark(mark);
}

/*
===============
GL_Upload8
===============
*/
void
GL_Upload8(const qpic8_t *pic, qboolean mipmap)
{
    qpic32_t *pic32;
    int mark;

    mark = Hunk_LowMark();

    pic32 = QPic32_Alloc(pic->width, pic->height);
    QPic_8to32(pic, pic32);
    GL_Upload32(pic32, mipmap, false);

    Hunk_FreeToLowMark(mark);
}

/*
===============
GL_Upload8_Alpha
===============
*/
void
GL_Upload8_Alpha(const qpic8_t *pic, qboolean mipmap, byte alpha)
{
    qpic32_t *pic32;
    int mark;

    mark = Hunk_LowMark();

    pic32 = QPic32_Alloc(pic->width, pic->height);
    QPic_8to32_Alpha(pic, pic32, alpha);
    GL_Upload32(pic32, mipmap, true);

    Hunk_FreeToLowMark(mark);
}

/*
================
GL_LoadTexture_
FIXME - ugly multiplexer for alpha...
================
*/
static int
GL_LoadTexture_(const char *name, const qpic8_t *pic, qboolean mipmap,
		qboolean alpha, byte alphabyte)
{
    int i;
    gltexture_t *glt;
    unsigned short crc;

    crc = CRC_Block(pic->pixels, pic->width * pic->height);

    // see if the texture is already present
    if (name[0]) {
	for (i = 0, glt = gltextures; i < numgltextures; i++, glt++) {
	    if (!strcmp(name, glt->name)) {
		if (crc != glt->crc)
		    goto GL_LoadTexture_setup;
		if (pic->width != glt->width || pic->height != glt->height)
		    goto GL_LoadTexture_setup;
		return glt->texnum;
	    }
	}
    }

    if (numgltextures == MAX_GLTEXTURES)
	Sys_Error("numgltextures == MAX_GLTEXTURES");

    glt = &gltextures[numgltextures];
    numgltextures++;

    strncpy(glt->name, name, sizeof(glt->name) - 1);
    glt->name[sizeof(glt->name) - 1] = '\0';

    glGenTextures(1, &glt->texnum);

  GL_LoadTexture_setup:
    glt->crc = crc;
    glt->width = pic->width;
    glt->height = pic->height;
    glt->mipmap = mipmap;

#ifdef NQ_HACK
    if (!isDedicated) {
	GL_Bind(glt->texnum);
	if (alpha)
	    GL_Upload8_Alpha(pic, mipmap, alphabyte);
	else
	    GL_Upload8(pic, mipmap);
    }
#else
    GL_Bind(glt->texnum);
    if (alpha)
	GL_Upload8_Alpha(pic, mipmap, alphabyte);
    else
	GL_Upload8(pic, mipmap);
#endif

    return glt->texnum;
}

int
GL_LoadTexture(const char *name, const qpic8_t *pic, qboolean mipmap)
{
    return GL_LoadTexture_(name, pic, mipmap, false, 0);
}

int
GL_LoadTexture_Alpha(const char *name, const qpic8_t *pic, qboolean mipmap,
		     byte alpha)
{
    return GL_LoadTexture_(name, pic, mipmap, true, alpha);
}

void
GL_InitTextures(void)
{
    GLint max_size;

    glmode = gl_texturemodes;

    Cvar_RegisterVariable(&gl_nobind);
    Cvar_RegisterVariable(&gl_max_size);
    Cvar_RegisterVariable(&gl_picmip);

    // FIXME - could do better to check on each texture upload with
    //         GL_PROXY_TEXTURE_2D
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
    if (gl_max_size.value > max_size) {
	Con_DPrintf("Reducing gl_max_size from %d to %d\n",
		    (int)gl_max_size.value, max_size);
	Cvar_Set("gl_max_size", va("%d", max_size));
    }

    Cmd_AddCommand("gl_texturemode", GL_TextureMode_f);
    Cmd_SetCompletion("gl_texturemode", GL_TextureMode_Arg_f);
}
