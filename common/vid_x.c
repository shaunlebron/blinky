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
// vid_x.c -- general x video driver

#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#include <X11/extensions/XShm.h>
#include <X11/extensions/xf86vmode.h>

// FIXME - not declared in <X11/extensions/XShm.h>, should have been...
int XShmGetEventBase(Display *);

// FIXME - refactoring X11 support...
#include "x11_core.h"
#include "in_x11.h"

#include "client.h"
#include "common.h"
#include "console.h"
#include "d_local.h"
#include "keys.h"
#include "quakedef.h"
#include "sys.h"

// FIXME - header hacks
extern int scr_fullupdate;

static float old_mouse_x, old_mouse_y;
static int ignorenext;

typedef struct {
    int input;
    int output;
} keymap_t;

viddef_t vid;			// global video state
unsigned short d_8to16table[256];

cvar_t vid_mode = { "vid_mode", "0", false };

static qboolean doShm;
static Colormap x_cmap;
static GC x_gc;
static Visual *x_vis;
static XVisualInfo *x_visinfo;

static int x_shmeventtype;
static qboolean oktodraw = false;

static int current_framebuffer;
static XImage *x_framebuffer[2] = { 0, 0 };
static XShmSegmentInfo x_shminfo[2];

static int verbose = 0;

static byte current_palette[768];

static long X11_highhunkmark;
static long X11_buffersize;

static int vid_surfcachesize;
static void *vid_surfcache;

void (*vid_menudrawfn) (void);
void (*vid_menukeyfn) (int key);

typedef unsigned short PIXEL16;
typedef unsigned int PIXEL24;
static PIXEL16 st2d_8to16table[256];
static PIXEL24 st2d_8to24table[256];

static int shiftmask_fl = 0;
static int r_shift, g_shift, b_shift;
static unsigned int r_mask, g_mask, b_mask;

static XF86VidModeModeInfo saved_vidmode;
static qboolean vidmode_active;

static void
shiftmask_init()
{
    unsigned int x;

    r_mask = x_vis->red_mask;
    g_mask = x_vis->green_mask;
    b_mask = x_vis->blue_mask;
    for (r_shift = -8, x = 1; x < r_mask; x = x << 1)
	r_shift++;
    for (g_shift = -8, x = 1; x < g_mask; x = x << 1)
	g_shift++;
    for (b_shift = -8, x = 1; x < b_mask; x = x << 1)
	b_shift++;
    shiftmask_fl = 1;
}


static PIXEL16
xlib_rgb16(int r, int g, int b)
{
    PIXEL16 p;

    if (shiftmask_fl == 0)
	shiftmask_init();
    p = 0;

    if (r_shift > 0) {
	p = (r << (r_shift)) & r_mask;
    } else if (r_shift < 0) {
	p = (r >> (-r_shift)) & r_mask;
    } else
	p |= (r & r_mask);

    if (g_shift > 0) {
	p |= (g << (g_shift)) & g_mask;
    } else if (g_shift < 0) {
	p |= (g >> (-g_shift)) & g_mask;
    } else
	p |= (g & g_mask);

    if (b_shift > 0) {
	p |= (b << (b_shift)) & b_mask;
    } else if (b_shift < 0) {
	p |= (b >> (-b_shift)) & b_mask;
    } else
	p |= (b & b_mask);

    return p;
}

static PIXEL24
xlib_rgb24(int r, int g, int b)
{
    PIXEL24 p;

    if (shiftmask_fl == 0)
	shiftmask_init();
    p = 0;

    if (r_shift > 0) {
	p = (r << (r_shift)) & r_mask;
    } else if (r_shift < 0) {
	p = (r >> (-r_shift)) & r_mask;
    } else
	p |= (r & r_mask);

    if (g_shift > 0) {
	p |= (g << (g_shift)) & g_mask;
    } else if (g_shift < 0) {
	p |= (g >> (-g_shift)) & g_mask;
    } else
	p |= (g & g_mask);

    if (b_shift > 0) {
	p |= (b << (b_shift)) & b_mask;
    } else if (b_shift < 0) {
	p |= (b >> (-b_shift)) & b_mask;
    } else
	p |= (b & b_mask);

    return p;
}

static void
st2_fixup(XImage *framebuf, int x, int y, int width, int height)
{
    int yi;
    unsigned char *src;
    PIXEL16 *dest;
    register int count, n;

    if ((x < 0) || (y < 0))
	return;

    for (yi = y; yi < (y + height); yi++) {
	src = (unsigned char *)&framebuf->data[yi * framebuf->bytes_per_line];

	// Duff's Device
	count = width;
	n = (count + 7) / 8;
	dest = ((PIXEL16 *) src) + x + width - 1;
	src += x + width - 1;

	switch (count % 8) {
	case 0:
	    do {
		*dest-- = st2d_8to16table[*src--];
	case 7:
		*dest-- = st2d_8to16table[*src--];
	case 6:
		*dest-- = st2d_8to16table[*src--];
	case 5:
		*dest-- = st2d_8to16table[*src--];
	case 4:
		*dest-- = st2d_8to16table[*src--];
	case 3:
		*dest-- = st2d_8to16table[*src--];
	case 2:
		*dest-- = st2d_8to16table[*src--];
	case 1:
		*dest-- = st2d_8to16table[*src--];
	    } while (--n > 0);
	}

	//for (xi = (x + width - 1); xi >= x; xi--) {
	//    dest[xi] = st2d_8to16table[src[xi]];
	//}
    }
}

static void
st3_fixup(XImage * framebuf, int x, int y, int width, int height)
{
    int yi;
    unsigned char *src;
    PIXEL24 *dest;
    register int count, n;

    if ((x < 0) || (y < 0))
	return;

    for (yi = y; yi < (y + height); yi++) {
	src = (unsigned char *)&framebuf->data[yi * framebuf->bytes_per_line];

	// Duff's Device
	count = width;
	n = (count + 7) / 8;
	dest = ((PIXEL24 *) src) + x + width - 1;
	src += x + width - 1;

	switch (count % 8) {
	case 0:
	    do {
		*dest-- = st2d_8to24table[*src--];
	case 7:
		*dest-- = st2d_8to24table[*src--];
	case 6:
		*dest-- = st2d_8to24table[*src--];
	case 5:
		*dest-- = st2d_8to24table[*src--];
	case 4:
		*dest-- = st2d_8to24table[*src--];
	case 3:
		*dest-- = st2d_8to24table[*src--];
	case 2:
		*dest-- = st2d_8to24table[*src--];
	case 1:
		*dest-- = st2d_8to24table[*src--];
	    } while (--n > 0);
	}

//              for(xi = (x+width-1); xi >= x; xi--) {
//                      dest[xi] = st2d_8to16table[src[xi]];
//              }
    }
}


// ========================================================================
// Tragic death handler
// ========================================================================

static void
TragicDeath(int signal_num)
{
    XAutoRepeatOn(x_disp);
    XCloseDisplay(x_disp);
    Sys_Error("This death brought to you by the number %d", signal_num);
}

static void
ResetFrameBuffer(void)
{
    int mem;
    int pwidth;

    if (x_framebuffer[0]) {
	free(x_framebuffer[0]->data);
	free(x_framebuffer[0]);
    }

    if (d_pzbuffer) {
	D_FlushCaches();
	Hunk_FreeToHighMark(X11_highhunkmark);
	d_pzbuffer = NULL;
    }
    X11_highhunkmark = Hunk_HighMark();

// alloc an extra line in case we want to wrap, and allocate the z-buffer
    X11_buffersize = vid.width * vid.height * sizeof(*d_pzbuffer);

    vid_surfcachesize = D_SurfaceCacheForRes(vid.width, vid.height);

    X11_buffersize += vid_surfcachesize;

    d_pzbuffer = Hunk_HighAllocName(X11_buffersize, "video");
    if (d_pzbuffer == NULL)
	Sys_Error("Not enough memory for video mode");

    vid_surfcache = (byte *)d_pzbuffer
	+ vid.width * vid.height * sizeof(*d_pzbuffer);

    D_InitCaches(vid_surfcache, vid_surfcachesize);

    pwidth = x_visinfo->depth / 8;
    if (pwidth == 3)
	pwidth = 4;
    mem = ((vid.width * pwidth + 7) & ~7) * vid.height;

    x_framebuffer[0] = XCreateImage(x_disp,
				    x_vis,
				    x_visinfo->depth,
				    ZPixmap,
				    0,
				    malloc(mem),
				    vid.width, vid.height, 32, 0);

    if (!x_framebuffer[0])
	Sys_Error("VID: XCreateImage failed");

    vid.buffer = (byte *)(x_framebuffer[0]);
    vid.conbuffer = vid.buffer;

}

static void
ResetSharedFrameBuffers(void)
{

    int size;
    int key;
    int minsize = getpagesize();
    int frm;

    if (d_pzbuffer) {
	D_FlushCaches();
	Hunk_FreeToHighMark(X11_highhunkmark);
	d_pzbuffer = NULL;
    }

    X11_highhunkmark = Hunk_HighMark();

// alloc an extra line in case we want to wrap, and allocate the z-buffer
    X11_buffersize = vid.width * vid.height * sizeof(*d_pzbuffer);

    vid_surfcachesize = D_SurfaceCacheForRes(vid.width, vid.height);

    X11_buffersize += vid_surfcachesize;

    d_pzbuffer = Hunk_HighAllocName(X11_buffersize, "video");
    if (!d_pzbuffer)
	Sys_Error("Not enough memory for video mode");

    vid_surfcache = (byte *)d_pzbuffer
	+ vid.width * vid.height * sizeof(*d_pzbuffer);

    D_InitCaches(vid_surfcache, vid_surfcachesize);

    for (frm = 0; frm < 2; frm++) {

	// free up old frame buffer memory
	if (x_framebuffer[frm]) {
	    XShmDetach(x_disp, &x_shminfo[frm]);
	    free(x_framebuffer[frm]);
	    shmdt(x_shminfo[frm].shmaddr);
	}

	// create the image
	x_framebuffer[frm] = XShmCreateImage(x_disp,
					     x_vis,
					     x_visinfo->depth,
					     ZPixmap,
					     0,
					     &x_shminfo[frm],
					     vid.width, vid.height);

	// grab shared memory
	size = x_framebuffer[frm]->bytes_per_line
	    * x_framebuffer[frm]->height;
	if (size < minsize)
	    Sys_Error("VID: Window must use at least %d bytes", minsize);

	key = random();
	x_shminfo[frm].shmid = shmget((key_t) key, size, IPC_CREAT | 0777);
	if (x_shminfo[frm].shmid == -1)
	    Sys_Error("VID: Could not get any shared memory");

	// attach to the shared memory segment
	x_shminfo[frm].shmaddr = (void *)shmat(x_shminfo[frm].shmid, 0, 0);

	printf("VID: shared memory id=%d, addr=0x%lx\n",
	       x_shminfo[frm].shmid, (long)x_shminfo[frm].shmaddr);

	x_framebuffer[frm]->data = x_shminfo[frm].shmaddr;

	// get the X server to attach to it
	if (!XShmAttach(x_disp, &x_shminfo[frm]))
	    Sys_Error("VID: XShmAttach() failed");
	XSync(x_disp, 0);
	shmctl(x_shminfo[frm].shmid, IPC_RMID, 0);
    }
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

    XF86VidModeGetAllModeLines(x_disp, x_visinfo->screen, &num_modes, &modes);
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
	mode_changed = XF86VidModeSwitchToMode(x_disp, x_visinfo->screen,
					       mode);
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
	XF86VidModeSwitchToMode(x_disp, x_visinfo->screen, &saved_vidmode);
	if (saved_vidmode.privsize && saved_vidmode.private)
	    XFree(saved_vidmode.private);
    }
}


// Called at startup to set up translation tables, takes 256 8 bit RGB values
// the palette data will go away after the call, so it must be copied off if
// the video driver will need it again
void
VID_Init(unsigned char *palette)
{
    int pnum, i;
    XVisualInfo template;
    int num_visuals;
    int template_mask;
    XSetWindowAttributes attr;
    unsigned long mask;
    qboolean fullscreen = true;
    qboolean vidmode_ext = false;
    int MajorVersion, MinorVersion;
    Window root;

    ignorenext = 0;		// FIXME - what's this for??
    vid.width = 320;
    vid.height = 200;
    vid.maxwarpwidth = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;
    vid.numpages = 2;
    vid.colormap = host_colormap;
    //   vid.cbits = VID_CBITS;
    //   vid.grades = VID_GRADES;
    vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));

    srandom(getpid());

    verbose = COM_CheckParm("-verbose");

// open the display
    x_disp = XOpenDisplay(NULL);
    if (x_disp == NULL) {
	if (getenv("DISPLAY"))
	    Sys_Error("VID: Could not open display [%s]", getenv("DISPLAY"));
	else
	    Sys_Error("VID: Could not open local display");
    }

    // Check video mode extension
    MajorVersion = MinorVersion = 0;
    if (XF86VidModeQueryVersion(x_disp, &MajorVersion, &MinorVersion)) {
	Con_Printf("Using XFree86-VidModeExtension Version %i.%i\n",
		   MajorVersion, MinorVersion);
	vidmode_ext = true;
    }

    // catch signals so i can turn on auto-repeat
    {
	struct sigaction sa;

	sigaction(SIGINT, 0, &sa);
	sa.sa_handler = TragicDeath;
	sigaction(SIGINT, &sa, 0);
	sigaction(SIGTERM, &sa, 0);
    }

    XAutoRepeatOff(x_disp);

// for debugging only
    XSynchronize(x_disp, True);

    if (COM_CheckParm("-window") || COM_CheckParm("-w"))
	fullscreen = false;

// check for command-line window size
    if ((pnum = COM_CheckParm("-winsize"))) {
	if (pnum >= com_argc - 2)
	    Sys_Error("VID: -winsize <width> <height>");
	vid.width = Q_atoi(com_argv[pnum + 1]);
	vid.height = Q_atoi(com_argv[pnum + 2]);
	if (!vid.width || !vid.height)
	    Sys_Error("VID: Bad window width/height");
    }
    if ((pnum = COM_CheckParm("-width"))) {
	if (pnum >= com_argc - 1)
	    Sys_Error("VID: -width <width>");
	vid.width = Q_atoi(com_argv[pnum + 1]);
	if (!vid.width)
	    Sys_Error("VID: Bad window width");
    }
    if ((pnum = COM_CheckParm("-height"))) {
	if (pnum >= com_argc - 1)
	    Sys_Error("VID: -height <height>");
	vid.height = Q_atoi(com_argv[pnum + 1]);
	if (!vid.height)
	    Sys_Error("VID: Bad window height");
    }

    template_mask = 0;

// specify a visual id
    if ((pnum = COM_CheckParm("-visualid"))) {
	if (pnum >= com_argc - 1)
	    Sys_Error("VID: -visualid <id#>");
	template.visualid = Q_atoi(com_argv[pnum + 1]);
	template_mask = VisualIDMask;
    }
// If not specified, use default visual
    else {
	int screen;

	screen = DefaultScreen(x_disp);
	template.visualid =
	    XVisualIDFromVisual(XDefaultVisual(x_disp, screen));
	template_mask = VisualIDMask;
    }

// pick a visual- warn if more than one was available
    x_visinfo =
	XGetVisualInfo(x_disp, template_mask, &template, &num_visuals);
    if (num_visuals > 1) {
	printf("Found more than one visual id at depth %d:\n",
	       template.depth);
	for (i = 0; i < num_visuals; i++)
	    printf("	-visualid %d\n", (int)(x_visinfo[i].visualid));
    } else if (num_visuals == 0) {
	if (template_mask == VisualIDMask)
	    Sys_Error("VID: Bad visual id %lu",
		      (unsigned long)template.visualid);
	else
	    Sys_Error("VID: No visuals at depth %u", template.depth);
    }

    if (verbose) {
	printf("Using visualid %d:\n", (int)(x_visinfo->visualid));
	printf("	screen %d\n", x_visinfo->screen);
	printf("	red_mask 0x%x\n", (int)(x_visinfo->red_mask));
	printf("	green_mask 0x%x\n", (int)(x_visinfo->green_mask));
	printf("	blue_mask 0x%x\n", (int)(x_visinfo->blue_mask));
	printf("	colormap_size %d\n", x_visinfo->colormap_size);
	printf("	bits_per_rgb %d\n", x_visinfo->bits_per_rgb);
    }

    x_vis = x_visinfo->visual;
    root = XRootWindow(x_disp, x_visinfo->screen);

    /* Attempt to set the vidmode if needed */
    if (vidmode_ext && fullscreen)
	fullscreen = VID_set_vidmode(vid.width, vid.height);

    /* window attributes */
    mask = CWEventMask | CWColormap | CWBackPixel;
    attr.background_pixel = 0;
    attr.colormap = XCreateColormap(x_disp, root, x_vis, AllocNone);
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

// create the main window
    x_win = XCreateWindow(x_disp, XRootWindow(x_disp, x_visinfo->screen),
			  0, 0,	// x, y
			  vid.width, vid.height, 0,	// borderwidth
			  x_visinfo->depth,
			  InputOutput, x_vis, mask, &attr);
    XStoreName(x_disp, x_win, "xquake");

    //if (x_visinfo->class != TrueColor)
    //XFreeColormap(x_disp, tmpcmap);

    if (x_visinfo->depth == 8) {
	// create and upload the palette
	if (x_visinfo->class == PseudoColor) {
	    x_cmap = XCreateColormap(x_disp, x_win, x_vis, AllocAll);
	    VID_SetPalette(palette);
	    XSetWindowColormap(x_disp, x_win, x_cmap);
	}
    }

// create the GC
    {
	XGCValues xgcvalues;
	int valuemask = GCGraphicsExposures;

	xgcvalues.graphics_exposures = False;
	x_gc = XCreateGC(x_disp, x_win, valuemask, &xgcvalues);
    }

// map the window
    XMapWindow(x_disp, x_win);

    if (vidmode_active) {
	XMoveWindow(x_disp, x_win, 0, 0);
	XRaiseWindow(x_disp, x_win);

	// FIXME - mouse may not be active...
	IN_CenterMouse();
	XFlush(x_disp);

	XF86VidModeSetViewPort(x_disp, x_visinfo->screen, 0, 0);
    }

// wait for first exposure event
    {
	XEvent event;

	do {
	    XNextEvent(x_disp, &event);
	    if (event.type == Expose && !event.xexpose.count)
		oktodraw = true;
	} while (!oktodraw);
    }
// now safe to draw

// even if MITSHM is available, make sure it's a local connection
    if (XShmQueryExtension(x_disp)) {
	char *displayname;

	doShm = true;
	displayname = (char *)getenv("DISPLAY");
	if (displayname) {
	    char *d = displayname;

	    while (*d && (*d != ':'))
		d++;
	    if (*d)
		*d = 0;
	    if (!(!strcasecmp(displayname, "unix") || !*displayname))
		doShm = false;
	}
    }

    if (doShm) {
	x_shmeventtype = XShmGetEventBase(x_disp) + ShmCompletion;
	ResetSharedFrameBuffers();
    } else
	ResetFrameBuffer();

    current_framebuffer = 0;
    vid.rowbytes = x_framebuffer[0]->bytes_per_line;
    vid.buffer = (byte *)x_framebuffer[0]->data;
    vid.direct = 0;
    vid.conbuffer = vid.buffer;
    vid.conrowbytes = vid.rowbytes;
    vid.conwidth = vid.width;
    vid.conheight = vid.height;
    vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);

#if 0
    /*
     * FIXME - make this configurable; e.g. this makes the aspect ratio look
     *         correct on my non-standard (widescreen) laptop screen
     */
    vid.aspect = ((float)vid.height / (float)vid.width) * (1280.0 / 600.0);
#endif

//      XSynchronize(x_disp, False);
}

void
VID_ShiftPalette(unsigned char *p)
{
    VID_SetPalette(p);
}



void
VID_SetPalette(unsigned char *palette)
{

    int i;
    XColor colors[256];

    for (i = 0; i < 256; i++) {
	st2d_8to16table[i] =
	    xlib_rgb16(palette[i * 3], palette[i * 3 + 1],
		       palette[i * 3 + 2]);
	st2d_8to24table[i] =
	    xlib_rgb24(palette[i * 3], palette[i * 3 + 1],
		       palette[i * 3 + 2]);
    }

    if (x_visinfo->class == PseudoColor && x_visinfo->depth == 8) {
	if (palette != current_palette)
	    memcpy(current_palette, palette, 768);
	for (i = 0; i < 256; i++) {
	    colors[i].pixel = i;
	    colors[i].flags = DoRed | DoGreen | DoBlue;
	    colors[i].red = palette[i * 3] * 257;
	    colors[i].green = palette[i * 3 + 1] * 257;
	    colors[i].blue = palette[i * 3 + 2] * 257;
	}
	XStoreColors(x_disp, x_cmap, colors, 256);
    }

}

// Called at shutdown

void
VID_Shutdown(void)
{
    Con_Printf("VID_Shutdown\n");
    VID_restore_vidmode();
    XAutoRepeatOn(x_disp);
    XCloseDisplay(x_disp);
}

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
	// FIXME - what keys are these???
	key = K_AUX30;
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

static int config_notify = 0;
static int config_notify_width;
static int config_notify_height;

static void
HandleEvents(void)
{
    XEvent x_event;
    qboolean dowarp = false;

    if (!x_disp)
	return;

    while (XPending(x_disp)) {
	XNextEvent(x_disp, &x_event);

	switch (x_event.type) {
	case KeyPress:
	case KeyRelease:
	    Key_Event(XLateKey(&x_event.xkey), x_event.type == KeyPress);
	    break;

	case MotionNotify:
	    if (mouse_grab_active) {
		if (dga_mouse_active) {
		    mouse_x += x_event.xmotion.x_root;
		    mouse_y += x_event.xmotion.y_root;
		} else {
		    mouse_x = x_event.xmotion.x - (int)(vid.width / 2);
		    mouse_y = x_event.xmotion.y - (int)(vid.height / 2);

		    if (mouse_x || mouse_y)
			dowarp = true;
		}
	    }
	    break;

	case ButtonPress:
	    if (x_event.xbutton.button == 1)
		Key_Event(K_MOUSE1, true);
	    else if (x_event.xbutton.button == 2)
		Key_Event(K_MOUSE3, true);
	    else if (x_event.xbutton.button == 3)
		Key_Event(K_MOUSE2, true);
	    else if (x_event.xbutton.button == 4)
		Key_Event(K_MWHEELUP, true);
	    else if (x_event.xbutton.button == 5)
		Key_Event(K_MWHEELDOWN, true);
	    else if (x_event.xbutton.button == 6)
		Key_Event(K_MOUSE4, true);
	    else if (x_event.xbutton.button == 7)
		Key_Event(K_MOUSE5, true);
	    else if (x_event.xbutton.button == 8)
		Key_Event(K_MOUSE6, true);
	    else if (x_event.xbutton.button == 9)
		Key_Event(K_MOUSE7, true);
	    else if (x_event.xbutton.button == 10)
		Key_Event(K_MOUSE8, true);
	    break;

	case ButtonRelease:
	    if (x_event.xbutton.button == 1)
		Key_Event(K_MOUSE1, false);
	    else if (x_event.xbutton.button == 2)
		Key_Event(K_MOUSE3, false);
	    else if (x_event.xbutton.button == 3)
		Key_Event(K_MOUSE2, false);
	    else if (x_event.xbutton.button == 4)
		Key_Event(K_MWHEELUP, false);
	    else if (x_event.xbutton.button == 5)
		Key_Event(K_MWHEELDOWN, false);
	    else if (x_event.xbutton.button == 6)
		Key_Event(K_MOUSE4, false);
	    else if (x_event.xbutton.button == 7)
		Key_Event(K_MOUSE5, false);
	    else if (x_event.xbutton.button == 8)
		Key_Event(K_MOUSE6, false);
	    else if (x_event.xbutton.button == 9)
		Key_Event(K_MOUSE7, false);
	    else if (x_event.xbutton.button == 10)
		Key_Event(K_MOUSE8, false);
	    break;

	case ConfigureNotify:
	    config_notify_width = x_event.xconfigure.width;
	    config_notify_height = x_event.xconfigure.height;
	    config_notify = 1;
	    break;

	default:
	    if (doShm && x_event.type == x_shmeventtype)
		oktodraw = true;
	}
    }

    if (dowarp)
	IN_CenterMouse();
}

// flushes the given rectangles from the view buffer to the screen

void
VID_Update(vrect_t *rects)
{
// if the window changes dimension, skip this frame

    if (config_notify) {
	fprintf(stderr, "config notify\n");
	config_notify = 0;
	vid.width = config_notify_width & ~7;
	vid.height = config_notify_height;
	if (doShm)
	    ResetSharedFrameBuffers();
	else
	    ResetFrameBuffer();
	vid.rowbytes = x_framebuffer[0]->bytes_per_line;
	vid.buffer = (byte *)x_framebuffer[current_framebuffer]->data;
	vid.conbuffer = vid.buffer;
	vid.conwidth = vid.width;
	vid.conheight = vid.height;
	vid.conrowbytes = vid.rowbytes;
	vid.recalc_refdef = 1;	// force a surface cache flush
	Con_CheckResize();
	Con_Clear_f();
	return;
    }
    // force full update if not 8bit
    if (x_visinfo->depth != 8)
	scr_fullupdate = 0;

    if (doShm) {

	while (rects) {
	    if (x_visinfo->depth == 16) {
		st2_fixup(x_framebuffer[current_framebuffer],
			  rects->x, rects->y, rects->width, rects->height);
	    } else if (x_visinfo->depth == 24) {
		st3_fixup(x_framebuffer[current_framebuffer],
			  rects->x, rects->y, rects->width, rects->height);
	    }
	    if (!XShmPutImage(x_disp, x_win, x_gc,
			      x_framebuffer[current_framebuffer], rects->x,
			      rects->y, rects->x, rects->y, rects->width,
			      rects->height, True))
		Sys_Error("VID_Update: XShmPutImage failed");
	    oktodraw = false;
	    while (!oktodraw)
		HandleEvents();
	    rects = rects->pnext;
	}
	current_framebuffer = !current_framebuffer;
	vid.buffer = (byte *)x_framebuffer[current_framebuffer]->data;
	vid.conbuffer = vid.buffer;
	XSync(x_disp, False);
    } else {
	while (rects) {
	    if (x_visinfo->depth == 16)
		st2_fixup(x_framebuffer[current_framebuffer],
			  rects->x, rects->y, rects->width, rects->height);
	    else if (x_visinfo->depth == 24)
		st3_fixup(x_framebuffer[current_framebuffer],
			  rects->x, rects->y, rects->width, rects->height);
	    XPutImage(x_disp, x_win, x_gc, x_framebuffer[0], rects->x,
		      rects->y, rects->x, rects->y, rects->width,
		      rects->height);
	    rects = rects->pnext;
	}
	XSync(x_disp, False);
    }

}

#if 0
/* FIXME - Dither functions not used? */
static int  dither;

static void
VID_DitherOn (void)
{
    if (dither == 0) {
	vid.recalc_refdef = 1;
	dither = 1;
    }
}

static void
VID_DitherOff (void)
{
    if (dither) {
	vid.recalc_refdef = 1;
	dither = 0;
    }
}

/* FIXME - some unused Sys_ functions? */
static int
Sys_OpenWindow (void)
{
    return 0;
}

static void
Sys_EraseWindow (int window)
{
}

static void
Sys_DrawCircle (int window, int x, int y, int r)
{
}

static void
Sys_DisplayWindow (int window)
{
}
#endif

void
Sys_SendKeyEvents(void)
{
    HandleEvents();
}

#if 0
/* FIXME - ever going to need this? */
char *
Sys_ConsoleInput(void)
{

    static char text[256];
    int len;
    fd_set readfds;
    int ready;
    struct timeval timeout;

    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    FD_ZERO(&readfds);
    FD_SET(0, &readfds);
    ready = select(1, &readfds, 0, 0, &timeout);

    if (ready > 0) {
	len = read(0, text, sizeof(text));
	if (len >= 1) {
	    text[len - 1] = 0;	/* rip off the /n and terminate */
	    return text;
	}
    }

    return 0;
}
#endif

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

void
IN_Commands(void)
{
    if (!mouse_available)
	return;

    // FIXME - Need this consistant, robust

    // If we have the mouse, but are not in the game...
    if (mouse_grab_active && key_dest != key_game && !vidmode_active) {
	IN_UngrabMouse();
	IN_UngrabKeyboard();
    }
    // If we don't have the mouse, but we're in the game and we want it...
    if (!mouse_grab_active && key_dest == key_game &&
	(_windowed_mouse.value || vidmode_active)) {
	IN_GrabKeyboard();
	IN_GrabMouse();
	IN_CenterMouse();
    }
}

// FIXME - is this target independent?
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
    mouse_x = mouse_y = 0.0;
}

void
IN_Move(usercmd_t *cmd)
{
    IN_MouseMove(cmd);
}

void
VID_LockBuffer(void)
{
}

void
VID_UnlockBuffer(void)
{
}

qboolean
VID_IsFullScreen()
{
    return vidmode_active;
}
