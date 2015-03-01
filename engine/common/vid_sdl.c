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

#include <stdlib.h>

#include "SDL.h"

#include "cdaudio.h"
#include "cmd.h"
#include "common.h"
#include "console.h"
#include "cvar.h"
#include "d_iface.h"
#include "d_local.h"
#include "draw.h"
#include "input.h"
#include "keys.h"
#include "menu.h"
#include "quakedef.h"
#include "screen.h"
#include "sdl_common.h"
#include "sound.h"
#include "sys.h"
#include "vid.h"
#include "view.h"
#include "wad.h"

#ifdef _WIN32
#include "winquake.h"
#endif

#ifdef NQ_HACK
#include "host.h"
#endif
#ifdef QW_HACK
#include "client.h"
#endif

// FIXME: evil hack to get full DirectSound support with SDL
#ifdef _WIN32
#include <windows.h>
HWND mainwindow;
qboolean DDActive = false;
#endif

static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;
static SDL_PixelFormat *sdl_format = NULL;
static SDL_PixelFormat *sdl_desktop_format = NULL;

/* ------------------------------------------------------------------------- */

static byte *vid_surfcache;
static int vid_surfcachesize;
static int VID_highhunkmark;
static int window_width, window_height;

unsigned short d_8to16table[256];
unsigned d_8to24table[256];
viddef_t vid; /* global video state */

#ifdef _WIN32
static HICON hIcon;
#endif

void VID_Shutdown(void)
{
    if (renderer)
	SDL_DestroyRenderer(renderer);
    if (sdl_window)
	SDL_DestroyWindow(sdl_window);
    if (sdl_format)
	SDL_FreeFormat(sdl_format);
    if (sdl_desktop_format)
	SDL_FreeFormat(sdl_desktop_format);
}

static qboolean palette_changed;

void
VID_ShiftPalette(const byte *palette)
{
    VID_SetPalette(palette);
}

void VID_SetDefaultMode(void) { }

static cvar_t vid_mode = {
    .name = "vid_mode",
    .string = stringify(VID_MODE_WINDOWED),
    .archive = false
};
static cvar_t _vid_default_mode = {
    .name = "_vid_default_mode",
    .string = stringify(VID_MODE_WINDOWED),
    .archive = true
};
static cvar_t _vid_default_mode_win = {
    .name = "_vid_default_mode_win",
    .string = stringify(VID_MODE_FULLSCREEN_DEFAULT),
    .archive = true
};
static cvar_t vid_fullscreen_mode = {
    .name = "vid_fullscreen_mode",
    .string = stringify(VID_MODE_FULLSCREEN_DEFAULT),
    .archive = true
};
static cvar_t vid_windowed_mode = {
    .name = "vid_windowed_mode",
    .string = stringify(VID_MODE_WINDOWED),
    .archive = true
};

static cvar_t vid_wait = { "vid_wait", "0" };
static cvar_t vid_nopageflip = { "vid_nopageflip", "0", true };
static cvar_t _vid_wait_override = { "_vid_wait_override", "0", true };
static cvar_t block_switch = { "block_switch", "0", true };
static cvar_t vid_window_x = { "vid_window_x", "0", true };
static cvar_t vid_window_y = { "vid_window_y", "0", true };

int vid_modenum = VID_MODE_NONE;

typedef struct {
    typeof(SDL_PIXELFORMAT_UNKNOWN) format;
} qvidformat_t;

static qboolean Minimized;

qboolean
window_visible(void)
{
    return !Minimized;
}

static void
VID_InitModeList(void)
{
    int i, err;
    int displays, sdlmodes;
    SDL_DisplayMode sdlmode;
    qvidmode_t *mode;
    qvidformat_t *format;

    displays = SDL_GetNumVideoDisplays();
    if (displays < 1)
	Sys_Error("%s: no displays found (%s)", __func__, SDL_GetError());

    /* FIXME - allow use of more than one display */
    sdlmodes = SDL_GetNumDisplayModes(0);
    if (sdlmodes < 0)
	Con_SafePrintf("%s: error enumerating SDL display modes (%s)\n",
		       __func__, SDL_GetError());

    /*
     * Check availability of fullscreen modes
     * (default to display 0 for now)
     */
    mode = &modelist[1];
    nummodes = 1;
    for (i = 0; i < sdlmodes && nummodes < MAX_MODE_LIST; i++) {
	err = SDL_GetDisplayMode(0, i, &sdlmode);
	if (err)
	    Sys_Error("%s: couldn't get mode %d info (%s)",
		      __func__, i, SDL_GetError());

	printf("%s: checking mode %i: %dx%d, %s\n", __func__,
	       i, sdlmode.w, sdlmode.h, SDL_GetPixelFormatName(sdlmode.format));

	if (sdlmode.h > MAXHEIGHT || sdlmode.w > MAXWIDTH)
	    continue;

	if (SDL_PIXELTYPE(sdlmode.format) == SDL_PIXELTYPE_PACKED32)
	    modelist[nummodes].bpp = 32;
	else if (SDL_PIXELTYPE(sdlmode.format) == SDL_PIXELTYPE_PACKED16)
	    modelist[nummodes].bpp = 16;
	else
	    continue;

	mode->modenum = nummodes;
	mode->width = sdlmode.w;
	mode->height = sdlmode.h;
	mode->refresh = sdlmode.refresh_rate;
	format = (qvidformat_t *)mode->driverdata;
	format->format = sdlmode.format;
	nummodes++;
	mode++;
    }

    VID_SortModeList(modelist, nummodes);
}

/*
====================
VID_CheckAdequateMem
====================
*/
qboolean
VID_CheckAdequateMem(int width, int height)
{
    int tbuffersize;

    tbuffersize = width * height * sizeof(*d_pzbuffer);
    tbuffersize += D_SurfaceCacheForRes(width, height);

    /*
     * see if there's enough memory, allowing for the normal mode 0x13 pixel,
     * z, and surface buffers
     */
    if ((host_parms.memsize - tbuffersize + SURFCACHE_SIZE_AT_320X200 +
	 0x10000 * 3) < minimum_memory)
	return false;

    return true;
}


/*
================
VID_AllocBuffers
================
*/
static qboolean
VID_AllocBuffers(int width, int height)
{
    int tsize, tbuffersize;

    tsize = D_SurfaceCacheForRes(width, height);
    tbuffersize = width * height * sizeof(*d_pzbuffer);
    tbuffersize += tsize;

    /*
     * see if there's enough memory, allowing for the normal mode 0x13 pixel,
     * z, and surface buffers
     */
    if ((host_parms.memsize - tbuffersize + SURFCACHE_SIZE_AT_320X200 +
	 0x10000 * 3) < minimum_memory) {
	Con_SafePrintf("Not enough memory for video mode\n");
	return false;
    }

    vid_surfcachesize = tsize;

    if (d_pzbuffer) {
	D_FlushCaches();
	Hunk_FreeToHighMark(VID_highhunkmark);
	d_pzbuffer = NULL;
    }

    VID_highhunkmark = Hunk_HighMark();
    d_pzbuffer = Hunk_HighAllocName(tbuffersize, "video");
    vid_surfcache = (byte *)d_pzbuffer + width * height * sizeof(*d_pzbuffer);

    return true;
}

qboolean
VID_IsFullScreen()
{
    return !!vid_modenum;
}

qboolean
VID_SetMode(const qvidmode_t *mode, const byte *palette)
{
    Uint32 flags;
    qboolean mouse_grab;
    const qvidformat_t *format;

    /* FIXME - hack to reset mouse grabs */
    mouse_grab = _windowed_mouse.value;
    if (mouse_grab) {
	_windowed_mouse.value = 0;
	_windowed_mouse.callback(&_windowed_mouse);
    }

    flags = SDL_WINDOW_SHOWN;
    if (mode != modelist)
	flags |= SDL_WINDOW_FULLSCREEN;

    if (renderer)
	SDL_DestroyRenderer(renderer);
    if (sdl_window)
	SDL_DestroyWindow(sdl_window);
    if (sdl_format)
	SDL_FreeFormat(sdl_format);

    format = (const qvidformat_t *)mode->driverdata;
    sdl_format = SDL_AllocFormat(format->format);

    sdl_window = SDL_CreateWindow("TyrQuake",
				  SDL_WINDOWPOS_UNDEFINED,
				  SDL_WINDOWPOS_UNDEFINED,
				  mode->width, mode->height, flags);
    if (!sdl_window)
	Sys_Error("%s: Unable to create window: %s", __func__, SDL_GetError());

    renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
	Sys_Error("%s: Unable to create renderer: %s", __func__, SDL_GetError());

    texture = SDL_CreateTexture(renderer,
				format->format,
				SDL_TEXTUREACCESS_STREAMING,
				mode->width, mode->height);
    if (!texture)
	Sys_Error("%s: Unable to create texture: %s", __func__, SDL_GetError());

    //VID_InitGamma(palette);
    VID_SetPalette(palette);

    vid.numpages = 1;
    vid.width = vid.conwidth = mode->width;
    vid.height = vid.conheight = mode->height;
    vid.maxwarpwidth = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;
    vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);

    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));

    VID_AllocBuffers(vid.width, vid.height);

    // In-memory buffer which we upload via SDL texture
    vid.buffer = vid.conbuffer = vid.direct = Hunk_HighAllocName(vid.width * vid.height, "vidbuf");
    vid.rowbytes = vid.conrowbytes = vid.width;

    D_InitCaches(vid_surfcache, vid_surfcachesize);

    window_width = vid.width;
    window_height = vid.height;

    vid_modenum = mode - modelist;
    Cvar_SetValue("vid_mode", vid_modenum);

    vid.recalc_refdef = 1;

#ifdef _WIN32
    mainwindow=GetActiveWindow();
    SendMessage(mainwindow, WM_SETICON, (WPARAM)ICON_BIG, (LPARAM)hIcon);
    SendMessage(mainwindow, WM_SETICON, (WPARAM)ICON_SMALL, (LPARAM)hIcon);
#endif

    /* FIXME - hack to reset mouse grabs */
    if (mouse_grab) {
	_windowed_mouse.value = 1;
	_windowed_mouse.callback(&_windowed_mouse);
    }

    Cvar_SetValue("vid_fullscreen", mode != modelist);
    Cvar_SetValue("vid_width", mode->width);
    Cvar_SetValue("vid_height", mode->height);
    Cvar_SetValue("vid_bpp", mode->bpp);
    Cvar_SetValue("vid_refreshrate", mode->refresh);

    return true;
}

/* ------------------------------------------------------------------------- */

// The original defaults
#define BASEWIDTH 320
#define BASEHEIGHT 200

byte *VGA_pagebase;
int VGA_width, VGA_height, VGA_rowbytes, VGA_bufferrowbytes = 0;

void
VID_SetPalette(const byte *palette)
{
    unsigned i, r, g, b;

    switch (SDL_PIXELTYPE(sdl_format->format)) {
    case SDL_PIXELTYPE_PACKED32:
	for (i = 0; i < 256; i++) {
	    r = palette[0];
	    g = palette[1];
	    b = palette[2];
	    palette += 3;
	    d_8to24table[i] = SDL_MapRGB(sdl_format, r, g, b);
	}
	break;
    case SDL_PIXELTYPE_PACKED16:
	for (i = 0; i < 256; i++) {
	    r = palette[0];
	    g = palette[1];
	    b = palette[2];
	    palette += 3;
	    d_8to16table[i] = SDL_MapRGB(sdl_format, r, g, b);
	}
	break;
    default:
	Sys_Error("%s: unsupported pixel format (%s)", __func__,
		  SDL_GetPixelFormatName(sdl_format->format));
    }

    palette_changed = true;
}

void
VID_Init(const byte *palette)
{
    int err;
    SDL_DisplayMode desktop_mode;
    qvidmode_t *mode;
    qvidformat_t *format;
    const qvidmode_t *setmode;

    Cvar_RegisterVariable(&vid_mode);
    Cvar_RegisterVariable(&vid_wait);
    Cvar_RegisterVariable(&vid_nopageflip);
    Cvar_RegisterVariable(&_vid_wait_override);
    Cvar_RegisterVariable(&_vid_default_mode);
    Cvar_RegisterVariable(&_vid_default_mode_win);
    Cvar_RegisterVariable(&vid_fullscreen_mode);
    Cvar_RegisterVariable(&vid_windowed_mode);
    Cvar_RegisterVariable(&block_switch);
    Cvar_RegisterVariable(&vid_window_x);
    Cvar_RegisterVariable(&vid_window_y);

    VID_InitModeCvars();

    Cmd_AddCommand("vid_describemodes", VID_DescribeModes_f);

    /*
     * Init SDL and the video subsystem
     */
    Q_SDL_InitOnce();
    err = SDL_InitSubSystem(SDL_INIT_VIDEO);
    if (err < 0)
	Sys_Error("VID: Couldn't load SDL: %s", SDL_GetError());

    err = SDL_GetDesktopDisplayMode(0, &desktop_mode);
    if (err)
	Sys_Error("%s: Unable to query desktop display mode (%s)",
		  __func__, SDL_GetError());
    sdl_desktop_format = SDL_AllocFormat(desktop_mode.format);
    if (!sdl_desktop_format)
	Sys_Error("%s: Unable to allocate desktop pixel format (%s)",
		  __func__, SDL_GetError());

    /* Init default windowed mode */
    mode = modelist;
    mode->modenum = 0;
    mode->bpp = sdl_desktop_format->BitsPerPixel;
    format = (qvidformat_t *)mode->driverdata;
    format->format = sdl_desktop_format->format;
    mode->refresh = desktop_mode.refresh_rate;
    mode->width = 640;
    mode->height = 480;
    nummodes = 1;

    /* TODO: read config files first to avoid multiple mode sets */
    VID_InitModeList();
    setmode = VID_GetCmdlineMode();
    if (!setmode)
	setmode = &modelist[0];

    VID_SetMode(setmode, palette);

#ifdef _WIN32
    mainwindow=GetActiveWindow();
    SendMessage(mainwindow, WM_SETICON, (WPARAM)ICON_BIG, (LPARAM)hIcon);
    SendMessage(mainwindow, WM_SETICON, (WPARAM)ICON_SMALL, (LPARAM)hIcon);
#endif

    vid_menudrawfn = VID_MenuDraw;
    vid_menukeyfn = VID_MenuKey;
}

void
VID_Update(vrect_t *rects)
{
    SDL_Rect subrect;
    int i;
    vrect_t *rect;
    vrect_t fullrect;
    byte *src;
    void *dst;
    Uint32 *dst32;
    Uint16 *dst16;
    int pitch;
    int height;
    int err;
    const qvidmode_t *mode;

    /*
     * Check for vid_mode changes
     */
    if ((int)vid_mode.value != vid_modenum) {
	mode = &modelist[(int)vid_mode.value];
	VID_SetMode(mode, host_basepal);
	/* FIXME - not the right place! redraw the scene to buffer first */
	return;
    }

    /*
     * If the palette changed, refresh the whole screen
     */
    if (palette_changed) {
	palette_changed = false;
	fullrect.x = 0;
	fullrect.y = 0;
	fullrect.width = vid.width;
	fullrect.height = vid.height;
	fullrect.pnext = NULL;
	rects = &fullrect;
    }

    for (rect = rects; rect; rect = rect->pnext) {
	subrect.x = rect->x;
	subrect.y = rect->y;
	subrect.w = rect->width;
	subrect.h = rect->height;

	err = SDL_LockTexture(texture, &subrect, (void **)&dst, &pitch);
	if (err)
	    Sys_Error("%s: unable to lock texture (%s)",
		      __func__, SDL_GetError());
	src = vid.buffer + rect->y * vid.width + rect->x;
	height = subrect.h;
	switch (SDL_PIXELTYPE(sdl_format->format)) {
	case SDL_PIXELTYPE_PACKED32:
	    dst32 = dst;
	    while (height--) {
		for (i = 0; i < rect->width; i++)
		    dst32[i] = d_8to24table[src[i]];
		dst32 += pitch / sizeof(*dst32);
		src += vid.width;
	    }
	    break;
	case SDL_PIXELTYPE_PACKED16:
	    dst16 = dst;
	    while (height--) {
		for (i = 0; i < rect->width; i++)
		    dst16[i] = d_8to16table[src[i]];
		dst16 += pitch / sizeof(*dst16);
		src += vid.width;
	    }
	    break;
	default:
	    Sys_Error("%s: unsupported pixel format (%s)", __func__,
		      SDL_GetPixelFormatName(sdl_format->format));
	}
	SDL_UnlockTexture(texture);
    }
    err = SDL_RenderCopy(renderer, texture, NULL, NULL);
    if (err)
	Sys_Error("%s: unable to render texture (%s)", __func__, SDL_GetError());
    SDL_RenderPresent(renderer);
}

void
D_BeginDirectRect(int x, int y, const byte *pbitmap, int width, int height)
{
    int err, i;
    const byte *src;
    unsigned *dst;
    int pitch;
    SDL_Rect subrect;

    if (!texture || !renderer)
	return;

    subrect.x = (x < 0) ? vid.width + x - 1 : x;
    subrect.y = y;
    subrect.w = width;
    subrect.h = height;

    err = SDL_LockTexture(texture, &subrect, (void **)&dst, &pitch);
    if (err)
	Sys_Error("%s: unable to lock texture (%s)", __func__, SDL_GetError());
    src = pbitmap;
    while (height--) {
	for (i = 0; i < width; i++)
	    dst[i] = d_8to24table[src[i]];
	dst += pitch / sizeof(*dst);
	src += width;
    }
    SDL_UnlockTexture(texture);

    err = SDL_RenderCopy(renderer, texture, NULL, NULL);
    if (err)
	Sys_Error("%s: unable to render texture (%s)", __func__, SDL_GetError());
    SDL_RenderPresent(renderer);
}

void
D_EndDirectRect(int x, int y, int width, int height)
{
    int err, i;
    byte *src;
    unsigned *dst;
    int pitch;
    SDL_Rect subrect;

    if (!texture || !renderer)
	return;

    subrect.x = (x < 0) ? vid.width + x - 1 : x;
    subrect.y = y;
    subrect.w = width;
    subrect.h = height;

    err = SDL_LockTexture(texture, &subrect, (void **)&dst, &pitch);
    if (err)
	Sys_Error("%s: unable to lock texture (%s)", __func__, SDL_GetError());
    src = vid.buffer + y * vid.width + subrect.x;
    while (height--) {
	for (i = 0; i < width; i++)
	    dst[i] = d_8to24table[src[i]];
	dst += pitch / sizeof(*dst);
	src += vid.width;
    }
    SDL_UnlockTexture(texture);

    err = SDL_RenderCopy(renderer, texture, NULL, NULL);
    if (err)
	Sys_Error("%s: unable to render texture (%s)", __func__, SDL_GetError());
    SDL_RenderPresent(renderer);
}

void
VID_LockBuffer(void)
{
}

void
VID_UnlockBuffer(void)
{
}

#ifndef _WIN32
void
Sys_SendKeyEvents(void)
{
    IN_ProcessEvents();
}
#endif
