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
// console.c

#include <string.h>

#include "client.h"
#include "cmd.h"
#include "console.h"
#include "draw.h"
#include "keys.h"
#include "quakedef.h"
#include "screen.h"
#include "sys.h"
#include "zone.h"

#ifdef NQ_HACK
#include "host.h"
#include "sound.h"
#endif

#define CON_TEXTSIZE 16384
#define	NUM_CON_TIMES 4

console_t *con;			// point to current console
static console_t con_main;

int con_ormask = 0;
qboolean con_forcedup;
int con_totallines;		// total lines in console scrollback
int con_notifylines;		// scan lines to clear for notify lines

static int con_linewidth;	// characters across screen
static int con_vislines;

int
Con_GetWidth(void)
{
    return con_linewidth;
}

static float con_cursorspeed = 4;
static cvar_t con_notifytime = { "con_notifytime", "3" };	//seconds

static float con_times[NUM_CON_TIMES];	// realtime time the line was generated
					// for transparent notify lines

static qboolean debuglog;

qboolean con_initialized;

/*
====================
Con_ToggleConsole_f
====================
*/
void
Con_ToggleConsole_f(void)
{
    Key_ClearTyping();

    if (key_dest == key_console) {
	if (!con_forcedup) {
	    key_dest = key_game;
	    Key_ClearTyping();
	}
    } else
	key_dest = key_console;

    Con_ClearNotify();
}

/*
================
Con_Clear_f
================
*/
void
Con_Clear_f(void)
{
    memset(con_main.text, ' ', CON_TEXTSIZE);
}


/*
================
Con_ClearNotify
================
*/
void
Con_ClearNotify(void)
{
    int i;

    for (i = 0; i < NUM_CON_TIMES; i++)
	con_times[i] = 0;
}


/*
================
Con_MessageMode_f
================
*/
void
Con_MessageMode_f(void)
{
    key_dest = key_message;
    chat_team = false;
}

/*
================
Con_MessageMode2_f
================
*/
void
Con_MessageMode2_f(void)
{
    key_dest = key_message;
    chat_team = true;
}

/*
================
Con_Resize

================
*/
static void
Con_Resize(console_t * c)
{
    int i, j, width, oldwidth, oldtotallines, numlines, numchars;
    char tbuf[CON_TEXTSIZE];

    width = (vid.width >> 3) - 2;

    if (width == con_linewidth)
	return;

    if (width < 1)		// video hasn't been initialized yet
    {
	width = 38;
	con_linewidth = width;
	con_totallines = CON_TEXTSIZE / con_linewidth;
	memset(c->text, ' ', CON_TEXTSIZE);
    } else {
	oldwidth = con_linewidth;
	con_linewidth = width;
	oldtotallines = con_totallines;
	con_totallines = CON_TEXTSIZE / con_linewidth;
	numlines = oldtotallines;

	if (con_totallines < numlines)
	    numlines = con_totallines;

	numchars = oldwidth;

	if (con_linewidth < numchars)
	    numchars = con_linewidth;

	memcpy(tbuf, c->text, CON_TEXTSIZE);
	memset(c->text, ' ', CON_TEXTSIZE);

	for (i = 0; i < numlines; i++) {
	    for (j = 0; j < numchars; j++) {
		c->text[(con_totallines - 1 - i) * con_linewidth + j] =
		    tbuf[((c->current - i + oldtotallines) %
			  oldtotallines) * oldwidth + j];
	    }
	}
	Con_ClearNotify();
    }

    c->current = con_totallines - 1;
    c->display = c->current;
}

/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void
Con_CheckResize(void)
{
    Con_Resize(&con_main);
}

/*
===============
Con_Linefeed
===============
*/
void
Con_Linefeed(void)
{
    con->x = 0;
    if (con->display == con->current)
	con->display++;
    con->current++;
    memset(&con->text[(con->current % con_totallines) * con_linewidth]
	   , ' ', con_linewidth);
}

/*
================
Con_Print

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the notify window will pop up.
================
*/
void
Con_Print(const char *txt)
{
    int y;
    int c, l;
    static int cr;
    int mask;

    if (txt[0] == 1 || txt[0] == 2) {
	mask = 128;		// go to colored text
	txt++;
#ifdef NQ_HACK
	if (txt[0] == 1)
	    S_LocalSound("misc/talk.wav");	// play talk wav
#endif
    } else
	mask = 0;

    while ((c = *txt)) {
	// count word length
	for (l = 0; l < con_linewidth; l++)
	    if (txt[l] <= ' ')
		break;

	// word wrap
	if (l != con_linewidth && (con->x + l > con_linewidth))
	    con->x = 0;

	txt++;

	if (cr) {
	    con->current--;
	    cr = false;
	}


	if (!con->x) {
	    Con_Linefeed();
	    // mark time for transparent overlay
	    if (con->current >= 0)
		con_times[con->current % NUM_CON_TIMES] = realtime;
	}

	switch (c) {
	case '\n':
	    con->x = 0;
	    break;

	case '\r':
	    con->x = 0;
	    cr = 1;
	    break;

	default:		// display character and advance
	    y = con->current % con_totallines;
	    con->text[y * con_linewidth + con->x] = c | mask | con_ormask;
	    con->x++;
	    if (con->x >= con_linewidth)
		con->x = 0;
	    break;
	}

    }
}


/*
================
Con_Printf

Handles cursor positioning, line wrapping, etc
================
*/
void
Con_Printf(const char *fmt, ...)
{
    va_list argptr;
    char msg[MAX_PRINTMSG];
    static qboolean inupdate;

    va_start(argptr, fmt);
    vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

// also echo to debugging console
    Sys_Printf("%s", msg);	// also echo to debugging console

// log all messages to file
    if (debuglog)
	Sys_DebugLog(va("%s/qconsole.log", com_gamedir), "%s", msg);

    if (!con_initialized)
	return;

#ifdef NQ_HACK
    if (cls.state == ca_dedicated)
	return;			// no graphics mode
#endif

// write it to the scrollable buffer
    Con_Print(msg);

    /*
     * FIXME - not sure if this is ok, need to rework the screen update
     * criteria so it gets done once per frame unless explicitly flushed. For
     * now, don't update until we see a newline char.
     */
    if (!strchr(msg, '\n'))
	return;

// update the screen immediately if the console is displayed
#ifdef NQ_HACK
    if (cls.state != ca_active && !scr_disabled_for_loading) {
#else
    if (con_forcedup) {
#endif
	// protect against infinite loop if something in SCR_UpdateScreen calls
	// Con_Printd
	if (!inupdate) {
	    inupdate = true;
	    SCR_UpdateScreen();
	    inupdate = false;
	}
    }
}

/*
================
Con_DPrintf

A Con_Printf that only shows up if the "developer" cvar is set
================
*/
void
Con_DPrintf(const char *fmt, ...)
{
    va_list argptr;
    char msg[MAX_PRINTMSG];

    if (!developer.value) {
	if (debuglog) {
	    strcpy(msg, "DEBUG: ");
	    va_start(argptr, fmt);
	    vsnprintf(msg + 7, sizeof(msg) - 7, fmt, argptr);
	    va_end(argptr);
	    Sys_DebugLog(va("%s/qconsole.log", com_gamedir), "%s", msg);
	}
	return;
    }

    va_start(argptr, fmt);
    vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    Con_Printf("%s", msg);
}


/*
==============================================================================

DRAWING

==============================================================================
*/


/*
================
Con_DrawInput

The input line scrolls horizontally if typing goes beyond the right edge
================
*/
void
Con_DrawInput(void)
{
    int y;
    int i;
    char *text;

    if (key_dest != key_console && !con_forcedup)
	return;			// don't draw anything

    text = key_lines[edit_line];

// add the cursor frame
    text[key_linepos] = 10 + ((int)(realtime * con_cursorspeed) & 1);

// fill out remainder with spaces
    for (i = key_linepos + 1; i < con_linewidth; i++)
	text[i] = ' ';

//      prestep if horizontally scrolling
    if (key_linepos >= con_linewidth)
	text += 1 + key_linepos - con_linewidth;

// draw it
    y = con_vislines - 22;

    for (i = 0; i < con_linewidth; i++)
	Draw_Character((i + 1) << 3, con_vislines - 22, text[i]);

// remove cursor
    key_lines[edit_line][key_linepos] = 0;
}


/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
void
Con_DrawNotify(void)
{
    int x, v;
    char *text;
    int i;
    float time;
    char *s;
    int skip;

    v = 0;
    for (i = con->current - NUM_CON_TIMES + 1; i <= con->current; i++) {
	if (i < 0)
	    continue;
	time = con_times[i % NUM_CON_TIMES];
	if (time == 0)
	    continue;
	time = realtime - time;
	if (time > con_notifytime.value)
	    continue;
	text = con->text + (i % con_totallines) * con_linewidth;

	clearnotify = 0;
	scr_copytop = 1;

	for (x = 0; x < con_linewidth; x++)
	    Draw_Character((x + 1) << 3, v, text[x]);

	v += 8;
    }


    if (key_dest == key_message) {
	clearnotify = 0;
	scr_copytop = 1;

	if (chat_team) {
	    Draw_String(8, v, "say_team:");
	    skip = 11;
	} else {
	    Draw_String(8, v, "say:");
	    skip = 6;
	}

	s = chat_buffer;
	// FIXME = Truncating? should be while, not if?
	if (chat_bufferlen > (vid.width >> 3) - (skip + 1))
	    s += chat_bufferlen - ((vid.width >> 3) - (skip + 1));

	x = 0;
	while (s[x]) {
	    Draw_Character((x + skip) << 3, v, s[x]);
	    x++;
	}
	Draw_Character((x + skip) << 3, v,
		       10 + ((int)(realtime * con_cursorspeed) & 1));
	v += 8;
    }

    if (v > con_notifylines)
	con_notifylines = v;
}

/*
================
Con_DrawConsole

Draws the console with the solid background
FIXME - The input line at the bottom should only be drawn if typing is allowed
================
*/
void
Con_DrawConsole(int lines)
{
    int i, x, y;
    int rows;
    char *text;
    int row;

#ifdef QW_HACK
    int j, n;
    char dlbar[1024];
#endif

    if (lines <= 0)
	return;

// draw the background
    Draw_ConsoleBackground(lines);

// draw the text
    con_vislines = lines;
    rows = (lines - 22) >> 3;	// rows of text to draw
    y = lines - 30;

// draw from the bottom up
    if (con->display != con->current) {
	// draw arrows to show the buffer is backscrolled
	for (x = 0; x < con_linewidth; x += 4)
	    Draw_Character((x + 1) << 3, y, '^');
	y -= 8;
	rows--;
    }

    row = con->display;
    for (i = 0; i < rows; i++, y -= 8, row--) {
	if (row < 0)
	    break;
	if (con->current - row >= con_totallines)
	    break;		// past scrollback wrap point

	text = con->text + (row % con_totallines) * con_linewidth;
	for (x = 0; x < con_linewidth; x++)
	    Draw_Character((x + 1) << 3, y, text[x]);
    }

#ifdef QW_HACK
    // draw the download bar
    // figure out width
    if (cls.download) {
	if ((text = strrchr(cls.downloadname, '/')) != NULL)
	    text++;
	else
	    text = cls.downloadname;

	x = con_linewidth - ((con_linewidth * 7) / 40);
	y = x - strlen(text) - 8;
	i = con_linewidth / 3;
	if (strlen(text) > i) {
	    y = x - i - 11;
	    strncpy(dlbar, text, i);
	    dlbar[i] = 0;
	    strcat(dlbar, "...");
	} else
	    strcpy(dlbar, text);
	strcat(dlbar, ": ");
	i = strlen(dlbar);
	dlbar[i++] = '\x80';
	// where's the dot go?
	if (cls.downloadpercent == 0)
	    n = 0;
	else
	    n = y * cls.downloadpercent / 100;

	for (j = 0; j < y; j++)
	    if (j == n)
		dlbar[i++] = '\x83';
	    else
		dlbar[i++] = '\x81';
	dlbar[i++] = '\x82';
	dlbar[i] = 0;

	sprintf(dlbar + strlen(dlbar), " %02d%%", cls.downloadpercent);

	// draw it
	y = con_vislines - 22 + 8;
	for (i = 0; i < strlen(dlbar); i++)
	    Draw_Character((i + 1) << 3, y, dlbar[i]);
    }
#endif

    // draw the input prompt, user text, and cursor if desired
    Con_DrawInput();
}


/*
==================
Con_NotifyBox
==================
*/
void
Con_NotifyBox(char *text)
{
    double t1, t2;

// during startup for sound / cd warnings
    Con_Printf
	("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n");

    Con_Printf(text);

    Con_Printf("Press a key.\n");
    Con_Printf
	("\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n");

    key_count = -2;		// wait for a key down and up
    key_dest = key_console;

    do {
	t1 = Sys_DoubleTime();
	SCR_UpdateScreen();
	Sys_SendKeyEvents();
	t2 = Sys_DoubleTime();
	realtime += t2 - t1;	// make the cursor blink
    } while (key_count < 0);

    Con_Printf("\n");
    key_dest = key_game;
    realtime = 0;		// put the cursor back to invisible
}


/*
==================
Con_SafePrintf

Okay to call even when the screen can't be updated
==================
*/
void
Con_SafePrintf(const char *fmt, ...)
{
    va_list argptr;
    char msg[MAX_PRINTMSG];
    int temp;

    va_start(argptr, fmt);
    vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    temp = scr_disabled_for_loading;
    scr_disabled_for_loading = true;
    Con_Printf("%s", msg);
    scr_disabled_for_loading = temp;
}

void
Con_ShowList(const char **list, int cnt, int maxlen)
{
    const char *s;
    unsigned i, j, len, cols, rows;
    char *line;

    /* Lay them out in columns */
    line = Z_Malloc(Con_GetWidth() + 1);
    cols = Con_GetWidth() / (maxlen + 2);
    rows = cnt / cols + ((cnt % cols) ? 1 : 0);

    /* Looks better if we have a few rows before spreading out */
    if (rows < 5) {
	cols = cnt / 5 + ((cnt % 5) ? 1 : 0);
	rows = cnt / cols + ((cnt % cols) ? 1 : 0);
    }

    for (i = 0; i < rows; ++i) {
	line[0] = '\0';
	for (j = 0; j < cols; ++j) {
	    if (j * rows + i >= cnt)
		break;
	    s = list[j * rows + i];
	    len = strlen(s);

	    strcat(line, s);
	    if (j < cols - 1) {
		while (len < maxlen) {
		    strcat(line, " ");
		    len++;
		}
		strcat(line, "  ");
	    }
	}
	Con_Printf("%s\n", line);
    }
    Z_Free(line);
}

static const char **showtree_list;
static unsigned showtree_idx;

static void
Con_ShowTree_Populate(struct rb_node *n)
{
    if (n) {
	Con_ShowTree_Populate(n->rb_left);
	showtree_list[showtree_idx++] = stree_entry(n)->string;
	Con_ShowTree_Populate(n->rb_right);
    }
}

void
Con_ShowTree(struct stree_root *root)
{
    /* FIXME - cheating with malloc */
    showtree_list = malloc(root->entries * sizeof(char *));
    if (showtree_list) {
	showtree_idx = 0;
	Con_ShowTree_Populate(root->root.rb_node);
	Con_ShowList(showtree_list, root->entries, root->maxlen);
	free(showtree_list);
    }
}


void
Con_Maplist_f()
{
    struct stree_root st_root = STREE_ROOT;
    char *pfx = NULL;

    if (Cmd_Argc() == 2)
	pfx = Cmd_Argv(1);

    STree_AllocInit();
    COM_ScanDir(&st_root, "maps", pfx, ".bsp", true);
    Con_ShowTree(&st_root);
}


/*
================
Con_Init
================
*/
void
Con_Init(void)
{
    debuglog = COM_CheckParm("-condebug");

    con_main.text = Hunk_AllocName(CON_TEXTSIZE, "conmain");

    con = &con_main;
    con_linewidth = -1;
    Con_CheckResize();

    Con_Printf("Console initialized.\n");

    /* register our commands */
    Cvar_RegisterVariable(&con_notifytime);

    Cmd_AddCommand("toggleconsole", Con_ToggleConsole_f);
    Cmd_AddCommand("messagemode", Con_MessageMode_f);
    Cmd_AddCommand("messagemode2", Con_MessageMode2_f);
    Cmd_AddCommand("clear", Con_Clear_f);
    Cmd_AddCommand("maplist", Con_Maplist_f);

    con_initialized = true;
}
