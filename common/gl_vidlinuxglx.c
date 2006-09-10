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
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>

#define GLX_GLXEXT_PROTOTYPES
#include <GL/glx.h>

#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/extensions/xf86vmode.h>

#include "common.h"
#include "console.h"
#include "glquake.h"
#include "keys.h"
#include "quakedef.h"
#include "sys.h"
#include "vid.h"

#ifdef NQ_HACK
#include "client.h"
#include "host.h"
#endif

// FIXME - complete the refactoring of core X stuff into here
#include "x11_core.h"
#include "in_x11.h"

/*
 * glXGetProcAddress - This function is defined in GLX version 1.4, but this
 * is not common enough yet to rely on it being present in any old libGL.so
 * (e.g. Nvidia's proprietary drivers). glXGetProcAddressARB has been around
 * longer and actually forms part of the current Linux OpenGL ABI
 * - http://oss.sgi.com/projects/ogl-sample/ABI/
 */
#ifndef GLX_ARB_get_proc_address
#error "glXGetProcAddressARB is REQUIRED"
#endif
#define glXGetProcAddress glXGetProcAddressARB

/*
 * Ignore the fact that our char type may be signed
 */
#define qglXGetProcAddress(s) glXGetProcAddress((GLubyte *)(s))

#define WARP_WIDTH              320
#define WARP_HEIGHT             200

static int scrnum;
static GLXContext ctx = NULL;

viddef_t vid;			// global video state

unsigned short d_8to16table[256];
unsigned int d_8to24table[256];
unsigned char d_15to8table[65536];

cvar_t vid_mode = { "vid_mode", "0", false };

// FIXME - useless, or for vidmode changes?
static int win_x, win_y;
static int old_mouse_x, old_mouse_y;

static int scr_width, scr_height;

static XF86VidModeModeInfo saved_vidmode;
static qboolean vidmode_active = false;

/*-----------------------------------------------------------------------*/

//int           texture_mode = GL_NEAREST;
//int           texture_mode = GL_NEAREST_MIPMAP_NEAREST;
//int           texture_mode = GL_NEAREST_MIPMAP_LINEAR;
int texture_mode = GL_LINEAR;

//int           texture_mode = GL_LINEAR_MIPMAP_NEAREST;
//int           texture_mode = GL_LINEAR_MIPMAP_LINEAR;

float gldepthmin, gldepthmax;

cvar_t gl_ztrick = { "gl_ztrick", "1" };

const char *gl_vendor;
const char *gl_renderer;
const char *gl_version;
const char *gl_extensions;

static qboolean is8bit = false;
qboolean gl_mtexable = false;
static int gl_num_texture_units;

// FIXME - lose this hack?
qboolean isPermedia = false;

/*-----------------------------------------------------------------------*/
void
D_BeginDirectRect(int x, int y, byte *pbitmap, int width, int height)
{
// direct drawing of the "accessing disk" icon isn't supported under Linux
}

void
D_EndDirectRect(int x, int y, int width, int height)
{
// direct drawing of the "accessing disk" icon isn't supported under Linux
}

// XLateKey - Transform from X key symbols to Quake's symbols
static int
XLateKey(XKeyEvent * ev)
{
    int key;
    char buf[64];
    KeySym keysym;

    key = 0;

    XLookupString(ev, buf, sizeof(buf), &keysym, 0);

    switch (keysym) {
    case XK_KP_Page_Up:
    case XK_Page_Up:
	key = K_PGUP;
	break;

    case XK_KP_Page_Down:
    case XK_Page_Down:
	key = K_PGDN;
	break;

    case XK_KP_Home:
    case XK_Home:
	key = K_HOME;
	break;

    case XK_KP_End:
    case XK_End:
	key = K_END;
	break;

    case XK_KP_Left:
    case XK_Left:
	key = K_LEFTARROW;
	break;

    case XK_KP_Right:
    case XK_Right:
	key = K_RIGHTARROW;
	break;

    case XK_KP_Down:
    case XK_Down:
	key = K_DOWNARROW;
	break;

    case XK_KP_Up:
    case XK_Up:
	key = K_UPARROW;
	break;

    case XK_Escape:
	key = K_ESCAPE;
	break;

    case XK_KP_Enter:
    case XK_Return:
	key = K_ENTER;
	break;

    case XK_Tab:
	key = K_TAB;
	break;

    case XK_F1:
	key = K_F1;
	break;

    case XK_F2:
	key = K_F2;
	break;

    case XK_F3:
	key = K_F3;
	break;

    case XK_F4:
	key = K_F4;
	break;

    case XK_F5:
	key = K_F5;
	break;

    case XK_F6:
	key = K_F6;
	break;

    case XK_F7:
	key = K_F7;
	break;

    case XK_F8:
	key = K_F8;
	break;

    case XK_F9:
	key = K_F9;
	break;

    case XK_F10:
	key = K_F10;
	break;

    case XK_F11:
	key = K_F11;
	break;

    case XK_F12:
	key = K_F12;
	break;

    case XK_BackSpace:
	key = K_BACKSPACE;
	break;

    case XK_KP_Delete:
    case XK_Delete:
	key = K_DEL;
	break;

    case XK_Pause:
	key = K_PAUSE;
	break;

    case XK_Shift_L:
    case XK_Shift_R:
	key = K_SHIFT;
	break;

    case XK_Execute:
    case XK_Control_L:
    case XK_Control_R:
	key = K_CTRL;
	break;

    case XK_Alt_L:
    case XK_Meta_L:
    case XK_Alt_R:
    case XK_Meta_R:
	key = K_ALT;
	break;

    case XK_KP_Begin:
	key = '5';
	break;

    case XK_Insert:
    case XK_KP_Insert:
	key = K_INS;
	break;

    case XK_KP_Multiply:
	key = '*';
	break;
    case XK_KP_Add:
	key = '+';
	break;
    case XK_KP_Subtract:
	key = '-';
	break;
    case XK_KP_Divide:
	key = '/';
	break;

#if 0
    case 0x021:
	key = '1';
	break;			/* [!] */
    case 0x040:
	key = '2';
	break;			/* [@] */
    case 0x023:
	key = '3';
	break;			/* [#] */
    case 0x024:
	key = '4';
	break;			/* [$] */
    case 0x025:
	key = '5';
	break;			/* [%] */
    case 0x05e:
	key = '6';
	break;			/* [^] */
    case 0x026:
	key = '7';
	break;			/* [&] */
    case 0x02a:
	key = '8';
	break;			/* [*] */
    case 0x028:
	key = '9';
	break;			/* [(] */
    case 0x029:
	key = '0';
	break;			/* [)] */
    case 0x05f:
	key = '-';
	break;			/* [_] */
    case 0x02b:
	key = '=';
	break;			/* [+] */
    case 0x07c:
	key = '\'';
	break;			/* [|] */
    case 0x07d:
	key = '[';
	break;			/* [}] */
    case 0x07b:
	key = ']';
	break;			/* [{] */
    case 0x022:
	key = '\'';
	break;			/* ["] */
    case 0x03a:
	key = ';';
	break;			/* [:] */
    case 0x03f:
	key = '/';
	break;			/* [?] */
    case 0x03e:
	key = '.';
	break;			/* [>] */
    case 0x03c:
	key = ',';
	break;			/* [<] */
#endif

    default:
	key = *(unsigned char *)buf;
	if (key >= 'A' && key <= 'Z')
	    key = key - 'A' + 'a';
	break;
    }

    return key;
}

static void
HandleEvents(void)
{
    XEvent event;
    qboolean dowarp = false;

    if (!x_disp)
	return;

    while (XPending(x_disp)) {
	XNextEvent(x_disp, &event);

	switch (event.type) {
	case KeyPress:
	case KeyRelease:
	    Key_Event(XLateKey(&event.xkey), event.type == KeyPress);
	    break;

	case MotionNotify:
	    if (mouse_grab_active) {
		if (dga_mouse_active) {
		    mouse_x += event.xmotion.x_root;
		    mouse_y += event.xmotion.y_root;
		} else {
		    mouse_x = event.xmotion.x - (int)(vid.width / 2);
		    mouse_y = event.xmotion.y - (int)(vid.height / 2);

		    if (mouse_x || mouse_y)
			dowarp = true;
		}
	    }
	    break;

	case ButtonPress:
	    if (event.xbutton.button == 1)
		Key_Event(K_MOUSE1, true);
	    else if (event.xbutton.button == 2)
		Key_Event(K_MOUSE3, true);
	    else if (event.xbutton.button == 3)
		Key_Event(K_MOUSE2, true);
	    else if (event.xbutton.button == 4)
		Key_Event(K_MWHEELUP, true);
	    else if (event.xbutton.button == 5)
		Key_Event(K_MWHEELDOWN, true);
	    else if (event.xbutton.button == 6)
		Key_Event(K_MOUSE4, true);
	    else if (event.xbutton.button == 7)
		Key_Event(K_MOUSE5, true);
	    else if (event.xbutton.button == 8)
		Key_Event(K_MOUSE6, true);
	    else if (event.xbutton.button == 9)
		Key_Event(K_MOUSE7, true);
	    else if (event.xbutton.button == 10)
		Key_Event(K_MOUSE8, true);
	    break;

	case ButtonRelease:
	    if (event.xbutton.button == 1)
		Key_Event(K_MOUSE1, false);
	    else if (event.xbutton.button == 2)
		Key_Event(K_MOUSE3, false);
	    else if (event.xbutton.button == 3)
		Key_Event(K_MOUSE2, false);
	    else if (event.xbutton.button == 4)
		Key_Event(K_MWHEELUP, false);
	    else if (event.xbutton.button == 5)
		Key_Event(K_MWHEELDOWN, false);
	    else if (event.xbutton.button == 6)
		Key_Event(K_MOUSE4, false);
	    else if (event.xbutton.button == 7)
		Key_Event(K_MOUSE5, false);
	    else if (event.xbutton.button == 8)
		Key_Event(K_MOUSE6, false);
	    else if (event.xbutton.button == 9)
		Key_Event(K_MOUSE7, false);
	    else if (event.xbutton.button == 10)
		Key_Event(K_MOUSE8, false);
	    break;

	case CreateNotify:
	    win_x = event.xcreatewindow.x;
	    win_y = event.xcreatewindow.y;
	    break;

	case ConfigureNotify:
	    win_x = event.xconfigure.x;
	    win_y = event.xconfigure.y;
	    break;
	}
    }

    if (dowarp)
	IN_CenterMouse();
}

void (*VID_SetGammaRamp)(unsigned short ramp[3][256]);
static unsigned short *x11_gamma_ramp;
static int x11_gamma_size;

void
signal_handler(int sig)
{
    printf("Received signal %d, exiting...\n", sig);
    XAutoRepeatOn(x_disp);
    if (VID_SetGammaRamp)
	XF86VidModeSetGammaRamp(x_disp, scrnum, x11_gamma_size,
				x11_gamma_ramp,
				x11_gamma_ramp + x11_gamma_size,
				x11_gamma_ramp + x11_gamma_size * 2);
    XCloseDisplay(x_disp);
    Sys_Quit();
}

void
InitSig(void)
{
    signal(SIGHUP, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGILL, signal_handler);
    signal(SIGTRAP, signal_handler);
    signal(SIGIOT, signal_handler);
    signal(SIGBUS, signal_handler);
    signal(SIGFPE, signal_handler);
    signal(SIGSEGV, signal_handler);
    signal(SIGTERM, signal_handler);
}

/*
 * VID_ShiftPalette
 * - Updates hardware gamma
 */
void
VID_ShiftPalette(unsigned char *p)
{
//      VID_SetPalette(p);
}

void
VID_SetPalette(unsigned char *palette)
{
    byte *pal;
    unsigned r, g, b;
    unsigned v;
    int r1, g1, b1;
    int k;
    unsigned short i;
    unsigned *table;
    int dist, bestdist;

//
// 8 8 8 encoding
//
    pal = palette;
    table = d_8to24table;
    for (i = 0; i < 256; i++) {
	r = pal[0];
	g = pal[1];
	b = pal[2];
	pal += 3;

	v = (255 << 24) + (r << 0) + (g << 8) + (b << 16);
	*table++ = LittleLong(v);
    }
    d_8to24table[255] &= LittleLong(0xffffff);	// 255 is transparent

    for (i = 0; i < (1 << 15); i++) {
	/*
	 * Maps
	 * 000000000000000
	 * 000000000011111 = Red  = 0x1F
	 * 000001111100000 = Blue = 0x03E0
	 * 111110000000000 = Grn  = 0x7C00
	 */
	r = ((i & 0x1F) << 3) + 4;
	g = ((i & 0x03E0) >> 2) + 4;
	b = ((i & 0x7C00) >> 7) + 4;
	pal = (unsigned char *)d_8to24table;
	for (v = 0, k = 0, bestdist = 10000 * 10000; v < 256; v++, pal += 4) {
	    r1 = (int)r - (int)pal[0];
	    g1 = (int)g - (int)pal[1];
	    b1 = (int)b - (int)pal[2];
	    dist = (r1 * r1) + (g1 * g1) + (b1 * b1);
	    if (dist < bestdist) {
		k = v;
		bestdist = dist;
	    }
	}
	d_15to8table[i] = k;
    }
}

void
CheckMultiTextureExtensions(void)
{
    // FIXME - no space at end of string? Check properly...
    gl_mtexable = false;
    if (!COM_CheckParm("-nomtex")
	&& strstr(gl_extensions, "GL_ARB_multitexture ")) {
	Con_Printf("ARB multitexture extensions found.\n");

	qglMultiTexCoord2fARB =
	    (lpMultiTexFUNC)qglXGetProcAddress("glMultiTexCoord2fARB");
	qglActiveTextureARB =
	    (lpActiveTextureFUNC)qglXGetProcAddress("glActiveTextureARB");

	/* Check how many texture units there actually are */
	glGetIntegerv(GL_MAX_TEXTURE_UNITS, &gl_num_texture_units);

	if (gl_num_texture_units < 2) {
	    Con_Printf("Only %i texture units, multitexture disabled.\n",
		       gl_num_texture_units);
	} else if (!qglMultiTexCoord2fARB || !qglActiveTextureARB) {
	    Con_Printf("ARB Multitexture symbols not found, disabled.\n");
	} else {
	    Con_Printf("ARB multitexture extension enabled\n"
		       "-> %i texture units available\n",
		       gl_num_texture_units);
	    gl_mtexable = true;
	}
    }
}

static void
VID_SetXF86GammaRamp(unsigned short ramp[3][256])
{
    int i;
    unsigned short *r, *g, *b;

    if (!x_disp)
	Sys_Error("%s: x_disp == NULL!", __func__);

    /*
     * Need to scale the gamma ramp to the hardware size
     */
    r = Hunk_TempAlloc(3 * x11_gamma_size * sizeof(unsigned short));
    g = r + x11_gamma_size;
    b = r + x11_gamma_size * 2;
    for (i = 0; i < x11_gamma_size; i++) {
	r[i] = ramp[0][i * 256 / x11_gamma_size];
	g[i] = ramp[1][i * 256 / x11_gamma_size];
	b[i] = ramp[2][i * 256 / x11_gamma_size];
    }

    XF86VidModeSetGammaRamp(x_disp, scrnum, x11_gamma_size, r, g, b);
}

/*
 * Gamma_Init
 * - Checks to see if gamma settings are available
 * - Saves the current gamma settings
 * - Sets the default gamma ramp function
 */
static void
Gamma_Init()
{
    Bool ret;
    int size;

    ret = XF86VidModeGetGammaRampSize(x_disp, scrnum, &x11_gamma_size);
    if (!ret|| !x11_gamma_size) {
	VID_SetGammaRamp = NULL;
	return;
    }

    size = 3 * x11_gamma_size * sizeof(unsigned short);
    x11_gamma_ramp = Hunk_AllocName(size, "x11_gamma_ramp");

    ret = XF86VidModeGetGammaRamp(x_disp, scrnum, x11_gamma_size,
				  x11_gamma_ramp,
				  x11_gamma_ramp + x11_gamma_size,
				  x11_gamma_ramp + x11_gamma_size * 2);
    if (ret)
	VID_SetGammaRamp = VID_SetXF86GammaRamp;
    else
	VID_SetGammaRamp = NULL;
}

/*
===============
GL_Init
===============
*/
void
GL_Init(void)
{
    gl_vendor = (char *)glGetString(GL_VENDOR);
    Con_Printf("GL_VENDOR: %s\n", gl_vendor);
    gl_renderer = (char *)glGetString(GL_RENDERER);
    Con_Printf("GL_RENDERER: %s\n", gl_renderer);

    gl_version = (char *)glGetString(GL_VERSION);
    Con_Printf("GL_VERSION: %s\n", gl_version);
    gl_extensions = (char *)glGetString(GL_EXTENSIONS);
    Con_DPrintf("GL_EXTENSIONS: %s\n", gl_extensions);

    CheckMultiTextureExtensions();

    glClearColor(0.5, 0.5, 0.5, 0);
    glCullFace(GL_FRONT);
    glEnable(GL_TEXTURE_2D);

    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.666);

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glShadeModel(GL_FLAT);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

/*
=================
GL_BeginRendering

=================
*/
void
GL_BeginRendering(int *x, int *y, int *width, int *height)
{
    *x = *y = 0;
    *width = scr_width;
    *height = scr_height;
}


void
GL_EndRendering(void)
{
    glFlush();
    glXSwapBuffers(x_disp, x_win);
}

qboolean
VID_Is8bit(void)
{
    return is8bit;
}

typedef void (*tdfxSetPaletteFUNC) (GLuint *);
typedef void (*ColorTableFUNC) (GLenum, GLenum, GLsizei, GLenum, GLenum,
				const GLvoid *);

// FIXME - allow 8-bit palettes to be enabled/disabled
static void
VID_Init8bitPalette(void)
{
    /*
     * Check for 8bit Extensions and initialize them.
     * Try color table extension first, then 3dfx specific
     */
    tdfxSetPaletteFUNC qgl3DfxSetPaletteEXT;
    ColorTableFUNC qglColorTableEXT;
    int i;

    if (strstr(gl_extensions, "GL_EXT_shared_texture_palette")) {
	char thePalette[256 * 3];
	char *oldPalette, *newPalette;

	qglColorTableEXT =
	    (ColorTableFUNC)qglXGetProcAddress("glColorTableEXT");
	Con_SafePrintf("8-bit GL extensions enabled.\n");
	glEnable(GL_SHARED_TEXTURE_PALETTE_EXT);
	oldPalette = (char *)d_8to24table;	//d_8to24table3dfx;
	newPalette = thePalette;
	for (i = 0; i < 256; i++) {
	    *newPalette++ = *oldPalette++;
	    *newPalette++ = *oldPalette++;
	    *newPalette++ = *oldPalette++;
	    oldPalette++;
	}
	qglColorTableEXT(GL_SHARED_TEXTURE_PALETTE_EXT, GL_RGB, 256, GL_RGB,
			 GL_UNSIGNED_BYTE, (void *)thePalette);
	is8bit = true;
    } else if (strstr(gl_extensions, "3DFX_set_global_palette")) {
	GLubyte table[256][4];
	char *oldpal;

	qgl3DfxSetPaletteEXT =
	    (tdfxSetPaletteFUNC)qglXGetProcAddress("gl3DfxSetPaletteEXT");
	Con_SafePrintf("8-bit GL extensions (3dfx) enabled.\n");
	oldpal = (char *)d_8to24table;	//d_8to24table3dfx;
	for (i = 0; i < 256; i++) {
	    table[i][2] = *oldpal++;
	    table[i][1] = *oldpal++;
	    table[i][0] = *oldpal++;
	    table[i][3] = 255;
	    oldpal++;
	}
	glEnable(GL_SHARED_TEXTURE_PALETTE_EXT);
	qgl3DfxSetPaletteEXT((GLuint *)table);
	is8bit = true;
    }
}

#if 0
/* FIXME - re-enable? */
static void
Check_Gamma (unsigned char *pal)
{
    float       f, inf;
    unsigned char palette[768];
    int         i;

    if ((i = COM_CheckParm("-gamma")) == 0) {
	if ((gl_renderer && strstr(gl_renderer, "Voodoo")) ||
	    (gl_vendor && strstr(gl_vendor, "3Dfx"))) {
	    vid_gamma = 1;
	} else {
	    //vid_gamma = 0.7;	// default to 0.7 on non-3dfx hardware
	    vid_gamma = 1.0;	// Be like the original...
	}
    } else {
	vid_gamma = Q_atof(com_argv[i + 1]);
    }

    for (i = 0; i < 768; i++) {
	f = pow((pal[i] + 1) / 256.0, vid_gamma);
	inf = f * 255 + 0.5;
	if (inf < 0)
	    inf = 0;
	if (inf > 255)
	    inf = 255;
	palette[i] = inf;
    }

    memcpy(pal, palette, sizeof(palette));
}
#endif

static void
VID_InitCvars(void)
{
    Cvar_RegisterVariable(&vid_mode);
    Cvar_RegisterVariable(&gl_ztrick);
}

/*
 * Set the vidmode to the requested width/height if possible
 * Return true if successful, false otherwise
 */
static qboolean
VID_set_vidmode(int width, int height)
{
    int i, x, y, dist;
    int best_dist = 9999999;
    int num_modes;
    XF86VidModeModeInfo **modes;
    XF86VidModeModeInfo *mode = NULL;
    qboolean mode_changed = false;

    if (vidmode_active)
	Sys_Error("%s: called while vidmode_active == true.", __func__);

    XF86VidModeGetAllModeLines(x_disp, scrnum, &num_modes, &modes);
    for (i = 0; i < num_modes; i++) {
	if (width > modes[i]->hdisplay || height > modes[i]->vdisplay)
	    continue;
	x = width - modes[i]->hdisplay;
	y = height - modes[i]->vdisplay;
	dist = (x * x) + (y * y);
	if (dist < best_dist) {
	    best_dist = dist;
	    mode = modes[i];
	}
    }

    if (mode) {
	mode_changed = XF86VidModeSwitchToMode(x_disp, scrnum, mode);
	if (mode_changed) {
	    vidmode_active = true;
	    memcpy(&saved_vidmode, modes[0], sizeof(XF86VidModeModeInfo));
	}
    }

    return mode_changed;
}

static void
VID_restore_vidmode()
{
    if (vidmode_active) {
	XF86VidModeSwitchToMode(x_disp, scrnum, &saved_vidmode);
	if (saved_vidmode.privsize && saved_vidmode.private)
	    XFree(saved_vidmode.private);
    }
}

void
VID_Init(unsigned char *palette)
{
    int i;
    int attrib[] = {
	GLX_RGBA,
	GLX_RED_SIZE, 1,
	GLX_GREEN_SIZE, 1,
	GLX_BLUE_SIZE, 1,
	GLX_DOUBLEBUFFER,
	GLX_DEPTH_SIZE, 1,
	None
    };
    char gldir[MAX_OSPATH];
    int width = 640, height = 480;
    XSetWindowAttributes attr;
    unsigned long mask;
    Window root;
    XVisualInfo *visinfo;
    qboolean fullscreen = true;
    qboolean vidmode_ext = false;
    int MajorVersion, MinorVersion;

    VID_InitCvars();

    vid.maxwarpwidth = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));

// interpret command-line params

// set vid parameters
    if (COM_CheckParm("-window") || COM_CheckParm("-w"))
	fullscreen = false;
    if ((i = COM_CheckParm("-width")) != 0)
	width = atoi(com_argv[i + 1]);
    if ((i = COM_CheckParm("-height")) != 0)
	height = atoi(com_argv[i + 1]);
    if ((i = COM_CheckParm("-conwidth")) != 0)
	vid.conwidth = Q_atoi(com_argv[i + 1]);
    else
	vid.conwidth = 640;

    vid.conwidth &= 0xfff8;	// make it a multiple of eight

    if (vid.conwidth < 320)
	vid.conwidth = 320;

    // pick a conheight that matches with correct aspect
    vid.conheight = vid.conwidth * 3 / 4;

    if ((i = COM_CheckParm("-conheight")) != 0)
	vid.conheight = Q_atoi(com_argv[i + 1]);
    if (vid.conheight < 200)
	vid.conheight = 200;

    x_disp = XOpenDisplay(NULL);
    if (!x_disp) {
	if (getenv("DISPLAY"))
	    Sys_Error("VID: Could not open display [%s]", getenv("DISPLAY"));
	else
	    Sys_Error("VID: Could not open local display\n");
    }

    scrnum = DefaultScreen(x_disp);
    root = RootWindow(x_disp, scrnum);

    // Check video mode extension
    MajorVersion = MinorVersion = 0;
    if (XF86VidModeQueryVersion(x_disp, &MajorVersion, &MinorVersion)) {
	Con_Printf("Using XFree86-VidModeExtension Version %i.%i\n",
		   MajorVersion, MinorVersion);
	vidmode_ext = true;
    }

    visinfo = glXChooseVisual(x_disp, scrnum, attrib);
    if (!visinfo) {
	fprintf(stderr,
		"qkHack: Error couldn't get an RGB, Double-buffered, "
		"Depth visual\n");
	exit(EXIT_FAILURE);
    }

    /* Attempt to set the vidmode if needed */
    if (vidmode_ext && fullscreen)
	fullscreen = VID_set_vidmode(width, height);

    Gamma_Init();

    /* window attributes */
    mask = CWBackPixel | CWColormap | CWEventMask;
    attr.background_pixel = 0;
    attr.colormap = XCreateColormap(x_disp, root, visinfo->visual, AllocNone);
    attr.event_mask = X_CORE_MASK | X_KEY_MASK | X_MOUSE_MASK;

    if (vidmode_active) {
	mask |= CWSaveUnder | CWBackingStore | CWOverrideRedirect;
	attr.override_redirect = True;
	attr.backing_store = NotUseful;
	attr.save_under = False;
    } else {
	mask |= CWBorderPixel;
	attr.border_pixel = 0;
    }

    x_win = XCreateWindow(x_disp, root, 0, 0, width, height,
			  0, visinfo->depth, InputOutput,
			  visinfo->visual, mask, &attr);
    XMapWindow(x_disp, x_win);

    if (vidmode_active) {
	XMoveWindow(x_disp, x_win, 0, 0);
	XRaiseWindow(x_disp, x_win);

	// FIXME - mouse may not be active...
	IN_CenterMouse();
	XFlush(x_disp);

	// Move the viewport to top left
	XF86VidModeSetViewPort(x_disp, scrnum, 0, 0);
    }

    XFlush(x_disp);

    ctx = glXCreateContext(x_disp, visinfo, NULL, True);
    glXMakeCurrent(x_disp, x_win, ctx);

    scr_width = width;
    scr_height = height;

    if (vid.conheight > height)
	vid.conheight = height;
    if (vid.conwidth > width)
	vid.conwidth = width;

    // FIXME - what?
    vid.width = vid.conwidth;
    vid.height = vid.conheight;

    vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);
    vid.numpages = 2;

    InitSig();			// trap evil signals

    GL_Init();

    sprintf(gldir, "%s/glquake", com_gamedir);
    Sys_mkdir(gldir);

    VID_SetPalette(palette);

    // Check for 3DFX Extensions and initialize them.
    VID_Init8bitPalette();

    Con_SafePrintf("Video mode %dx%d initialized.\n", width, height);

    vid.recalc_refdef = 1;	// force a surface cache flush
}

void
VID_Shutdown(void)
{
    if (VID_SetGammaRamp) {
	XF86VidModeSetGammaRamp(x_disp, scrnum, x11_gamma_size,
				x11_gamma_ramp,
				x11_gamma_ramp + x11_gamma_size,
				x11_gamma_ramp + x11_gamma_size * 2);
    }
    if (x_disp != NULL) {
	if (ctx != NULL)
	    glXDestroyContext(x_disp, ctx);
	if (x_win != None)
	    XDestroyWindow(x_disp, x_win);
	if (vidmode_active)
	    VID_restore_vidmode();
	XCloseDisplay(x_disp);
    }
    vidmode_active = false;
    x_disp = NULL;
    x_win = None;
    ctx = NULL;
}

void
Sys_SendKeyEvents(void)
{
    HandleEvents();
}

void
Force_CenterView_f(void)
{
    cl.viewangles[PITCH] = 0;
}

/*
===========
IN_Commands
===========
*/
void
IN_Commands(void)
{
}

/*
===========
IN_Move
===========
*/
static void
IN_MouseMove(usercmd_t *cmd)
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

// add mouse X/Y movement to cmd
    if ((in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1)))
	cmd->sidemove += m_side.value * mouse_x;
    else
	cl.viewangles[YAW] -= m_yaw.value * mouse_x;

    if (in_mlook.state & 1)
	V_StopPitchDrift();

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

void
IN_Move(usercmd_t *cmd)
{
    IN_MouseMove(cmd);
}

void
VID_UnlockBuffer()
{
}

void
VID_LockBuffer()
{
}

qboolean
VID_IsFullScreen()
{
    return vidmode_active;
}
