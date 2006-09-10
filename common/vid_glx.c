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

#define MAXWIDTH    100000
#define MAXHEIGHT   100000
#define WARP_WIDTH  320
#define WARP_HEIGHT 200

/* compatibility cludges for new menu code */
qboolean VID_CheckAdequateMem(int width, int height) { return true; }
int vid_modenum;

static int scrnum;
static GLXContext ctx = NULL;

viddef_t vid;			// global video state

unsigned short d_8to16table[256];
unsigned int d_8to24table[256];
unsigned char d_15to8table[65536];

cvar_t vid_mode = { "vid_mode", "0", false };

// FIXME - useless, or for vidmode changes?
static int win_x, win_y;

static int scr_width, scr_height;

static XF86VidModeModeInfo saved_vidmode;
static qboolean vidmode_active = false;
static XVisualInfo *x_visinfo;

/*-----------------------------------------------------------------------*/

float gldepthmin, gldepthmax;

cvar_t gl_ztrick = { "gl_ztrick", "1" };

const char *gl_vendor;
const char *gl_renderer;
const char *gl_version;
const char *gl_extensions;

qboolean gl_mtexable = false;
static int gl_num_texture_units;

/*-----------------------------------------------------------------------*/
void
D_BeginDirectRect(int x, int y, const byte *pbitmap, int width, int height)
{
// direct drawing of the "accessing disk" icon isn't supported under GLX
}

void
D_EndDirectRect(int x, int y, int width, int height)
{
// direct drawing of the "accessing disk" icon isn't supported under GLX
}

// XLateKey - Transform from X key symbols to Quake's symbols
static int
XLateKey(XKeyEvent *event)
{
    char buffer[4];
    KeySym keysym;
    knum_t key;

    memset(buffer, 0, sizeof(buffer));
    XLookupString(event, buffer, sizeof(buffer), &keysym, NULL);

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
	key = K_LSHIFT;
	break;

    case XK_Shift_R:
	key = K_RSHIFT;
	break;

    case XK_Execute:
    case XK_Control_L:
	key = K_LCTRL;
	break;

    case XK_Control_R:
	key = K_RCTRL;
	break;

    case XK_Alt_L:
	key = K_LALT;
	break;

    case XK_Meta_L:
	key = K_LMETA;
	break;

    case XK_Alt_R:
	key = K_RALT;
	break;

    case XK_Meta_R:
	key = K_RMETA;
	break;

    case XK_KP_Begin:
    case XK_KP_5:
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

    default:
	key = (unsigned char)buffer[0];
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
#ifdef USE_XF86DGA
		if (dga_mouse_active) {
		    mouse_x += event.xmotion.x_root;
		    mouse_y += event.xmotion.y_root;
		} else {
#endif
		    mouse_x = event.xmotion.x - (int)(vid.width / 2);
		    mouse_y = event.xmotion.y - (int)(vid.height / 2);

		    if (mouse_x || mouse_y)
			dowarp = true;
#ifdef USE_XF86DGA
		}
#endif
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
VID_ShiftPalette(const byte *palette)
{
//      VID_SetPalette(palette);
}

void
VID_SetPalette(const byte *palette)
{
    const byte *pal;
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
    GL_ExtensionCheck_NPoT();

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

#if 0
/* FIXME - re-enable? */
static void
Check_Gamma(byte *palette)
{
    float f, inf;
    byte newpalette[768];
    int i;

    if ((i = COM_CheckParm("-gamma")) == 0)
	vid_gamma = 1.0;
    else
	vid_gamma = Q_atof(com_argv[i + 1]);

    for (i = 0; i < 768; i++) {
	f = pow((palette[i] + 1) / 256.0, vid_gamma);
	inf = f * 255 + 0.5;
	if (inf < 0)
	    inf = 0;
	if (inf > 255)
	    inf = 255;
	newpalette[i] = inf;
    }

    memcpy(palette, newpalette, sizeof(newpalette));
}
#endif

static void
VID_InitCvars(void)
{
    Cvar_RegisterVariable(&vid_mode);
    Cvar_RegisterVariable(&gl_npot);
    Cvar_RegisterVariable(&gl_ztrick);
}

static void
VID_InitModeList(void)
{
    XF86VidModeModeInfo **xmodes, *xmode;
    qvidmode_t *mode;
    int i, numxmodes;

    nummodes = 1;
    mode = &modelist[1];

    XF86VidModeGetAllModeLines(x_disp, x_visinfo->screen, &numxmodes, &xmodes);
    xmode = *xmodes;
    for (i = 0; i < numxmodes; i++, xmode++) {
	if (nummodes == MAX_MODE_LIST)
	    break;
	if (xmode->hdisplay > MAXWIDTH || xmode->vdisplay > MAXHEIGHT)
	    continue;

	mode->modenum = nummodes;
	mode->width = xmode->hdisplay;
	mode->height = xmode->vdisplay;
	mode->bpp = x_visinfo->depth;
	mode->refresh = 1000 * xmode->dotclock / xmode->htotal / xmode->vtotal;
	nummodes++;
	mode++;
    }
    free(xmodes);

    VID_SortModeList(modelist, nummodes);
}

qboolean
VID_SetMode(const qvidmode_t *mode, const byte *palette)
{
    unsigned long valuemask;
    XSetWindowAttributes attributes;
    Window root;

    /* Free the existing structures */
    if (ctx) {
	glXDestroyContext(x_disp, ctx);
	ctx = NULL;
    }
    if (x_win) {
	XDestroyWindow(x_disp, x_win);
	x_win = 0;
    }

    root = RootWindow(x_disp, scrnum);

    /* window attributes */
    valuemask = CWBackPixel | CWColormap | CWEventMask;
    attributes.background_pixel = 0;
    attributes.colormap = XCreateColormap(x_disp, root, x_visinfo->visual, AllocNone);
    attributes.event_mask = X_CORE_MASK | X_KEY_MASK | X_MOUSE_MASK;

    if (mode != modelist) {
	/* Fullscreen */
	valuemask |= CWSaveUnder | CWBackingStore | CWOverrideRedirect;
	attributes.override_redirect = True;
	attributes.backing_store = NotUseful;
	attributes.save_under = False;
    } else {
	/* Windowed */
	valuemask |= CWBorderPixel;
	attributes.border_pixel = 0;
    }

    /* Attempt to set the vid mode if necessary */
    if (mode != modelist) {
	XF86VidModeModeInfo **xmodes, *xmode;
	int i, numxmodes, refresh;
	Bool result;

	XF86VidModeGetAllModeLines(x_disp, x_visinfo->screen, &numxmodes, &xmodes);
	xmode = *xmodes;

	for (i = 0; i < numxmodes; i++, xmode++) {
	    if (xmode->hdisplay != mode->width || xmode->vdisplay != mode->height)
		continue;
	    refresh = 1000 * xmode->dotclock / xmode->htotal / xmode->vtotal;
	    if (refresh == mode->refresh)
		break;
	}
	if (i == numxmodes)
	    Sys_Error("%s: unable to find matching X display mode", __func__);

	result = XF86VidModeSwitchToMode(x_disp, x_visinfo->screen, xmode);
	if (!result)
	    Sys_Error("%s: mode switch failed", __func__);

	free(xmodes);
    }

    x_win = XCreateWindow(x_disp, root, 0, 0, mode->width, mode->height,
			  0, x_visinfo->depth, InputOutput,
			  x_visinfo->visual, valuemask, &attributes);
    XStoreName(x_disp, x_win, "TyrQuake");
    XMapWindow(x_disp, x_win);

    if (mode != modelist) {
	XMoveWindow(x_disp, x_win, 0, 0);
	XRaiseWindow(x_disp, x_win);

	// FIXME - mouse may not be active...
	IN_CenterMouse();
	XFlush(x_disp);

	// Move the viewport to top left
	XF86VidModeSetViewPort(x_disp, scrnum, 0, 0);
    }

    XFlush(x_disp);

    ctx = glXCreateContext(x_disp, x_visinfo, NULL, True);
    glXMakeCurrent(x_disp, x_win, ctx);

    vid.width = vid.conwidth = scr_width = mode->width;
    vid.height = vid.conheight = scr_height = mode->height;
    vid.maxwarpwidth = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;
    vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);
    vid.numpages = 2;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));

    vid_modenum = mode - modelist;

    GL_Init();

    VID_SetPalette(palette);

    Con_SafePrintf("Video mode %dx%d initialized.\n", mode->width, mode->height);

    vid.recalc_refdef = 1;	// force a surface cache flush

    return true;
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
VID_Init(const byte *palette)
{
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
    int MajorVersion, MinorVersion;
    const qvidmode_t *setmode;
    qvidmode_t *mode;

    VID_InitCvars();

    x_disp = XOpenDisplay(NULL);
    if (!x_disp) {
	if (getenv("DISPLAY"))
	    Sys_Error("VID: Could not open display [%s]", getenv("DISPLAY"));
	else
	    Sys_Error("VID: Could not open local display\n");
    }
    scrnum = DefaultScreen(x_disp);

    // Check video mode extension
    MajorVersion = MinorVersion = 0;
    if (XF86VidModeQueryVersion(x_disp, &MajorVersion, &MinorVersion)) {
	Con_Printf("Using XFree86-VidModeExtension Version %i.%i\n",
		   MajorVersion, MinorVersion);
    }

    x_visinfo = glXChooseVisual(x_disp, scrnum, attrib);
    if (!x_visinfo) {
	fprintf(stderr,
		"qkHack: Error couldn't get an RGB, Double-buffered, "
		"Depth visual\n");
	exit(EXIT_FAILURE);
    }

    Gamma_Init();

    /* Init a default windowed mode */
    mode = modelist;
    mode->modenum = 0;
    mode->width = 640;
    mode->height = 480;
    mode->bpp = x_visinfo->depth;
    mode->refresh = 0;
    nummodes = 1;

    VID_InitModeList();
    setmode = VID_GetCmdlineMode();
    if (!setmode)
	setmode = &modelist[0];

    VID_SetMode(setmode, palette);

    vid_menudrawfn = VID_MenuDraw;
    vid_menukeyfn = VID_MenuKey;

    InitSig();			// trap evil signals

    sprintf(gldir, "%s/glquake", com_gamedir);
    Sys_mkdir(gldir);
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
    return vid_modenum != 0;
}
