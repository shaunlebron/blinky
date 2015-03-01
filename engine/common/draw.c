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

#include <stdint.h>

#include "common.h"
#include "console.h"
#include "d_iface.h"
#include "draw.h"
#include "qpic.h"
#include "quakedef.h"
#include "sys.h"
#include "vid.h"
#include "view.h"
#include "wad.h"
#include "zone.h"

#ifdef NQ_HACK
#include "host.h"
#include "sound.h"
#endif
#ifdef QW_HACK
#include "bothdefs.h"
#include "client.h"
#endif

typedef struct {
    vrect_t rect;
    int width;
    int height;
    const byte *ptexbytes;
    int rowbytes;
} rectdesc_t;

static rectdesc_t r_rectdesc;

const byte *draw_chars;		/* 8*8 graphic characters */
const qpic8_t *draw_disc;

static const qpic8_t *draw_backtile;

//=============================================================================
/* Support Routines */

typedef struct cachepic_s {
    char name[MAX_QPATH];
    qpic8_t pic;
    cache_user_t cache;
} cachepic_t;

#define	MAX_CACHED_PICS		128
static cachepic_t menu_cachepics[MAX_CACHED_PICS];
static int menu_numcachepics;

const qpic8_t *
Draw_PicFromWad(const char *name)
{
    qpic8_t *pic;
    dpic8_t *dpic;

    dpic = W_GetLumpName(&host_gfx, name);
    pic = Hunk_AllocName(sizeof(*pic), "qpic8_t");
    pic->width = pic->stride = dpic->width;
    pic->height = dpic->height;
    pic->pixels = dpic->data;

    return pic;
}

/*
================
Draw_FindCache
================
*/
static cachepic_t *
Draw_FindCache(const char *path)
{
    cachepic_t *cachepic;
    int i;

    cachepic = menu_cachepics;
    for (i = 0; i < menu_numcachepics; i++, cachepic++)
	if (!strcmp(path, cachepic->name))
	    break;

    if (i == menu_numcachepics) {
	if (menu_numcachepics == MAX_CACHED_PICS)
	    Sys_Error("menu_numcachepics == MAX_CACHED_PICS");
	menu_numcachepics++;
	snprintf(cachepic->name, sizeof(cachepic->name), "%s", path);
    }

    Cache_Check(&cachepic->cache);

    return cachepic;
}

/*
================
Draw_CachePic
================
*/
const qpic8_t *
Draw_CachePic(const char *path)
{
    cachepic_t *cachepic;
    qpic8_t *pic;
    dpic8_t *dpic;

    cachepic = Draw_FindCache(path);
    pic = &cachepic->pic;
    dpic = cachepic->cache.data;
    if (dpic) {
	/* Update pixel pointer in case of cache moves */
	pic->pixels = dpic->data;
	return pic;
    }

    /* load the pic from disk */
    COM_LoadCacheFile(path, &cachepic->cache);
    dpic = cachepic->cache.data;
    if (!dpic)
	Sys_Error("%s: failed to load %s", __func__, path);
    SwapDPic(dpic);

    pic->width = pic->stride = dpic->width;
    pic->height = dpic->height;
    pic->pixels = dpic->data;

    return pic;
}

/*
================
Draw_CacheConback
================
*/
static void Draw_ConbackString(const qpic8_t *conback, byte *pixels,
			       const char *str);

const qpic8_t *
Draw_CacheConback(void)
{
    const char conbackfile[] = "gfx/conback.lmp";
    cachepic_t *cachepic;
    dpic8_t *dpic;
    qpic8_t *pic;
    char version[5];

    cachepic = Draw_FindCache(conbackfile);
    pic = &cachepic->pic;
    dpic = cachepic->cache.data;
    if (dpic) {
	/* Update pixel pointer in case of cache moves */
	pic->pixels = dpic->data;
	return pic;
    }

    /* load the pic from disk */
    COM_LoadCacheFile(conbackfile, &cachepic->cache);
    dpic = cachepic->cache.data;
    if (!dpic)
	Sys_Error("%s: failed to load %s", __func__, conbackfile);
    SwapDPic(dpic);

    pic->width = pic->stride = dpic->width;
    pic->height = dpic->height;
    pic->pixels = dpic->data;

    /* hack the version number directly into the pic */
    snprintf(version, sizeof(version), "%s", stringify(TYR_VERSION));
    Draw_ConbackString(pic, dpic->data, version);

    return pic;
}


/*
===============
Draw_Init
===============
*/
void
Draw_Init(void)
{
    static qpic8_t draw_disc_pic;
    static qpic8_t draw_backtile_pic;
    dpic8_t *dpic;

    draw_chars = W_GetLumpName(&host_gfx, "conchars");

    dpic = W_GetLumpName(&host_gfx, "disc");
    draw_disc_pic.width = draw_disc_pic.stride = dpic->width;
    draw_disc_pic.height = dpic->height;
    draw_disc_pic.pixels = dpic->data;
    draw_disc = &draw_disc_pic;

    dpic = W_GetLumpName(&host_gfx, "backtile");
    draw_backtile_pic.width = draw_backtile_pic.stride = dpic->width;
    draw_backtile_pic.height = dpic->height;
    draw_backtile_pic.pixels = dpic->data;
    draw_backtile = &draw_backtile_pic;

    r_rectdesc.width = draw_backtile->width;
    r_rectdesc.height = draw_backtile->height;
    r_rectdesc.ptexbytes = draw_backtile->pixels;
    r_rectdesc.rowbytes = draw_backtile->stride;
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
    byte *dest;
    const byte *source;
    uint16_t *pusdest;
    int drawline;
    int row, col;

    num &= 255;

    if (y <= -8)
	return;

    /*
     * FIXME - this was #ifdef PARANOID and Sys_Error instead of Con_DPrintf
     *       - seems to be affected by "-conwidth ..."
     */
    if (y > vid.height - 8 || x < 0 || x > vid.width - 8) {
	//Con_DPrintf("%s: (%i, %i)\n", __func__, x, y);
	return;
    }
    if (num < 0 || num > 255) {
	//Con_DPrintf("%s: char %i\n", __func__, num);
	return;
    }

    row = num >> 4;
    col = num & 15;
    source = draw_chars + (row << 10) + (col << 3);

    if (y < 0) {		// clipped
	drawline = 8 + y;
	source -= 128 * y;
	y = 0;
    } else
	drawline = 8;

    if (r_pixbytes == 1) {
	dest = vid.conbuffer + y * vid.conrowbytes + x;

	while (drawline--) {
	    if (source[0])
		dest[0] = source[0];
	    if (source[1])
		dest[1] = source[1];
	    if (source[2])
		dest[2] = source[2];
	    if (source[3])
		dest[3] = source[3];
	    if (source[4])
		dest[4] = source[4];
	    if (source[5])
		dest[5] = source[5];
	    if (source[6])
		dest[6] = source[6];
	    if (source[7])
		dest[7] = source[7];
	    source += 128;
	    dest += vid.conrowbytes;
	}
    } else {
	pusdest = (uint16_t *)
	    ((byte *)vid.conbuffer + y * vid.conrowbytes + (x << 1));

	while (drawline--) {
	    if (source[0])
		pusdest[0] = d_8to16table[source[0]];
	    if (source[1])
		pusdest[1] = d_8to16table[source[1]];
	    if (source[2])
		pusdest[2] = d_8to16table[source[2]];
	    if (source[3])
		pusdest[3] = d_8to16table[source[3]];
	    if (source[4])
		pusdest[4] = d_8to16table[source[4]];
	    if (source[5])
		pusdest[5] = d_8to16table[source[5]];
	    if (source[6])
		pusdest[6] = d_8to16table[source[6]];
	    if (source[7])
		pusdest[7] = d_8to16table[source[7]];

	    source += 128;
	    pusdest += vid.conrowbytes / 2;
	}
    }
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

static void
Draw_Pixel(int x, int y, byte color)
{
    byte *dest;
    uint16_t *pusdest;

    if (r_pixbytes == 1) {
	dest = vid.conbuffer + y * vid.conrowbytes + x;
	*dest = color;
    } else {
	pusdest = (uint16_t *)
	    ((byte *)vid.conbuffer + y * vid.conrowbytes + (x << 1));
	*pusdest = d_8to16table[color];
    }
}

void
Draw_Crosshair(void)
{
    int x, y;
    byte c = (byte)crosshaircolor.value;

    if (crosshair.value == 2) {
	x = scr_vrect.x + scr_vrect.width / 2 + cl_crossx.value;
	y = scr_vrect.y + scr_vrect.height / 2 + cl_crossy.value;
	Draw_Pixel(x - 1, y, c);
	Draw_Pixel(x - 3, y, c);
	Draw_Pixel(x + 1, y, c);
	Draw_Pixel(x + 3, y, c);
	Draw_Pixel(x, y - 1, c);
	Draw_Pixel(x, y - 3, c);
	Draw_Pixel(x, y + 1, c);
	Draw_Pixel(x, y + 3, c);
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
Draw_Pic(int x, int y, const qpic8_t *pic)
{
    byte *dest;
    const byte *source;
    uint16_t *pusdest;
    int v, u;

    if (x < 0 || x + pic->width > vid.width ||
	y < 0 || y + pic->height > vid.height) {
	Sys_Error("%s: bad coordinates", __func__);
    }

    source = pic->pixels;

    if (r_pixbytes == 1) {
	dest = vid.buffer + y * vid.rowbytes + x;

	for (v = 0; v < pic->height; v++) {
	    memcpy(dest, source, pic->width);
	    dest += vid.rowbytes;
	    source += pic->width;
	}
    } else {
	pusdest = (uint16_t *)vid.buffer + y * (vid.rowbytes / 2) + x;

	for (v = 0; v < pic->height; v++) {
	    for (u = 0; u < pic->width; u++) {
		pusdest[u] = d_8to16table[source[u]];
	    }

	    pusdest += vid.rowbytes / 2;
	    source += pic->width;
	}
    }
}


/*
=============
Draw_SubPic
=============
*/
void
Draw_SubPic(int x, int y, const qpic8_t *pic, int srcx, int srcy, int width,
	    int height)
{
    const byte *source;
    byte *dest;
    uint16_t *pusdest;
    int v, u;

    if (x < 0 || x + width > vid.width ||
	y < 0 || y + height > vid.height) {
	Sys_Error("%s: bad coordinates", __func__);
    }

    source = pic->pixels + srcy * pic->width + srcx;

    if (r_pixbytes == 1) {
	dest = vid.buffer + y * vid.rowbytes + x;

	for (v = 0; v < height; v++) {
	    memcpy(dest, source, width);
	    dest += vid.rowbytes;
	    source += pic->width;
	}
    } else {
	pusdest = (uint16_t *)vid.buffer + y * (vid.rowbytes / 2) + x;

	for (v = 0; v < height; v++) {
	    for (u = srcx; u < (srcx + width); u++) {
		pusdest[u] = d_8to16table[source[u]];
	    }

	    pusdest += vid.rowbytes / 2;
	    source += pic->width;
	}
    }
}


/*
=============
Draw_TransPic
=============
*/
void
Draw_TransPic(int x, int y, const qpic8_t *pic)
{
    byte *dest, tbyte;
    const byte *source;
    uint16_t *pusdest;
    int v, u;

    if (x < 0 || (unsigned)(x + pic->width) > vid.width ||
	y < 0 || (unsigned)(y + pic->height) > vid.height) {
	Sys_Error("%s: bad coordinates", __func__);
    }

    source = pic->pixels;

    if (r_pixbytes == 1) {
	dest = vid.buffer + y * vid.rowbytes + x;

	if (pic->width & 7) {	// general
	    for (v = 0; v < pic->height; v++) {
		for (u = 0; u < pic->width; u++)
		    if ((tbyte = source[u]) != TRANSPARENT_COLOR)
			dest[u] = tbyte;

		dest += vid.rowbytes;
		source += pic->width;
	    }
	} else {		// unwound
	    for (v = 0; v < pic->height; v++) {
		for (u = 0; u < pic->width; u += 8) {
		    if ((tbyte = source[u]) != TRANSPARENT_COLOR)
			dest[u] = tbyte;
		    if ((tbyte = source[u + 1]) != TRANSPARENT_COLOR)
			dest[u + 1] = tbyte;
		    if ((tbyte = source[u + 2]) != TRANSPARENT_COLOR)
			dest[u + 2] = tbyte;
		    if ((tbyte = source[u + 3]) != TRANSPARENT_COLOR)
			dest[u + 3] = tbyte;
		    if ((tbyte = source[u + 4]) != TRANSPARENT_COLOR)
			dest[u + 4] = tbyte;
		    if ((tbyte = source[u + 5]) != TRANSPARENT_COLOR)
			dest[u + 5] = tbyte;
		    if ((tbyte = source[u + 6]) != TRANSPARENT_COLOR)
			dest[u + 6] = tbyte;
		    if ((tbyte = source[u + 7]) != TRANSPARENT_COLOR)
			dest[u + 7] = tbyte;
		}
		dest += vid.rowbytes;
		source += pic->width;
	    }
	}
    } else {
	pusdest = (uint16_t *)vid.buffer + y * (vid.rowbytes / 2) + x;

	for (v = 0; v < pic->height; v++) {
	    for (u = 0; u < pic->width; u++) {
		tbyte = source[u];

		if (tbyte != TRANSPARENT_COLOR) {
		    pusdest[u] = d_8to16table[tbyte];
		}
	    }

	    pusdest += vid.rowbytes / 2;
	    source += pic->width;
	}
    }
}


/*
=============
Draw_TransPicTranslate
=============
*/
void
Draw_TransPicTranslate(int x, int y, const qpic8_t *pic, byte *translation)
{
    byte *dest, tbyte;
    const byte *source;
    uint16_t *pusdest;
    int v, u;

    if (x < 0 || (unsigned)(x + pic->width) > vid.width ||
	y < 0 || (unsigned)(y + pic->height) > vid.height) {
	Sys_Error("%s: bad coordinates", __func__);
    }

    source = pic->pixels;

    if (r_pixbytes == 1) {
	dest = vid.buffer + y * vid.rowbytes + x;

	if (pic->width & 7) {	// general
	    for (v = 0; v < pic->height; v++) {
		for (u = 0; u < pic->width; u++)
		    if ((tbyte = source[u]) != TRANSPARENT_COLOR)
			dest[u] = translation[tbyte];

		dest += vid.rowbytes;
		source += pic->width;
	    }
	} else {		// unwound
	    for (v = 0; v < pic->height; v++) {
		for (u = 0; u < pic->width; u += 8) {
		    if ((tbyte = source[u]) != TRANSPARENT_COLOR)
			dest[u] = translation[tbyte];
		    if ((tbyte = source[u + 1]) != TRANSPARENT_COLOR)
			dest[u + 1] = translation[tbyte];
		    if ((tbyte = source[u + 2]) != TRANSPARENT_COLOR)
			dest[u + 2] = translation[tbyte];
		    if ((tbyte = source[u + 3]) != TRANSPARENT_COLOR)
			dest[u + 3] = translation[tbyte];
		    if ((tbyte = source[u + 4]) != TRANSPARENT_COLOR)
			dest[u + 4] = translation[tbyte];
		    if ((tbyte = source[u + 5]) != TRANSPARENT_COLOR)
			dest[u + 5] = translation[tbyte];
		    if ((tbyte = source[u + 6]) != TRANSPARENT_COLOR)
			dest[u + 6] = translation[tbyte];
		    if ((tbyte = source[u + 7]) != TRANSPARENT_COLOR)
			dest[u + 7] = translation[tbyte];
		}
		dest += vid.rowbytes;
		source += pic->width;
	    }
	}
    } else {
	// FIXME: pretranslate at load time?
	pusdest = (uint16_t *)vid.buffer + y * (vid.rowbytes / 2) + x;

	for (v = 0; v < pic->height; v++) {
	    for (u = 0; u < pic->width; u++) {
		tbyte = source[u];

		if (tbyte != TRANSPARENT_COLOR) {
		    pusdest[u] = d_8to16table[tbyte];
		}
	    }

	    pusdest += vid.rowbytes / 2;
	    source += pic->width;
	}
    }
}


#define CHAR_WIDTH	8
#define CHAR_HEIGHT	8

static void
Draw_ScaledCharToConback(const qpic8_t *conback, int num, byte *dest)
{
    const byte *source, *src;
    int row, col;
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
	    if (src[f >> 16])
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
Draw_ConbackString(const qpic8_t *conback, byte *pixels, const char *str)
{
    int len, row, col, i, x;
    byte *dest;

    len = strlen(str);
    row = conback->height - ((CHAR_HEIGHT + 6) * conback->height / 200);
    col = conback->width - ((11 + CHAR_WIDTH * len) * conback->width / 320);

    dest = pixels + conback->width * row + col;
    for (i = 0; i < len; i++) {
	x = i * CHAR_WIDTH * conback->width / 320;
	Draw_ScaledCharToConback(conback, str[i], dest + x);
    }
}


/*
================
Draw_ConsoleBackground

================
*/
void
Draw_ConsoleBackground(int lines)
{
    const qpic8_t *conback;
    const byte *src;
    int x, y, v;
    int f, fstep;

    conback = Draw_CacheConback();

    /* draw the pic */
    if (r_pixbytes == 1) {
	byte *dest = vid.conbuffer;

	for (y = 0; y < lines; y++, dest += vid.conrowbytes) {
	    v = (vid.conheight - lines + y) * conback->height / vid.conheight;
	    src = conback->pixels + v * conback->width;
	    if (vid.conwidth == conback->width)
		memcpy(dest, src, vid.conwidth);
	    else {
		f = 0;
		fstep = conback->width * 0x10000 / vid.conwidth;
		for (x = 0; x < vid.conwidth; x += 4) {
		    dest[x] = src[f >> 16];
		    f += fstep;
		    dest[x + 1] = src[f >> 16];
		    f += fstep;
		    dest[x + 2] = src[f >> 16];
		    f += fstep;
		    dest[x + 3] = src[f >> 16];
		    f += fstep;
		}
	    }
	}
    } else {
	uint16_t *pusdest = (uint16_t *)vid.conbuffer;

	for (y = 0; y < lines; y++, pusdest += vid.conrowbytes / 2) {
	    // FIXME: pre-expand to native format?
	    // FIXME: does the endian switching go away in production?
	    v = (vid.conheight - lines + y) * conback->height / vid.conheight;
	    src = conback->pixels + v * conback->width;
	    f = 0;
	    fstep = conback->width * 0x10000 / vid.conwidth;
	    for (x = 0; x < vid.conwidth; x += 4) {
		pusdest[x] = d_8to16table[src[f >> 16]];
		f += fstep;
		pusdest[x + 1] = d_8to16table[src[f >> 16]];
		f += fstep;
		pusdest[x + 2] = d_8to16table[src[f >> 16]];
		f += fstep;
		pusdest[x + 3] = d_8to16table[src[f >> 16]];
		f += fstep;
	    }
	}
    }
}


/*
==============
R_DrawRect8
==============
*/
static void
R_DrawRect8(vrect_t *prect, int rowbytes, const byte *psrc, int transparent)
{
    byte t;
    int i, j, srcdelta, destdelta;
    byte *pdest;

    pdest = vid.buffer + (prect->y * vid.rowbytes) + prect->x;

    srcdelta = rowbytes - prect->width;
    destdelta = vid.rowbytes - prect->width;

    if (transparent) {
	for (i = 0; i < prect->height; i++) {
	    for (j = 0; j < prect->width; j++) {
		t = *psrc;
		if (t != TRANSPARENT_COLOR) {
		    *pdest = t;
		}

		psrc++;
		pdest++;
	    }

	    psrc += srcdelta;
	    pdest += destdelta;
	}
    } else {
	for (i = 0; i < prect->height; i++) {
	    memcpy(pdest, psrc, prect->width);
	    psrc += rowbytes;
	    pdest += vid.rowbytes;
	}
    }
}


/*
==============
R_DrawRect16
==============
*/
static void
R_DrawRect16(vrect_t *prect, int rowbytes, const byte *psrc, int transparent)
{
    byte t;
    int i, j, srcdelta, destdelta;
    uint16_t *pdest;

// FIXME: would it be better to pre-expand native-format versions?

    pdest = (uint16_t *)vid.buffer;
    pdest += prect->y * (vid.rowbytes / 2) + prect->x;

    srcdelta = rowbytes - prect->width;
    destdelta = (vid.rowbytes / 2) - prect->width;

    if (transparent) {
	for (i = 0; i < prect->height; i++) {
	    for (j = 0; j < prect->width; j++) {
		t = *psrc;
		if (t != TRANSPARENT_COLOR) {
		    *pdest = d_8to16table[t];
		}

		psrc++;
		pdest++;
	    }

	    psrc += srcdelta;
	    pdest += destdelta;
	}
    } else {
	for (i = 0; i < prect->height; i++) {
	    for (j = 0; j < prect->width; j++) {
		*pdest = d_8to16table[*psrc];
		psrc++;
		pdest++;
	    }

	    psrc += srcdelta;
	    pdest += destdelta;
	}
    }
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
    int width, height, tileoffsetx, tileoffsety;
    const byte *psrc;
    vrect_t vr;

    if (x < 0 || (unsigned)(x + w) > vid.width ||
	y < 0 || (unsigned)(y + h) > vid.height) {
	Sys_Error("%s: bad coordinates", __func__);
    }

    r_rectdesc.rect.x = x;
    r_rectdesc.rect.y = y;
    r_rectdesc.rect.width = w;
    r_rectdesc.rect.height = h;

    vr.y = r_rectdesc.rect.y;
    height = r_rectdesc.rect.height;

    tileoffsety = vr.y % r_rectdesc.height;

    while (height > 0) {
	vr.x = r_rectdesc.rect.x;
	width = r_rectdesc.rect.width;

	if (tileoffsety != 0)
	    vr.height = r_rectdesc.height - tileoffsety;
	else
	    vr.height = r_rectdesc.height;

	if (vr.height > height)
	    vr.height = height;

	tileoffsetx = vr.x % r_rectdesc.width;

	while (width > 0) {
	    if (tileoffsetx != 0)
		vr.width = r_rectdesc.width - tileoffsetx;
	    else
		vr.width = r_rectdesc.width;

	    if (vr.width > width)
		vr.width = width;

	    psrc = r_rectdesc.ptexbytes +
		(tileoffsety * r_rectdesc.rowbytes) + tileoffsetx;

	    if (r_pixbytes == 1) {
		R_DrawRect8(&vr, r_rectdesc.rowbytes, psrc, 0);
	    } else {
		R_DrawRect16(&vr, r_rectdesc.rowbytes, psrc, 0);
	    }

	    vr.x += vr.width;
	    width -= vr.width;
	    tileoffsetx = 0;	// only the left tile can be left-clipped
	}

	vr.y += vr.height;
	height -= vr.height;
	tileoffsety = 0;	// only the top tile can be top-clipped
    }
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
    byte *dest;
    uint16_t *pusdest;
    uint16_t uc;
    int u, v;

    if (x < 0 || x + w > vid.width || y < 0 || y + h > vid.height) {
	Con_Printf("Bad Draw_Fill(%d, %d, %d, %d, %c)\n", x, y, w, h, c);
	return;
    }

    if (r_pixbytes == 1) {
	dest = vid.buffer + y * vid.rowbytes + x;
	for (v = 0; v < h; v++, dest += vid.rowbytes)
	    for (u = 0; u < w; u++)
		dest[u] = c;
    } else {
	uc = d_8to16table[c];

	pusdest = (uint16_t *)vid.buffer + y * (vid.rowbytes / 2) + x;
	for (v = 0; v < h; v++, pusdest += vid.rowbytes / 2)
	    for (u = 0; u < w; u++)
		pusdest[u] = uc;
    }
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
    int x, y;
    byte *pbuf;

    VID_UnlockBuffer();
    S_ExtraUpdate();
    VID_LockBuffer();

    for (y = 0; y < vid.height; y++) {
	int t;

	pbuf = (byte *)(vid.buffer + vid.rowbytes * y);
	t = (y & 1) << 1;

	for (x = 0; x < vid.width; x++) {
	    if ((x & 3) != t)
		pbuf[x] = 0;
	}
    }

    VID_UnlockBuffer();
    S_ExtraUpdate();
    VID_LockBuffer();
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
    /* May not have loaded the draw disc yet... */
    if (!draw_disc)
	return;

    D_BeginDirectRect(vid.width - 24, 0, draw_disc->pixels, 24, 24);
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
    D_EndDirectRect(vid.width - 24, 0, 24, 24);
}
