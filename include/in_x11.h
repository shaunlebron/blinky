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

#ifndef IN_X11_H
#define IN_X11_H

#include "qtypes.h"
#include "cvar.h"

#define X_KEY_MASK (KeyPressMask | KeyReleaseMask)
#define X_MOUSE_MASK (ButtonPressMask | ButtonReleaseMask | PointerMotionMask)

extern qboolean mouse_available;	// Mouse available for use

extern qboolean mouse_grab_active;
extern qboolean dga_mouse_active;

extern int mouse_x, mouse_y;

extern cvar_t in_mouse;
extern cvar_t in_dgamouse;
extern cvar_t _windowed_mouse;
extern cvar_t m_filter;

void IN_Init();
void IN_Shutdown();

void IN_CenterMouse();
void IN_GrabMouse();
void IN_UngrabMouse();
void IN_GrabKeyboard();
void IN_UngrabKeyboard();

#endif /* IN_X11_H */
