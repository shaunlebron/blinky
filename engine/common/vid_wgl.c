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

#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>

#include "cdaudio.h"
#include "cmd.h"
#include "console.h"
#include "draw.h"
#include "glquake.h"
#include "input.h"
#include "keys.h"
#include "menu.h"
#include "quakedef.h"
#include "resource.h"
#include "sbar.h"
#include "screen.h"
#include "sound.h"
#include "sys.h"
#include "vid.h"
#include "wad.h"
#include "winquake.h"

#ifdef NQ_HACK
#include "host.h"
#endif

#define WARP_WIDTH	320
#define WARP_HEIGHT	200
#define MAXWIDTH	10000
#define MAXHEIGHT	10000
#define BASEWIDTH	320
#define BASEHEIGHT	200

qboolean VID_CheckAdequateMem(int width, int height) { return true; }

static qboolean Minimized;

qboolean
window_visible(void)
{
    return !Minimized;
}

HWND mainwindow;
qboolean DDActive;
viddef_t vid; /* global video state */
int vid_modenum = VID_MODE_NONE;

static HICON hIcon;
static RECT WindowRect;
static DEVMODE gdevmode;
static qboolean vid_initialized = false;
static int vid_default = VID_MODE_WINDOWED;
static modestate_t modestate = MS_UNINIT;

static HDC maindc;

static HGLRC baseRC;
static RECT GL_WindowRect;
static qboolean vid_canalttab = false;
static qboolean vid_wassuspended = false;
static int windowed_mouse;

const char *gl_renderer;
const char *gl_extensions;
static const char *gl_vendor;
static const char *gl_version;
static int gl_num_texture_units;

static qboolean fullsbardraw = false;

static float vid_gamma = 1.0;

static cvar_t vid_mode = { "vid_mode", "0", false };
static cvar_t _vid_default_mode = { "_vid_default_mode", "0", true };
static cvar_t _vid_default_mode_win = { "_vid_default_mode_win", "0", true };
static cvar_t vid_wait = { "vid_wait", "0" };
static cvar_t vid_nopageflip = { "vid_nopageflip", "0", true };
static cvar_t _vid_wait_override = { "_vid_wait_override", "0", true };
static cvar_t vid_config_x = { "vid_config_x", "800", true };
static cvar_t vid_config_y = { "vid_config_y", "600", true };
static cvar_t vid_stretch_by_2 = { "vid_stretch_by_2", "1", true };

cvar_t gl_ztrick = { "gl_ztrick", "1" };

unsigned short d_8to16table[256];
unsigned d_8to24table[256];
byte d_15to8table[65536];

float gldepthmin, gldepthmax;

static void AppActivate(BOOL fActive, BOOL minimize);
static LONG WINAPI MainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam,
			       LPARAM lParam);

static void ClearAllStates(void);
static void GL_Init(void);

// FIXME - Shouldn't use the exact names from the library?
#ifndef GL_VERSION_1_2
static PROC glArrayElementEXT;
static PROC glColorPointerEXT;
static PROC glTexCoordPointerEXT;
static PROC glVertexPointerEXT;
#endif

qboolean gl_mtexable = false;

static BOOL bSetupPixelFormat(HDC hDC);

static void
VID_CenterWindow(HWND window)
{
    RECT workarea, rect;
    BOOL ret;
    UINT flags;
    int x, y, wa_width, wr_width, wa_height, wr_height;

    ret = SystemParametersInfo(SPI_GETWORKAREA, 0, &workarea, 0);
    if (!ret)
	Sys_Error("%s: SPI_GETWORKAREA failed", __func__);
    ret = GetWindowRect(window, &rect);
    if (!ret)
	Sys_Error("%s: GetWindowRect() failed", __func__);

    wa_width = workarea.right - workarea.left;
    wa_height = workarea.bottom - workarea.top;
    wr_width = rect.right - rect.left;
    wr_height = rect.bottom - rect.top;

    /* Center within the workarea. If too large, justify top left. */
    x = qmax(workarea.left + (wa_width - wr_width) / 2, 0L);
    y = qmax(workarea.top + (wa_height - wr_height) / 2, 0L);

    flags = SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_DRAWFRAME;
    SetWindowPos(window, NULL, x, y, 0, 0, flags);

    /* Update the input rect */
    GetWindowRect(window, &WindowRect);
    wr_width = WindowRect.right - WindowRect.left;
    wr_height = WindowRect.bottom - WindowRect.top;
    IN_UpdateWindowRect(WindowRect.left, WindowRect.top, wr_width, wr_height);
}

static void
VID_DestroyWindow(void)
{
    HGLRC hrc;
    HDC hdc;

    hrc = wglGetCurrentContext();
    hdc = wglGetCurrentDC();
    wglMakeCurrent(NULL, NULL);

    if (hdc && mainwindow)
	ReleaseDC(mainwindow, hdc);
    if (modestate == MS_FULLSCREEN)
	ChangeDisplaySettings(NULL, 0);
    if (maindc && mainwindow)
	ReleaseDC(mainwindow, maindc);
    maindc = NULL;

    if (mainwindow)
	DestroyWindow(mainwindow);
    mainwindow = NULL;

    wglDeleteContext(hrc);
}

/*
 * VID_SetDisplayMode
 * Pass NULL to restore desktop resolution
 */
static void
VID_SetDisplayMode(const qvidmode_t *mode)
{
    LONG result;

    if (!mode) {
	if (modestate == MS_FULLSCREEN)
	    ChangeDisplaySettings(NULL, CDS_FULLSCREEN);
	modestate = MS_WINDOWED;
	return;
    }

    gdevmode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    gdevmode.dmFields |= DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
    gdevmode.dmPelsWidth = mode->width;
    gdevmode.dmPelsHeight = mode->height;
    gdevmode.dmBitsPerPel = mode->bpp;
    gdevmode.dmDisplayFrequency = mode->refresh;
    gdevmode.dmSize = sizeof(gdevmode);

    result = ChangeDisplaySettings(&gdevmode, CDS_FULLSCREEN);
    if (result != DISP_CHANGE_SUCCESSFUL)
	Sys_Error("Couldn't set fullscreen DIB mode");

    modestate = MS_FULLSCREEN;
    //vid_fulldib_on_focus_mode = mode - modelist;
}

static qboolean
VID_SetWindowedMode(const qvidmode_t *mode)
{
    HDC hdc;
    DWORD WindowStyle, ExWindowStyle;

    VID_DestroyWindow();
    VID_SetDisplayMode(NULL);

    WindowRect.top = WindowRect.left = 0;
    WindowRect.right = mode->width;
    WindowRect.bottom = mode->height;

    WindowStyle =
	WS_OVERLAPPED | WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    ExWindowStyle = 0;

    GL_WindowRect = WindowRect;
    AdjustWindowRectEx(&WindowRect, WindowStyle, FALSE, 0);

    // Create the DIB window
    mainwindow = CreateWindowEx(ExWindowStyle,
			       "TyrQuake",
			       "TyrQuake",
			       WindowStyle,
			       WindowRect.left, WindowRect.top,
			       WindowRect.right - WindowRect.left,
			       WindowRect.bottom - WindowRect.top,
			       NULL, NULL, global_hInstance, NULL);

    if (!mainwindow)
	Sys_Error("Couldn't create window");

    /* Center and show the window */
    VID_CenterWindow(mainwindow);
    ShowWindow(mainwindow, SW_SHOWDEFAULT);
    UpdateWindow(mainwindow);

    /*
     * because we have set the background brush for the window to NULL
     * (to avoid flickering when re-sizing the window on the desktop),
     * we clear the window to black when created, otherwise it will be
     * empty while Quake starts up.
     */
    hdc = GetDC(mainwindow);
    PatBlt(hdc, 0, 0, WindowRect.right, WindowRect.bottom, BLACKNESS);
    ReleaseDC(mainwindow, hdc);

    if (vid.conheight > mode->height)
	vid.conheight = mode->height;
    if (vid.conwidth > mode->width)
	vid.conwidth = mode->width;
    vid.width = vid.conwidth;
    vid.height = vid.conheight;

    vid.numpages = 2;
    mainwindow = mainwindow;

    SendMessage(mainwindow, WM_SETICON, (WPARAM)ICON_BIG, (LPARAM)hIcon);
    SendMessage(mainwindow, WM_SETICON, (WPARAM)ICON_SMALL, (LPARAM)hIcon);

    maindc = GetDC(mainwindow);
    bSetupPixelFormat(maindc);

    baseRC = wglCreateContext(maindc);
    if (!baseRC)
	Sys_Error("Could not initialize GL (wglCreateContext failed).\n\n"
		  "Make sure you in are 65535 color mode, "
		  "and try running -window.");
    if (!wglMakeCurrent(maindc, baseRC))
	Sys_Error("wglMakeCurrent failed");

    GL_Init();

    return true;
}

static qboolean
VID_SetFullDIBMode(const qvidmode_t *mode)
{
    HDC hdc;
    DWORD WindowStyle, ExWindowStyle;

    VID_DestroyWindow();
    VID_SetDisplayMode(mode);

    WindowRect.top = WindowRect.left = 0;
    WindowRect.right = mode->width;
    WindowRect.bottom = mode->height;

    WindowStyle = WS_POPUP;
    ExWindowStyle = 0;

    GL_WindowRect = WindowRect;
    AdjustWindowRectEx(&WindowRect, WindowStyle, FALSE, 0);

    // Create the DIB window
    mainwindow = CreateWindowEx(ExWindowStyle,
			       "TyrQuake",
			       "TyrQuake",
			       WindowStyle,
			       WindowRect.left, WindowRect.top,
			       WindowRect.right - WindowRect.left,
			       WindowRect.bottom - WindowRect.top,
			       NULL, NULL, global_hInstance, NULL);

    if (!mainwindow)
	Sys_Error("Couldn't create DIB window");

    SetWindowLong(mainwindow, GWL_STYLE, WindowStyle | WS_VISIBLE);
    SetWindowLong(mainwindow, GWL_EXSTYLE, ExWindowStyle);

    /* Raise to top and show the DIB window */
    //SetWindowPos(mainwindow, HWND_TOPMOST, 0, 0, 0, 0,
    //SWP_NOSIZE | SWP_SHOWWINDOW | SWP_DRAWFRAME);
    ShowWindow(mainwindow, SW_SHOWDEFAULT);
    UpdateWindow(mainwindow);

    /*
     * Because we have set the background brush for the window to NULL
     * (to avoid flickering when re-sizing the window on the desktop), we
     * clear the window to black when created, otherwise it will be
     * empty while Quake starts up.
     */
    hdc = GetDC(mainwindow);
    PatBlt(hdc, 0, 0, WindowRect.right, WindowRect.bottom, BLACKNESS);
    ReleaseDC(mainwindow, hdc);

    if (vid.conheight > mode->height)
	vid.conheight = mode->height;
    if (vid.conwidth > mode->width)
	vid.conwidth = mode->width;
    vid.width = vid.conwidth;
    vid.height = vid.conheight;

    vid.numpages = 2;
    mainwindow = mainwindow;

    SendMessage(mainwindow, WM_SETICON, (WPARAM)ICON_BIG, (LPARAM)hIcon);
    SendMessage(mainwindow, WM_SETICON, (WPARAM)ICON_SMALL, (LPARAM)hIcon);

    maindc = GetDC(mainwindow);
    bSetupPixelFormat(maindc);

    baseRC = wglCreateContext(maindc);
    if (!baseRC)
	Sys_Error("Could not initialize GL (wglCreateContext failed).\n\n"
		  "Make sure you in are 65535 color mode, "
		  "and try running -window.");
    if (!wglMakeCurrent(maindc, baseRC))
	Sys_Error("wglMakeCurrent failed");

    GL_Init();

    return true;
}

qboolean
VID_SetMode(const qvidmode_t *mode, const byte *palette)
{
    qboolean scr_disabled_for_loading_save;
    int modenum;
    qboolean stat;
    MSG msg;

    modenum = mode - modelist;
    if (modenum < 0 ||  modenum >= nummodes)
	Sys_Error("Bad video mode");

    /* so Con_Printfs don't mess us up by forcing vid and snd updates */
    scr_disabled_for_loading_save = scr_disabled_for_loading;
    scr_disabled_for_loading = true;

    CDAudio_Pause();

    // Set either the fullscreen or windowed mode
    stat = false;
    if (mode != modelist) {
	stat = VID_SetFullDIBMode(mode);
	IN_UpdateWindowRect(0, 0, mode->width, mode->height);
	IN_ActivateMouse();
	IN_HideMouse();
    } else {
	if (_windowed_mouse.value && key_dest == key_game) {
	    stat = VID_SetWindowedMode(mode);
	    IN_UpdateWindowRect(WindowRect.left, WindowRect.top, mode->width, mode->height);
	    IN_ActivateMouse();
	    IN_HideMouse();
	} else {
	    IN_DeactivateMouse();
	    IN_ShowMouse();
	    stat = VID_SetWindowedMode(mode);
	    IN_UpdateWindowRect(WindowRect.left, WindowRect.top, mode->width, mode->height);
	}
    }

    CDAudio_Resume();
    scr_disabled_for_loading = scr_disabled_for_loading_save;

    if (!stat)
	Sys_Error("Couldn't set video mode");

    /*
     * now we try to make sure we get the focus on the mode switch, because
     * sometimes in some systems we don't.  We grab the foreground, then
     * finish setting up, pump all our messages, and sleep for a little while
     * to let messages finish bouncing around the system, then we put
     * ourselves at the top of the z order, then grab the foreground again,
     * Who knows if it helps, but it probably doesn't hurt
     */
    SetForegroundWindow(mainwindow);

    VID_SetPalette(palette);
    vid_modenum = modenum;
    Cvar_SetValue("vid_mode", (float)vid_modenum);

    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
	TranslateMessage(&msg);
	DispatchMessage(&msg);
    }

    Sleep(100);

    SetWindowPos(mainwindow, HWND_TOP, 0, 0, 0, 0,
		 SWP_DRAWFRAME | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW |
		 SWP_NOCOPYBITS);

    SetForegroundWindow(mainwindow);

// fix the leftover Alt from any Alt-Tab or the like that switched us away
    ClearAllStates();

    VID_SetPalette(palette);

    vid.recalc_refdef = 1;

    return true;
}

static void
CheckMultiTextureExtensions(void)
{
    // FIXME - Do proper substring testing (could be last extension, no space)
    // FIXME - Check for wglGetProcAddress errors...
    gl_mtexable = false;
    if (COM_CheckParm("-nomtex"))
	return;
    if (!strstr(gl_extensions, "GL_ARB_multitexture "))
	return;

    Con_Printf("ARB multitexture extensions found.\n");
    qglMultiTexCoord2fARB = (void *)wglGetProcAddress("glMultiTexCoord2fARB");
    qglActiveTextureARB = (void *)wglGetProcAddress("glActiveTextureARB");

    /* Check how many texture units there actually are */
    glGetIntegerv(GL_MAX_TEXTURE_UNITS, &gl_num_texture_units);

    if (gl_num_texture_units < 2) {
	Con_Printf("Only %i texture units, multitexture disabled.\n",
		   gl_num_texture_units);
	return;
    }
    if (!qglMultiTexCoord2fARB || !qglActiveTextureARB) {
	Con_Printf("ARB Multitexture symbols not found, disabled.\n");
	return;
    }

    Con_Printf("ARB multitexture extension enabled\n"
	       "-> %i texture units available\n",
	       gl_num_texture_units);
    gl_mtexable = true;
}

/*
===============
GL_Init
===============
*/
static void
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

    //glClearColor(1, 0, 0, 0);
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

//      glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
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
    *width = GL_WindowRect.right - GL_WindowRect.left;
    *height = GL_WindowRect.bottom - GL_WindowRect.top;

//    if (!wglMakeCurrent( maindc, baseRC ))
//              Sys_Error("wglMakeCurrent failed");

//      glViewport(*x, *y, *width, *height);
}


void
GL_EndRendering(void)
{
    if (!scr_skipupdate || scr_block_drawing)
	SwapBuffers(maindc);

// handle the mouse state when windowed if that's changed
    if (modestate == MS_WINDOWED) {
	if (!_windowed_mouse.value) {
	    if (windowed_mouse) {
		IN_DeactivateMouse();
		IN_ShowMouse();
		windowed_mouse = false;
	    }
	} else {
	    windowed_mouse = true;
	    if (key_dest == key_game && !mouseactive && ActiveApp) {
		IN_ActivateMouse();
		IN_HideMouse();
	    } else if (mouseactive && key_dest != key_game) {
		IN_DeactivateMouse();
		IN_ShowMouse();
	    }
	}
    }
    if (fullsbardraw)
	Sbar_Changed();
}

void
VID_SetPalette(const byte *palette)
{
    const byte *pal;
    unsigned r, g, b;
    unsigned v;
    int r1, g1, b1;
    int j, k, l;
    unsigned short i;
    unsigned *table;

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
	*table++ = v;
    }

    // JACK: 3D distance calcs - k is last closest, l is the distance.
    // FIXME: Precalculate this and cache to disk.
    for (i = 0; i < (1 << 15); i++) {
	/* Maps
	   000000000000000
	   000000000011111 = Red  = 0x1F
	   000001111100000 = Blue = 0x03E0
	   111110000000000 = Grn  = 0x7C00
	 */
	r = ((i & 0x1F) << 3) + 4;
	g = ((i & 0x03E0) >> 2) + 4;
	b = ((i & 0x7C00) >> 7) + 4;
	pal = (const byte *)d_8to24table;
	for (v = 0, k = 0, l = 10000 * 10000; v < 256; v++, pal += 4) {
	    r1 = r - pal[0];
	    g1 = g - pal[1];
	    b1 = b - pal[2];
	    j = (r1 * r1) + (g1 * g1) + (b1 * b1);
	    if (j < l) {
		k = v;
		l = j;
	    }
	}
	d_15to8table[i] = k;
    }
}

void (*VID_SetGammaRamp)(unsigned short ramp[3][256]);
static unsigned short saved_gamma_ramp[3][256];

static void
VID_SetWinGammaRamp(unsigned short ramp[3][256])
{
    BOOL result;

    result = SetDeviceGammaRamp(maindc, ramp);
}

void
Gamma_Init(void)
{
    BOOL result = GetDeviceGammaRamp(maindc, saved_gamma_ramp);

    if (result)
	result = SetDeviceGammaRamp(maindc, saved_gamma_ramp);
    if (result)
	VID_SetGammaRamp = VID_SetWinGammaRamp;
    else
	VID_SetGammaRamp = NULL;
}

void
VID_ShiftPalette(const byte *palette)
{
    //VID_SetPalette(palette);
    //gammaworks = SetDeviceGammaRamp(maindc, ramps);
}


void
VID_SetDefaultMode(void)
{
    IN_DeactivateMouse();
}


void
VID_Shutdown(void)
{
    HGLRC hRC;
    HDC hDC;

    if (vid_initialized) {
	if (VID_SetGammaRamp)
	    VID_SetGammaRamp(saved_gamma_ramp);

	vid_canalttab = false;
	hRC = wglGetCurrentContext();
	hDC = wglGetCurrentDC();

	wglMakeCurrent(NULL, NULL);

	if (hRC)
	    wglDeleteContext(hRC);

	if (hDC && mainwindow)
	    ReleaseDC(mainwindow, hDC);

	if (modestate == MS_FULLSCREEN)
	    ChangeDisplaySettings(NULL, 0);

	if (maindc && mainwindow)
	    ReleaseDC(mainwindow, maindc);

	AppActivate(false, false);
    }
}


//==========================================================================


static BOOL
bSetupPixelFormat(HDC hDC)
{
    BOOL result;
    int pixelformat;

    static PIXELFORMATDESCRIPTOR pfd = {
	sizeof(PIXELFORMATDESCRIPTOR),	// size of this pfd
	1,			// version number
	PFD_DRAW_TO_WINDOW	// support window
	    | PFD_SUPPORT_OPENGL	// support OpenGL
	    | PFD_DOUBLEBUFFER,	// double buffered
	PFD_TYPE_RGBA,		// RGBA type
	24,			// 24-bit color depth
	0, 0, 0, 0, 0, 0,	// color bits ignored
	0,			// no alpha buffer
	0,			// shift bit ignored
	0,			// no accumulation buffer
	0, 0, 0, 0,		// accum bits ignored
	32,			// 32-bit z-buffer
	0,			// no stencil buffer
	0,			// no auxiliary buffer
	PFD_MAIN_PLANE,		// main layer
	0,			// reserved
	0, 0, 0			// layer masks ignored
    };

    pixelformat = ChoosePixelFormat(hDC, &pfd);
    if (!pixelformat) {
	MessageBox(NULL, "ChoosePixelFormat failed", "Error", MB_OK);
	return FALSE;
    }

    result = SetPixelFormat(hDC, pixelformat, &pfd);
    if (!result) {
	MessageBox(NULL, "SetPixelFormat failed", "Error", MB_OK);
	return FALSE;
    }

    return TRUE;
}


/*
================
ClearAllStates
================
*/
static void
ClearAllStates(void)
{
    knum_t keynum;

    /* send an up event for each key, to ensure the server clears them all */
    for (keynum = K_UNKNOWN; keynum < K_LAST; keynum++)
	Key_Event(keynum, false);

    Key_ClearStates();
    IN_ClearStates();
}

static void
VID_InitWindowClass(HINSTANCE hInstance)
{
    WNDCLASS wc;

    /* Register the frame class */
    wc.style = 0;
    wc.lpfnWndProc = (WNDPROC)MainWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = 0;
    wc.lpszClassName = "TyrQuake";

    if (!RegisterClass(&wc))
	Sys_Error("Couldn't register window class");
}


/*
=================
VID_InitFullDIB
=================
*/
static void
VID_InitModeList(void)
{
    DEVMODE devmode;
    DWORD testmodenum;
    LONG result;
    BOOL success;
    qvidmode_t *mode;

    /* Query the desktop mode */
    memset(&devmode, 0, sizeof(devmode));
    success = EnumDisplaySettings(NULL, ENUM_REGISTRY_SETTINGS, &devmode);
    if (!success)
	Sys_Error("Unable to query desktop display settings");

    /* Setup the default windowed mode */
    mode = modelist;
    mode->width = 640;
    mode->height = 480;
    mode->bpp = devmode.dmBitsPerPel;
    mode->refresh = devmode.dmDisplayFrequency;
    mode++;
    nummodes = 1;

    memset(&devmode, 0, sizeof(devmode));
    testmodenum = 0;
    while (EnumDisplaySettings(NULL, testmodenum++, &devmode)) {
	if (nummodes == MAX_MODE_LIST)
	    break;
	if (devmode.dmPelsWidth > MAXWIDTH || devmode.dmPelsHeight > MAXHEIGHT)
	    continue;
	if (devmode.dmBitsPerPel < 15)
	    continue;

	devmode.dmFields =
	    DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
	result = ChangeDisplaySettings(&devmode, CDS_TEST | CDS_FULLSCREEN);
	if (result != DISP_CHANGE_SUCCESSFUL)
	    continue;

	mode->width = devmode.dmPelsWidth;
	mode->height = devmode.dmPelsHeight;
	mode->bpp = devmode.dmBitsPerPel;
	mode->refresh = devmode.dmDisplayFrequency;
	mode->modenum = nummodes++;
	mode++;
    }

    if (nummodes == 1)
	Con_SafePrintf("No fullscreen DIB modes found\n");
}

static void
Check_Gamma(const byte *palette, byte *newpalette)
{
    float f, inf;
    int i;

    i = COM_CheckParm("-gamma");
    vid_gamma = i ? Q_atof(com_argv[i + 1]) : 1.0;

    for (i = 0; i < 768; i++) {
	f = pow((palette[i] + 1) / 256.0, vid_gamma);
	inf = f * 255 + 0.5;
	if (inf < 0)
	    inf = 0;
	if (inf > 255)
	    inf = 255;
	newpalette[i] = inf;
    }
}

/*
===================
VID_Init
===================
*/
void
VID_Init(const byte *palette)
{
    int i;
    byte gamma_palette[256 * 3];
    char gldir[MAX_OSPATH];
    DEVMODE devmode;
    const qvidmode_t *mode;

    memset(&devmode, 0, sizeof(devmode));

    Cvar_RegisterVariable(&vid_mode);
    Cvar_RegisterVariable(&vid_wait);
    Cvar_RegisterVariable(&vid_nopageflip);
    Cvar_RegisterVariable(&_vid_wait_override);
    Cvar_RegisterVariable(&_vid_default_mode);
    Cvar_RegisterVariable(&_vid_default_mode_win);
    Cvar_RegisterVariable(&vid_config_x);
    Cvar_RegisterVariable(&vid_config_y);
    Cvar_RegisterVariable(&vid_stretch_by_2);
    Cvar_RegisterVariable(&gl_ztrick);
    Cvar_RegisterVariable(&gl_npot);

    Cmd_AddCommand("vid_nummodes", VID_NumModes_f);
    Cmd_AddCommand("vid_describecurrentmode", VID_DescribeCurrentMode_f);
    Cmd_AddCommand("vid_describemode", VID_DescribeMode_f);
    Cmd_AddCommand("vid_describemodes", VID_DescribeModes_f);

    hIcon = LoadIcon(global_hInstance, MAKEINTRESOURCE(IDI_ICON2));

    InitCommonControls();

    VID_InitWindowClass(global_hInstance);
    VID_InitModeList();
    mode = VID_GetCmdlineMode();
    if (!mode)
	mode = &modelist[vid_default];

    vid_initialized = true;

    if ((i = COM_CheckParm("-conwidth")) != 0)
	vid.conwidth = Q_atoi(com_argv[i + 1]);
    else
	vid.conwidth = 640;

    vid.conwidth &= ~7;	// make it a multiple of eight

    if (vid.conwidth < 320)
	vid.conwidth = 320;

    // pick a conheight that matches with correct aspect
    vid.conheight = vid.conwidth * 3 / 4;

    if ((i = COM_CheckParm("-conheight")) != 0)
	vid.conheight = Q_atoi(com_argv[i + 1]);
    if (vid.conheight < 200)
	vid.conheight = 200;

    vid.maxwarpwidth = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));

    DestroyWindow(hwnd_dialog);

    Check_Gamma(palette, gamma_palette);
    VID_SetPalette(gamma_palette);

    VID_SetMode(mode, gamma_palette);
    Gamma_Init();

    snprintf(gldir, sizeof(gldir), "%s/glquake", com_gamedir);
    Sys_mkdir(gldir);

    vid_realmode = vid_modenum;
    vid_menudrawfn = VID_MenuDraw;
    vid_menukeyfn = VID_MenuKey;
    vid_canalttab = true;

    if (COM_CheckParm("-fullsbar"))
	fullsbardraw = true;
}

//==========================================================================

static knum_t scantokey[128] = {
//  0       1       2       3       4       5       6       7
//  8       9       A       B       C       D       E       F
    0,      K_ESCAPE, '1',  '2',    '3',    '4',    '5',    '6',
    '7',    '8',    '9',    '0',    '-',    '=',    K_BACKSPACE, K_TAB,	// 0
    'q',    'w',    'e',    'r',    't',    'y',    'u',    'i',
    'o',    'p',    '[',    ']',    13,     K_LCTRL,'a',    's',	// 1
    'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';',
    '\'',   '`',    K_LSHIFT, '\\', 'z',    'x',    'c',    'v',	// 2
    'b',    'n',    'm',    ',',    '.',    '/',    K_RSHIFT, '*',
    K_LALT, ' ',    0,      K_F1,   K_F2,   K_F3,   K_F4,   K_F5,	// 3
    K_F6,   K_F7,   K_F8,   K_F9,   K_F10,  K_PAUSE, 0,     K_HOME,
    K_UPARROW, K_PGUP, '-', K_LEFTARROW, '5', K_RIGHTARROW, '+', K_END,	// 4
    K_DOWNARROW, K_PGDN, K_INS, K_DEL, 0,   0,      0,      K_F11,
    K_F12,  0,      0,      0,      0,      0,      0,      0,		// 5
    0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,		// 6
    0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0		// 7
};

/*
=======
MapKey

Map from windows to quake keynums
=======
*/
static knum_t
MapKey(int key)
{
    key = (key >> 16) & 255;
    if (key > 127)
	return 0;
    if (!scantokey[key])
	Con_DPrintf("key 0x%02x has no translation\n", key);
    return scantokey[key];
}

/*
 * Function:     AppActivate
 * Parameters:   fActive - True if app is activating
 *
 * Description:  If the application is activating, then swap the system
 *               into SYSPAL_NOSTATIC mode so that our palettes will display
 *               correctly.
 */
static void
AppActivate(BOOL fActive, BOOL minimize)
{
    static BOOL sound_active;

    ActiveApp = fActive;
    Minimized = minimize;

    /* enable/disable sound on focus gain/loss */
    if (!ActiveApp && sound_active) {
	S_BlockSound();
	sound_active = false;
    } else if (ActiveApp && !sound_active) {
	S_UnblockSound();
	sound_active = true;
    }

    if (fActive) {
	if (modestate == MS_FULLSCREEN) {
	    IN_ActivateMouse();
	    IN_HideMouse();
	    if (vid_canalttab && vid_wassuspended) {
		vid_wassuspended = false;
		ChangeDisplaySettings(&gdevmode, CDS_FULLSCREEN);
		ShowWindow(mainwindow, SW_SHOWNORMAL);

		/*
		 * Work-around for a bug in some video drivers that don't
		 * correctly update the offsets into the GL front buffer after
		 * alt-tab to the desktop and back.
		 */
		MoveWindow(mainwindow, 0, 0, gdevmode.dmPelsWidth,
			   gdevmode.dmPelsHeight, false);
	    }
	} else if ((modestate == MS_WINDOWED) && _windowed_mouse.value
		   && key_dest == key_game) {
	    IN_ActivateMouse();
	    IN_HideMouse();
	}
	/* Restore game gamma */
	if (VID_SetGammaRamp)
	    VID_SetGammaRamp(ramps);
    }

    if (!fActive) {
	/* Restore desktop gamma */
	if (VID_SetGammaRamp)
	    VID_SetGammaRamp(saved_gamma_ramp);
	if (modestate == MS_FULLSCREEN) {
	    IN_DeactivateMouse();
	    IN_ShowMouse();
	    if (vid_canalttab) {
		ChangeDisplaySettings(NULL, 0);
		vid_wassuspended = true;
	    }
	} else if ((modestate == MS_WINDOWED) && _windowed_mouse.value) {
	    IN_DeactivateMouse();
	    IN_ShowMouse();
	}
    }
}


/*
===================================================================
MAIN WINDOW
===================================================================
*/

/* main window procedure */
static LONG WINAPI
MainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    LONG msg_handled = 1;
    int fActive, fMinimized, buttons, window_x, window_y, result;
    const qvidmode_t *mode;

    if (uMsg == uiWheelMessage)
	uMsg = WM_MOUSEWHEEL;

    switch (uMsg) {
    case WM_KILLFOCUS:
	if (modestate == MS_FULLSCREEN)
	    ShowWindow(mainwindow, SW_SHOWMINNOACTIVE);
	break;

    case WM_CREATE:
	break;

    case WM_MOVE:
	window_x = (int)LOWORD(lParam);
	window_y = (int)HIWORD(lParam);
	mode = &modelist[vid_modenum];
	IN_UpdateWindowRect(window_x, window_y, mode->width, mode->height);
	break;

    case WM_SIZE:
	break;

    case WM_SYSCHAR:
	/* keep Alt-Space from happening */
	break;

    case WM_ACTIVATE:
	fActive = LOWORD(wParam);
	fMinimized = (BOOL)HIWORD(wParam);
	AppActivate(!(fActive == WA_INACTIVE), fMinimized);

	/*
	 * Fix the leftover Alt from any Alt-Tab or the like that
	 * switched us away
	 */
	ClearAllStates();
	break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
	Key_Event(MapKey(lParam), true);
	break;

    case WM_KEYUP:
    case WM_SYSKEYUP:
	Key_Event(MapKey(lParam), false);
	break;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEMOVE:
	/*
	 * this is complicated because Win32 seems to pack multiple mouse
	 * events into one update sometimes, so we always check all states and
	 * look for events
	 */
	buttons = 0;
	if (wParam & MK_LBUTTON)
	    buttons |= 1;
	if (wParam & MK_RBUTTON)
	    buttons |= 2;
	if (wParam & MK_MBUTTON)
	    buttons |= 4;
	IN_MouseEvent(buttons);
	break;

    case WM_MOUSEWHEEL:
	/*
	 * This is the mouse wheel with the Intellimouse. Its delta is
	 * either positive or neg, and we generate the proper Event.
	 */
	if ((short)HIWORD(wParam) > 0) {
	    Key_Event(K_MWHEELUP, true);
	    Key_Event(K_MWHEELUP, false);
	} else {
	    Key_Event(K_MWHEELDOWN, true);
	    Key_Event(K_MWHEELDOWN, false);
	}
	break;

    case WM_CLOSE:
	result = MessageBox(mainwindow,
			    "Are you sure you want to quit?",
			    "Confirm Exit",
			    MB_YESNO | MB_SETFOREGROUND | MB_ICONQUESTION);
	if (result == IDYES)
	    Sys_Quit();
	break;

    case WM_DESTROY:
	if (mainwindow)
	    DestroyWindow(mainwindow);
	PostQuitMessage(0);
	break;

    case MM_MCINOTIFY:
	msg_handled = CDDrv_MessageHandler(hWnd, uMsg, wParam, lParam);
	break;

    default:
	/* pass all unhandled messages to DefWindowProc */
	msg_handled = DefWindowProc(hWnd, uMsg, wParam, lParam);
	break;
    }

    return msg_handled;
}

qboolean
VID_IsFullScreen()
{
    return vid_modenum != 0;
}

void VID_LockBuffer(void) {}
void VID_UnlockBuffer(void) {}
void D_BeginDirectRect(int x, int y, const byte *pbitmap, int width, int height) {}
void D_EndDirectRect(int x, int y, int width, int height) {}
