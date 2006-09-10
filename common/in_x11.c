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

#include "common.h"
#include "console.h"
#include "in_x11.h"
#include "x11_core.h"
#include "vid.h"
#include "sys.h"

#include <X11/extensions/xf86dga.h>

// FIXME - make static when possible
qboolean mouse_available = false;	// Mouse available for use

static qboolean keyboard_grab_active = false;
qboolean mouse_grab_active = false;
static qboolean dga_available = false;
qboolean dga_mouse_active = false;

int mouse_x, mouse_y;

//static int old_mouse_x, old_mouse_y;

static void
windowed_mouse_f(struct cvar_s *var)
{
    if (var->value) {
	Con_DPrintf("Callback: _windowed_mouse ON\n");
	if (!VID_IsFullScreen()) {
	    IN_GrabMouse();
	    IN_GrabKeyboard();
	}
    } else {
	Con_DPrintf("Callback: _windowed_mouse OFF\n");
	if (!VID_IsFullScreen()) {
	    IN_UngrabMouse();
	    IN_UngrabKeyboard();
	}
    }
}


static void
IN_ActivateDGAMouse(void)
{
    if (dga_available && !dga_mouse_active) {
	XF86DGADirectVideo(x_disp, DefaultScreen(x_disp), XF86DGADirectMouse);
	dga_mouse_active = true;
    }
}

static void
IN_DeactivateDGAMouse(void)
{
    if (dga_available && dga_mouse_active) {
	XF86DGADirectVideo(x_disp, DefaultScreen(x_disp), 0);
	dga_mouse_active = false;
	IN_CenterMouse();	// maybe set mouse_x = 0 and mouse_y = 0?
    }
}

static void
in_dgamouse_f(struct cvar_s *var)
{
    if (var->value) {
	Con_DPrintf("Callback: in_dgamouse ON\n");
	IN_ActivateDGAMouse();
    } else {
	Con_DPrintf("Callback: in_dgamouse OFF\n");
	IN_DeactivateDGAMouse();
    }
}

cvar_t in_mouse = { "in_mouse", "1", false };
cvar_t in_dgamouse = { "in_dgamouse", "1", false, false, 0,
    in_dgamouse_f
};
cvar_t _windowed_mouse = { "_windowed_mouse", "0", true, false, 0,
    windowed_mouse_f
};
cvar_t m_filter = { "m_filter", "0" };

static Cursor
CreateNullCursor(void)
{
    Pixmap cursormask;
    XGCValues xgc;
    GC gc;
    XColor dummycolour;
    Cursor cursor;

    cursormask = XCreatePixmap(x_disp, x_win, 1, 1, 1 /*depth */ );
    xgc.function = GXclear;
    gc = XCreateGC(x_disp, cursormask, GCFunction, &xgc);
    XFillRectangle(x_disp, cursormask, gc, 0, 0, 1, 1);
    dummycolour.pixel = 0;
    dummycolour.flags = 0;	// ~(DoRed | DoGreen | DoBlue)
    cursor = XCreatePixmapCursor(x_disp, cursormask, cursormask,
				 &dummycolour, &dummycolour, 0, 0);
    XFreePixmap(x_disp, cursormask);
    XFreeGC(x_disp, gc);
    return cursor;
}

void
IN_CenterMouse(void)
{
    // FIXME - work with the current mask...
    // FIXME - check active mouse, etc.
    XSelectInput(x_disp, x_win, (X_CORE_MASK | X_KEY_MASK | X_MOUSE_MASK)
		 & ~PointerMotionMask);
    XWarpPointer(x_disp, None, x_win, 0, 0, 0, 0,
		 vid.width / 2, vid.height / 2);
    XSelectInput(x_disp, x_win, X_CORE_MASK | X_KEY_MASK | X_MOUSE_MASK);
}

void
IN_GrabMouse(void)
{
    int err;

    if (mouse_available && !mouse_grab_active) {
	XDefineCursor(x_disp, x_win, CreateNullCursor());

	err = XGrabPointer(x_disp, x_win, True, 0, GrabModeAsync,
			   GrabModeAsync, x_win, None, CurrentTime);
	if (err) {
	    if (err == GrabNotViewable)
		Sys_Error("%s: GrabNotViewable", __func__);
	    if (err == AlreadyGrabbed)
		Sys_Error("%s: AlreadyGrabbed", __func__);
	    if (err == GrabFrozen)
		Sys_Error("%s: GrabFrozen", __func__);
	    if (err == GrabInvalidTime)
		Sys_Error("%s: GrabInvalidTime", __func__);
	}
	mouse_grab_active = true;

	// FIXME - need those cvar callbacks to fix changed values...
	if (dga_available) {
	    if (in_dgamouse.value)
		IN_ActivateDGAMouse();
	} else {
	    in_dgamouse.value = 0;
	}
    } else {
	Sys_Error("Bad grab?");
    }
}

void
IN_UngrabMouse(void)
{
    if (mouse_grab_active) {
	XUngrabPointer(x_disp, CurrentTime);
	XUndefineCursor(x_disp, x_win);
	mouse_grab_active = false;
    }

    if (dga_mouse_active) {
	IN_DeactivateDGAMouse();
    }
}

void
IN_GrabKeyboard(void)
{
    if (!keyboard_grab_active) {
	int err;

	err = XGrabKeyboard(x_disp, x_win, False,
			    GrabModeAsync, GrabModeAsync, CurrentTime);
	if (err)
	    Sys_Error("Couldn't grab keyboard!");

	keyboard_grab_active = true;
    }
}

void
IN_UngrabKeyboard(void)
{
    if (keyboard_grab_active) {
	XUngrabKeyboard(x_disp, CurrentTime);
	keyboard_grab_active = false;
    }
}

static void
IN_InitCvars(void)
{
    Cvar_RegisterVariable(&in_mouse);
    Cvar_RegisterVariable(&in_dgamouse);
    Cvar_RegisterVariable(&m_filter);
    Cvar_RegisterVariable(&_windowed_mouse);
}

void
IN_Init(void)
{
    int MajorVersion, MinorVersion;

    keyboard_grab_active = false;
    mouse_grab_active = false;
    dga_mouse_active = false;

    // FIXME - do proper detection?
    //       - Also, look at other vid_*.c files for clues
    mouse_available = (COM_CheckParm("-nomouse")) ? false : true;

    if (x_disp == NULL)
	Sys_Error("x_disp not initialised before input...");

    if (!XF86DGAQueryVersion(x_disp, &MajorVersion, &MinorVersion)) {
	Con_Printf("Failed to detect XF86DGA Mouse\n");
	in_dgamouse.value = 0;
	dga_available = false;
    } else {
	dga_available = true;
    }

    // Need to grab the input focus at startup, just in case...
    // FIXME - must be viewable or get BadMatch
    XSetInputFocus(x_disp, x_win, RevertToParent, CurrentTime);

    IN_InitCvars();

    if (VID_IsFullScreen()) {
	if (!mouse_grab_active)
	    IN_GrabMouse();
	if (!keyboard_grab_active)
	    IN_GrabKeyboard();
    }
}

void
IN_Shutdown(void)
{
    IN_UngrabMouse();
    IN_UngrabKeyboard();
    mouse_available = 0;
}

#if 0
void
IN_MouseMove (usercmd_t *cmd)
{
    if (!mouse_available)
	return;

    if (m_filter.value) {
	mouse_x = (mouse_x + old_mouse_x) * 0.5;
	mouse_y = (mouse_y + old_mouse_y) * 0.5;
    }
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;

    mouse_x *= sensitivity.value;
    mouse_y *= sensitivity.value;

    /* add mouse X/Y movement to cmd */
    if ((in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1)))
	cmd->sidemove += m_side.value * mouse_x;
    else
	cl.viewangles[YAW] -= m_yaw.value * mouse_x;

    if (in_mlook.state & 1)
	V_StopPitchDrift ();

    if ((in_mlook.state & 1) && !(in_strafe.state & 1)) {
	cl.viewangles[PITCH] += m_pitch.value * mouse_y;
	if (cl.viewangles[PITCH] > 80)
	    cl.viewangles[PITCH] = 80;
	if (cl.viewangles[PITCH] < -70)
	    cl.viewangles[PITCH] = -70;
    } else {
	if ((in_strafe.state & 1) && noclip_anglehack)
	    cmd->upmove -= m_forward.value * mouse_y;
	else
	    cmd->forwardmove -= m_forward.value * mouse_y;
    }
    mouse_x = mouse_y = 0;
}
#endif
