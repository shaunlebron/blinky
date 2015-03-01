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

#include <string.h>

#include "client.h"
#include "cmd.h"
#include "common.h"
#include "console.h"
#include "draw.h"
#include "keys.h"
#include "menu.h"
#include "quakedef.h"
#include "sbar.h"
#include "screen.h"
#include "sound.h"
#include "sys.h"
#include "view.h"

#ifdef GLQUAKE
#include "glquake.h"
#else
#include "d_iface.h"
#include "r_local.h"
#endif

#ifdef NQ_HACK
#include "host.h"
#endif
#ifdef QW_HACK
#include "client.h"
#endif

/*

background clear
rendering
turtle/net/ram icons
sbar
centerprint / slow centerprint
notify lines
intermission / finale overlay
loading plaque
console
menu

required background clears
required update regions

syncronous draw mode or async
One off screen buffer, with updates either copied or xblited
Need to double buffer?

async draw will require the refresh area to be cleared, because it will be
xblited, but sync draw can just ignore it.

sync
draw

CenterPrint();
SlowPrint();
Screen_Update();
Con_Printf();

net
turn off messages option

the refresh is always rendered, unless the console is full screen

console is:
	notify lines
	half
	full
*/

static qboolean scr_initialized;	/* ready to draw */

// only the refresh window will be updated unless these variables are flagged
int scr_copytop;
int scr_copyeverything;

float scr_con_current;
static float scr_conlines;		/* lines of console to display */

int scr_fullupdate;
static int clearconsole;
int clearnotify;

vrect_t scr_vrect;

qboolean scr_disabled_for_loading;
qboolean scr_block_drawing;
qboolean scr_skipupdate;

static cvar_t scr_centertime = { "scr_centertime", "2" };
static cvar_t scr_printspeed = { "scr_printspeed", "8" };

cvar_t scr_viewsize = { "viewsize", "100", true };
cvar_t scr_fov = { "fov", "90" };	// 10 - 170
static cvar_t scr_conspeed = { "scr_conspeed", "300" };
static cvar_t scr_showram = { "showram", "1" };
static cvar_t scr_showturtle = { "showturtle", "0" };
static cvar_t scr_showpause = { "showpause", "1" };
static cvar_t show_fps = { "show_fps", "0" };	/* set for running times */
#ifdef GLQUAKE
static cvar_t gl_triplebuffer = { "gl_triplebuffer", "1", true };
#else
static vrect_t *pconupdate;
#endif

static const qpic8_t *scr_ram;
static const qpic8_t *scr_net;
static const qpic8_t *scr_turtle;

static char scr_centerstring[1024];
static float scr_centertime_start;	// for slow victory printing
float scr_centertime_off;
static int scr_center_lines;
static int scr_erase_lines;
static int scr_erase_center;

#ifdef NQ_HACK
static qboolean scr_drawloading;
static float scr_disabled_time;
#endif
#ifdef QW_HACK
static float oldsbar;
static cvar_t scr_allowsnap = { "scr_allowsnap", "1" };
#endif


//=============================================================================

/*
==============
SCR_DrawRam
==============
*/
static void
SCR_DrawRam(void)
{
    if (!scr_showram.value)
	return;

    if (!r_cache_thrash)
	return;

    Draw_Pic(scr_vrect.x + 32, scr_vrect.y, scr_ram);
}


/*
==============
SCR_DrawTurtle
==============
*/
static void
SCR_DrawTurtle(void)
{
    static int count;

    if (!scr_showturtle.value)
	return;

    if (host_frametime < 0.1) {
	count = 0;
	return;
    }

    count++;
    if (count < 3)
	return;

    Draw_Pic(scr_vrect.x, scr_vrect.y, scr_turtle);
}


/*
==============
SCR_DrawNet
==============
*/
static void
SCR_DrawNet(void)
{
#ifdef NQ_HACK
    if (realtime - cl.last_received_message < 0.3)
	return;
#endif
#ifdef QW_HACK
    if (cls.netchan.outgoing_sequence - cls.netchan.incoming_acknowledged <
	UPDATE_BACKUP - 1)
	return;
#endif

    if (cls.demoplayback)
	return;

    Draw_Pic(scr_vrect.x + 64, scr_vrect.y, scr_net);
}


static void
SCR_DrawFPS(void)
{
    static double lastframetime;
    static int lastfps;
    double t;
    int x, y;
    char st[80];

    if (!show_fps.value)
	return;

    t = Sys_DoubleTime();
    if ((t - lastframetime) >= 1.0) {
	lastfps = fps_count;
	fps_count = 0;
	lastframetime = t;
    }

    sprintf(st, "%3d FPS", lastfps);
    x = vid.width - strlen(st) * 8 - 8;
    y = vid.height - sb_lines - 8;
    Draw_String(x, y, st);
}


/*
==============
DrawPause
==============
*/
static void
SCR_DrawPause(void)
{
    const qpic8_t *pic;

    if (!scr_showpause.value)	// turn off for screenshots
	return;

    if (!cl.paused)
	return;

    pic = Draw_CachePic("gfx/pause.lmp");
    Draw_Pic((vid.width - pic->width) / 2,
	     (vid.height - 48 - pic->height) / 2, pic);
}

//=============================================================================

/*
==================
SCR_SetUpToDrawConsole
==================
*/
static void
SCR_SetUpToDrawConsole(void)
{
    Con_CheckResize();

#ifdef NQ_HACK
    if (scr_drawloading)
	return;			// never a console with loading plaque
#endif

// decide on the height of the console
#ifdef NQ_HACK
    con_forcedup = !cl.worldmodel || cls.state != ca_active;
#endif
#ifdef QW_HACK
    con_forcedup = cls.state != ca_active;
#endif

    if (con_forcedup) {
	scr_conlines = vid.height;	// full screen
	scr_con_current = scr_conlines;
    } else if (key_dest == key_console)
	scr_conlines = vid.height / 2;	// half screen
    else
	scr_conlines = 0;	// none visible

    if (scr_conlines < scr_con_current) {
	scr_con_current -= scr_conspeed.value * host_frametime;
	if (scr_conlines > scr_con_current)
	    scr_con_current = scr_conlines;

    } else if (scr_conlines > scr_con_current) {
	scr_con_current += scr_conspeed.value * host_frametime;
	if (scr_conlines < scr_con_current)
	    scr_con_current = scr_conlines;
    }

    if (clearconsole++ < vid.numpages) {
#ifdef GLQUAKE
	scr_copytop = 1;
	Draw_TileClear(0, (int)scr_con_current, vid.width,
		       vid.height - (int)scr_con_current);
#endif
	Sbar_Changed();
    } else if (clearnotify++ < vid.numpages) {
	scr_copytop = 1;
	Draw_TileClear(0, 0, vid.width, con_notifylines);
    } else
	con_notifylines = 0;
}


/*
==================
SCR_DrawConsole
==================
*/
static void
SCR_DrawConsole(void)
{
    if (scr_con_current) {
	scr_copyeverything = 1;
	Con_DrawConsole(scr_con_current);
	clearconsole = 0;
    } else {
	if (key_dest == key_game || key_dest == key_message)
	    Con_DrawNotify();	// only draw notify in game
    }
}

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void
SCR_CenterPrint(const char *str)
{
    strncpy(scr_centerstring, str, sizeof(scr_centerstring));
    scr_centerstring[sizeof(scr_centerstring) - 1] = 0;
    scr_centertime_off = scr_centertime.value;
    scr_centertime_start = cl.time;

    /* count the number of lines for centering */
    scr_center_lines = 1;
    str = scr_centerstring;
    while (*str) {
	if (*str == '\n')
	    scr_center_lines++;
	str++;
    }
}

#ifndef GLQUAKE
static void
SCR_EraseCenterString(void)
{
    int y, height;

    if (scr_erase_center++ > vid.numpages) {
	scr_erase_lines = 0;
	return;
    }

    if (scr_center_lines <= 4)
	y = vid.height * 0.35;
    else
	y = 48;

    /* Make sure we don't draw off the bottom of the screen*/
    height = qmin(8 * scr_erase_lines, ((int)vid.height) - y - 1);

    scr_copytop = 1;
    Draw_TileClear(0, y, vid.width, height);
}
#endif

static void
SCR_DrawCenterString(void)
{
    char *start;
    int l;
    int j;
    int x, y;
    int remaining;

    scr_copytop = 1;
    if (scr_center_lines > scr_erase_lines)
	scr_erase_lines = scr_center_lines;

    scr_centertime_off -= host_frametime;

    if (scr_centertime_off <= 0 && !cl.intermission)
	return;
    if (key_dest != key_game)
	return;

// the finale prints the characters one at a time
    if (cl.intermission)
	remaining = scr_printspeed.value * (cl.time - scr_centertime_start);
    else
	remaining = 9999;

    scr_erase_center = 0;
    start = scr_centerstring;

    if (scr_center_lines <= 4)
	y = vid.height * 0.35;
    else
	y = 48;

    do {
	// scan the width of the line
	for (l = 0; l < 40; l++)
	    if (start[l] == '\n' || !start[l])
		break;
	x = (vid.width - l * 8) / 2;
	for (j = 0; j < l; j++, x += 8) {
	    Draw_Character(x, y, start[j]);
	    if (!remaining--)
		return;
	}

	y += 8;

	while (*start && *start != '\n')
	    start++;

	if (!*start)
	    break;
	start++;		// skip the \n
    } while (1);
}

//=============================================================================

static const char *scr_notifystring;
static qboolean scr_drawdialog;

static void
SCR_DrawNotifyString(void)
{
    const char *start;
    int l;
    int j;
    int x, y;

    start = scr_notifystring;

    y = vid.height * 0.35;

    do {
	// scan the width of the line
	for (l = 0; l < 40; l++)
	    if (start[l] == '\n' || !start[l])
		break;
	x = (vid.width - l * 8) / 2;
	for (j = 0; j < l; j++, x += 8)
	    Draw_Character(x, y, start[j]);

	y += 8;

	while (*start && *start != '\n')
	    start++;

	if (!*start)
	    break;
	start++;		// skip the \n
    } while (1);
}


/*
==================
SCR_ModalMessage

Displays a text string in the center of the screen and waits for a Y or N
keypress.
==================
*/
int
SCR_ModalMessage(const char *text)
{
#ifdef NQ_HACK
    if (cls.state == ca_dedicated)
	return true;
#endif

    scr_notifystring = text;

// draw a fresh screen
    scr_fullupdate = 0;
    scr_drawdialog = true;
    SCR_UpdateScreen();
    scr_drawdialog = false;

    S_ClearBuffer();		// so dma doesn't loop current sound

    do {
	key_count = -1;		// wait for a key down and up
	Sys_SendKeyEvents();
	Sys_Sleep();
    } while (key_lastpress != 'y' && key_lastpress != 'n'
	     && key_lastpress != K_ESCAPE);

    scr_fullupdate = 0;
    SCR_UpdateScreen();

    return key_lastpress == 'y';
}

//============================================================================

/*
====================
CalcFov
====================
*/
static float
CalcFov(float fov_x, float width, float height)
{
    float a;
    float x;

    if (fov_x < 1 || fov_x > 179)
	Sys_Error("Bad fov: %f", fov_x);

    x = width / tan(fov_x / 360 * M_PI);
    a = atan(height / x);
    a = a * 360 / M_PI;

    return a;
}


/*
=================
SCR_CalcRefdef

Must be called whenever vid changes
Internal use only
=================
*/
static void
SCR_CalcRefdef(void)
{
    vrect_t vrect;
    float size;

    scr_fullupdate = 0;		// force a background redraw
    vid.recalc_refdef = 0;

// force the status bar to redraw
    Sbar_Changed();

//========================================

// bound viewsize
    if (scr_viewsize.value < 30)
	Cvar_Set("viewsize", "30");
    if (scr_viewsize.value > 120)
	Cvar_Set("viewsize", "120");

// bound field of view
    if (scr_fov.value < 10)
	Cvar_Set("fov", "10");
    if (scr_fov.value > 170)
	Cvar_Set("fov", "170");

// intermission is always full screen
    if (cl.intermission)
	size = 120;
    else
	size = scr_viewsize.value;

    if (size >= 120)
	sb_lines = 0;		// no status bar at all
    else if (size >= 110)
	sb_lines = 24;		// no inventory
    else
	sb_lines = 24 + 16 + 8;

// these calculations mirror those in R_Init() for r_refdef, but take no
// account of water warping
    vrect.x = 0;
    vrect.y = 0;
    vrect.width = vid.width;
    vrect.height = vid.height;

#ifdef GLQUAKE
    R_SetVrect(&vrect, &r_refdef.vrect, sb_lines);
#else
    R_SetVrect(&vrect, &scr_vrect, sb_lines);
#endif

    r_refdef.fov_x = scr_fov.value;
    r_refdef.fov_y =
	CalcFov(r_refdef.fov_x, r_refdef.vrect.width, r_refdef.vrect.height);

#ifdef GLQUAKE
    scr_vrect = r_refdef.vrect;
#else
// guard against going from one mode to another that's less than half the
// vertical resolution
    if (scr_con_current > vid.height)
	scr_con_current = vid.height;

// notify the refresh of the change
    R_ViewChanged(&vrect, sb_lines, vid.aspect);
#endif
}

/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
static void
SCR_SizeUp_f(void)
{
    Cvar_SetValue("viewsize", scr_viewsize.value + 10);
    vid.recalc_refdef = 1;
}


/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
static void
SCR_SizeDown_f(void)
{
    Cvar_SetValue("viewsize", scr_viewsize.value - 10);
    vid.recalc_refdef = 1;
}

/*
==============================================================================

				SCREEN SHOTS

==============================================================================
*/

#if !defined(NQ_HACK) || !defined(GLQUAKE)
/*
==============
WritePCXfile
==============
*/
static void
WritePCXfile(const char *filename, const byte *data, int width, int height,
	     int rowbytes, const byte *palette, qboolean upload)
{
    int i, j, length;
    pcx_t *pcx;
    byte *pack;

    pcx = Hunk_TempAlloc(width * height * 2 + 1000);
    if (pcx == NULL) {
	Con_Printf("SCR_ScreenShot_f: not enough memory\n");
	return;
    }

    pcx->manufacturer = 0x0a;	// PCX id
    pcx->version = 5;		// 256 color
    pcx->encoding = 1;		// uncompressed
    pcx->bits_per_pixel = 8;	// 256 color
    pcx->xmin = 0;
    pcx->ymin = 0;
    pcx->xmax = LittleShort((short)(width - 1));
    pcx->ymax = LittleShort((short)(height - 1));
    pcx->hres = LittleShort((short)width);
    pcx->vres = LittleShort((short)height);
    memset(pcx->palette, 0, sizeof(pcx->palette));
    pcx->color_planes = 1;	// chunky image
    pcx->bytes_per_line = LittleShort((short)width);
    pcx->palette_type = LittleShort(1);	// not a grey scale
    memset(pcx->filler, 0, sizeof(pcx->filler));

    // pack the image
    pack = &pcx->data;

#ifdef GLQUAKE
    // The GL buffer addressing is bottom to top?
    data += rowbytes * (height - 1);
    for (i = 0; i < height; i++) {
	for (j = 0; j < width; j++) {
	    if ((*data & 0xc0) != 0xc0) {
		*pack++ = *data++;
	    } else {
		*pack++ = 0xc1;
		*pack++ = *data++;
	    }
	}
	data += rowbytes - width;
	data -= rowbytes * 2;
    }
#else
    for (i = 0; i < height; i++) {
	for (j = 0; j < width; j++) {
	    if ((*data & 0xc0) != 0xc0) {
		*pack++ = *data++;
	    } else {
		*pack++ = 0xc1;
		*pack++ = *data++;
	    }
	}
	data += rowbytes - width;
    }
#endif

    // write the palette
    *pack++ = 0x0c;		// palette ID byte
    for (i = 0; i < 768; i++)
	*pack++ = *palette++;

    // write output file
    length = pack - (byte *)pcx;

#ifdef QW_HACK
    if (upload) {
	CL_StartUpload((byte *)pcx, length);
	return;
    }
#endif

    COM_WriteFile(filename, pcx, length);
}
#endif /* !defined(NQ_HACK) && !defined(GLQUAKE) */


#ifdef QW_HACK
/*
Find closest color in the palette for named color
*/
static int
MipColor(int r, int g, int b)
{
    int i;
    float dist;
    int best;
    float bestdist;
    int r1, g1, b1;
    static int lr = -1, lg = -1, lb = -1;
    static int lastbest;

    if (r == lr && g == lg && b == lb)
	return lastbest;

    bestdist = 256 * 256 * 3;

    best = 0;			// FIXME - Uninitialised? Zero ok?
    for (i = 0; i < 256; i++) {
	r1 = host_basepal[i * 3] - r;
	g1 = host_basepal[i * 3 + 1] - g;
	b1 = host_basepal[i * 3 + 2] - b;
	dist = r1 * r1 + g1 * g1 + b1 * b1;
	if (dist < bestdist) {
	    bestdist = dist;
	    best = i;
	}
    }
    lr = r;
    lg = g;
    lb = b;
    lastbest = best;
    return best;
}

static void
SCR_DrawCharToSnap(int num, byte *dest, int width)
{
    int row, col;
    const byte *source;
    int drawline;
    int x, stride;

    row = num >> 4;
    col = num & 15;
    source = draw_chars + (row << 10) + (col << 3);

#ifdef GLQUAKE
    stride = -128;
#else
    stride = 128;
#endif

    if (stride < 0)
	source -= 7 * stride;

    drawline = 8;
    while (drawline--) {
	for (x = 0; x < 8; x++)
	    if (source[x])
		dest[x] = source[x];
	    else
		dest[x] = 98;
	source += stride;
	dest += width;
    }
}

static void
SCR_DrawStringToSnap(const char *s, byte *buf, int x, int y, int width, int height)
{
    byte *dest;
    const unsigned char *p;

#ifdef GLQUAKE
    dest = buf + (height - y - 8) * width + x;
#else
    dest = buf + y * width + x;
#endif

    p = (const unsigned char *)s;
    while (*p) {
	SCR_DrawCharToSnap(*p++, dest, width);
	dest += 8;
    }
}


/*
==================
SCR_RSShot_f
==================
*/
static void
SCR_RSShot_f(void)
{
    int i;
    int x, y;
    unsigned char *src, *dest;
    char pcxname[80];
    unsigned char *newbuf;
    int w, h;
    int dx, dy, dex, dey, nx;
    int r, b, g;
    int count;
    float fracw, frach;
    char st[80];
    time_t now;

    if (CL_IsUploading())
	return;			// already one pending

    if (cls.state < ca_onserver)
	return;			// gotta be connected

#ifndef GLQUAKE /* <- probably a bug, should check always? */
    if (!scr_allowsnap.value) {
	MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
	MSG_WriteString(&cls.netchan.message, "snap\n");
	Con_Printf("Refusing remote screen shot request.\n");
	return;
    }
#endif

    Con_Printf("Remote screen shot requested.\n");

//
// find a file name to save it to
//
    strcpy(pcxname, "mquake00.pcx");

    for (i = 0; i <= 99; i++) {
	pcxname[6] = i / 10 + '0';
	pcxname[7] = i % 10 + '0';
	if (Sys_FileTime(va("%s/%s", com_gamedir, pcxname)) == -1)
	    break;		// file doesn't exist
    }
    if (i == 100) {
	Con_Printf("SCR_ScreenShot_f: Couldn't create a PCX\n");
	return;
    }

//
// save the pcx file
//
#ifdef GLQUAKE /* FIXME - consolidate common bits */
    newbuf = malloc(glheight * glwidth * 4);

    glReadPixels(glx, gly, glwidth, glheight, GL_RGBA, GL_UNSIGNED_BYTE,
		 newbuf);

    w = (vid.width < RSSHOT_WIDTH) ? glwidth : RSSHOT_WIDTH;
    h = (vid.height < RSSHOT_HEIGHT) ? glheight : RSSHOT_HEIGHT;

    fracw = (float)glwidth / (float)w;
    frach = (float)glheight / (float)h;

    for (y = 0; y < h; y++) {
	dest = newbuf + (w * 4 * y);

	for (x = 0; x < w; x++) {
	    r = g = b = 0;

	    dx = x * fracw;
	    dex = (x + 1) * fracw;
	    if (dex == dx)
		dex++;		// at least one
	    dy = y * frach;
	    dey = (y + 1) * frach;
	    if (dey == dy)
		dey++;		// at least one

	    count = 0;
	    for ( /* */ ; dy < dey; dy++) {
		src = newbuf + (glwidth * 4 * dy) + dx * 4;
		for (nx = dx; nx < dex; nx++) {
		    r += *src++;
		    g += *src++;
		    b += *src++;
		    src++;
		    count++;
		}
	    }
	    r /= count;
	    g /= count;
	    b /= count;
	    *dest++ = r;
	    *dest++ = g;
	    *dest++ = b;
	    dest++;
	}
    }

    // convert to eight bit
    for (y = 0; y < h; y++) {
	src = newbuf + (w * 4 * y);
	dest = newbuf + (w * y);

	for (x = 0; x < w; x++) {
	    *dest++ = MipColor(src[0], src[1], src[2]);
	    src += 4;
	}
    }
#else
    D_EnableBackBufferAccess();	// enable direct drawing of console to back
    //  buffer

    w = (vid.width < RSSHOT_WIDTH) ? vid.width : RSSHOT_WIDTH;
    h = (vid.height < RSSHOT_HEIGHT) ? vid.height : RSSHOT_HEIGHT;

    fracw = (float)vid.width / (float)w;
    frach = (float)vid.height / (float)h;

    newbuf = malloc(w * h);

    for (y = 0; y < h; y++) {
	dest = newbuf + (w * y);

	for (x = 0; x < w; x++) {
	    r = g = b = 0;

	    dx = x * fracw;
	    dex = (x + 1) * fracw;
	    if (dex == dx)
		dex++;		// at least one
	    dy = y * frach;
	    dey = (y + 1) * frach;
	    if (dey == dy)
		dey++;		// at least one

	    count = 0;
	    for ( /* */ ; dy < dey; dy++) {
		src = vid.buffer + (vid.rowbytes * dy) + dx;
		for (nx = dx; nx < dex; nx++) {
		    r += host_basepal[*src * 3];
		    g += host_basepal[*src * 3 + 1];
		    b += host_basepal[*src * 3 + 2];
		    src++;
		    count++;
		}
	    }
	    r /= count;
	    g /= count;
	    b /= count;
	    *dest++ = MipColor(r, g, b);
	}
    }
#endif

    time(&now);
    strcpy(st, ctime(&now));
    st[strlen(st) - 1] = 0;
    SCR_DrawStringToSnap(st, newbuf, w - strlen(st) * 8, 0, w, h);

    strncpy(st, cls.servername, sizeof(st));
    st[sizeof(st) - 1] = 0;
    SCR_DrawStringToSnap(st, newbuf, w - strlen(st) * 8, 10, w, h);

    strncpy(st, name.string, sizeof(st));
    st[sizeof(st) - 1] = 0;
    SCR_DrawStringToSnap(st, newbuf, w - strlen(st) * 8, 20, w, h);

    WritePCXfile(pcxname, newbuf, w, h, w, host_basepal, true);

    free(newbuf);

#ifndef GLQUAKE
    /* for adapters that can't stay mapped in for linear writes all the time */
    D_DisableBackBufferAccess();
#endif

    Con_Printf("Wrote %s\n", pcxname);
    Con_Printf("Sending shot to server...\n");
}

#endif /* QW_HACK */

#ifdef GLQUAKE
typedef struct _TargaHeader {
    unsigned char id_length, colormap_type, image_type;
    unsigned short colormap_index, colormap_length;
    unsigned char colormap_size;
    unsigned short x_origin, y_origin, width, height;
    unsigned char pixel_size, attributes;
} TargaHeader;

/* FIXME - poorly chosen globals? need to be global? */
int glx, gly, glwidth, glheight;
#endif

/*
==================
SCR_ScreenShot_f
==================
*/
static void
SCR_ScreenShot_f(void)
{
#ifdef GLQUAKE
    byte *buffer;
    char tganame[80];
    char checkname[MAX_OSPATH];
    int i, c, temp;

//
// find a file name to save it to
//
    strcpy(tganame, "quake00.tga");

    for (i = 0; i <= 99; i++) {
	tganame[5] = i / 10 + '0';
	tganame[6] = i % 10 + '0';
	sprintf(checkname, "%s/%s", com_gamedir, tganame);
	if (Sys_FileTime(checkname) == -1)
	    break;		// file doesn't exist
    }
    if (i == 100) {
	Con_Printf("%s: Couldn't create a TGA file\n", __func__);
	return;
    }

    /* Construct the TGA header */
    buffer = malloc(glwidth * glheight * 3 + 18);
    memset(buffer, 0, 18);
    buffer[2] = 2;		// uncompressed type
    buffer[12] = glwidth & 255;
    buffer[13] = glwidth >> 8;
    buffer[14] = glheight & 255;
    buffer[15] = glheight >> 8;
    buffer[16] = 24;		// pixel size

    glReadPixels(glx, gly, glwidth, glheight, GL_RGB, GL_UNSIGNED_BYTE,
		 buffer + 18);

    // swap rgb to bgr
    c = 18 + glwidth * glheight * 3;
    for (i = 18; i < c; i += 3) {
	temp = buffer[i];
	buffer[i] = buffer[i + 2];
	buffer[i + 2] = temp;
    }
    COM_WriteFile(tganame, buffer, glwidth * glheight * 3 + 18);

    free(buffer);
    Con_Printf("Wrote %s\n", tganame);
#else
    int i;
    char pcxname[80];
    char checkname[MAX_OSPATH];

//
// find a file name to save it to
//
    strcpy(pcxname, "quake00.pcx");

    for (i = 0; i <= 99; i++) {
	pcxname[5] = i / 10 + '0';
	pcxname[6] = i % 10 + '0';
	sprintf(checkname, "%s/%s", com_gamedir, pcxname);
	if (Sys_FileTime(checkname) == -1)
	    break;		// file doesn't exist
    }
    if (i == 100) {
	Con_Printf("%s: Couldn't create a PCX file\n", __func__);
	return;
    }
//
// save the pcx file
//
    D_EnableBackBufferAccess();	// enable direct drawing of console to back
    //  buffer

    WritePCXfile(pcxname, vid.buffer, vid.width, vid.height, vid.rowbytes,
		 host_basepal, false);

    D_DisableBackBufferAccess();	// for adapters that can't stay mapped in
    //  for linear writes all the time

    Con_Printf("Wrote %s\n", pcxname);
#endif
}

//=============================================================================

#ifdef NQ_HACK
/*
===============
SCR_BeginLoadingPlaque

================
*/
void
SCR_BeginLoadingPlaque(void)
{
    S_StopAllSounds(true);

    if (cls.state != ca_active)
	return;

// redraw with no console and the loading plaque
    Con_ClearNotify();
    scr_centertime_off = 0;
    scr_con_current = 0;

    scr_drawloading = true;
    scr_fullupdate = 0;
    Sbar_Changed();
    SCR_UpdateScreen();
    scr_drawloading = false;

    scr_disabled_for_loading = true;
    scr_disabled_time = realtime;
    scr_fullupdate = 0;
}

/*
==============
SCR_DrawLoading
==============
*/
static void
SCR_DrawLoading(void)
{
    const qpic8_t *pic;

    if (!scr_drawloading)
	return;

    pic = Draw_CachePic("gfx/loading.lmp");
    Draw_Pic((vid.width - pic->width) / 2,
	     (vid.height - 48 - pic->height) / 2, pic);
}

/*
===============
SCR_EndLoadingPlaque

================
*/
void
SCR_EndLoadingPlaque(void)
{
    scr_disabled_for_loading = false;
    scr_fullupdate = 0;
    Con_ClearNotify();
}
#endif /* NQ_HACK */

//=============================================================================

#ifdef GLQUAKE
static void
SCR_TileClear(void)
{
    if (r_refdef.vrect.x > 0) {
	// left
	Draw_TileClear(0, 0, r_refdef.vrect.x, vid.height - sb_lines);
	// right
	Draw_TileClear(r_refdef.vrect.x + r_refdef.vrect.width, 0,
		       vid.width - r_refdef.vrect.x + r_refdef.vrect.width,
		       vid.height - sb_lines);
    }
    if (r_refdef.vrect.y > 0) {
	// top
	Draw_TileClear(r_refdef.vrect.x, 0,
		       r_refdef.vrect.x + r_refdef.vrect.width,
		       r_refdef.vrect.y);
	// bottom
	Draw_TileClear(r_refdef.vrect.x,
		       r_refdef.vrect.y + r_refdef.vrect.height,
		       r_refdef.vrect.width,
		       vid.height - sb_lines -
		       (r_refdef.vrect.height + r_refdef.vrect.y));
    }
}
#endif

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.

WARNING: be very careful calling this from elsewhere, because the refresh
needs almost the entire 256k of stack space!
==================
*/
void
SCR_UpdateScreen(void)
{
    static float old_viewsize, old_fov;
#ifndef GLQUAKE
    vrect_t vrect;

    if (scr_skipupdate)
	return;
#endif
    if (scr_block_drawing)
	return;

#ifdef GLQUAKE
    vid.numpages = 2 + gl_triplebuffer.value;
#endif

#ifdef NQ_HACK
    if (scr_disabled_for_loading) {
	/*
	 * FIXME - this really needs to be fixed properly.
	 * Simply starting a new game and typing "changelevel foo" will hang
	 * the engine for 5s (was 60s!) if foo.bsp does not exist.
	 */
	if (realtime - scr_disabled_time > 5) {
	    scr_disabled_for_loading = false;
	    Con_Printf("load failed.\n");
	} else
	    return;
    }
#endif
#ifdef QW_HACK
    if (scr_disabled_for_loading)
	return;
#endif

#if defined(_WIN32) && !defined(GLQUAKE)
    /* Don't suck up CPU if minimized */
    if (!window_visible())
	return;
#endif

#ifdef NQ_HACK
    if (cls.state == ca_dedicated)
	return;			// stdout only
#endif

    if (!scr_initialized || !con_initialized)
	return;			// not initialized yet

    scr_copytop = 0;
    scr_copyeverything = 0;

#ifdef GLQUAKE
    GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
#endif

    /*
     * Check for vid setting changes
     */
    if (old_fov != scr_fov.value) {
	old_fov = scr_fov.value;
	vid.recalc_refdef = true;
    }
    if (old_viewsize != scr_viewsize.value) {
	old_viewsize = scr_viewsize.value;
	vid.recalc_refdef = true;
    }
#ifdef QW_HACK
    if (oldsbar != cl_sbar.value) {
	oldsbar = cl_sbar.value;
	vid.recalc_refdef = true;
    }
#endif

    if (vid.recalc_refdef)
	SCR_CalcRefdef();

    /*
     * do 3D refresh drawing, and then update the screen
     */
#ifdef GLQUAKE
    SCR_SetUpToDrawConsole();
#else
    D_EnableBackBufferAccess();	/* for overlay stuff, if drawing directly */

    if (scr_fullupdate++ < vid.numpages) {
	/* clear the entire screen */
	scr_copyeverything = 1;
	Draw_TileClear(0, 0, vid.width, vid.height);
	Sbar_Changed();
    }
    pconupdate = NULL;
    SCR_SetUpToDrawConsole();
    SCR_EraseCenterString();

    /* for adapters that can't stay mapped in for linear writes all the time */
    D_DisableBackBufferAccess();

    VID_LockBuffer();
#endif /* !GLQUAKE */

    V_RenderView();

#ifdef GLQUAKE
    GL_Set2D();

    /* draw any areas not covered by the refresh */
    SCR_TileClear();

#ifdef QW_HACK /* FIXME - draw from same place as SW renderer? */
    if (r_netgraph.value)
	R_NetGraph();
#endif

#else /* !GLQUAKE */
    VID_UnlockBuffer();
    D_EnableBackBufferAccess();	// of all overlay stuff if drawing directly
#endif /* !GLQUAKE */

    if (scr_drawdialog) {
	Sbar_Draw();
	if (con_forcedup)
	    Draw_ConsoleBackground(vid.height);
	Draw_FadeScreen();
	SCR_DrawNotifyString();
	scr_copyeverything = true;
#ifdef NQ_HACK
    } else if (scr_drawloading) {
	SCR_DrawLoading();
	Sbar_Draw();
#endif
    } else if (cl.intermission == 1 && key_dest == key_game) {
	Sbar_IntermissionOverlay();
    } else if (cl.intermission == 2 && key_dest == key_game) {
	Sbar_FinaleOverlay();
	SCR_DrawCenterString();
#if defined(NQ_HACK) && !defined(GLQUAKE) /* FIXME? */
    } else if (cl.intermission == 3 && key_dest == key_game) {
	SCR_DrawCenterString();
#endif
    } else {
#if defined(NQ_HACK) && defined(GLQUAKE)
	if (crosshair.value) {
	    //Draw_Crosshair();
	    Draw_Character(scr_vrect.x + scr_vrect.width / 2,
			   scr_vrect.y + scr_vrect.height / 2, '+');
	}
#endif
#if defined(QW_HACK) && defined(GLQUAKE)
	if (crosshair.value)
	    Draw_Crosshair();
#endif
	SCR_DrawRam();
	SCR_DrawNet();
	SCR_DrawFPS();
	SCR_DrawTurtle();
	SCR_DrawPause();
	SCR_DrawCenterString();
	Sbar_Draw();
	SCR_DrawConsole();
	M_Draw();
    }

#ifndef GLQUAKE
    /* for adapters that can't stay mapped in for linear writes all the time */
    D_DisableBackBufferAccess();
    if (pconupdate)
	D_UpdateRects(pconupdate);
#endif

    V_UpdatePalette();

#ifdef GLQUAKE
    GL_EndRendering();
#else
    /*
     * update one of three areas
     */
    if (scr_copyeverything) {
	vrect.x = 0;
	vrect.y = 0;
	vrect.width = vid.width;
	vrect.height = vid.height;
	vrect.pnext = 0;

	VID_Update(&vrect);
    } else if (scr_copytop) {
	vrect.x = 0;
	vrect.y = 0;
	vrect.width = vid.width;
	vrect.height = vid.height - sb_lines;
	vrect.pnext = 0;

	VID_Update(&vrect);
    } else {
	vrect.x = scr_vrect.x;
	vrect.y = scr_vrect.y;
	vrect.width = scr_vrect.width;
	vrect.height = scr_vrect.height;
	vrect.pnext = 0;

	VID_Update(&vrect);
    }
#endif
}

#if !defined(GLQUAKE) && defined(_WIN32)
/*
==================
SCR_UpdateWholeScreen
FIXME - vid_win.c only?
==================
*/
void
SCR_UpdateWholeScreen(void)
{
    scr_fullupdate = 0;
    SCR_UpdateScreen();
}
#endif

//=============================================================================

/*
==================
SCR_Init
==================
*/
void
SCR_Init(void)
{
    Cvar_RegisterVariable(&scr_fov);
    Cvar_RegisterVariable(&scr_viewsize);
    Cvar_RegisterVariable(&scr_conspeed);
    Cvar_RegisterVariable(&scr_showram);
    Cvar_RegisterVariable(&scr_showturtle);
    Cvar_RegisterVariable(&scr_showpause);
    Cvar_RegisterVariable(&scr_centertime);
    Cvar_RegisterVariable(&scr_printspeed);
    Cvar_RegisterVariable(&show_fps);
#ifdef GLQUAKE
    Cvar_RegisterVariable(&gl_triplebuffer);
#endif

    Cmd_AddCommand("screenshot", SCR_ScreenShot_f);
    Cmd_AddCommand("sizeup", SCR_SizeUp_f);
    Cmd_AddCommand("sizedown", SCR_SizeDown_f);

    scr_ram = Draw_PicFromWad("ram");
    scr_net = Draw_PicFromWad("net");
    scr_turtle = Draw_PicFromWad("turtle");

#ifdef QW_HACK
    Cvar_RegisterVariable(&scr_allowsnap);
    Cmd_AddCommand("snap", SCR_RSShot_f);
#endif

    scr_initialized = true;
}
