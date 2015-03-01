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

#include <string.h>

#include "console.h"
#include "cvar.h"
#include "glquake.h"
#include "qtypes.h"

qboolean gl_npotable;
cvar_t gl_npot = { "gl_npot", "1", false };

static qboolean
GL_ExtensionCheck(const char *extension)
{
    int length = strlen(extension);
    char *check;
    char buf[1024];

    if (!strncmp(gl_extensions, extension, length)) {
	char end = gl_extensions[length];
	if (!end || end == ' ')
	    return true;
    }

    snprintf(buf, sizeof(buf), " %s", extension);
    length++;
    while (1) {
	check = strstr(gl_extensions, buf);
	if (!check)
	    return false;
	if (!check[length] || check[length] == ' ')
	    return true;
	check += length;
    }
}

void
GL_ExtensionCheck_NPoT(void)
{
    gl_npotable = false;
    if (COM_CheckParm("-nonpot"))
	return;
    if (!GL_ExtensionCheck("GL_ARB_texture_non_power_of_two"))
	return;

    Con_DPrintf("Non-power-of-two textures available.\n");
    gl_npotable = true;
}
