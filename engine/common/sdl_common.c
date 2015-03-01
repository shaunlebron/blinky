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

#include "SDL.h"

#include "qtypes.h"
#include "sdl_common.h"
#include "sys.h"

SDL_Window *sdl_window = NULL;

void
Q_SDL_InitOnce(void)
{
    static qboolean init_done = false;

    if (init_done)
	return;

    if (SDL_Init(0) < 0)
	Sys_Error("SDL_Init(0) failed: %s", SDL_GetError());

    init_done = true;
}
