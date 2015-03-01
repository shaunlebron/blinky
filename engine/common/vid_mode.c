/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2013 Kevin Shanahan

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

#include "cmd.h"
#include "common.h"
#include "console.h"
#include "draw.h"
#include "keys.h"
#include "menu.h"
#include "sound.h"
#include "sys.h"
#include "vid.h"

#ifdef NQ_HACK
#include "host.h" /* realtime */
#endif
#ifdef QW_HACK
#include "quakedef.h" /* realtime */
#endif

qvidmode_t modelist[MAX_MODE_LIST];
int nummodes;

/* FIXME - vid mode testing */
int vid_testingmode;
int vid_realmode;
double vid_testendtime;

static const char *VID_GetModeDescription(const qvidmode_t *mode);

static cvar_t vid_fullscreen = { "vid_fullscreen", "0", true };
static cvar_t vid_width = { "vid_width", "640", true };
static cvar_t vid_height = { "vid_height", "480", true };
static cvar_t vid_bpp = { "vid_bpp", "32", true };
static cvar_t vid_refreshrate = { "vid_refreshrate", "60", true };

/* Compare function for qsort - highest res to lowest */
static int
qvidmodecmp(const void *inmode1, const void *inmode2)
{
    const qvidmode_t *const mode1 = inmode1;
    const qvidmode_t *const mode2 = inmode2;

    if (mode1->width < mode2->width)
	return -1;
    if (mode1->width > mode2->width)
	return 1;
    if (mode1->height < mode2->height)
	return -1;
    if (mode1->height > mode2->height)
	return 1;
    if (mode1->bpp < mode2->bpp)
	return -1;
    if (mode1->bpp > mode2->bpp)
	return 1;
    if (mode1->refresh < mode2->refresh)
	return -1;
    if (mode1->refresh > mode2->refresh)
	return 1;

    return 0;
}

void
VID_SortModeList(qvidmode_t *modelist, int nummodes)
{
    /* First entry is for windowed mode */
    qsort(modelist + 1, nummodes - 1, sizeof(qvidmode_t), qvidmodecmp);
}

typedef enum {
    VID_MENU_CURSOR_RESOLUTION,
    VID_MENU_CURSOR_BPP,
    VID_MENU_CURSOR_REFRESH,
    VID_MENU_CURSOR_FULLSCREEN,
    VID_MENU_CURSOR_TEST,
    VID_MENU_CURSOR_APPLY,
    VID_MENU_CURSOR_LINES,
} vid_menu_cursor_t;

typedef struct {
    qvidmode_t mode;
    qboolean fullscreen;
    vid_menu_cursor_t cursor;
} vid_menustate_t;

static vid_menustate_t vid_menustate;

void
VID_MenuInitState(const qvidmode_t *mode)
{
    vid_menustate.mode = *mode;
    vid_menustate.fullscreen = !!vid_modenum;
    vid_menustate.cursor = VID_MENU_CURSOR_RESOLUTION;
}

/*
================
VID_MenuDraw
================
*/
void
VID_MenuDraw_(const vid_menustate_t *menu)
{
    static const int cursor_heights[] = { 48, 56, 64, 72, 88, 96 };
    const qpic8_t *pic;
    const char *text;
    vid_menu_cursor_t cursor = VID_MENU_CURSOR_RESOLUTION;
    int rwidth, rheight, divisor;

    /* Calculate relative width/height for aspect ratio */
    divisor = Q_gcd(menu->mode.width, menu->mode.height);
    rwidth = menu->mode.width / divisor;
    rheight = menu->mode.height / divisor;

    M_DrawPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));

    pic = Draw_CachePic("gfx/p_option.lmp");
    M_DrawPic((320 - pic->width) / 2, 4, pic);

    text = "Video Options";
    M_PrintWhite((320 - 8 * strlen(text)) / 2, 32, text);

    M_Print(16, cursor_heights[cursor], "        Video mode");
    M_Print(184, cursor_heights[cursor], va("%ix%i (%i:%i)", menu->mode.width, menu->mode.height, rwidth, rheight));
    cursor++;

    M_Print(16, cursor_heights[cursor], "       Color depth");
    M_Print(184, cursor_heights[cursor], va("%i", menu->mode.bpp));
    cursor++;

    /* Refresh rate is not always available */
    if (menu->mode.refresh && menu->fullscreen)
	text = va("%i Hz", menu->mode.refresh);
    else
	text = "n/a";
    M_Print(16, cursor_heights[cursor], "      Refresh rate");
    M_Print(184, cursor_heights[cursor], text);
    cursor++;

    M_Print(16, cursor_heights[cursor], "        Fullscreen");
    M_DrawCheckbox(184, cursor_heights[cursor], menu->fullscreen);
    cursor++;

    M_Print(16, cursor_heights[cursor], "      Test changes");
    cursor++;

    M_Print (16, cursor_heights[cursor], "     Apply changes");
    cursor++;

    /* cursor */
    M_DrawCharacter(168, cursor_heights[menu->cursor],
		    12 + ((int)(realtime * 4) & 1));
}

void
VID_MenuDraw(void)
{
    VID_MenuDraw_(&vid_menustate);
}

static const qvidmode_t *
VID_FindNextResolution(int width, int height)
{
    const qvidmode_t *mode;

    /* Find where the given resolution fits into the modelist */
    mode = &modelist[1];
    while (mode - modelist < nummodes) {
	if (mode->width > width)
	    break;
	if (mode->width == width && mode->height > height)
	    break;
	mode++;
    }

    /* If we didn't find anything bigger, return the first mode */
    if (mode - modelist == nummodes)
	return &modelist[1];

    return mode;
}

static const qvidmode_t *
VID_FindPrevResolution(int width, int height)
{
    const qvidmode_t *mode;

    /* Find where the given resolution fits into the modelist */
    mode = &modelist[1];
    while (mode - modelist < nummodes) {
	if (mode->width == width && mode->height == height)
	    break;
	if (mode->width > width)
	    break;
	if (mode->width == width && mode->height > height)
	    break;
	mode++;
    }

    /* The preceding resolution is the one we want */
    if (mode == &modelist[1])
	mode = &modelist[nummodes - 1];
    else
	mode--;

    /* Now, run backwards to find the first mode for this resolution */
    while (mode > &modelist[1]) {
	const qvidmode_t *check = mode - 1;
	if (check->width != mode->width || check->height != mode->height)
	    break;
	mode--;
    }

    return mode;
}

/*
 * VID_BestModeMatch
 *
 * Find the best match for bpp and refresh in 'mode' in the modelist.
 * 'match' is a pointer to the first mode with matching resolution.
 */
static const qvidmode_t *
VID_BestModeMatch(const qvidmode_t *mode, const qvidmode_t *test)
{
    const qvidmode_t *match;

    for (match = test; test - modelist < nummodes; test++) {
	if (test->width != mode->width || test->height != mode->height)
	    break;

	/* If this bpp is a better match, take match this mode */
	if (abs(test->bpp - mode->bpp) < abs(match->bpp - mode->bpp)) {
	    match = test;
	    continue;
	}
	if (test->bpp != match->bpp)
	    continue;

	/* If the bpp are equal, take the better match for refresh */
	if (abs(test->refresh - mode->refresh) < abs(match->refresh - mode->refresh))
	    match = test;
    }

    return match;
}

static const qvidmode_t *
VID_FindMode(int width, int height, int bpp)
{
    const qvidmode_t *mode;

    for (mode = &modelist[1]; mode - modelist < nummodes; mode++) {
	if (mode->width != width || mode->height != height)
	    continue;
	if (bpp && mode->bpp != bpp)
	    continue;
	return mode;
    }

    return NULL;
}

static const qvidmode_t *
VID_FindNextBpp(const qvidmode_t *mode)
{
    const qvidmode_t *first, *test, *match, *next;

    /* If we're passed an invalid mode, just return the default mode */
    test = VID_FindMode(mode->width, mode->height, 0);
    if (!test)
	return &modelist[1];

    /*
     * Scan the mode list until we find the entries with matching bpp.
     * Find the next bpp (if any) with the same resolution.
     * If we didn't find the next, loop back to the first entry.
     */
    first = test;
    match = NULL;
    next = NULL;
    while (test - modelist < nummodes) {
	if (test->width != mode->width || test->height != mode->height)
	    break;
	if (match && match->bpp != test->bpp) {
	    next = test;
	    break;
	}
	if (!match && test->bpp == mode->bpp)
	    match = test;
	test++;
    }
    if (!next)
	next = first;

    /* Match the refresh rate as best we can for this resolution/bpp */
    test = next;
    while (test - modelist < nummodes) {
	if (test->width != mode->width || test->height != mode->height)
	    break;
	if (test->bpp != next->bpp)
	    break;
	if (abs(test->refresh - mode->refresh) < abs(next->refresh - mode->refresh))
	    next = test;
	test++;
    }

    return next;
}

static const qvidmode_t *
VID_FindPrevBpp(const qvidmode_t *mode)
{
    const qvidmode_t *prev, *test;

    /* If we're passed an invalid mode, just return the default mode */
    test = VID_FindMode(mode->width, mode->height, 0);
    if (!test)
	return &modelist[1];

    /*
     * Scan the mode list until we find the entries with matching bpp.
     * Scan backwards for the previous bpp (if any) with same resolution.
     * If we didn't find a previous, loop back the last one.
     */
    prev = NULL;
    while (test - modelist < nummodes) {
	if (test->width != mode->width || test->height != mode->height)
	    break;
	if (test->bpp == mode->bpp)
	    break;
	prev = test++;
    }
    if (!prev) {
	/* place prev on the last mode for this resolution */
	while (test - modelist < nummodes) {
	    if (test->width != mode->width || test->height != mode->height)
		break;
	    prev = test++;
	}
    }
    /* Should never happen... */
    if (!prev)
	return mode;

    /* Find the best matching refresh at the new bpp */
    test = prev;
    while (test > &modelist[0]) {
	if (test->width != mode->width || test->height != mode->height)
	    break;
	if (test->bpp != prev->bpp)
	    break;
	if (abs(test->refresh - mode->refresh) < abs(prev->refresh - mode->refresh))
	    prev = test;
	test--;
    }

    return prev;
}

static const qvidmode_t *
VID_FindNextRefresh(const qvidmode_t *mode)
{
    const qvidmode_t *test, *first, *next, *match;

    first = NULL;
    for (test = &modelist[1]; test - modelist < nummodes; test++) {
	if (test->width != mode->width || test->height != mode->height)
	    continue;
	if (test->bpp != mode->bpp)
	    continue;
	first = test;
	break;
    }

    if (!first)
	return mode;

    next = NULL;
    match = NULL;
    while (test - modelist < nummodes) {
	if (test->width != mode->width || test->height != mode->height)
	    break;
	if (test->bpp != mode->bpp)
	    break;
	if (match && test->refresh != mode->refresh) {
	    next = test;
	    break;
	}
	if (!match && test->refresh == mode->refresh)
	    match = test;
	test++;
    }

    return next ? next : first;
}


static const qvidmode_t *
VID_FindPrevRefresh(const qvidmode_t *mode)
{
    const qvidmode_t *test, *first, *prev;

    first = NULL;
    for (test = &modelist[1]; test - modelist < nummodes; test++) {
	if (test->width != mode->width || test->height != mode->height)
	    continue;
	if (test->bpp != mode->bpp)
	    continue;
	first = test;
	break;
    }
    if (!first)
	return mode;

    prev = NULL;
    while (test - modelist < nummodes) {
	if (test->width != mode->width || test->height != mode->height)
	    break;
	if (test->bpp != mode->bpp)
	    break;
	if (test->refresh == mode->refresh && prev)
	    break;
	prev = test++;
    }

    return prev ? prev : mode;
}

/*
================
VID_MenuKey
================
*/
void
VID_MenuKey_(vid_menustate_t *menu, knum_t keynum)
{
    const qvidmode_t *mode;

    switch (keynum) {
    case K_ESCAPE:
	S_LocalSound("misc/menu1.wav");
	M_Menu_Options_f();
	break;
    case K_UPARROW:
	S_LocalSound("misc/menu1.wav");
	if (menu->cursor == VID_MENU_CURSOR_RESOLUTION)
	    menu->cursor = VID_MENU_CURSOR_LINES - 1;
	else
	    menu->cursor--;
	break;
    case K_DOWNARROW:
	S_LocalSound("misc/menu1.wav");
	if (menu->cursor == VID_MENU_CURSOR_LINES - 1)
	    menu->cursor = VID_MENU_CURSOR_RESOLUTION;
	else
	    menu->cursor++;
	break;
    case K_LEFTARROW:
	S_LocalSound("misc/menu3.wav");
	switch (menu->cursor) {
	case VID_MENU_CURSOR_RESOLUTION:
	    mode = VID_FindPrevResolution(menu->mode.width, menu->mode.height);
	    menu->mode.width = mode->width;
	    menu->mode.height = mode->height;
	    mode = VID_BestModeMatch(&menu->mode, mode);
	    menu->mode = *mode;
	    break;
	case VID_MENU_CURSOR_BPP:
	    mode = VID_FindPrevBpp(&menu->mode);
	    menu->mode.bpp = mode->bpp;
	    menu->mode.refresh = mode->refresh;
	    break;
	case VID_MENU_CURSOR_REFRESH:
	    if (menu->fullscreen) {
		mode = VID_FindPrevRefresh(&menu->mode);
		menu->mode.refresh = mode->refresh;
	    }
	    break;
	case VID_MENU_CURSOR_FULLSCREEN:
	    menu->fullscreen = !menu->fullscreen;
	    break;
	default:
	    break;
	}
	break;
    case K_RIGHTARROW:
	S_LocalSound("misc/menu3.wav");
	switch (menu->cursor) {
	case VID_MENU_CURSOR_RESOLUTION:
	    mode = VID_FindNextResolution(menu->mode.width, menu->mode.height);
	    menu->mode.width = mode->width;
	    menu->mode.height = mode->height;
	    mode = VID_BestModeMatch(&menu->mode, mode);
	    menu->mode = *mode;
	    break;
	case VID_MENU_CURSOR_BPP:
	    mode = VID_FindNextBpp(&menu->mode);
	    menu->mode.bpp = mode->bpp;
	    menu->mode.refresh = mode->refresh;
	    break;
	case VID_MENU_CURSOR_REFRESH:
	    if (menu->fullscreen) {
		mode = VID_FindNextRefresh(&menu->mode);
		menu->mode.refresh = mode->refresh;
	    }
	    break;
	case VID_MENU_CURSOR_FULLSCREEN:
	    menu->fullscreen = !menu->fullscreen;
	    break;
	default:
	    break;
	}
	break;
    case K_ENTER:
	S_LocalSound("misc/menu1.wav");
	switch (menu->cursor) {
	case VID_MENU_CURSOR_APPLY:
	    /* If it's a windowed mode, update the modelist entry */
	    if (!menu->fullscreen) {
		modelist[0] = menu->mode;
		VID_SetMode(&modelist[0], host_basepal);
		break;
	    }
	    /* If fullscreen, give the existing mode from modelist array */
	    for (mode = &modelist[1]; mode - modelist < nummodes; mode++) {
		if (mode->width != menu->mode.width)
		    continue;
		if (mode->height != menu->mode.height)
		    continue;
		if (mode->bpp != menu->mode.bpp)
		    continue;
		if (mode->refresh != menu->mode.refresh)
		    continue;
		VID_SetMode(mode, host_basepal);
		break;
	    }
	    break;
	case VID_MENU_CURSOR_FULLSCREEN:
	    menu->fullscreen = !menu->fullscreen;
	    break;
	default:
	    break;
	}
    default:
	break;
    }
}

void
VID_MenuKey(knum_t keynum)
{
    VID_MenuKey_(&vid_menustate, keynum);
}

/*
=================
VID_GetModeDescription

Tacks on "windowed" or "fullscreen"
=================
*/
static const char *
VID_GetModeDescription(const qvidmode_t *mode)
{
    static char pinfo[40];

    if (mode != modelist)
	snprintf(pinfo, sizeof(pinfo), "%4d x %4d x %2d @ %3dHz",
		 mode->width, mode->height, mode->bpp, mode->refresh);
    else
	snprintf(pinfo, sizeof(pinfo), "%4d x %4d windowed",
		 mode->width, mode->height);

    return pinfo;
}

/*
=================
VID_DescribeModes_f
=================
*/
void
VID_DescribeModes_f(void)
{
    int i;
    const char *pinfo;
    qboolean na;
    const qvidmode_t *mode;

    na = false;

    for (i = 0; i < nummodes; i++) {
	mode = &modelist[i];
	pinfo = VID_GetModeDescription(mode);
	if (VID_CheckAdequateMem(mode->width, mode->height)) {
	    Con_Printf("%2d: %s\n", i, pinfo);
	} else {
	    Con_Printf("**: %s\n", pinfo);
	    na = true;
	}
    }

    if (na) {
	Con_Printf("\n[**: not enough system RAM for mode]\n");
    }
}

/*
=================
VID_DescribeCurrentMode_f
=================
*/
void
VID_DescribeCurrentMode_f(void)
{
    Con_Printf("%s\n", VID_GetModeDescription(&modelist[vid_modenum]));
}


/*
=================
VID_NumModes_f
=================
*/
void
VID_NumModes_f(void)
{
    if (nummodes == 1)
	Con_Printf("%d video mode is available\n", nummodes);
    else
	Con_Printf("%d video modes are available\n", nummodes);
}


/*
=================
VID_DescribeMode_f
=================
*/
void
VID_DescribeMode_f(void)
{
    int modenum;

    if (Cmd_Argc() == 2) {
	modenum = Q_atoi(Cmd_Argv(1));
	if (modenum >= 0 && modenum < nummodes) {
	    Con_Printf("%s\n", VID_GetModeDescription(&modelist[modenum]));
	    return;
	}
	Con_Printf("Invalid video mode (%d)\n", modenum);
    }
    Con_Printf("vid_describemode <modenum>\n"
	       "  Print a descrition of the specified mode number.\n");
}

void
VID_InitModeCvars(void)
{
    Cvar_RegisterVariable(&vid_fullscreen);
    Cvar_RegisterVariable(&vid_width);
    Cvar_RegisterVariable(&vid_height);
    Cvar_RegisterVariable(&vid_bpp);
    Cvar_RegisterVariable(&vid_refreshrate);
}

/*
 * Check command line paramters to see if any special mode properties
 * were requested. Try to find a matching mode from the modelist.
 */
const qvidmode_t *
VID_GetCmdlineMode(void)
{
    int width, height, bpp, fullscreen, windowed;
    qvidmode_t *mode, *next;

    width = COM_CheckParm("-width");
    if (width)
	width = (com_argc > width + 1) ? atoi(com_argv[width + 1]) : 0;

    height = COM_CheckParm("-height");
    if (height)
	height = (com_argc > height + 1) ? atoi(com_argv[height + 1]) : 0;
    bpp = COM_CheckParm("-bpp");
    if (bpp)
	bpp = (com_argc > bpp + 1) ? atoi(com_argv[bpp + 1]) : 0;
    fullscreen = COM_CheckParm("-fullscreen");
    windowed = COM_CheckParm("-window") || COM_CheckParm("-w");

    /* If nothing was specified, don't return a mode */
    if (!width && !height && !bpp && !fullscreen && !windowed)
	return NULL;

    /* Default to fullscreen mode unless windowed requested */
    fullscreen = fullscreen || !windowed;

    /* FIXME - default to desktop resolution? */
    if (!width && !height) {
	width = modelist[0].width;
	height = modelist[0].height;
    } else if (!width) {
	width = height * 4 / 3;
    } else if (!height) {
	height = width * 3 / 4;
    }
    if (!bpp)
	bpp = modelist[0].bpp;

    /* If windowed mode was requested, set up and return mode 0 */
    if (!fullscreen) {
	mode = modelist;
	mode->width = width;
	mode->height = height;
	mode->bpp = bpp;

	return mode;
    }

    /* Search for a matching mode */
    for (mode = &modelist[1]; mode - modelist < nummodes; mode++) {
	if (mode->width != width || mode->height != height)
	    continue;
	if (mode->bpp != bpp)
	    continue;
	break;
    }
    if (mode - modelist == nummodes)
	Sys_Error("Requested video mode (%dx%dx%d) not available.",
		  width, height, bpp);

    /* Return the highest refresh rate at this width/height/bpp */
    for (next = mode + 1; next - modelist < nummodes; mode = next++) {
	if (next->width != width || next->height != height)
	    break;
	if (next->bpp != bpp)
	    break;
    }

    return mode;
}
