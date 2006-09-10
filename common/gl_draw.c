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

// draw.c -- this is the only file outside the refresh that touches the
// vid buffer

#include "cmd.h"
#include "console.h"
#include "crc.h"
#include "glquake.h"
#include "quakedef.h"
#include "sbar.h"
#include "screen.h"
#include "sys.h"
#include "view.h"
#include "wad.h"

#ifdef NQ_HACK
#include "host.h"
#endif
#ifdef QW_HACK
#include "vid.h"
#endif

// FIXME - GL Header hacks for missing define on MinGW 3.1.0-1
#if defined(_WIN32) && !defined(GL_COLOR_INDEX8_EXT)
#define GL_COLOR_INDEX8_EXT 0x80E5
#endif

static cvar_t gl_nobind = { "gl_nobind", "0" };
static cvar_t gl_picmip = { "gl_picmip", "0" };
static cvar_t gl_constretch = { "gl_constretch", "0", true };

// FIXME - should I let this get larger, with view to enhancements?
cvar_t gl_max_size = { "gl_max_size", "1024" };

byte *draw_chars;		/* 8*8 graphic characters */
qpic_t *draw_disc;
static qpic_t *draw_backtile;

static GLuint translate_texture;
static GLuint char_texture;
static GLuint cs_texture;		// crosshair texture

static byte cs_data[64] = {
    0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
    0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff,
    0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

typedef struct {
    int texnum;
    float sl, tl, sh, th;
} glpic_t;

static byte conback_buffer[sizeof(qpic_t) + sizeof(glpic_t)];
static qpic_t *conback = (qpic_t *)&conback_buffer;

int gl_lightmap_format = GL_RGBA;	// 4
int gl_solid_format = GL_RGB;	// 3
int gl_alpha_format = GL_RGBA;	// 4

static int gl_filter_min = GL_LINEAR_MIPMAP_NEAREST;
static int gl_filter_max = GL_LINEAR;

static int texels;

typedef struct {
    GLuint texnum;
    char identifier[MAX_QPATH];
    int width, height;
    qboolean mipmap;
    unsigned short crc;		// CRC for texture cache matching
} gltexture_t;

#define	MAX_GLTEXTURES	1024
static gltexture_t gltextures[MAX_GLTEXTURES];
static int numgltextures;

// FIXME - clean up forward declarations later (forward declare all?)
static int GL_LoadPicTexture(const qpic_t *pic);

void
GL_Bind(int texnum)
{
    if (gl_nobind.value)
	texnum = char_texture;
    if (currenttexture == texnum)
	return;
    currenttexture = texnum;
#ifdef _WIN32
    bindTexFunc(GL_TEXTURE_2D, texnum);
#else
    glBindTexture(GL_TEXTURE_2D, texnum);
#endif
}


/*
=============================================================================

  scrap allocation

  Allocate all the little status bar obejcts into a single texture
  to crutch up stupid hardware / drivers

=============================================================================
*/

#define	MAX_SCRAPS	2
#define	BLOCK_WIDTH	256
#define	BLOCK_HEIGHT	256

static int scrap_allocated[MAX_SCRAPS][BLOCK_WIDTH];
static byte scrap_texels[MAX_SCRAPS][BLOCK_WIDTH * BLOCK_HEIGHT * 4];
static qboolean scrap_dirty;
static GLuint scrap_textures[MAX_SCRAPS];

// returns a texture number and the position inside it
static int
Scrap_AllocBlock(int w, int h, int *x, int *y)
{
    int i, j;
    int best, best2;
    int texnum;

    for (texnum = 0; texnum < MAX_SCRAPS; texnum++) {
	best = BLOCK_HEIGHT;

	for (i = 0; i < BLOCK_WIDTH - w; i++) {
	    best2 = 0;

	    for (j = 0; j < w; j++) {
		if (scrap_allocated[texnum][i + j] >= best)
		    break;
		if (scrap_allocated[texnum][i + j] > best2)
		    best2 = scrap_allocated[texnum][i + j];
	    }
	    if (j == w) {	// this is a valid spot
		*x = i;
		*y = best = best2;
	    }
	}

	if (best + h > BLOCK_HEIGHT)
	    continue;

	for (i = 0; i < w; i++)
	    scrap_allocated[texnum][*x + i] = best + h;

	return texnum;
    }

    Sys_Error("%s: full", __func__);
}

static int scrap_uploads;

static void
Scrap_Upload(void)
{
    int texnum;

    scrap_uploads++;
    for (texnum = 0; texnum < MAX_SCRAPS; ++texnum) {
	GL_Bind(scrap_textures[texnum]);
	GL_Upload8(scrap_texels[texnum], BLOCK_WIDTH, BLOCK_HEIGHT, false,
		   true);
    }
    scrap_dirty = false;
}

//=============================================================================
/* Support Routines */

typedef struct cachepic_s {
    char name[MAX_QPATH];
    qpic_t pic;
    byte padding[32];		// for appended glpic
} cachepic_t;

#define MAX_CACHED_PICS 128
static cachepic_t menu_cachepics[MAX_CACHED_PICS];
static int menu_numcachepics;

static byte menuplyr_pixels[4096];

static int pic_texels;
static int pic_count;

qpic_t *
Draw_PicFromWad(const char *name)
{
    qpic_t *p;
    glpic_t *gl;

    p = W_GetLumpName(name);
    gl = (glpic_t *)p->data;

    // load little ones into the scrap
    if (p->width < 64 && p->height < 64) {
	int x, y;
	int i, j, k;
	int texnum;

	texnum = Scrap_AllocBlock(p->width, p->height, &x, &y);
	scrap_dirty = true;
	k = 0;
	for (i = 0; i < p->height; i++)
	    for (j = 0; j < p->width; j++, k++)
		scrap_texels[texnum][(y + i) * BLOCK_WIDTH + x + j] =
		    p->data[k];
	gl->texnum = scrap_textures[texnum];
	gl->sl = (x + 0.01) / (float)BLOCK_WIDTH;
	gl->sh = (x + p->width - 0.01) / (float)BLOCK_WIDTH;
	gl->tl = (y + 0.01) / (float)BLOCK_WIDTH;
	gl->th = (y + p->height - 0.01) / (float)BLOCK_WIDTH;

	pic_count++;
	pic_texels += p->width * p->height;
    } else {
	gl->texnum = GL_LoadPicTexture(p);
	gl->sl = 0;
	gl->sh = 1;
	gl->tl = 0;
	gl->th = 1;
    }
    return p;
}


/*
================
Draw_CachePic
================
*/
qpic_t *
Draw_CachePic(const char *path)
{
    cachepic_t *pic;
    int i;
    qpic_t *dat;
    glpic_t *gl;

    for (pic = menu_cachepics, i = 0; i < menu_numcachepics; pic++, i++)
	if (!strcmp(path, pic->name))
	    return &pic->pic;

    if (menu_numcachepics == MAX_CACHED_PICS)
	Sys_Error("menu_numcachepics == MAX_CACHED_PICS");
    menu_numcachepics++;
    strcpy(pic->name, path);

//
// load the pic from disk
//
    dat = (qpic_t *)COM_LoadTempFile(path);
    if (!dat)
	Sys_Error("%s: failed to load %s", __func__, path);
    SwapPic(dat);

    // HACK HACK HACK --- we need to keep the bytes for
    // the translatable player picture just for the menu
    // configuration dialog
    if (!strcmp(path, "gfx/menuplyr.lmp"))
	memcpy(menuplyr_pixels, dat->data, dat->width * dat->height);

    pic->pic.width = dat->width;
    pic->pic.height = dat->height;

    gl = (glpic_t *)pic->pic.data;
    gl->texnum = GL_LoadPicTexture(dat);
    gl->sl = 0;
    gl->sh = 1;
    gl->tl = 0;
    gl->th = 1;

    return &pic->pic;
}


#define CHAR_WIDTH	8
#define CHAR_HEIGHT	8

static void
Draw_ScaledCharToConback(qpic_t *conback, int num, byte *dest)
{
    int row, col;
    byte *source, *src;
    int drawlines, drawwidth;
    int x, y, fstep, f;

    drawlines = conback->height * CHAR_HEIGHT / 200;
    drawwidth = conback->width * CHAR_WIDTH / 320;

    row = num >> 4;
    col = num & 15;
    source = draw_chars + (row << 10) + (col << 3);
    fstep = 320 * 0x10000 / conback->width;

    for (y = 0; y < drawlines; y++, dest += conback->width) {
	src = source + (y * CHAR_HEIGHT / drawlines) * 128;
	f = 0;
	for (x = 0; x < drawwidth; x++, f += fstep) {
	    if (src[f >> 16] != 255)
		dest[x] = 0x60 + src[f >> 16];
	}
    }
}

/*
 * Draw_ConbackString
 *
 * This function draws a string to a very specific location on the console
 * background. The position is such that for a 320x200 background, the text
 * will be 6 pixels from the bottom and 11 pixels from the right. For other
 * sizes, the positioning is scaled so as to make it appear the same size and
 * at the same location.
 */
static void
Draw_ConbackString(qpic_t *cb, char *str)
{
    int len, row, col, x;
    byte *dest;

    len = strlen(str);
    row = cb->height - ((CHAR_HEIGHT + 6) * cb->height / 200);
    col = cb->width - ((11 + CHAR_WIDTH * len) * cb->width / 320);

    dest = cb->data + cb->width * row + col;
    for (x = 0; x < len; x++)
	Draw_ScaledCharToConback(cb, str[x],
				 dest + (x * CHAR_WIDTH * cb->width / 320));
}


typedef struct {
    char *name;
    int minimize, maximize;
} glmode_t;

static glmode_t gl_texturemodes[] = {
    {"GL_NEAREST", GL_NEAREST, GL_NEAREST},
    {"GL_LINEAR", GL_LINEAR, GL_LINEAR},
    {"GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST},
    {"GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR},
    {"GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST},
    {"GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR}
};

/*
===============
Draw_TextureMode_f
===============
*/
static void
Draw_TextureMode_f(void)
{
    int i;
    gltexture_t *glt;

    if (Cmd_Argc() == 1) {
	for (i = 0; i < 6; i++)
	    if (gl_filter_min == gl_texturemodes[i].minimize) {
		Con_Printf("%s\n", gl_texturemodes[i].name);
		return;
	    }
	Con_Printf("current filter is unknown???\n");
	return;
    }

    for (i = 0; i < 6; i++) {
	if (!strcasecmp(gl_texturemodes[i].name, Cmd_Argv(1)))
	    break;
    }
    if (i == 6) {
	Con_Printf("bad filter name\n");
	return;
    }

    gl_filter_min = gl_texturemodes[i].minimize;
    gl_filter_max = gl_texturemodes[i].maximize;

    // change all the existing mipmap texture objects
    for (i = 0, glt = gltextures; i < numgltextures; i++, glt++) {
	if (glt->mipmap) {
	    GL_Bind(glt->texnum);
	    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
			    gl_filter_min);
	    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
			    gl_filter_max);
	}
    }
}

/*
===============
Draw_Init
===============
*/
void
Draw_Init(void)
{
    int i;
    qpic_t *cb;
    glpic_t *gl;
    int start;
    byte *ncdata;

    Cvar_RegisterVariable(&gl_nobind);
    Cvar_RegisterVariable(&gl_max_size);
    Cvar_RegisterVariable(&gl_picmip);
    Cvar_RegisterVariable(&gl_constretch);

    // FIXME - could do better to check on each texture upload with
    //         GL_PROXY_TEXTURE_2D
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &i);
    if (gl_max_size.value > i) {
	char tmp[20];

	Con_DPrintf("Reducing gl_max_size from %i to %i\n",
		    (int)gl_max_size.value, i);
	snprintf(tmp, 20, "%i", i);
	Cvar_Set("gl_max_size", tmp);
    }
#if 0
    // 3dfx can only handle 256 wide textures
    // FIXME - remove when proper tex size queries working?
    if (!strncasecmp((char *)gl_renderer, "3dfx", 4) ||
	!strncasecmp((char *)gl_renderer, "Mesa", 4))
	Cvar_Set("gl_max_size", "256");
#endif

    Cmd_AddCommand("gl_texturemode", &Draw_TextureMode_f);

    // load the console background and the charset
    // by hand, because we need to write the version
    // string into the background before turning
    // it into a texture
    draw_chars = W_GetLumpName("conchars");
    for (i = 0; i < 256 * 64; i++)
	if (draw_chars[i] == 0)
	    draw_chars[i] = 255;	// proper transparent color

    // now turn them into textures
    char_texture =
	GL_LoadTexture("charset", 128, 128, draw_chars, false, true);
    cs_texture = GL_LoadTexture("crosshair", 8, 8, cs_data, false, true);

    start = Hunk_LowMark();

#ifdef NQ_HACK
    cb = (qpic_t *)COM_LoadTempFile("gfx/conback.lmp");
#endif
#ifdef QW_HACK
    cb = (qpic_t *)COM_LoadHunkFile("gfx/conback.lmp");
#endif
    if (!cb)
	Sys_Error("Couldn't load gfx/conback.lmp");
    SwapPic(cb);

    /* hack the version number directly into the pic */
    Draw_ConbackString(cb, stringify(TYR_VERSION));

    conback->width = cb->width;
    conback->height = cb->height;
    ncdata = cb->data;

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    gl = (glpic_t *)conback->data;
    gl->texnum =
	GL_LoadTexture("conback", conback->width, conback->height, ncdata,
		       false, false);
    gl->sl = 0;
    gl->sh = 1;
    gl->tl = 0;
    gl->th = 1;
#ifdef NQ_HACK
    conback->width = vid.width;
    conback->height = vid.height;
#endif
#ifdef QW_HACK
    conback->width = vid.conwidth;
    conback->height = vid.conheight;
#endif

    // free loaded console
    Hunk_FreeToLowMark(start);

    // save a texture slot for translated picture
    glGenTextures(1, &translate_texture);

    // save slots for scraps
    glGenTextures(MAX_SCRAPS, scrap_textures);

    //
    // get the other pics we need
    //
    draw_disc = Draw_PicFromWad("disc");
    draw_backtile = Draw_PicFromWad("backtile");
}



/*
================
Draw_Character

Draws one 8*8 graphics character with 0 being transparent.
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/
void
Draw_Character(int x, int y, int num)
{
    int row, col;
    float frow, fcol, size;

    if (num == 32)
	return;			// space

    num &= 255;

    if (y <= -8)
	return;			// totally off screen

    row = num >> 4;
    col = num & 15;

    frow = row * 0.0625;
    fcol = col * 0.0625;
    size = 0.0625;

    GL_Bind(char_texture);
    glBegin(GL_QUADS);
    glTexCoord2f(fcol, frow);
    glVertex2f(x, y);
    glTexCoord2f(fcol + size, frow);
    glVertex2f(x + 8, y);
    glTexCoord2f(fcol + size, frow + size);
    glVertex2f(x + 8, y + 8);
    glTexCoord2f(fcol, frow + size);
    glVertex2f(x, y + 8);
    glEnd();
}

/*
================
Draw_String
================
*/
void
Draw_String(int x, int y, const char *str)
{
    while (*str) {
	Draw_Character(x, y, *str);
	str++;
	x += 8;
    }
}

/*
================
Draw_Alt_String
================
*/
void
Draw_Alt_String(int x, int y, const char *str)
{
    while (*str) {
	Draw_Character(x, y, (*str) | 0x80);
	str++;
	x += 8;
    }
}

void
Draw_Crosshair(void)
{
    int x, y;
    unsigned char *pColor;

    if (crosshair.value == 2) {
	x = scr_vrect.x + scr_vrect.width / 2 - 3 + cl_crossx.value;
	y = scr_vrect.y + scr_vrect.height / 2 - 3 + cl_crossy.value;

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	pColor = (unsigned char *)&d_8to24table[(byte)crosshaircolor.value];
	glColor4ubv(pColor);
	GL_Bind(cs_texture);

	glBegin(GL_QUADS);
	glTexCoord2f(0, 0);
	glVertex2f(x - 4, y - 4);
	glTexCoord2f(1, 0);
	glVertex2f(x + 12, y - 4);
	glTexCoord2f(1, 1);
	glVertex2f(x + 12, y + 12);
	glTexCoord2f(0, 1);
	glVertex2f(x - 4, y + 12);
	glEnd();

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    } else if (crosshair.value)
	Draw_Character(scr_vrect.x + scr_vrect.width / 2 - 4 +
		       cl_crossx.value,
		       scr_vrect.y + scr_vrect.height / 2 - 4 +
		       cl_crossy.value, '+');
}


/*
=============
Draw_Pic
=============
*/
void
Draw_Pic(int x, int y, const qpic_t *pic)
{
    glpic_t *gl;

    if (scrap_dirty)
	Scrap_Upload();
    gl = (glpic_t *)pic->data;
    glColor4f(1, 1, 1, 1);
    GL_Bind(gl->texnum);
    glBegin(GL_QUADS);
    glTexCoord2f(gl->sl, gl->tl);
    glVertex2f(x, y);
    glTexCoord2f(gl->sh, gl->tl);
    glVertex2f(x + pic->width, y);
    glTexCoord2f(gl->sh, gl->th);
    glVertex2f(x + pic->width, y + pic->height);
    glTexCoord2f(gl->sl, gl->th);
    glVertex2f(x, y + pic->height);
    glEnd();
}

void
Draw_SubPic(int x, int y, const qpic_t *pic, int srcx, int srcy, int width,
	    int height)
{
    glpic_t *gl;
    float newsl, newtl, newsh, newth;
    float oldglwidth, oldglheight;

    if (scrap_dirty)
	Scrap_Upload();
    gl = (glpic_t *)pic->data;

    oldglwidth = gl->sh - gl->sl;
    oldglheight = gl->th - gl->tl;

    newsl = gl->sl + (srcx * oldglwidth) / pic->width;
    newsh = newsl + (width * oldglwidth) / pic->width;

    newtl = gl->tl + (srcy * oldglheight) / pic->height;
    newth = newtl + (height * oldglheight) / pic->height;

    glColor4f(1, 1, 1, 1);
    GL_Bind(gl->texnum);
    glBegin(GL_QUADS);
    glTexCoord2f(newsl, newtl);
    glVertex2f(x, y);
    glTexCoord2f(newsh, newtl);
    glVertex2f(x + width, y);
    glTexCoord2f(newsh, newth);
    glVertex2f(x + width, y + height);
    glTexCoord2f(newsl, newth);
    glVertex2f(x, y + height);
    glEnd();
}

/*
=============
Draw_TransPic
=============
*/
void
Draw_TransPic(int x, int y, const qpic_t *pic)
{
    if (x < 0 || (unsigned)(x + pic->width) > vid.width ||
	y < 0 || (unsigned)(y + pic->height) > vid.height) {
	Sys_Error("%s: bad coordinates", __func__);
    }

    Draw_Pic(x, y, pic);
}


/*
=============
Draw_TransPicTranslate

Only used for the player color selection menu
=============
*/
void
Draw_TransPicTranslate(int x, int y, const qpic_t *pic, byte *translation)
{
    int v, u, c;
    unsigned trans[64 * 64], *dest;
    byte *src;
    int p;

    GL_Bind(translate_texture);

    c = pic->width * pic->height;

    dest = trans;
    for (v = 0; v < 64; v++, dest += 64) {
	src = &menuplyr_pixels[((v * pic->height) >> 6) * pic->width];
	for (u = 0; u < 64; u++) {
	    p = src[(u * pic->width) >> 6];
	    if (p == 255)
		dest[u] = p;
	    else
		dest[u] = d_8to24table[translation[p]];
	}
    }

    glTexImage2D(GL_TEXTURE_2D, 0, gl_alpha_format, 64, 64, 0, GL_RGBA,
		 GL_UNSIGNED_BYTE, trans);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(x, y);
    glTexCoord2f(1, 0);
    glVertex2f(x + pic->width, y);
    glTexCoord2f(1, 1);
    glVertex2f(x + pic->width, y + pic->height);
    glTexCoord2f(0, 1);
    glVertex2f(x, y + pic->height);
    glEnd();
}


/*
================
Draw_ConsoleBackground

================
*/
static void
Draw_ConsolePic(int lines, float offset, const qpic_t *pic, float alpha)
{
    glpic_t *gl;

    if (scrap_dirty)
	Scrap_Upload();
    gl = (glpic_t *)pic->data;

    glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glCullFace(GL_FRONT);
    glColor4f(1, 1, 1, alpha);
    GL_Bind(gl->texnum);

    glBegin (GL_QUADS);
    glTexCoord2f (0, offset);
    glVertex2f (0, 0);
    glTexCoord2f (1, offset);
    glVertex2f (vid.conwidth, 0);
    glTexCoord2f (1, 1);
    glVertex2f (vid.conwidth, lines);
    glTexCoord2f (0, 1);
    glVertex2f (0, lines);
    glEnd();

    glColor4f(1, 1, 1, 1);
    glEnable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
}

void
Draw_ConsoleBackground(int lines)
{
    int y;
    float offset, alpha;

    y = (vid.height * 3) >> 2;

    if (gl_constretch.value)
	offset = 0.0f;
    else
	offset = (vid.conheight - lines) / (float)vid.conheight;

    if (lines > y)
	alpha = 1.0f;
    else
	alpha = (float) 1.1 * lines / y;

    Draw_ConsolePic(lines, offset, conback, alpha);

#ifdef QW_HACK
    {
	char ver[40];
	int x, i;

	// hack the version number directly into the pic
	y = lines - 14;
	if (!cls.download) {
	    sprintf(ver, "TyrQuake (%s) QuakeWorld", stringify(TYR_VERSION));
	    x = vid.conwidth - (strlen(ver) * 8 + 11) -
		(vid.conwidth * 8 / conback->width) * 7;
	    for (i = 0; i < strlen(ver); i++)
		Draw_Character(x + i * 8, y, ver[i] | 0x80);
	}
    }
#endif
}


/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void
Draw_TileClear(int x, int y, int w, int h)
{
    glColor3f(1, 1, 1);
    GL_Bind(*(int *)draw_backtile->data);
    glBegin(GL_QUADS);
    glTexCoord2f(x / 64.0, y / 64.0);
    glVertex2f(x, y);
    glTexCoord2f((x + w) / 64.0, y / 64.0);
    glVertex2f(x + w, y);
    glTexCoord2f((x + w) / 64.0, (y + h) / 64.0);
    glVertex2f(x + w, y + h);
    glTexCoord2f(x / 64.0, (y + h) / 64.0);
    glVertex2f(x, y + h);
    glEnd();
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void
Draw_Fill(int x, int y, int w, int h, int c)
{
    glDisable(GL_TEXTURE_2D);
    glColor3f(host_basepal[c * 3] / 255.0,
	      host_basepal[c * 3 + 1] / 255.0,
	      host_basepal[c * 3 + 2] / 255.0);

    glBegin(GL_QUADS);

    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);

    glEnd();
    glColor3f(1, 1, 1);
    glEnable(GL_TEXTURE_2D);
}

//=============================================================================

/*
================
Draw_FadeScreen

================
*/
void
Draw_FadeScreen(void)
{
    glEnable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glColor4f(0, 0, 0, 0.8);
    glBegin(GL_QUADS);

    glVertex2f(0, 0);
    glVertex2f(vid.width, 0);
    glVertex2f(vid.width, vid.height);
    glVertex2f(0, vid.height);

    glEnd();
    glColor4f(1, 1, 1, 1);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);

    Sbar_Changed();
}

//=============================================================================

/*
================
Draw_BeginDisc

Draws the little blue disc in the corner of the screen.
Call before beginning any disc IO.
================
*/
void
Draw_BeginDisc(void)
{
    if (!draw_disc)
	return;
    glDrawBuffer(GL_FRONT);
    Draw_Pic(vid.width - 24, 0, draw_disc);
    glDrawBuffer(GL_BACK);
}


/*
================
Draw_EndDisc

Erases the disc icon.
Call after completing any disc IO
================
*/
void
Draw_EndDisc(void)
{
}

/*
================
GL_Set2D

Setup as if the screen was 320*200
================
*/
void
GL_Set2D(void)
{
    glViewport(glx, gly, glwidth, glheight);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, vid.width, vid.height, 0, -99999, 99999);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glEnable(GL_ALPHA_TEST);
//      glDisable(GL_ALPHA_TEST);

    glColor4f(1, 1, 1, 1);
}

//====================================================================

/*
================
GL_FindTexture
================
*/
int
GL_FindTexture(const char *identifier)
{
    int i;
    gltexture_t *glt;

    for (i = 0, glt = gltextures; i < numgltextures; i++, glt++) {
	if (!strcmp(identifier, glt->identifier))
	    return gltextures[i].texnum;
    }

    return -1;
}

/*
================
GL_ResampleTexture
================
*/
static void
GL_ResampleTexture(const unsigned *in, int inwidth, int inheight,
		   unsigned *out, int outwidth, int outheight)
{
    int i, j;
    const unsigned *inrow;
    unsigned frac, fracstep;

    fracstep = inwidth * 0x10000 / outwidth;
    for (i = 0; i < outheight; i++, out += outwidth) {
	inrow = in + inwidth * (i * inheight / outheight);
	frac = fracstep >> 1;
	for (j = 0; j < outwidth; j += 4) {
	    out[j] = inrow[frac >> 16];
	    frac += fracstep;
	    out[j + 1] = inrow[frac >> 16];
	    frac += fracstep;
	    out[j + 2] = inrow[frac >> 16];
	    frac += fracstep;
	    out[j + 3] = inrow[frac >> 16];
	    frac += fracstep;
	}
    }
}

/*
================
GL_Resample8BitTexture -- JACK
================
*/
static void
GL_Resample8BitTexture(const unsigned char *in, int inwidth, int inheight,
		       unsigned char *out, int outwidth, int outheight)
{
    int i, j;
    const unsigned char *inrow;
    unsigned frac, fracstep;

    fracstep = inwidth * 0x10000 / outwidth;
    for (i = 0; i < outheight; i++, out += outwidth) {
	inrow = in + inwidth * (i * inheight / outheight);
	frac = fracstep >> 1;
	for (j = 0; j < outwidth; j += 4) {
	    out[j] = inrow[frac >> 16];
	    frac += fracstep;
	    out[j + 1] = inrow[frac >> 16];
	    frac += fracstep;
	    out[j + 2] = inrow[frac >> 16];
	    frac += fracstep;
	    out[j + 3] = inrow[frac >> 16];
	    frac += fracstep;
	}
    }
}

/*
================
GL_MipMap

Operates in place, quartering the size of the texture
================
*/
static void
GL_MipMap(byte *in, int width, int height)
{
    int i, j;
    byte *out;

    width <<= 2;
    height >>= 1;
    out = in;
    for (i = 0; i < height; i++, in += width) {
	for (j = 0; j < width; j += 8, out += 4, in += 8) {
	    out[0] = (in[0] + in[4] + in[width + 0] + in[width + 4]) >> 2;
	    out[1] = (in[1] + in[5] + in[width + 1] + in[width + 5]) >> 2;
	    out[2] = (in[2] + in[6] + in[width + 2] + in[width + 6]) >> 2;
	    out[3] = (in[3] + in[7] + in[width + 3] + in[width + 7]) >> 2;
	}
    }
}

/*
================
GL_MipMap8Bit

Mipping for 8 bit textures
================
*/
static void
GL_MipMap8Bit(byte *in, int width, int height)
{
    int i, j;
    unsigned short r, g, b;
    byte *out, *at1, *at2, *at3, *at4;

    height >>= 1;
    out = in;
    for (i = 0; i < height; i++, in += width) {
	for (j = 0; j < width; j += 2, out += 1, in += 2) {
	    at1 = (byte *)(d_8to24table + in[0]);
	    at2 = (byte *)(d_8to24table + in[1]);
	    at3 = (byte *)(d_8to24table + in[width + 0]);
	    at4 = (byte *)(d_8to24table + in[width + 1]);

	    r = (at1[0] + at2[0] + at3[0] + at4[0]);
	    r >>= 5;
	    g = (at1[1] + at2[1] + at3[1] + at4[1]);
	    g >>= 5;
	    b = (at1[2] + at2[2] + at3[2] + at4[2]);
	    b >>= 5;

	    out[0] = d_15to8table[(r << 0) + (g << 5) + (b << 10)];
	}
    }
}

/*
===============
GL_Upload32
===============
*/
void
GL_Upload32(const unsigned *data, int width, int height, qboolean mipmap,
	    qboolean alpha)
{
    int samples;
    static unsigned scaled[1024 * 512];	// [512*256];
    int scaled_width, scaled_height;

    for (scaled_width = 1; scaled_width < width; scaled_width <<= 1);
    for (scaled_height = 1; scaled_height < height; scaled_height <<= 1);

    scaled_width >>= (int)gl_picmip.value;
    scaled_height >>= (int)gl_picmip.value;

    if (scaled_width > gl_max_size.value)
	scaled_width = gl_max_size.value;
    if (scaled_height > gl_max_size.value)
	scaled_height = gl_max_size.value;

    if (scaled_width * scaled_height > sizeof(scaled) / 4)
	Sys_Error("%s: too big", __func__);

    samples = alpha ? gl_alpha_format : gl_solid_format;

    texels += scaled_width * scaled_height;

    if (scaled_width == width && scaled_height == height) {
	if (!mipmap) {
	    glTexImage2D(GL_TEXTURE_2D, 0, samples, scaled_width,
			 scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	    goto done;
	}
	memcpy(scaled, data, width * height * 4);
    } else
	GL_ResampleTexture(data, width, height, scaled, scaled_width,
			   scaled_height);

    glTexImage2D(GL_TEXTURE_2D, 0, samples, scaled_width, scaled_height, 0,
		 GL_RGBA, GL_UNSIGNED_BYTE, scaled);

    if (mipmap) {
	int miplevel;

	miplevel = 0;
	while (scaled_width > 1 || scaled_height > 1) {
	    GL_MipMap((byte *)scaled, scaled_width, scaled_height);
	    scaled_width >>= 1;
	    scaled_height >>= 1;
	    if (scaled_width < 1)
		scaled_width = 1;
	    if (scaled_height < 1)
		scaled_height = 1;
	    miplevel++;
	    glTexImage2D(GL_TEXTURE_2D, miplevel, samples, scaled_width,
			 scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);
	}
    }

  done:
    if (mipmap) {
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
    } else {
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
    }
}

void
GL_Upload8_EXT(const byte *data, int width, int height, qboolean mipmap,
	       qboolean alpha)
{
    int i, s;
    qboolean noalpha;
    int samples;
    static unsigned char scaled[1024 * 512];	// [512*256];
    int scaled_width, scaled_height;

    s = width * height;
    // if there are no transparent pixels, make it a 3 component
    // texture even if it was specified as otherwise
    if (alpha) {
	noalpha = true;
	for (i = 0; i < s; i++) {
	    if (data[i] == 255)
		noalpha = false;
	}

	if (alpha && noalpha)
	    alpha = false;
    }
    for (scaled_width = 1; scaled_width < width; scaled_width <<= 1);
    for (scaled_height = 1; scaled_height < height; scaled_height <<= 1);

    scaled_width >>= (int)gl_picmip.value;
    scaled_height >>= (int)gl_picmip.value;

    if (scaled_width > gl_max_size.value)
	scaled_width = gl_max_size.value;
    if (scaled_height > gl_max_size.value)
	scaled_height = gl_max_size.value;

    if (scaled_width * scaled_height > sizeof(scaled))
	Sys_Error("%s: too big", __func__);

    samples = 1;		// alpha ? gl_alpha_format : gl_solid_format;

    texels += scaled_width * scaled_height;

    if (scaled_width == width && scaled_height == height) {
	if (!mipmap) {
	    glTexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, scaled_width,
			 scaled_height, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE,
			 data);
	    goto done;
	}
	memcpy(scaled, data, width * height);
    } else
	GL_Resample8BitTexture(data, width, height, scaled, scaled_width,
			       scaled_height);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, scaled_width,
		 scaled_height, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, scaled);
    if (mipmap) {
	int miplevel;

	miplevel = 0;
	while (scaled_width > 1 || scaled_height > 1) {
	    GL_MipMap8Bit((byte *)scaled, scaled_width, scaled_height);
	    scaled_width >>= 1;
	    scaled_height >>= 1;
	    if (scaled_width < 1)
		scaled_width = 1;
	    if (scaled_height < 1)
		scaled_height = 1;
	    miplevel++;
	    glTexImage2D(GL_TEXTURE_2D, miplevel, GL_COLOR_INDEX8_EXT,
			 scaled_width, scaled_height, 0, GL_COLOR_INDEX,
			 GL_UNSIGNED_BYTE, scaled);
	}
    }
  done:

    if (mipmap) {
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
    } else {
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
    }
}

/*
===============
GL_Upload8
===============
*/
void
GL_Upload8(const byte *data, int width, int height, qboolean mipmap,
	   qboolean alpha)
{
    static unsigned trans[640 * 480];	// FIXME, temporary
    int i, s;
    qboolean noalpha;
    int p;

    s = width * height;
    // if there are no transparent pixels, make it a 3 component
    // texture even if it was specified as otherwise
    if (alpha) {
	noalpha = true;
	for (i = 0; i < s; i++) {
	    p = data[i];
	    if (p == 255)
		noalpha = false;
	    trans[i] = d_8to24table[p];
	}

	if (alpha && noalpha)
	    alpha = false;
    } else {
	if (s & 3)
	    Sys_Error("%s: s&3", __func__);
	for (i = 0; i < s; i += 4) {
	    trans[i] = d_8to24table[data[i]];
	    trans[i + 1] = d_8to24table[data[i + 1]];
	    trans[i + 2] = d_8to24table[data[i + 2]];
	    trans[i + 3] = d_8to24table[data[i + 3]];
	}
    }

    if (VID_Is8bit() && !alpha && (data != scrap_texels[0])) {
	GL_Upload8_EXT(data, width, height, mipmap, alpha);
	return;
    }
    GL_Upload32(trans, width, height, mipmap, alpha);
}

/*
================
GL_LoadTexture
================
*/
int
GL_LoadTexture(const char *identifier, int width, int height,
	       const byte *data, qboolean mipmap, qboolean alpha)
{
    int i;
    gltexture_t *glt;
    unsigned short crc;

    crc = CRC_Block(data, width * height);

    // see if the texture is already present
    if (identifier[0]) {
	for (i = 0, glt = gltextures; i < numgltextures; i++, glt++) {
	    if (!strcmp(identifier, glt->identifier)) {
		if (crc != glt->crc
		    || width != glt->width || height != glt->height)
		    goto GL_LoadTexture_setup;
		else
		    return glt->texnum;
	    }
	}
    }

    if (numgltextures == MAX_GLTEXTURES)
	Sys_Error("numgltextures == MAX_GLTEXTURES");

    glt = &gltextures[numgltextures];
    numgltextures++;

    strncpy(glt->identifier, identifier, sizeof(glt->identifier) - 1);
    glt->identifier[sizeof(glt->identifier) - 1] = '\0';

    glGenTextures(1, &glt->texnum);

  GL_LoadTexture_setup:
    glt->crc = crc;
    glt->width = width;
    glt->height = height;
    glt->mipmap = mipmap;

#ifdef NQ_HACK
    if (!isDedicated) {
	GL_Bind(glt->texnum);
	GL_Upload8(data, width, height, mipmap, alpha);
    }
#else
    GL_Bind(glt->texnum);
    GL_Upload8(data, width, height, mipmap, alpha);
#endif

    return glt->texnum;
}

/*
================
GL_LoadPicTexture
================
*/
static int
GL_LoadPicTexture(const qpic_t *pic)
{
    return GL_LoadTexture("", pic->width, pic->height, pic->data, false,
			  true);
}
