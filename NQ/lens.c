/*

LENS.C
======

   This is a fisheye addon based on Fisheye Quake.  It renders up to 6 camera views
   per frame, and melds them together to allow a Field of View (FoV) greater than 180 degrees:

             ---------
             |       |
             | UP    |                          -----------------------------
             |       |                          |\--         UP          --/|
             ---------                          |   \---             ---/   |
   --------- --------- --------- ---------      |       \-----------/       |
   |       | |       | |       | |       |      |        |         |        |
   | LEFT  | | FRONT | | RIGHT | | BACK  | ---> |  LEFT  |  FRONT  | RIGHT  |
   |       | |       | |       | |       |      |        |         |        |
   --------- --------- --------- ---------      |       /-----------\       |
          ^  ---------                          |   /---             ---\   |
          |  |       |                          |/--        DOWN         --\|
         90º | DOWN  |                          -----------------------------
          |  |       |                          <---------- +180º ---------->
          v  ---------
             <--90º-->

         (a GLOBE controls the separate             (a LENS controls how the
          camera views to render)                  views are melded together)


   To enable this fisheye rendering, enter the command:
   ```
   ] fisheye 1
   ```

   To resume the standard view, enter the command:
   ```
   ] fisheye 0
   ```

NOTE
----

   You should have a "globes/" and "lenses/" folder in the game directory.  These
   contain Lua scripts used to configure the views.  I've included several
   preconfigured globes and lenses in the root directory of this repo.

   The game directory is "~/.tyrquake/" in UNIX environments.

GLOBES
------

   The multiple camera views are controlled by a "globe" script.  It contains a
   "plates" array, with each element containing a single camera's forward vector,
   up vector, and fov. Together, the plates should form a complete globe around
   the player.

   For example, this is the default globe (globes/cube.lua):

   ```
   plates = {
      { { 0, 0, 1 }, { 0, 1, 0 }, 90 }, -- front
      { { 1, 0, 0 }, { 0, 1, 0 }, 90 }, -- right
      { { -1, 0, 0 }, { 0, 1, 0 }, 90 }, -- left
      { { 0, 0, -1 }, { 0, 1, 0 }, 90 }, -- back
      { { 0, 1, 0 }, { 0, 0, -1 }, 90 }, -- top
      { { 0, -1, 0 }, { 0, 0, 1 }, 90 } -- bottom
   }
   ```
             ---------
             |       |
             | UP    |
             |       |
             ---------
   --------- --------- --------- ---------
   |       | |       | |       | |       |
   | LEFT  | | FRONT | | RIGHT | | BACK  |
   |       | |       | |       | |       |
   --------- --------- --------- ---------
          ^  ---------
          |  |       |
         90º | DOWN  |
          |  |       |
          v  ---------
             <--90º-->

   To use this globe, enter the command:

   ```
   ] globe cube
   ```

   There are other included globes:

   - trism: a triangular prism with 5 views
   - tetra: a tetrahedron with 4 views
   - fast:  2 overlaid views in the same direction (90 and 160 degrees)

LENSES
------

   The camera views are melded together by a "lens" script.

*/

#include "bspfile.h"
#include "client.h"
#include "cmd.h"
#include "console.h"
#include "cvar.h"
#include "draw.h"
#include "host.h"
#include "lens.h"
#include "mathlib.h"
#include "quakedef.h"
#include "r_local.h"
#include "screen.h"
#include "sys.h"
#include "view.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <time.h>

// -------------------------------------------------------------------------------- 
// |                                                                              |
// |                VARIABLES / FUNCTION SIGNATURES / DESCRIPTIONS                |
// |                                                                              |
// --------------------------------------------------------------------------------

// This is a globally accessible variable that determines if these fisheye features
// should be on.  It is used by other files for modifying behaviors that fisheye 
// depends on (e.g. square refdef, disabling water warp, hooking renderer).
int fisheye_enabled;

// Lens computation is slow, so we don't want to block the game while its busy.
// Instead of dealing with threads, we are just limiting the time that the
// lens builder can work each frame.  It keeps track of its work between frames
// so it can resume without problems.  This allows the user to watch the lens
// pixels become visible as they are calculated.
static struct _lens_builder
{
   int working;
   int start_time;
   float seconds_per_frame;
   struct _inverse_state
   {
      int ly;
   } inverse_state;
   struct _forward_state
   {
      int *top;
      int *bot;
      int plate_index;
      int py;
   } forward_state;
} lens_builder;

// the Lua state pointer
static lua_State *lua;

// lua reference indexes (for reference lua functions)
static struct _lua_refs {
   int lens_forward;
   int lens_inverse;
   int globe_plate;
} lua_refs;

// type to represent one pixel (one byte)
typedef unsigned char B;


// -------------------------------------------------------------------------------- 
// |                                                                              |
// |                      VARIABLE / FUNCTION DECLARATIONS                        |
// |                                                                              |
// --------------------------------------------------------------------------------

static void start_lens_builder_clock(void) {
   lens_builder.start_time = clock();
}
static int is_lens_builder_time_up(void) {
   clock_t time = clock() - lens_builder.start_time;
   float s = ((float)time) / CLOCKS_PER_SEC;
   return (s >= lens_builder.seconds_per_frame);
}

// -------------------------------------------------------------------------------- 
// |                                                                              |
// |                          STILL ORGANIZING BELOW                              |
// |                                                                              |
// --------------------------------------------------------------------------------


// the environment map
// a large array of pixels that hold all rendered views
static B *platemap = NULL;  

// the lookup table
// an array of pointers to platemap pixels
// (the view constructed by the lens)
static B **lensmap = NULL;

// how each pixel in the lensmap is colored
// an array of palette indices
// (used for displaying transparent colored overlays)
static B *palimap = NULL;

// top left coordinates of the view in the vid buffer
static int left, top;

// size of the view in the vid buffer
static int width, height;

// desired FOV in radians
static double fov;

// specific desired FOVs in degrees
static double hfov, vfov;

// render FOV for each plate
double renderfov;

// fit sizes
static double lens_width;
static double lens_height;

// fit mode
static int fit;
static int hfit;
static int vfit;

// name of the current lens
static char lens[50];

// indicates if the current lens is valid
static int valid_lens;

// name of the current globe
static char globe[50];

// indicates if the current globe is valid
static int valid_globe;

// pointer to the screen dimension (width,height) attached to the desired fov
static int* framesize;

// scale determined from desired zoom level
// (multiplier used to transform lens coordinates to screen coordinates)
static double scale;

// cubemap color display flag
static int colorcube = 0;
static int colorcells = 10;
static int colorwfrac = 5;

// maximum FOV width of the current lens
static double max_vfov;

// maximum FOV height of the current lens
static double max_hfov;

// change flags
static int lenschange;
static int globechange;
static int fovchange;

static int mapType;
#define MAP_NONE 0
#define MAP_FORWARD 1
#define MAP_INVERSE 2

// retrieves a pointer to a pixel in the video buffer
#define VBUFFER(x,y) (vid.buffer + (x) + (y)*vid.rowbytes)

// retrieves a pointer to a pixel in the platemap
#define PLATEMAP(plate,x,y) (platemap + (plate)*platesize*platesize + (x) + (y)*platesize)

// retrieves a pointer to a pixel in the lensmap
#define LENSMAP(x,y) (lensmap + (x) + (y)*width)

// retrieves a pointer to a pixel in the palimap
#define PALIMAP(x,y) (palimap + (x) + (y)*width)

// globe plates
typedef struct {
   vec3_t forward;
   vec3_t right;
   vec3_t up;
   vec_t fov;
   vec_t dist;
} plate_t;

#define MAX_PLATES 6
static plate_t plates[MAX_PLATES];

// number of plates used by the current globe
static int numplates;

// the palettes for each cube face used by the rubix filter
static B palmap[MAX_PLATES][256];

// determines which plates should be rendered
// (plates will only be drawn if they are used by the current lens)
static int plate_display[MAX_PLATES] = {0};

// size of each rendered square plate in the vid buffer
static int platesize;

// find closest pallete index for color
static int find_closest_pal_index(int r, int g, int b)
{
   int i;
   int mindist = 256*256*256;
   int minindex = 0;
   B* pal = host_basepal;
   for (i=0; i<256; ++i)
   {
      int dr = (int)pal[0]-r;
      int dg = (int)pal[1]-g;
      int db = (int)pal[2]-b;
      int dist = dr*dr+dg*dg+db*db;
      if (dist < mindist)
      {
         mindist = dist;
         minindex = i;
      }
      pal += 3;
   }
   return minindex;
}

static void create_palmap(void)
{
   int i,j;
   int percent = 256/6;
   int tint[3];

   for (j=0; j<MAX_PLATES; ++j)
   {
      tint[0] = tint[1] = tint[2] = 0;
      switch (j)
      {
         case 0:
            tint[0] = tint[1] = tint[2] = 255;
            break;
         case 1:
            tint[2] = 255;
            break;
         case 2:
            tint[0] = 255;
            break;
         case 3:
            tint[0] = tint[1] = 255;
            break;
         case 4:
            tint[0] = tint[2] = 255;
            break;
         case 5:
            tint[1] = tint[2] = 255;
            break;
      }
   
      B* pal = host_basepal;
      for (i=0; i<256; ++i)
      {
         int r = pal[0];
         int g = pal[1];
         int b = pal[2];

         r += percent * (tint[0] - r) >> 8;
         g += percent * (tint[1] - g) >> 8;
         b += percent * (tint[2] - b) >> 8;

         if (r < 0) r=0; if (r > 255) r=255;
         if (g < 0) g=0; if (g > 255) g=255;
         if (b < 0) b=0; if (b > 255) b=255;

         palmap[j][i] = find_closest_pal_index(r,g,b);

         pal += 3;
      }
   }
}

static void L_DumpPalette(void)
{
   int i;
   B *pal = host_basepal;
   FILE *pFile = fopen("palette","w");
   if (NULL == pFile) {
      Con_Printf("could not open \"palette\" for writing\n");
      return;
   }
   for (i=0; i<256; ++i) {
      fprintf(pFile, "%d, %d, %d,\n",
            pal[0],pal[1],pal[2]);
      pal+=3;
   }
   fclose(pFile);
}

static void L_ColorCube(void)
{
   colorcube = colorcube ? 0 : 1;
   Con_Printf("Rubix is %s\n", colorcube ? "ON" : "OFF");
}

/* START CONVERSION LUA HELPER FUNCTIONS */
static int lua_latlon_to_ray(lua_State *L);
static int lua_ray_to_latlon(lua_State *L);
static int lua_plate_to_ray(lua_State *L);

static void latlon_to_ray(double lat, double lon, vec3_t ray)
{
   double clat = cos(lat);
   ray[0] = sin(lon)*clat;
   ray[1] = sin(lat);
   ray[2] = cos(lon)*clat;
}

static void ray_to_latlon(vec3_t ray, double *lat, double *lon)
{
   *lon = atan2(ray[0], ray[2]);
   *lat = atan2(ray[1], sqrt(ray[0]*ray[0]+ray[2]*ray[2]));
}

static void plate_uv_to_ray(plate_t *plate, double u, double v, vec3_t ray);

/* END CONVERSION LUA HELPER FUNCTIONS */

static void L_InitLua(void)
{
   // create Lua state
   lua = luaL_newstate();

   // open Lua standard libraries
   luaL_openlibs(lua);

   char *aliases = 
      "cos = math.cos\n"
      "sin = math.sin\n"
      "tan = math.tan\n"
      "asin = math.asin\n"
      "acos = math.acos\n"
      "atan = math.atan\n"
      "atan2 = math.atan2\n"
      "sinh = math.sinh\n"
      "cosh = math.cosh\n"
      "tanh = math.tanh\n"
      "log = math.log\n"
      "log10 = math.log10\n"
      "abs = math.abs\n"
      "sqrt = math.sqrt\n"
      "exp = math.exp\n"
      "pi = math.pi\n"
      "tau = math.pi*2\n"
      "pow = math.pow\n";

   int error = luaL_loadbuffer(lua, aliases, strlen(aliases), "aliases") ||
      lua_pcall(lua, 0, 0, 0);
   if (error) {
      fprintf(stderr, "%s", lua_tostring(lua, -1));
      lua_pop(lua, 1);  // pop error message from the stack
   }

   lua_pushcfunction(lua, lua_latlon_to_ray);
   lua_setglobal(lua, "latlon_to_ray");

   lua_pushcfunction(lua, lua_ray_to_latlon);
   lua_setglobal(lua, "ray_to_latlon");

   lua_pushcfunction(lua, lua_plate_to_ray);
   lua_setglobal(lua, "plate_to_ray");
}

static void clearFov(void)
{
   fit = hfit = vfit = 0;
   fov = hfov = vfov = 0;
   framesize = 0; // clear framesize pointer
   fovchange = 1; // trigger change
}

static void L_HFit(void)
{
   clearFov();
   hfit = 1;
}

static void L_VFit(void)
{
   clearFov();
   vfit = 1;
}

static void L_Fit(void)
{
   clearFov();
   fit = 1;
}

void L_WriteConfig(FILE* f)
{
   if (hfov != 0)
   {
      fprintf(f,"hfov %f\n", hfov);
   }
   else if (vfov != 0)
   {
      fprintf(f,"vfov %f\n", vfov);
   }
   else if (hfit) 
   {
      fprintf(f,"hfit\n");
   }
   else if (vfit)
   {
      fprintf(f,"vfit\n");
   }
   else if (fit)
   {
      fprintf(f,"fit\n");
   }

   fprintf(f,"fisheye %d\n", fisheye_enabled);
   fprintf(f,"lens \"%s\"\n",lens);
   fprintf(f,"globe \"%s\"\n", globe);
}

static void printActiveFov(void)
{
   Con_Printf("Currently: ");
   if (hfov != 0) {
      Con_Printf("hfov %d\n",(int)hfov);
   }
   else if (vfov != 0) {
      Con_Printf("vfov %d\n",(int)vfov);
   }
}

static void L_Fisheye(void)
{
   if (Cmd_Argc() < 2) {
      Con_Printf("Currently: ");
      Con_Printf("fisheye %d\n", fisheye_enabled);
      return;
   }
   fisheye_enabled = Q_atoi(Cmd_Argv(1)); // will return 0 if not valid
   vid.recalc_refdef = true;
}

static void L_HFov(void)
{
   if (Cmd_Argc() < 2) { // no fov given
      Con_Printf("hfov <degrees>: set horizontal FOV\n");
      printActiveFov();
      return;
   }

   clearFov();

   hfov = Q_atof(Cmd_Argv(1)); // will return 0 if not valid
   framesize = &width;
   fov = hfov * M_PI / 180;
}

static void L_VFov(void)
{
   if (Cmd_Argc() < 2) { // no fov given
      Con_Printf("vfov <degrees>: set vertical FOV\n");
      printActiveFov();
      return;
   }

   clearFov();
   vfov = Q_atof(Cmd_Argv(1)); // will return 0 if not valid
   framesize = &height;
   fov = vfov * M_PI / 180;
}

static int lua_lens_load(void);

// lens command
static void L_Lens(void)
{
   if (Cmd_Argc() < 2) { // no lens name given
      Con_Printf("lens <name>: use a new lens\n");
      Con_Printf("Currently: %s\n", lens);
      return;
   }

   // trigger change
   lenschange = 1;

   // get name
   strcpy(lens, Cmd_Argv(1));

   // load lens
   valid_lens = lua_lens_load();
   if (!valid_lens) {
      strcpy(lens,"");
      Con_Printf("not a valid lens\n");
   }

   // execute the lens' onload command string if given
   // (this is to provide a user-friendly default view of the lens (e.g. "hfov 180"))
   lua_getglobal(lua, "onload");
   if (lua_isstring(lua, -1))
   {
      const char* onload = lua_tostring(lua, -1);
      Cmd_ExecuteString(onload, src_command);
   }
   else {
      // fail silently for now, resulting from two cases:
      // 1. onload is nil (undefined)
      // 2. onload is not a string
   }
   lua_pop(lua, 1); // pop "onload"
}

// autocompletion for lens names
static struct stree_root * L_LensArg(const char *arg)
{
   struct stree_root *root;

   root = Z_Malloc(sizeof(struct stree_root));
   if (root) {
      *root = STREE_ROOT;

      STree_AllocInit();
      COM_ScanDir(root, "../lenses", arg, ".lua", true);
   }
   return root;
}

static int lua_globe_load(void);

static int save_globe;
static int save_globe_full;
static char save_globe_name[32];
static void L_SaveGlobe(void)
{
   if (Cmd_Argc() < 2) { // no file name given
      Con_Printf("saveglobe <name> [full flag=0]: screenshot the globe plates\n");
      return;
   }

   strncpy(save_globe_name, Cmd_Argv(1), 32);

   if (Cmd_Argc() >= 3) {
      save_globe_full = Q_atoi(Cmd_Argv(2));
   }
   else {
      save_globe_full = 0;
   }
   save_globe = 1;
}

static int ray_to_plate_index(vec3_t ray);

// copied from WritePCXfile in NQ/screen.c
// write a plate 
static void WritePCXplate(char *filename, int plate_index, int full)
{
    // parameters from WritePCXfile
    byte *data = platemap+platesize*platesize*plate_index;
    int width = platesize;
    int height = platesize;
    int rowbytes = platesize;
    byte *palette = host_basepal;

    int i, j, length;
    pcx_t *pcx;
    byte *pack;

    pcx = Hunk_TempAlloc(width * height * 2 + 1000);
    if (pcx == NULL) {
	Con_Printf("SCR_ScreenShot_f: not enough memory\n");
	return;
    }

    pcx->manufacturer = 0x0a;	// PCX id
    pcx->version = 5;		// 256 color
    pcx->encoding = 1;		// uncompressed
    pcx->bits_per_pixel = 8;	// 256 color
    pcx->xmin = 0;
    pcx->ymin = 0;
    pcx->xmax = LittleShort((short)(width - 1));
    pcx->ymax = LittleShort((short)(height - 1));
    pcx->hres = LittleShort((short)width);
    pcx->vres = LittleShort((short)height);
    memset(pcx->palette, 0, sizeof(pcx->palette));
    pcx->color_planes = 1;	// chunky image
    pcx->bytes_per_line = LittleShort((short)width);
    pcx->palette_type = LittleShort(2);	// not a grey scale
    memset(pcx->filler, 0, sizeof(pcx->filler));

// pack the image
    pack = &pcx->data;

    for (i = 0; i < height; i++) {
       double v = ((double)i)/height;
	for (j = 0; j < width; j++) {
       double u = ((double)j)/width;

          // 
          vec3_t ray;
          plate_uv_to_ray(&plates[plate_index], u, v, ray);
          byte col = full || plate_index == ray_to_plate_index(ray) ? *data : 0xFE;

          if ((col & 0xc0) == 0xc0) {
             *pack++ = 0xc1;
          }
          *pack = col;

          pack++;
          data++;
      }

	data += rowbytes - width;
    }

// write the palette
    *pack++ = 0x0c;		// palette ID byte
    for (i = 0; i < 768; i++)
	*pack++ = *palette++;

// write output file
    length = pack - (byte *)pcx;
    COM_WriteFile(filename, pcx, length);
}

static void SaveGlobe(void)
{
   int i;
   char pcxname[32];

   save_globe = 0;

    D_EnableBackBufferAccess();	// enable direct drawing of console to back

   for (i=0; i<numplates; ++i) 
   {
      snprintf(pcxname, 32, "%s%d.pcx", save_globe_name, i);
      WritePCXplate(pcxname, i, save_globe_full);

    Con_Printf("Wrote %s\n", pcxname);
   }

    D_DisableBackBufferAccess();	// for adapters that can't stay mapped in
    //  for linear writes all the time
}

static void L_Globe(void)
{
   if (Cmd_Argc() < 2) { // no globe name given
      Con_Printf("globe <name>: use a new globe\n");
      Con_Printf("Currently: %s\n", globe);
      return;
   }

   // trigger change
   globechange = 1;

   // get name
   strcpy(globe, Cmd_Argv(1));

   // load globe
   valid_globe = lua_globe_load();
   if (!valid_globe) {
      strcpy(globe,"");
      Con_Printf("not a valid globe\n");
   }
   top = lua_gettop(lua);
}

// autocompletion for globe names
static struct stree_root * L_GlobeArg(const char *arg)
{
   struct stree_root *root;

   root = Z_Malloc(sizeof(struct stree_root));
   if (root) {
      *root = STREE_ROOT;

      STree_AllocInit();
      COM_ScanDir(root, "../globes", arg, ".lua", true);
   }
   return root;
}

void L_Init(void)
{
   lens_builder.working = 0;
   lens_builder.seconds_per_frame = 1.0f / 60;

   L_InitLua();

   Cmd_AddCommand("dumppal", L_DumpPalette);
   Cmd_AddCommand("rubix", L_ColorCube);
   Cmd_AddCommand("hfit", L_HFit);
   Cmd_AddCommand("vfit", L_VFit);
   Cmd_AddCommand("fit", L_Fit);
   Cmd_AddCommand("hfov", L_HFov);
   Cmd_AddCommand("vfov", L_VFov);
   Cmd_AddCommand("lens", L_Lens);
   Cmd_SetCompletion("lens", L_LensArg);
   Cmd_AddCommand("globe", L_Globe);
   Cmd_SetCompletion("globe", L_GlobeArg);
   Cmd_AddCommand("saveglobe", L_SaveGlobe);
   Cmd_AddCommand("fisheye", L_Fisheye);

   // default view state
   Cmd_ExecuteString("globe cube", src_command);
   Cmd_ExecuteString("lens panini", src_command);
   Cmd_ExecuteString("hfov 180", src_command);

   // create palette maps
   create_palmap();
}

void L_Shutdown(void)
{
   lua_close(lua);
}

// -----------------------------------
// Lua Functions
// -----------------------------------

static int lua_latlon_to_ray(lua_State *L)
{
   double lat = luaL_checknumber(L,1);
   double lon = luaL_checknumber(L,2);
   vec3_t ray;
   latlon_to_ray(lat,lon,ray);
   lua_pushnumber(L, ray[0]);
   lua_pushnumber(L, ray[1]);
   lua_pushnumber(L, ray[2]);
   return 3;
}

static int lua_ray_to_latlon(lua_State *L)
{
   double rx = luaL_checknumber(L, 1);
   double ry = luaL_checknumber(L, 2);
   double rz = luaL_checknumber(L, 3);

   vec3_t ray = {rx,ry,rz};
   double lat,lon;
   ray_to_latlon(ray,&lat,&lon);

   lua_pushnumber(L, lat);
   lua_pushnumber(L, lon);
   return 2;
}

static int lua_plate_to_ray(lua_State *L)
{
   int plate_index = luaL_checknumber(L,1);
   double u = luaL_checknumber(L,2);
   double v = luaL_checknumber(L,3);
   vec3_t ray;
   if (plate_index < 0 || plate_index >= numplates) {
      lua_pushnil(L);
      return 1;
   }

   plate_uv_to_ray(&plates[plate_index],u,v,ray);
   lua_pushnumber(L, ray[0]);
   lua_pushnumber(L, ray[1]);
   lua_pushnumber(L, ray[2]);
   return 3;
}

static int lua_lens_inverse(double x, double y, vec3_t ray)
{
   int top = lua_gettop(lua);
   lua_rawgeti(lua, LUA_REGISTRYINDEX, lua_refs.lens_inverse);
   lua_pushnumber(lua, x);
   lua_pushnumber(lua, y);
   lua_call(lua, 2, LUA_MULTRET);

   int numret = lua_gettop(lua) - top;
   int status;

   switch(numret) {
      case 3:
         if (lua_isnumber(lua,-3) && lua_isnumber(lua,-2) && lua_isnumber(lua,-1)) {
            ray[0] = lua_tonumber(lua, -3);
            ray[1] = lua_tonumber(lua, -2);
            ray[2] = lua_tonumber(lua, -1);
            VectorNormalize(ray);
            status = 1;
         }
         else {
            Con_Printf("lens_inverse returned a non-number value for x,y,z\n");
            status = -1;
         }
         break;

      case 1:
         if (lua_isnil(lua,-1)) {
            status = 0;
         }
         else {
            status = -1;
            Con_Printf("lens_inverse returned a single non-nil value\n");
         }
         break;

      default:
         Con_Printf("lens_inverse returned %d values instead of 3\n", numret);
         status = -1;
   }

   lua_pop(lua, numret);
   return status;
}

static int lua_lens_forward(vec3_t ray, double *x, double *y)
{
   int top = lua_gettop(lua);
   lua_rawgeti(lua, LUA_REGISTRYINDEX, lua_refs.lens_forward);
   lua_pushnumber(lua,ray[0]);
   lua_pushnumber(lua,ray[1]);
   lua_pushnumber(lua,ray[2]);
   lua_call(lua, 3, LUA_MULTRET);

   int numret = lua_gettop(lua) - top;
   int status;

   switch (numret) {
      case 2:
         if (lua_isnumber(lua,-2) && lua_isnumber(lua,-1)) {
            *x = lua_tonumber(lua, -2);
            *y = lua_tonumber(lua, -1);
            status = 1;
         }
         else {
            Con_Printf("lens_forward returned a non-number value for x,y\n");
            status = -1;
         }
         break;

      case 1:
         if (lua_isnil(lua,-1)) {
            status = 0;
         }
         else {
            status = -1;
            Con_Printf("lens_forward returned a single non-nil value\n");
         }
         break;

      default:
         Con_Printf("lens_forward returned %d values instead of 2\n", numret);
         status = -1;
   }

   lua_pop(lua,numret);
   return status;
}

static int lua_globe_plate(vec3_t ray, int *plate)
{
   lua_rawgeti(lua, LUA_REGISTRYINDEX, lua_refs.globe_plate);
   lua_pushnumber(lua, ray[0]);
   lua_pushnumber(lua, ray[1]);
   lua_pushnumber(lua, ray[2]);
   lua_call(lua, 3, LUA_MULTRET);

   if (!lua_isnumber(lua, -1))
   {
      lua_pop(lua,1);
      return 0;
   }

   *plate = lua_tointeger(lua,-1);
   lua_pop(lua,1);
   return 1;
}


#define CLEARVAR(var) lua_pushnil(lua); lua_setglobal(lua, var);

// used to clear the state when switching lenses
static void lua_lens_clear(void)
{
   CLEARVAR("map");
   CLEARVAR("max_hfov");
   CLEARVAR("max_vfov");
   CLEARVAR("lens_width");
   CLEARVAR("lens_height");
   CLEARVAR("lens_inverse");
   CLEARVAR("lens_forward");
   CLEARVAR("onload");

   // set "numplates" var
   lua_pushinteger(lua, numplates);
   lua_setglobal(lua, "numplates");
}

// used to clear the state when switching globes
static void lua_globe_clear(void)
{
   CLEARVAR("plates");
   CLEARVAR("globe_plate");

   numplates = 0;
}

#undef CLEARVAR

static int lua_func_exists(const char* name)
{
   lua_getglobal(lua, name);
   int exists = lua_isfunction(lua,-1);
   lua_pop(lua, 1); // pop name
   return exists;
}

static int lua_globe_load(void)
{
   // clear Lua variables
   lua_globe_clear();

   // set full filename
   char filename[100];
   sprintf(filename, "%s/../globes/%s.lua",com_gamedir,globe);

   // check if loaded correctly
   int errcode = 0;
   if ((errcode=luaL_loadfile(lua, filename))) {
      Con_Printf("could not loadfile (%d) \nERROR: %s", errcode, lua_tostring(lua,-1));
      lua_pop(lua,1); // pop error message
      return 0;
   }
   else {
      if ((errcode=lua_pcall(lua, 0, 0, 0))) {
         Con_Printf("could not pcall (%d) \nERROR: %s", errcode, lua_tostring(lua,-1));
         lua_pop(lua,1); // pop error message
         return 0;
      }
   }

   // check for the globe_plate function
   lua_refs.globe_plate = -1;
   if (lua_func_exists("globe_plate"))
   {
      lua_getglobal(lua, "globe_plate");
      lua_refs.globe_plate = luaL_ref(lua, LUA_REGISTRYINDEX);
   }

   // load plates array
   lua_getglobal(lua, "plates");
   if (!lua_istable(lua,-1) || lua_rawlen(lua,-1) < 1)
   {
      Con_Printf("plates must be an array of one or more elements\n");
      lua_pop(lua, 1); // pop plates
      return 0;
   }

   // iterate plates
   int i = 0;
   int j;
   for (lua_pushnil(lua); lua_next(lua,-2); lua_pop(lua,1), ++i)
   {
      // get forward vector
      lua_rawgeti(lua, -1, 1);

      // verify table of length 3
      if (!lua_istable(lua,-1) || lua_rawlen(lua,-1) != 3 )
      {
         Con_Printf("plate %d: forward vector is not a 3d vector\n", i+1);
         lua_pop(lua, 3); // pop forward vector, plate, and plates
         return 0;
      }

      // get forward vector elements
      for (j=0; j<3; ++j) {
         lua_rawgeti(lua, -1, j+1);
         if (!lua_isnumber(lua,-1))
         {
            Con_Printf("plate %d: forward vector: element %d not a number\n", i+1, j+1);
            lua_pop(lua, 4); // pop element, vector, plate, and plates
            return 0;
         }
         plates[i].forward[j] = lua_tonumber(lua,-1);
         lua_pop(lua, 1); // pop element
      }
      lua_pop(lua,1); // pop forward vector

      // get up vector
      lua_rawgeti(lua, -1, 2);

      // verify table of length 3
      if (!lua_istable(lua,-1) || lua_rawlen(lua,-1) != 3 )
      {
         Con_Printf("plate %d: up vector is not a 3d vector\n", i+1);
         lua_pop(lua, 3); // pop forward vector, plate, and plates
         return 0;
      }

      // get up vector elements
      for (j=0; j<3; ++j) {
         lua_rawgeti(lua, -1, j+1);
         if (!lua_isnumber(lua,-1))
         {
            Con_Printf("plate %d: up vector: element %d not a number\n", i+1, j+1);
            lua_pop(lua, 4); // pop element, vector, plate, and plates
            return 0;
         }
         plates[i].up[j] = lua_tonumber(lua,-1);
         lua_pop(lua,1); // pop element
      }
      lua_pop(lua, 1); // pop up vector

      // calculate right vector (and correct up vector)
      CrossProduct(plates[i].up, plates[i].forward, plates[i].right);
      CrossProduct(plates[i].forward, plates[i].right, plates[i].up);

      // get fov
      lua_rawgeti(lua,-1,3);
      if (!lua_isnumber(lua,-1))
      {
         Con_Printf("plate %d: fov not a number\n", i+1);
      }
      plates[i].fov = lua_tonumber(lua,-1) * M_PI / 180;
      lua_pop(lua, 1); // pop fov

      if (plates[i].fov <= 0)
      {
         Con_Printf("plate %d: fov must > 0\n", i+1);
         return 0;
      }

      // calculate distance to camera
      plates[i].dist = 0.5/tan(plates[i].fov/2);
   }
   lua_pop(lua, 1); // pop plates

   numplates = i;

   return 1;
}

static int lua_lens_load(void)
{
   // clear Lua variables
   lua_lens_clear();

   // set full filename
   char filename[100];
   sprintf(filename,"%s/../lenses/%s.lua",com_gamedir,lens);

   // check if loaded correctly
   int errcode = 0;
   if ((errcode=luaL_loadfile(lua, filename))) {
      Con_Printf("could not loadfile (%d) \nERROR: %s", errcode, lua_tostring(lua,-1));
      lua_pop(lua,1); // pop error message
      return 0;
   }
   else {
      if ((errcode=lua_pcall(lua, 0, 0, 0))) {
         Con_Printf("could not pcall (%d) \nERROR: %s", errcode, lua_tostring(lua,-1));
         lua_pop(lua,1); // pop error message
         return 0;
      }
   }

   // clear current maps
   mapType = MAP_NONE;
   lua_refs.lens_forward = lua_refs.lens_inverse = -1;

   // check if the inverse map function is provided
   lua_getglobal(lua, "lens_inverse");
   if (!lua_isfunction(lua,-1)) {
      Con_Printf("lens_inverse is not found\n");
      lua_pop(lua,1); // pop lens_inverse
   }
   else {
      lua_refs.lens_inverse = luaL_ref(lua, LUA_REGISTRYINDEX);
      mapType = MAP_INVERSE;
   }

   // check if the forward map function is provided
   lua_getglobal(lua, "lens_forward");
   if (!lua_isfunction(lua,-1)) {
      Con_Printf("lens_forward is not found\n");
      lua_pop(lua,1); // pop lens_forward
   }
   else {
      lua_refs.lens_forward = luaL_ref(lua, LUA_REGISTRYINDEX);
      if (mapType == MAP_NONE) {
         mapType = MAP_FORWARD;
      }
   }

   // get map function preference if provided
   lua_getglobal(lua, "map");
   if (lua_isstring(lua, -1))
   {
      // get desired map function name
      const char* funcname = lua_tostring(lua, -1);

      // check for valid map function name
      if (!strcmp(funcname, "lens_inverse")) {
         mapType = MAP_INVERSE;
      }
      else if (!strcmp(funcname, "lens_forward")) {
         mapType = MAP_FORWARD;
      }
      else {
         Con_Printf("Unsupported map function: %s\n", funcname);
         lua_pop(lua, 1); // pop map
         return 0;
      }
   }
   lua_pop(lua,1); // pop map

   lua_getglobal(lua, "max_hfov");
   max_hfov = lua_isnumber(lua,-1) ? lua_tonumber(lua,-1) : 0;
   // TODO: use degrees to prevent roundoff errors (e.g. capping at 179 instead of 180)
   max_hfov *= M_PI / 180;
   lua_pop(lua,1); // pop max_hfov

   lua_getglobal(lua, "max_vfov");
   max_vfov = lua_isnumber(lua,-1) ? lua_tonumber(lua,-1) : 0;
   max_vfov *= M_PI / 180;
   lua_pop(lua,1); // pop max_vfov

   lua_getglobal(lua, "lens_width");
   lens_width = lua_isnumber(lua,-1) ? lua_tonumber(lua,-1) : 0;
   lua_pop(lua,1); // pop lens_width

   lua_getglobal(lua, "lens_height");
   lens_height = lua_isnumber(lua,-1) ? lua_tonumber(lua,-1) : 0;
   lua_pop(lua,1); // pop lens_height

   return 1;
}

// -----------------------------------
// End Lua Functions
// -----------------------------------

static int determine_lens_scale(void)
{
   // clear lens scale
   scale = -1;

   if (fit == 0 && hfit == 0 && vfit == 0) // scale based on FOV
   {
      // check FOV limits
      if (max_hfov <= 0 || max_vfov <= 0)
      {
         Con_Printf("max_hfov & max_vfov not specified, try \"fit\"\n");
         return 0;
      }

      if (width == *framesize && fov > max_hfov) {
         Con_Printf("hfov must be less than %d\n", (int)(max_hfov * 180 / M_PI));
         return 0;
      }
      else if (height == *framesize && fov > max_vfov) {
         Con_Printf("vfov must be less than %d\n", (int)(max_vfov * 180/ M_PI));
         return 0;
      }

      // try to scale based on FOV using the forward map
      if (lua_refs.lens_forward != -1) {
         vec3_t ray;
         double x,y;
         if (*framesize == width) {
            latlon_to_ray(0,fov*0.5,ray);
            if (lua_lens_forward(ray,&x,&y)) {
               scale = x / (*framesize * 0.5);
            }
            else {
               Con_Printf("ray_to_xy did not return a valid r value for determining FOV scale\n");
               return 0;
            }
         }
         else if (*framesize == height) {
            latlon_to_ray(fov*0.5,0,ray);
            if (lua_lens_forward(ray,&x,&y)) {
               scale = y / (*framesize * 0.5);
            }
            else {
               Con_Printf("ray_to_xy did not return a valid r value for determining FOV scale\n");
               return 0;
            }
         }
         else {
            Con_Printf("ray_to_xy does not support diagonal FOVs\n");
            return 0;
         }
      }
      else
      {
         Con_Printf("Please specify a forward mapping function in your script for FOV scaling\n");
         return 0;
      }
   }
   else // scale based on fitting
   {
      if (hfit) {
         if (lens_width <= 0)
         {
            //Con_Printf("Cannot use hfit unless a positive lens_width is in your script\n");
            Con_Printf("lens_width not specified.  Try hfov instead.\n");
            return 0;
         }
         scale = lens_width / width;
      }
      else if (vfit) {
         if (lens_height <= 0)
         {
            //Con_Printf("Cannot use vfit unless a positive lens_height is in your script\n");
            Con_Printf("lens_height not specified.  Try vfov instead.\n");
            return 0;
         }
         scale = lens_height / height;
      }
      else if (fit) {
         if (lens_width <= 0 && lens_height > 0) {
            scale = lens_height / height;
         }
         else if (lens_height <=0 && lens_width > 0) {
            scale = lens_width / width;
         }
         else if (lens_height <= 0 && lens_width <= 0) {
            Con_Printf("lens_height and lens_width not specified.  Try hfov instead.\n");
            return 0;
         }
         else if (lens_width / lens_height > (double)width / height) {
            scale = lens_width / width;
         }
         else {
            scale = lens_height / height;
         }
      }
   }

   // validate scale
   if (scale <= 0) {
      Con_Printf("init returned a scale of %f, which is  <= 0\n", scale);
      return 0;
   }

   return 1;
}

// ----------------------------------------
// Lens Map Creation
// ----------------------------------------

static void set_lensmap_grid(int lx, int ly, int px, int py, int plate_index)
{
   // designate the palette for this pixel
   // This will set the palette index map such that a grid is shown
   int numdivs = colorcells*colorwfrac+1;
   double divsize = (double)platesize/numdivs;
   double mod = colorwfrac;

   double x = px/divsize;
   double y = py/divsize;

   int ongrid = fmod(x,mod) < 1 || fmod(y,mod) < 1;

   if (!ongrid)
      *PALIMAP(lx,ly) = plate_index;
}

// set a pixel on the lensmap from plate coordinates
static void set_lensmap_from_plate(int lx, int ly, int px, int py, int plate_index)
{
   // check valid lens coordinates
   if (lx < 0 || lx >= width || ly < 0 || ly >= height) {
      return;
   }

   // check valid plate coordinates
   if (px <0 || px >= platesize || py < 0 || py >= platesize) {
      return;
   }

   // increase the number of times this side is used
   plate_display[plate_index] = 1;

   // map the lens pixel to this cubeface pixel
   *LENSMAP(lx,ly) = PLATEMAP(plate_index,px,py);

   set_lensmap_grid(lx,ly,px,py,plate_index);
}

// set a pixel on the lensmap from plate uv coordinates
static void set_lensmap_from_plate_uv(int lx, int ly, double u, double v, int plate_index)
{
   // convert to plate coordinates
   int px = (int)(u*platesize);
   int py = (int)(v*platesize);
   
   set_lensmap_from_plate(lx,ly,px,py,plate_index);
}

// retrieves the plate closest to the given ray
static int ray_to_plate_index(vec3_t ray)
{
   int plate_index = 0;

   if (lua_refs.globe_plate != -1) {
      // use user-defined plate selection function
      if (lua_globe_plate(ray, &plate_index)) {
         return plate_index;
      }
      return -1;
   }

   // maximum dotproduct 
   //  = minimum acos(dotproduct) 
   //  = minimum angle between vectors
   double max_dp = -2;

   int i;
   for (i=0; i<numplates; ++i) {
      double dp = DotProduct(ray, plates[i].forward);
      if (dp > max_dp) {
         max_dp = dp;
         plate_index = i;
      }
   }

   return plate_index;
}

static void plate_uv_to_ray(plate_t *plate, double u, double v, vec3_t ray)
{
   // transform to image coordinates
   u -= 0.5;
   v -= 0.5;
   v = -v;

   // clear ray
   ray[0] = ray[1] = ray[2] = 0;

   // get euclidean coordinate from texture uv
   VectorMA(ray, plate->dist, plate->forward, ray);
   VectorMA(ray, u, plate->right, ray);
   VectorMA(ray, v, plate->up, ray);

   VectorNormalize(ray);
}

static int ray_to_plate_uv(plate_t *plate, vec3_t ray, double *u, double *v)
{
   // get ray in the plate's relative view frame
   double x = DotProduct(plate->right, ray);
   double y = DotProduct(plate->up, ray);
   double z = DotProduct(plate->forward, ray);

   // project ray to the texture
   double dist = 0.5 / tan(plate->fov/2);
   *u = x/z*dist + 0.5;
   *v = -y/z*dist + 0.5;

   // return true if valid texture coordinates
   return *u>=0 && *u<=1 && *v>=0 && *v<=1;
}

// set the (lx,ly) pixel on the lensmap to the (sx,sy,sz) view vector
static void set_lensmap_from_ray(int lx, int ly, double sx, double sy, double sz)
{
   vec3_t ray = {sx,sy,sz};

   // get plate index
   int plate_index = ray_to_plate_index(ray);
   if (plate_index < 0) {
      return;
   }

   // get texture coordinates
   double u,v;
   if (!ray_to_plate_uv(&plates[plate_index], ray, &u, &v)) {
      return;
   }

   // map lens pixel to plate pixel
   set_lensmap_from_plate_uv(lx,ly,u,v,plate_index);
}

static int resume_lensmap_inverse(void)
{
   // image coordinates
   double x,y;

   // lens coordinates
   int lx, *ly;

   start_lens_builder_clock();
   for(ly = &(lens_builder.inverse_state.ly); *ly >= 0; --(*ly))
   {
      // pause building if we have exceeded time allowed per frame
      if (is_lens_builder_time_up()) {
         return true; 
      }

      y = -(*ly-height/2) * scale;

      // calculate all the pixels in this row
      for(lx = 0;lx<width;++lx)
      {
         x = (lx-width/2) * scale;

         // determine which light ray to follow
         vec3_t ray;
         int status = lua_lens_inverse(x,y,ray);
         if (status == 0) {
            continue;
         }
         else if (status == -1) {
            return false;
         }

         // get the pixel belonging to the light ray
         set_lensmap_from_ray(lx,*ly,ray[0],ray[1],ray[2]);
      }
   }

   // done building lens
   return false;
}

// convenience function for forward map calculation:
//    maps uv coordinate on a texture to a screen coordinate
static int uv_to_screen(int plate_index, double u, double v, int *lx, int *ly)
{
   // get ray from uv coordinates
   vec3_t ray;
   plate_uv_to_ray(&plates[plate_index], u, v, ray);

   // map ray to image coordinates
   double x,y;
   int status = lua_lens_forward(ray,&x,&y);
   if (status == 0 || status == -1) { return status; }

   // map image to screen coordinates
   *lx = (int)(x/scale + width/2);
   *ly = (int)(-y/scale + height/2);

   return status;
}

// fills a quad on the lensmap using the given plate coordinate
static void drawQuad(int *tl, int *tr, int *bl, int *br,
      int plate_index, int px, int py)
{
   // array for quad corners in clockwise order
   int *p[] = { tl, tr, br, bl };

   // get bounds
   int x = tl[0], y = tl[1];
   int miny=y, maxy=y;
   int minx=x, maxx=x;
   int i;
   for (i=1; i<4; i++) {
      int tx = p[i][0];
      if (tx < minx) { minx = tx; }
      else if (tx > maxx) { maxx = tx; }

      int ty = p[i][1];
      if (ty < miny) { miny = ty; }
      else if (ty > maxy) { maxy = ty; }
   }

   // temp solution for keeping quads from wrapping around
   //    the boundaries of the image. I guess that quads
   //    will not get very big unless they're doing wrapping.
   // actual clipping will require knowledge of the boundary.
   const int maxdiff = 20;
   if (abs(minx-maxx) > maxdiff || abs(miny-maxy) > maxdiff) {
      return;
   }

   // pixel
   if (miny == maxy && minx == maxx) {
      set_lensmap_from_plate(x,y,px,py,plate_index);
      return;
   }

   // horizontal line
   if (miny == maxy) {
      int tx;
      for (tx=minx; tx<=maxx; ++tx) {
         set_lensmap_from_plate(tx,miny,px,py,plate_index);
      }
      return;
   }

   // vertical line
   if (minx == maxx) {
      int ty;
      for (ty=miny; ty<=maxy; ++ty) {
         set_lensmap_from_plate(x,ty,px,py,plate_index);
      }
      return;
   }

   // quad
   for (y=miny; y<=maxy; ++y) {

      // get x points
      int tx[2] = {minx,maxx};
      int txi=0; // tx index
      int j=3;
      for (i=0; i<4; ++i) {
         int ix = p[i][0], iy = p[i][1];
         int jx = p[j][0], jy = p[j][1];
         if ((iy < y && y <= jy) || (jy < y && y <= iy)) {
            double dy = jy-iy;
            double dx = jx-ix;
            tx[txi] = (int)(ix + (y-iy)/dy*dx);
            if (++txi == 2) break;
         }
         j=i;
      }

      // order x points
      if (tx[0] > tx[1]) {
         int temp = tx[0];
         tx[0] = tx[1];
         tx[1] = temp;
      }

      // sanity check on distance
      if (tx[1] - tx[0] > maxdiff)
      {
         Con_Printf("%d > maxdiff\n", tx[1]-tx[0]);
         return;
      }

      // draw horizontal line between x points
      for (x=tx[0]; x<=tx[1]; ++x) {
         set_lensmap_from_plate(x,y,px,py,plate_index);
      }
   }
}

static int resume_lensmap_forward(void)
{
   int *top = lens_builder.forward_state.top;
   int *bot = lens_builder.forward_state.bot;
   int *py = &(lens_builder.forward_state.py);
   int *plate_index = &(lens_builder.forward_state.plate_index);

   start_lens_builder_clock();
   for (; *plate_index < numplates; ++(*plate_index))
   {
      int px;
      for (; *py >=0; --(*py)) {

         // pause building if we have exceeded time allowed per frame
         if (is_lens_builder_time_up()) {
            return true; 
         }

         // FIND ALL DESTINATION SCREEN COORDINATES FOR THIS TEXTURE ROW ********************

         // compute lower points
         if (*py == platesize-1) {
            double v = (*py + 0.5) / platesize;
            for (px = 0; px < platesize; ++px) {
               // compute left point
               if (px == 0) {
                  double u = (px - 0.5) / platesize;
                  int status = uv_to_screen(*plate_index, u, v, &bot[0], &bot[1]);
                  if (status == 0) continue; else if (status == -1) return false;
               }
               // compute right point
               double u = (px + 0.5) / platesize;
               int index = 2*(px+1);
               int status = uv_to_screen(*plate_index, u, v, &bot[index], &bot[index+1]);
               if (status == 0) continue; else if (status == -1) return false;
            }
         }
         else {
            // swap references so that the previous bottom becomes our current top
            int *temp = top;
            top = bot;
            bot = temp;
         }

         // compute upper points
         double v = (*py - 0.5) / platesize;
         for (px = 0; px < platesize; ++px) {
            // compute left point
            if (px == 0) {
               double u = (px - 0.5) / platesize;
               int status = uv_to_screen(*plate_index, u, v, &top[0], &top[1]);
               if (status == 0) continue; else if (status == -1) return false;
            }
            // compute right point
            double u = (px + 0.5) / platesize;
            int index = 2*(px+1);
            int status = uv_to_screen(*plate_index, u, v, &top[index], &top[index+1]);
            if (status == 0) continue; else if (status == -1) return false;
         }

         // DRAW QUAD FOR EACH PIXEL IN THIS TEXTURE ROW ***********************************

         v = ((double)*py)/platesize;
         for (px = 0; px < platesize; ++px) {
            
            // skip overlapping region of texture
            double u = ((double)px)/platesize;
            vec3_t ray;
            plate_uv_to_ray(&plates[*plate_index], u, v, ray);
            if (*plate_index != ray_to_plate_index(ray)) {
               continue;
            }

            int index = 2*px;
            drawQuad(&top[index], &top[index+2], &bot[index], &bot[index+2], *plate_index,px,*py);
         }

      }

      // reset row position
      // (we have to do it here because it cannot be reset until it is done iterating)
      // (we cannot do it at the beginning because the function could be resumed at some middle row)
      *py = platesize-1;
   }

   free(top);
   free(bot);

   // done building lens
   return false;
}

static void resume_lensmap(void)
{
   if (mapType == MAP_FORWARD) {
      lens_builder.working = resume_lensmap_forward();
   }
   else if (mapType == MAP_INVERSE) {
      lens_builder.working = resume_lensmap_inverse();
   }
}

static void create_lensmap_inverse(void)
{
   // initialize progress state
   lens_builder.inverse_state.ly = height-1;

   resume_lensmap();
}

static void create_lensmap_forward(void)
{
   // initialize progress state
   int *rowa = malloc((platesize+1)*sizeof(int[2]));
   int *rowb = malloc((platesize+1)*sizeof(int[2]));
   lens_builder.forward_state.top = rowa;
   lens_builder.forward_state.bot = rowb;
   lens_builder.forward_state.py = platesize-1;
   lens_builder.forward_state.plate_index = 0;

   resume_lensmap();
}

static void create_lensmap(void)
{
   lens_builder.working = false;

   // render nothing if current lens or globe is invalid
   if (!valid_lens || !valid_globe)
      return;

   // test if this lens can support the current fov
   if (!determine_lens_scale()) {
      //Con_Printf("This lens could not be initialized.\n");
      return;
   }

   // clear the side counts
   memset(plate_display, 0, sizeof(plate_display));

   // create lensmap
   if (mapType == MAP_FORWARD) {
      Con_Printf("using forward map\n");
      create_lensmap_forward();
   }
   else if (mapType == MAP_INVERSE) {
      Con_Printf("using inverse map\n");
      create_lensmap_inverse();
   }
   else { // MAP_NONE
      Con_Printf("no inverse or forward map being used\n");
   }
}

// draw the lensmap to the vidbuffer
static void render_lensmap(void)
{
   B **lmap = lensmap;
   B *pmap = palimap;
   int x, y;
   for(y=0; y<height; y++)
      for(x=0; x<width; x++,lmap++,pmap++)
         if (*lmap) {
            int lx = x+left;
            int ly = y+top;
            if (colorcube) {
               int i = *pmap;
               *VBUFFER(lx,ly) = i != 255 ? palmap[i][**lmap] : **lmap;
            }
            else {
               *VBUFFER(lx,ly) = **lmap;
            }
         }
}

// render a specific plate
static void render_plate(B* plate, vec3_t forward, vec3_t right, vec3_t up) 
{
   // set camera orientation
   VectorCopy(forward, r_refdef.forward);
   VectorCopy(right, r_refdef.right);
   VectorCopy(up, r_refdef.up);

   // render view
   R_PushDlights();
   R_RenderView();

   // copy from vid buffer to cubeface, row by row
   B *vbuffer = VBUFFER(left,top);
   int y;
   for(y = 0;y<platesize;y++) {
      memcpy(plate, vbuffer, platesize);

      // advance to the next row
      vbuffer += vid.rowbytes;
      plate += platesize;
   }
}

void L_RenderView(void)
{
   static int pwidth = -1;
   static int pheight = -1;

   // update screen size
   left = scr_vrect.x;
   top = scr_vrect.y;
   width = scr_vrect.width; 
   height = scr_vrect.height;
   platesize = (width < height) ? width : height;
   int area = width*height;
   int sizechange = pwidth!=width || pheight!=height;

   // allocate new buffers if size changes
   if(sizechange)
   {
      if(platemap) free(platemap);
      if(lensmap) free(lensmap);
      if(palimap) free(palimap);

      platemap = (B*)malloc(platesize*platesize*MAX_PLATES*sizeof(B));
      lensmap = (B**)malloc(area*sizeof(B*));
      palimap = (B*)malloc(area*sizeof(B));
      
      // the rude way
      if(!platemap || !lensmap || !palimap) {
         Con_Printf("Quake-Lenses: could not allocate enough memory\n");
         exit(1); 
      }
   }

   // recalculate lens
   if (sizechange || fovchange || lenschange || globechange) {
      memset(lensmap, 0, area*sizeof(B*));
      memset(palimap, 255, area*sizeof(B));

      // load lens again
      // (NOTE: this will be the second time this lens will be loaded in this frame if it has just changed)
      // (I'm just trying to force re-evaluation of lens variables that are dependent on globe variables (e.g. "lens_width = numplates" in debug.lua))
      valid_lens = lua_lens_load();
      if (!valid_lens) {
         strcpy(lens,"");
         Con_Printf("not a valid lens\n");
      }
      create_lensmap();
   }
   else if (lens_builder.working) {
      resume_lensmap();
   }

   // get the orientations required to render the plates
   vec3_t forward, right, up;
   AngleVectors(r_refdef.viewangles, forward, right, up);

   // do not do this every frame?
   extern int sb_lines;
   extern vrect_t scr_vrect;
   vrect_t vrect;
   vrect.x = 0;
   vrect.y = 0;
   vrect.width = vid.width;
   vrect.height = vid.height;
   R_SetVrect(&vrect, &scr_vrect, sb_lines);

   // render plates
   int i;
   for (i=0; i<numplates; ++i)
   {
      if (plate_display[i]) {

         B* plate = platemap+platesize*platesize*i;
         plate_t *p = &plates[i];

         // set view to change plate FOV
         renderfov = p->fov;
         R_ViewChanged(&vrect, sb_lines, vid.aspect);

         // compute absolute view vectors
         // right = x
         // top = y
         // forward = z

         vec3_t r = { 0,0,0};
         VectorMA(r, p->right[0], right, r);
         VectorMA(r, p->right[1], up, r);
         VectorMA(r, p->right[2], forward, r);

         vec3_t u = { 0,0,0};
         VectorMA(u, p->up[0], right, u);
         VectorMA(u, p->up[1], up, u);
         VectorMA(u, p->up[2], forward, u);

         vec3_t f = { 0,0,0};
         VectorMA(f, p->forward[0], right, f);
         VectorMA(f, p->forward[1], up, f);
         VectorMA(f, p->forward[2], forward, f);

         render_plate(plate, f, r, u);
      }
   }

   // save plates upon request from the "saveglobe" command
   if (save_globe) {
      SaveGlobe();
   }

   // render our view
   Draw_TileClear(0, 0, vid.width, vid.height);
   render_lensmap();

   // store current values for change detection
   pwidth = width;
   pheight = height;

   // reset change flags
   lenschange = globechange = fovchange = 0;
}

// vim: et:ts=3:sts=3:sw=3
