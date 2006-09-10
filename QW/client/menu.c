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

#include "client.h"
#include "cmd.h"
#include "console.h"
#include "draw.h"
#include "input.h"
#include "keys.h"
#include "menu.h"
#include "quakedef.h"
#include "render.h"
#include "screen.h"
#include "sys.h"
#include "vid.h"
#include "view.h"

void (*vid_menudrawfn) (void);
void (*vid_menukeyfn) (int key);

enum {
    m_none, m_main, m_options, m_video, m_keys, m_quit
} m_state;

void M_Menu_Options_f(void);
void M_Menu_Quit_f(void);

static void M_Menu_Main_f(void);
static void M_Menu_Keys_f(void);
static void M_Menu_Video_f(void);

static void M_Main_Draw(void);
static void M_Options_Draw(void);
static void M_Keys_Draw(void);
static void M_Video_Draw(void);
static void M_Quit_Draw(void);

static void M_Main_Key(int key);
static void M_Options_Key(int key);
static void M_Keys_Key(int key);
static void M_Video_Key(int key);
static void M_Quit_Key(int key);

static qboolean m_recursiveDraw;
static qboolean m_entersound;	// play after drawing a frame, so caching
				// won't disrupt the sound

//=============================================================================
/* Support Routines */

/*
================
M_DrawCharacter

Draws one solid graphics character
================
*/
void
M_DrawCharacter(int cx, int line, int num)
{
    Draw_Character(cx + ((vid.width - 320) >> 1), line, num);
}

void
M_Print(int cx, int cy, const char *str)
{
    while (*str) {
	M_DrawCharacter(cx, cy, (*str) + 128);
	str++;
	cx += 8;
    }
}

void
M_PrintWhite(int cx, int cy, const char *str)
{
    while (*str) {
	M_DrawCharacter(cx, cy, *str);
	str++;
	cx += 8;
    }
}

static void
M_DrawTransPic(int x, int y, const qpic_t *pic)
{
    Draw_TransPic(x + ((vid.width - 320) >> 1), y, pic);
}

void
M_DrawPic(int x, int y, const qpic_t *pic)
{
    Draw_Pic(x + ((vid.width - 320) >> 1), y, pic);
}

void
M_DrawTextBox(int x, int y, int width, int lines)
{
    qpic_t *p;
    int cx, cy;
    int n;

    // draw left side
    cx = x;
    cy = y;
    p = Draw_CachePic("gfx/box_tl.lmp");
    M_DrawTransPic(cx, cy, p);
    p = Draw_CachePic("gfx/box_ml.lmp");
    for (n = 0; n < lines; n++) {
	cy += 8;
	M_DrawTransPic(cx, cy, p);
    }
    p = Draw_CachePic("gfx/box_bl.lmp");
    M_DrawTransPic(cx, cy + 8, p);

    // draw middle
    cx += 8;
    while (width > 0) {
	cy = y;
	p = Draw_CachePic("gfx/box_tm.lmp");
	M_DrawTransPic(cx, cy, p);
	p = Draw_CachePic("gfx/box_mm.lmp");
	for (n = 0; n < lines; n++) {
	    cy += 8;
	    if (n == 1)
		p = Draw_CachePic("gfx/box_mm2.lmp");
	    M_DrawTransPic(cx, cy, p);
	}
	p = Draw_CachePic("gfx/box_bm.lmp");
	M_DrawTransPic(cx, cy + 8, p);
	width -= 2;
	cx += 16;
    }

    // draw right side
    cy = y;
    p = Draw_CachePic("gfx/box_tr.lmp");
    M_DrawTransPic(cx, cy, p);
    p = Draw_CachePic("gfx/box_mr.lmp");
    for (n = 0; n < lines; n++) {
	cy += 8;
	M_DrawTransPic(cx, cy, p);
    }
    p = Draw_CachePic("gfx/box_br.lmp");
    M_DrawTransPic(cx, cy + 8, p);
}

//=============================================================================

static int m_save_demonum;

/*
================
M_ToggleMenu_f
================
*/
void
M_ToggleMenu_f(void)
{
    m_entersound = true;

    if (key_dest == key_menu) {
	if (m_state != m_main) {
	    M_Menu_Main_f();
	    return;
	}
	key_dest = key_game;
	m_state = m_none;
	return;
    }
    if (key_dest == key_console) {
	Con_ToggleConsole_f();
    } else {
	M_Menu_Main_f();
    }
}


//=============================================================================
/* MAIN MENU */

static int m_main_cursor;

#define MAIN_ITEMS 3

static void
M_Menu_Main_f(void)
{
    if (key_dest != key_menu) {
	m_save_demonum = cls.demonum;
	cls.demonum = -1;
    }
    key_dest = key_menu;
    m_state = m_main;
    m_entersound = true;
}


static void
M_Main_Draw(void)
{
    int f;
    qpic_t *p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/ttl_main.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    M_DrawTransPic(72, 32, Draw_CachePic("gfx/mainmenu.lmp"));

    f = (int)(realtime * 10) % 6;

    M_DrawTransPic(54, 32 + 40 + m_main_cursor * 20,
		   Draw_CachePic(va("gfx/menudot%i.lmp", f + 1)));
}


static void
M_Main_Key(int key)
{
    switch (key) {
    case K_ESCAPE:
	key_dest = key_game;
	m_state = m_none;
	cls.demonum = m_save_demonum;
	if (cls.demonum != -1 && !cls.demoplayback
	    && cls.state == ca_disconnected)
	    CL_NextDemo();
	break;

    case K_DOWNARROW:
    case K_UPARROW:
	S_LocalSound("misc/menu1.wav");
	m_main_cursor = m_main_cursor ? 0 : 2;
	break;

    case K_ENTER:
	m_entersound = true;

	switch (m_main_cursor) {
	case 0:
	    M_Menu_Options_f();
	    break;

	case 2:
	    M_Menu_Quit_f();
	    break;
	}
    }
}


//=============================================================================
/* OPTIONS MENU */

#define	OPTIONS_ITEMS	16
#define	SLIDER_RANGE	10

static int options_cursor;

void
M_Menu_Options_f(void)
{
    key_dest = key_menu;
    m_state = m_options;
    m_entersound = true;
}


static void
M_AdjustSliders(int dir)
{
    S_LocalSound("misc/menu3.wav");

    switch (options_cursor) {
    case 3:			// screen size
	scr_viewsize.value += dir * 10;
	if (scr_viewsize.value < 30)
	    scr_viewsize.value = 30;
	if (scr_viewsize.value > 120)
	    scr_viewsize.value = 120;
	Cvar_SetValue("viewsize", scr_viewsize.value);
	break;
    case 4:			// gamma
	v_gamma.value -= dir * 0.05;
	if (v_gamma.value < 0.5)
	    v_gamma.value = 0.5;
	if (v_gamma.value > 1)
	    v_gamma.value = 1;
	Cvar_SetValue("gamma", v_gamma.value);
	break;
    case 5:			// mouse speed
	sensitivity.value += dir * 0.5;
	if (sensitivity.value < 1)
	    sensitivity.value = 1;
	if (sensitivity.value > 11)
	    sensitivity.value = 11;
	Cvar_SetValue("sensitivity", sensitivity.value);
	break;
    case 6:			// music volume
#ifdef _WIN32
	bgmvolume.value += dir * 1.0;
#else
	bgmvolume.value += dir * 0.1;
#endif
	if (bgmvolume.value < 0)
	    bgmvolume.value = 0;
	if (bgmvolume.value > 1)
	    bgmvolume.value = 1;
	Cvar_SetValue("bgmvolume", bgmvolume.value);
	break;
    case 7:			// sfx volume
	volume.value += dir * 0.1;
	if (volume.value < 0)
	    volume.value = 0;
	if (volume.value > 1)
	    volume.value = 1;
	Cvar_SetValue("volume", volume.value);
	break;

    case 8:			// allways run
	if (cl_forwardspeed.value > 200) {
	    Cvar_SetValue("cl_forwardspeed", 200);
	    Cvar_SetValue("cl_backspeed", 200);
	} else {
	    Cvar_SetValue("cl_forwardspeed", 400);
	    Cvar_SetValue("cl_backspeed", 400);
	}
	break;

    case 9:			// invert mouse
	Cvar_SetValue("m_pitch", -m_pitch.value);
	break;

    case 10:			// lookspring
	Cvar_SetValue("lookspring", !lookspring.value);
	break;

    case 11:			// lookstrafe
	Cvar_SetValue("lookstrafe", !lookstrafe.value);
	break;

    case 12:
	Cvar_SetValue("cl_sbar", !cl_sbar.value);
	break;

    case 13:
	Cvar_SetValue("cl_hudswap", !cl_hudswap.value);

    case 15:			// _windowed_mouse
	Cvar_SetValue("_windowed_mouse", !_windowed_mouse.value);
	break;
    }
}


static void
M_DrawSlider(int x, int y, float range)
{
    int i;

    if (range < 0)
	range = 0;
    if (range > 1)
	range = 1;
    M_DrawCharacter(x - 8, y, 128);
    for (i = 0; i < SLIDER_RANGE; i++)
	M_DrawCharacter(x + i * 8, y, 129);
    M_DrawCharacter(x + i * 8, y, 130);
    M_DrawCharacter(x + (SLIDER_RANGE - 1) * 8 * range, y, 131);
}

static void
M_DrawCheckbox(int x, int y, int on)
{
    if (on)
	M_Print(x, y, "on");
    else
	M_Print(x, y, "off");
}

static void
M_Options_Draw(void)
{
    float r;
    qpic_t *p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_option.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    M_Print(16, 32, "    Customize controls");
    M_Print(16, 40, "         Go to console");
    M_Print(16, 48, "     Reset to defaults");

    M_Print(16, 56, "           Screen size");
    r = (scr_viewsize.value - 30) / (120 - 30);
    M_DrawSlider(220, 56, r);

    M_Print(16, 64, "            Brightness");
    r = (1.0 - v_gamma.value) / 0.5;
    M_DrawSlider(220, 64, r);

    M_Print(16, 72, "           Mouse Speed");
    r = (sensitivity.value - 1) / 10;
    M_DrawSlider(220, 72, r);

    M_Print(16, 80, "       CD Music Volume");
    r = bgmvolume.value;
    M_DrawSlider(220, 80, r);

    M_Print(16, 88, "          Sound Volume");
    r = volume.value;
    M_DrawSlider(220, 88, r);

    M_Print(16, 96, "            Always Run");
    M_DrawCheckbox(220, 96, cl_forwardspeed.value > 200);

    M_Print(16, 104, "          Invert Mouse");
    M_DrawCheckbox(220, 104, m_pitch.value < 0);

    M_Print(16, 112, "            Lookspring");
    M_DrawCheckbox(220, 112, lookspring.value);

    M_Print(16, 120, "            Lookstrafe");
    M_DrawCheckbox(220, 120, lookstrafe.value);

    M_Print(16, 128, "    Use old status bar");
    M_DrawCheckbox(220, 128, cl_sbar.value);

    M_Print(16, 136, "      HUD on left side");
    M_DrawCheckbox(220, 136, cl_hudswap.value);

    if (vid_menudrawfn)
	M_Print(16, 144, "         Video Options");

    if (!VID_IsFullScreen()) {
	M_Print(16, 152, "             Use Mouse");
	M_DrawCheckbox(220, 152, _windowed_mouse.value);
    }

// cursor
    M_DrawCharacter(200, 32 + options_cursor * 8,
		    12 + ((int)(realtime * 4) & 1));
}


static void
M_Options_Key(int k)
{
    switch (k) {
    case K_ESCAPE:
	M_Menu_Main_f();
	break;

    case K_ENTER:
	m_entersound = true;
	switch (options_cursor) {
	case 0:
	    M_Menu_Keys_f();
	    break;
	case 1:
	    m_state = m_none;
	    Con_ToggleConsole_f();
	    break;
	case 2:
	    Cbuf_AddText("exec default.cfg\n");
	    break;
	case 14:
	    M_Menu_Video_f();
	    break;
	default:
	    M_AdjustSliders(1);
	    break;
	}
	return;

    case K_UPARROW:
	S_LocalSound("misc/menu1.wav");
	options_cursor--;
	if (options_cursor < 0)
	    options_cursor = OPTIONS_ITEMS - 1;
	break;

    case K_DOWNARROW:
	S_LocalSound("misc/menu1.wav");
	options_cursor++;
	options_cursor %= OPTIONS_ITEMS;
	break;

    case K_LEFTARROW:
	M_AdjustSliders(-1);
	break;

    case K_RIGHTARROW:
	M_AdjustSliders(1);
	break;
    }

    if (options_cursor == 14 && !vid_menudrawfn) {
	if (k == K_UPARROW)
	    options_cursor--;
	else {
	    options_cursor++;
	    options_cursor %= OPTIONS_ITEMS;
	}
    }

    if (options_cursor == 15 && VID_IsFullScreen()) {
	if (k == K_UPARROW) {
	    options_cursor--;
	    if (!vid_menudrawfn)
		options_cursor--;
	} else {
	    options_cursor++;
	    options_cursor %= OPTIONS_ITEMS;
	}
    }
}


//=============================================================================
/* KEYS MENU */

static const char *const bindnames[][2] = {
    {"+attack", "attack"},
    {"impulse 10", "next weapon"},
    {"impulse 12", "prev_weapon"},
    {"+jump", "jump / swim up"},
    {"+forward", "walk forward"},
    {"+back", "backpedal"},
    {"+left", "turn left"},
    {"+right", "turn right"},
    {"+speed", "run"},
    {"+moveleft", "step left"},
    {"+moveright", "step right"},
    {"+strafe", "sidestep"},
    {"+lookup", "look up"},
    {"+lookdown", "look down"},
    {"centerview", "center view"},
    {"+mlook", "mouse look"},
    {"+klook", "keyboard look"},
    {"+moveup", "swim up"},
    {"+movedown", "swim down"}
};

#define	NUMCOMMANDS (sizeof(bindnames)/sizeof(bindnames[0]))

static int keys_cursor;
static int bind_grab;

static void
M_Menu_Keys_f(void)
{
    key_dest = key_menu;
    m_state = m_keys;
    m_entersound = true;
}


static void
M_FindKeysForCommand(const char *command, int *twokeys)
{
    int count;
    int j;
    int l;
    char *b;

    twokeys[0] = twokeys[1] = -1;
    l = strlen(command);
    count = 0;

    for (j = 0; j < 256; j++) {
	b = keybindings[j];
	if (!b)
	    continue;
	if (!strncmp(b, command, l)) {
	    twokeys[count] = j;
	    count++;
	    if (count == 2)
		break;
	}
    }
}

static void
M_UnbindCommand(const char *const command)
{
    int j;
    int l;
    char *b;

    l = strlen(command);

    for (j = 0; j < 256; j++) {
	b = keybindings[j];
	if (!b)
	    continue;
	if (!strncmp(b, command, l))
	    Key_SetBinding(j, NULL);
    }
}


static void
M_Keys_Draw(void)
{
    int i, l;
    int keys[2];
    char *name;
    int x, y;
    qpic_t *p;

    p = Draw_CachePic("gfx/ttl_cstm.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    if (bind_grab)
	M_Print(12, 32, "Press a key or button for this action");
    else
	M_Print(18, 32, "Enter to change, backspace to clear");

// search for known bindings
    for (i = 0; i < NUMCOMMANDS; i++) {
	y = 48 + 8 * i;

	M_Print(16, y, bindnames[i][1]);

	l = strlen(bindnames[i][0]);

	M_FindKeysForCommand(bindnames[i][0], keys);

	if (keys[0] == -1) {
	    M_Print(140, y, "???");
	} else {
	    name = Key_KeynumToString(keys[0]);
	    M_Print(140, y, name);
	    x = strlen(name) * 8;
	    if (keys[1] != -1) {
		M_Print(140 + x + 8, y, "or");
		M_Print(140 + x + 32, y, Key_KeynumToString(keys[1]));
	    }
	}
    }

    if (bind_grab)
	M_DrawCharacter(130, 48 + keys_cursor * 8, '=');
    else
	M_DrawCharacter(130, 48 + keys_cursor * 8,
			12 + ((int)(realtime * 4) & 1));
}


static void
M_Keys_Key(int k)
{
    char cmd[80];
    int keys[2];

    if (bind_grab) {		// defining a key
	S_LocalSound("misc/menu1.wav");
	if (k == K_ESCAPE) {
	    bind_grab = false;
	} else if (k != '`') {
	    sprintf(cmd, "bind \"%s\" \"%s\"\n", Key_KeynumToString(k),
		    bindnames[keys_cursor][0]);
	    Cbuf_InsertText(cmd);
	}

	bind_grab = false;
	return;
    }

    switch (k) {
    case K_ESCAPE:
	M_Menu_Options_f();
	break;

    case K_LEFTARROW:
    case K_UPARROW:
	S_LocalSound("misc/menu1.wav");
	keys_cursor--;
	if (keys_cursor < 0)
	    keys_cursor = NUMCOMMANDS - 1;
	break;

    case K_DOWNARROW:
    case K_RIGHTARROW:
	S_LocalSound("misc/menu1.wav");
	keys_cursor++;
	if (keys_cursor >= NUMCOMMANDS)
	    keys_cursor = 0;
	break;

    case K_ENTER:		// go into bind mode
	M_FindKeysForCommand(bindnames[keys_cursor][0], keys);
	S_LocalSound("misc/menu2.wav");
	if (keys[1] != -1)
	    M_UnbindCommand(bindnames[keys_cursor][0]);
	bind_grab = true;
	break;

    case K_BACKSPACE:		// delete bindings
    case K_DEL:		// delete bindings
	S_LocalSound("misc/menu2.wav");
	M_UnbindCommand(bindnames[keys_cursor][0]);
	break;
    }
}

//=============================================================================
/* VIDEO MENU */

static void
M_Menu_Video_f(void)
{
    key_dest = key_menu;
    m_state = m_video;
    m_entersound = true;
}


static void
M_Video_Draw(void)
{
    (*vid_menudrawfn) ();
}


static void
M_Video_Key(int key)
{
    (*vid_menukeyfn) (key);
}

//=============================================================================
/* QUIT MENU */

static int msgNumber;
static int m_quit_prevstate;
static qboolean wasInMenus;

static const char *const quitMessage[] = {
    "  Are you gonna quit    ",
    "  this game just like   ",
    "   everything else?     ",
    "                        ",

    " Milord, methinks that  ",
    "   thou art a lowly     ",
    " quitter. Is this true? ",
    "                        ",

    " Do I need to bust your ",
    "  face open for trying  ",
    "        to quit?        ",
    "                        ",

    " Man, I oughta smack you",
    "   for trying to quit!  ",
    "     Press Y to get     ",
    "      smacked out.      ",

    " Press Y to quit like a ",
    "   big loser in life.   ",
    "  Press N to stay proud ",
    "    and successful!     ",

    "   If you press Y to    ",
    "  quit, I will summon   ",
    "  Satan all over your   ",
    "      hard drive!       ",

    "  Um, Asmodeus dislikes ",
    " his children trying to ",
    " quit. Press Y to return",
    "   to your Tinkertoys.  ",

    "  If you quit now, I'll ",
    "  throw a blanket-party ",
    "   for you next time!   ",
    "                        "
};


void
M_Menu_Quit_f(void)
{
    if (m_state == m_quit)
	return;
    wasInMenus = (key_dest == key_menu);
    key_dest = key_menu;
    m_quit_prevstate = m_state;
    m_state = m_quit;
    m_entersound = true;
    msgNumber = rand() & 7;
}


static void
M_Quit_Key(int key)
{
    switch (key) {
    case K_ESCAPE:
    case 'n':
    case 'N':
	if (wasInMenus) {
	    m_state = m_quit_prevstate;
	    m_entersound = true;
	} else {
	    key_dest = key_game;
	    m_state = m_none;
	}
	break;

    case 'Y':
    case 'y':
	key_dest = key_console;
	CL_Disconnect();
	Sys_Quit();
	break;

    default:
	break;
    }

}


static void
M_Quit_Draw(void)
{
    if (wasInMenus) {
	m_state = m_quit_prevstate;
	m_recursiveDraw = true;
	M_Draw();
	m_state = m_quit;
    }

    M_DrawTextBox(56, 76, 24, 4);
    M_Print(64, 84, quitMessage[msgNumber * 4 + 0]);
    M_Print(64, 92, quitMessage[msgNumber * 4 + 1]);
    M_Print(64, 100, quitMessage[msgNumber * 4 + 2]);
    M_Print(64, 108, quitMessage[msgNumber * 4 + 3]);
}


//=============================================================================
/* Menu Subsystem */


void
M_Init(void)
{
    Cmd_AddCommand("togglemenu", M_ToggleMenu_f);
    Cmd_AddCommand("menu_main", M_Menu_Main_f);
    Cmd_AddCommand("menu_options", M_Menu_Options_f);
    Cmd_AddCommand("menu_keys", M_Menu_Keys_f);
    Cmd_AddCommand("menu_video", M_Menu_Video_f);
    Cmd_AddCommand("menu_quit", M_Menu_Quit_f);
}


void
M_Draw(void)
{
    if (m_state == m_none || key_dest != key_menu)
	return;

    if (!m_recursiveDraw) {
	scr_copyeverything = 1;

	if (scr_con_current) {
	    Draw_ConsoleBackground(vid.height);
	    VID_UnlockBuffer();
	    S_ExtraUpdate();
	    VID_LockBuffer();
	} else
	    Draw_FadeScreen();

	scr_fullupdate = 0;
    } else {
	m_recursiveDraw = false;
    }

    switch (m_state) {
    case m_main:
	M_Main_Draw();
	break;

    case m_options:
	M_Options_Draw();
	break;

    case m_keys:
	M_Keys_Draw();
	break;

    case m_video:
	M_Video_Draw();
	break;

    case m_quit:
	M_Quit_Draw();
	break;

    case m_none:
	break;
    }

    if (m_entersound) {
	S_LocalSound("misc/menu2.wav");
	m_entersound = false;
    }

    VID_UnlockBuffer();
    S_ExtraUpdate();
    VID_LockBuffer();
}


void
M_Keydown(int key)
{
    switch (m_state) {
    case m_none:
	return;

    case m_main:
	M_Main_Key(key);
	return;

    case m_options:
	M_Options_Key(key);
	return;

    case m_keys:
	M_Keys_Key(key);
	return;

    case m_video:
	M_Video_Key(key);
	return;

    case m_quit:
	M_Quit_Key(key);
	return;
    }
}
