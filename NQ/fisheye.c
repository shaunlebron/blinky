/*

FISHEYE.C
=========

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

   Coordinate System:
      
               +Y = up
                  ^
                  |
                  |
                  |    / +Z = forward
                  |   /
                  |  /
                  | /
                  0------------------> +X = right

   NOTE: Plate coordinates are relative to the camera's frame.  They are NOT absolute coordinates.

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
   This is done with either a "forward" or "inverse" mapping.

             ---------
             |       |
             |       |                              -----------------------------
             |       |                              |\--                     --/|
             ---------                      FORWARD |   \---             ---/   |
   --------- --------- --------- ---------  ------> |       \-----------/       |
   |       | |       | |       | |       |          |        |         |        |
   |       | |       | |       | |       |          |        |         |        |
   |       | |       | |       | |       |          |        |         |        |
   --------- --------- --------- ---------  <------ |       /-----------\       |
             ---------                      INVERSE |   /---             ---\   |
             |       |                              |/--                     --\|
             |       |                              -----------------------------
             |       |
             ---------

   A "FORWARD" mapping does GLOBE -> LENS.
   An "INVERSE" mapping does LENS -> GLOBE (this is faster!).

   GLOBE COORDINATES:

      (you can use any of the following coord systems to get a globe pixel):

      - direction vector

               +Y = up
                  ^
                  |
                  |
                  |    / +Z = forward
                  |   /
                  |  /
                  | /
                  0------------------> +X = right

      - latitude/longitude (spherical degrees)

               +latitude (degrees up)
                  ^
                  |
                  |
                  |
                  |
                  |
                  |
                  0------------------> +longitude (degrees right)

      - plate index & uv (e.g. plate=1, u=0.5, v=0.5 to get the center pixel of plate 1)

                  0----------> +u (max 1)
                  | ---------
                  | |       |
                  | |       |
                  | |       |
                  | ---------
                  V
                  +v (max 1)

   LENS COORDINATES:

                 +Y
                  ^
                  |
                  |
                  |
                  |
                  |
                  |
                  0----------------> +X

ZOOMING
-------

   To control how much of the resulting lens image we can see on screen,
   we scale it such that the screen aligns with certain points on the lens' axes.

   These alignment points are defined in terms of either:
      - FOV
      OR
      - lens boundary
      

   -------------------------------------------------------------------------
   | LENS IMAGE                        ^                                   |
   |                                   |                                   |
   |                                   |                                   |
   |                 ------------------X-------------------                |
   |                 | SCREEN          |                  |                |
   |                 |                 |                  |                |
   |                 |                 |                  |                |
   |                 |                 0------------------X--------------> |
   |                 |                                    |                |
   |                 |                                    |                |
   |                 |                                    |                |
   |                 --------------------------------------                |
   |                                                                       |
   |                                                                       |
   |                                                                       |
   -------------------------------------------------------------------------

   Sometimes the lens image can be infinite, so we can resort to FOV.

*/

#include "bspfile.h"
#include "client.h"
#include "cmd.h"
#include "console.h"
#include "cvar.h"
#include "draw.h"
#include "fisheye.h"
#include "host.h"
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
qboolean fisheye_enabled;

// This is a globally accessible variable that is used to set the fov of each
// camera view that we render.
double fisheye_plate_fov;

// Lens computation is slow, so we don't want to block the game while its busy.
// Instead of dealing with threads, we are just limiting the time that the
// lens builder can work each frame.  It keeps track of its work between frames
// so it can resume without problems.  This allows the user to watch the lens
// pixels become visible as they are calculated.
static struct _lens_builder
{
   qboolean working;
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

static struct _globe {

   // name of the current globe
   char name[50];

   // indicates if the current globe is valid
   qboolean valid;

   // indiciates if the lens has changed and needs updating
   qboolean changed;

   // the environment map
   // a large array of pixels that hold all rendered views
   byte *pixels;  
   // retrieves a pointer to a pixel in the platemap
   #define GLOBEPIXEL(plate,x,y) (globe.pixels + (plate)*(globe.platesize)*(globe.platesize) + (x) + (y)*(globe.platesize))

   // globe plates
   #define MAX_PLATES 6
   struct {
      vec3_t forward;
      vec3_t right;
      vec3_t up;
      vec_t fov;
      vec_t dist;
      byte palette[256];
      int display;
   } plates[MAX_PLATES];

   // number of plates used by the current globe
   int numplates;

   // size of each rendered square plate in the vid buffer
   int platesize;

   // set when we want to save each globe plate
   // (make sure they are visible (i.e. current lens is using all plates))
   struct {
      qboolean should;
      int with_margins;
      char name[32];
   } save;

} globe;

static struct _lens {

   // boolean signaling if the lens is properly loaded
   qboolean valid;

   // boolean signaling if the lens has changed and needs updating
   qboolean changed;

   // name of the current lens
   char name[50];

   // the type of map projection (inverse/forward)
   enum { MAP_NONE, MAP_INVERSE, MAP_FORWARD } map_type;

   // size of the lens image in its arbitrary units
   double width, height;

   // controls the zoom of the lens image
   // (scale = units per pixel)
   double scale;

   // pixel size of the lens view (it is equal to the screen size below):
   //    ------------------
   //    |                |
   //    |   ---------- ^ |
   //    |   |        | | |
   //    |   | screen | h |
   //    |   |        | | |
   //    |   ---------- v |
   //    |   <---w---->   |
   //    |----------------|
   //    |   status bar   |
   //    |----------------|
   int width_px, height_px;

   // array of pointers (*) to plate pixels
   // (the view constructed by the lens)
   //
   //    **************************    ^
   //    **************************    |
   //    **************************    |
   //    **************************  height_px
   //    **************************    |
   //    **************************    |
   //    **************************    v
   // 
   //    <------- width_px ------->
   // 
   byte **pixels;

   // retrieves a pointer to a lens pixel
   #define LENSPIXEL(x,y) (lens.pixels + (x) + (y)*lens.width_px)

   // a color tint index (i) for each pixel (255 = no filter)
   // (new color = globe.plates[i].palette[old color])
   // (used for displaying transparent colored overlays over certain pixels)
   //
   //    iiiiiiiiiiiiiiiiiiiiiiiiii    ^
   //    iiiiiiiiiiiiiiiiiiiiiiiiii    |
   //    iiiiiiiiiiiiiiiiiiiiiiiiii    |
   //    iiiiiiiiiiiiiiiiiiiiiiiiii  height_px
   //    iiiiiiiiiiiiiiiiiiiiiiiiii    |
   //    iiiiiiiiiiiiiiiiiiiiiiiiii    |
   //    iiiiiiiiiiiiiiiiiiiiiiiiii    v
   // 
   //    <------- width_px ------->
   // 
   byte *pixel_tints;

   // retrieves a pointer to a lens pixel tint
   #define LENSPIXELTINT(x,y) (lens.pixel_tints + (x) + (y)*lens.width_px)

} lens;

static struct _zoom {

   qboolean changed;

   enum { ZOOM_NONE, ZOOM_FOV, ZOOM_VFOV, ZOOM_COVER, ZOOM_CONTAIN } type;

   // desired FOV in degrees
   int fov;

   // maximum FOV width and height of the current lens in degrees
   int max_vfov, max_fov;

} zoom;

static struct _rubix {

   // boolean signaling if rubix should be drawn
   qboolean enabled;

   int numcells;
   double cell_size;
   double pad_size;

   // We color a plate like the side of a rubix cube so we
   // can see how the lens distorts the plates. (indicatrix)
   //
   // EXAMPLE:
   //
   //    numcells  = 3
   //    cell_size = 2
   //    pad_size  = 1
   //
   //  A globe plate is split into a grid of "units"
   //  The squares that are colored below are called "cells".
   //  You can see below that there are 3x3 colored cells,
   //  since numcells=3.
   //
   //  You can see below that each cell is 2x2 units large,
   //  since cell_size=2.
   //
   //  Finally, you can see the cells have 1 unit of padding,
   //  since pad_size=1.
   //
   //    ---------------------------------------------------
   //    |    |    |    |    |    |    |    |    |    |    |
   //    |    |    |    |    |    |    |    |    |    |    |
   //    |----|----|----|----|----|----|----|----|----|----|
   //    |    |XXXXXXXXX|    |XXXXXXXXX|    |XXXXXXXXX|    |
   //    |    |XXXXXXXXX|    |XXXXXXXXX|    |XXXXXXXXX|    |
   //    |----|XXXXXXXXX|----|XXXXXXXXX|----|XXXXXXXXX|----|
   //    |    |XXXXXXXXX|    |XXXXXXXXX|    |XXXXXXXXX|    |
   //    |    |XXXXXXXXX|    |XXXXXXXXX|    |XXXXXXXXX|    |
   //    |----|----|----|----|----|----|----|----|----|----|
   //    |    |    |    |    |    |    |    |    |    |    |
   //    |    |    |    |    |    |    |    |    |    |    |
   //    |----|----|----|----|----|----|----|----|----|----|
   //    |    |XXXXXXXXX|    |XXXXXXXXX|    |XXXXXXXXX|    |
   //    |    |XXXXXXXXX|    |XXXXXXXXX|    |XXXXXXXXX|    |
   //    |----|XXXXXXXXX|----|XXXXXXXXX|----|XXXXXXXXX|----|
   //    |    |XXXXXXXXX|    |XXXXXXXXX|    |XXXXXXXXX|    |
   //    |    |XXXXXXXXX|    |XXXXXXXXX|    |XXXXXXXXX|    |
   //    |----|----|----|----|----|----|----|----|----|----|
   //    |    |    |    |    |    |    |    |    |    |    |
   //    |    |    |    |    |    |    |    |    |    |    |
   //    |----|----|----|----|----|----|----|----|----|----|
   //    |    |XXXXXXXXX|    |XXXXXXXXX|    |XXXXXXXXX|    |
   //    |    |XXXXXXXXX|    |XXXXXXXXX|    |XXXXXXXXX|    |
   //    |----|XXXXXXXXX|----|XXXXXXXXX|----|XXXXXXXXX|----|
   //    |    |XXXXXXXXX|    |XXXXXXXXX|    |XXXXXXXXX|    |
   //    |    |XXXXXXXXX|    |XXXXXXXXX|    |XXXXXXXXX|    |
   //    |----|----|----|----|----|----|----|----|----|----|
   //    |    |    |    |    |    |    |    |    |    |    |
   //    |    |    |    |    |    |    |    |    |    |    |
   //    ---------------------------------------------------

} rubix;

// -------------------------------------------------------------------------------- 
// |                                                                              |
// |                      VARIABLE / FUNCTION DECLARATIONS                        |
// |                                                                              |
// --------------------------------------------------------------------------------

// public main functions
void F_Init(void);
void F_Shutdown(void);
void F_WriteConfig(FILE* f);
void F_RenderView(void);

static void F_InitLua(void);

// console commands
static void F_Fisheye(void);
static void F_Lens(void);
static void F_Globe(void);
static void F_Fov(void);
static void F_VFov(void);
static void F_DumpPalette(void);
static void F_Rubix(void);
static void F_RubixGrid(void);
static void F_Cover(void);
static void F_Contain(void);
static void F_SaveGlobe(void);

// console autocomplete helpers
static struct stree_root * F_LensArg(const char *arg);
static struct stree_root * F_GlobeArg(const char *arg);

// lens builder timing functions
static void start_lens_builder_clock(void);
static qboolean is_lens_builder_time_up(void);

// palette functions
static int find_closest_pal_index(int r, int g, int b);
static void create_palmap(void);

// c->lua (c functions for use in lua)
static int CtoLUA_latlon_to_ray(lua_State *L);
static int CtoLUA_ray_to_latlon(lua_State *L);
static int CtoLUA_plate_to_ray(lua_State *L);

// lua->c (lua functions for use in c)
static int LUAtoC_lens_inverse(double x, double y, vec3_t ray);
static int LUAtoC_lens_forward(vec3_t ray, double *x, double *y);
static int LUAtoC_globe_plate(vec3_t ray, int *plate);

// functions to manage the data and functions in the Lua interpreter state
static qboolean LUA_load_lens(void);
static qboolean LUA_load_globe(void);
static void LUA_clear_lens(void);
static void LUA_clear_globe(void);

// lua helpers
static qboolean lua_func_exists(const char* name);

// zoom functions
static qboolean calcZoom(void);
static void clearZoom(void);
static void printZoom(void);

// lens pixel setters
static void set_lensmap_grid(int lx, int ly, int px, int py, int plate_index);
static void set_lensmap_from_plate(int lx, int ly, int px, int py, int plate_index);
static void set_lensmap_from_plate_uv(int lx, int ly, double u, double v, int plate_index);
static void set_lensmap_from_ray(int lx, int ly, double sx, double sy, double sz);

// globe plate getters
static int ray_to_plate_index(vec3_t ray);
static qboolean ray_to_plate_uv(int plate_index, vec3_t ray, double *u, double *v);

// pure coordinate convertors
static void latlon_to_ray(double lat, double lon, vec3_t ray);
static void ray_to_latlon(vec3_t ray, double *lat, double *lon);
static void plate_uv_to_ray(int plate_index, double u, double v, vec3_t ray);

// forward map getter/setter helpers
static int uv_to_screen(int plate_index, double u, double v, int *lx, int *ly);
static void drawQuad(int *tl, int *tr, int *bl, int *br, int plate_index, int px, int py);

// lens builder resumers
static void resume_lensmap(void);
static qboolean resume_lensmap_inverse(void);
static qboolean resume_lensmap_forward(void);

// lens creators
static void create_lensmap_inverse(void);
static void create_lensmap_forward(void);
static void create_lensmap(void);

// renderers
static void render_lensmap(void);
static void render_plate(int plate_index, vec3_t forward, vec3_t right, vec3_t up);

// globe saver functions
static void WritePCXplate(char *filename, int plate_index, int with_margins);
static void SaveGlobe(void);


// -------------------------------------------------------------------------------- 
// |                                                                              |
// |                          STILL ORGANIZING BELOW                              |
// |                                                                              |
// --------------------------------------------------------------------------------

static void start_lens_builder_clock(void) {
   lens_builder.start_time = clock();
}
static qboolean is_lens_builder_time_up(void) {
   clock_t time = clock() - lens_builder.start_time;
   float s = ((float)time) / CLOCKS_PER_SEC;
   return (s >= lens_builder.seconds_per_frame);
}

// retrieves a pointer to a pixel in the video buffer
#define VBUFFER(x,y) (vid.buffer + (x) + (y)*vid.rowbytes)

// find closest pallete index for color
static int find_closest_pal_index(int r, int g, int b)
{
   int i;
   int mindist = 256*256*256;
   int minindex = 0;
   byte* pal = host_basepal;
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
   
      byte* pal = host_basepal;
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

         globe.plates[j].palette[i] = find_closest_pal_index(r,g,b);

         pal += 3;
      }
   }
}

static void F_DumpPalette(void)
{
   int i;
   byte *pal = host_basepal;
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

static void F_Rubix(void)
{
   rubix.enabled = !rubix.enabled;
   Con_Printf("Rubix is %s\n", rubix.enabled ? "ON" : "OFF");
}

static void F_RubixGrid(void)
{
   if (Cmd_Argc() == 4) {
      rubix.numcells = Q_atof(Cmd_Argv(1));
      rubix.cell_size = Q_atof(Cmd_Argv(2));
      rubix.pad_size = Q_atof(Cmd_Argv(3));
      lens.changed = true; // need to recompute lens to update grid
   }
   else {
      Con_Printf("RubixGrid <numcells> <cellsize> <padsize>\n");
      Con_Printf("   numcells (default 10) = %d\n", rubix.numcells);
      Con_Printf("   cellsize (default  4) = %f\n", rubix.cell_size);
      Con_Printf("   padsize  (default  1) = %f\n", rubix.pad_size);
   }
}

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

static void F_InitLua(void)
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

   lua_pushcfunction(lua, CtoLUA_latlon_to_ray);
   lua_setglobal(lua, "latlon_to_ray");

   lua_pushcfunction(lua, CtoLUA_ray_to_latlon);
   lua_setglobal(lua, "ray_to_latlon");

   lua_pushcfunction(lua, CtoLUA_plate_to_ray);
   lua_setglobal(lua, "plate_to_ray");
}

static void clearZoom(void)
{
   zoom.type = ZOOM_NONE;
   zoom.fov = 0;
   zoom.changed = true; // trigger change
}

static void F_Cover(void)
{
   clearZoom();
   zoom.type = ZOOM_COVER;
}

static void F_Contain(void)
{
   clearZoom();
   zoom.type = ZOOM_CONTAIN;
}

void F_WriteConfig(FILE* f)
{
   fprintf(f,"fisheye %d\n", fisheye_enabled);
   fprintf(f,"f_lens \"%s\"\n", lens.name);
   fprintf(f,"f_globe \"%s\"\n", globe.name);
   fprintf(f,"f_rubixgrid %d %f %f\n", rubix.numcells, rubix.cell_size, rubix.pad_size);
   switch (zoom.type) {
      case ZOOM_FOV:     fprintf(f,"f_fov %d\n", zoom.fov); break;
      case ZOOM_VFOV:    fprintf(f,"f_vfov %d\n", zoom.fov); break;
      case ZOOM_COVER:   fprintf(f,"f_cover\n"); break;
      case ZOOM_CONTAIN: fprintf(f,"f_contain\n"); break;
      default: break;
   }
}

static void printZoom(void)
{
   Con_Printf("Zoom currently: ");
   switch (zoom.type) {
      case ZOOM_FOV:     Con_Printf("f_fov %d", zoom.fov); break;
      case ZOOM_VFOV:    Con_Printf("f_vfov %d", zoom.fov); break;
      case ZOOM_COVER:   Con_Printf("f_cover"); break;
      case ZOOM_CONTAIN: Con_Printf("f_contain"); break;
      default:           Con_Printf("none");
   }
   Con_Printf("\n");
}

static void F_Fisheye(void)
{
   if (Cmd_Argc() < 2) {
      Con_Printf("Currently: ");
      Con_Printf("fisheye %d\n", fisheye_enabled);
      return;
   }
   fisheye_enabled = Q_atoi(Cmd_Argv(1)); // will return 0 if not valid
   vid.recalc_refdef = true;
}

static void F_Fov(void)
{
   if (Cmd_Argc() < 2) { // no fov given
      Con_Printf("f_fov <degrees>: set horizontal FOV\n");
      printZoom();
      return;
   }

   clearZoom();

   zoom.type = ZOOM_FOV;
   zoom.fov = (int)Q_atof(Cmd_Argv(1)); // will return 0 if not valid
}

static void F_VFov(void)
{
   if (Cmd_Argc() < 2) { // no fov given
      Con_Printf("f_vfov <degrees>: set vertical FOV\n");
      printZoom();
      return;
   }

   clearZoom();

   zoom.type = ZOOM_VFOV;
   zoom.fov = (int)Q_atof(Cmd_Argv(1)); // will return 0 if not valid
}

// lens command
static void F_Lens(void)
{
   if (Cmd_Argc() < 2) { // no lens name given
      Con_Printf("f_lens <name>: use a new lens\n");
      Con_Printf("Currently: %s\n", lens.name);
      return;
   }

   // trigger change
   lens.changed = true;

   // get name
   strcpy(lens.name, Cmd_Argv(1));

   // load lens
   lens.valid = LUA_load_lens();
   if (!lens.valid) {
      strcpy(lens.name,"");
      Con_Printf("not a valid lens\n");
   }

   // execute the lens' onload command string if given
   // (this is to provide a user-friendly default view of the lens (e.g. "f_fov 180"))
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
static struct stree_root * F_LensArg(const char *arg)
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

static void F_SaveGlobe(void)
{
   if (Cmd_Argc() < 2) { // no file name given
      Con_Printf("f_saveglobe <name> [full flag=0]: screenshot the globe plates\n");
      return;
   }

   strncpy(globe.save.name, Cmd_Argv(1), 32);

   if (Cmd_Argc() >= 3) {
      globe.save.with_margins = Q_atoi(Cmd_Argv(2));
   }
   else {
      globe.save.with_margins = 0;
   }
   globe.save.should = true;
}

// copied from WritePCXfile in NQ/screen.c
// write a plate 
static void WritePCXplate(char *filename, int plate_index, int with_margins)
{
    // parameters from WritePCXfile
    int platesize = globe.platesize;
    byte *data = GLOBEPIXEL(plate_index,0,0);
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
          plate_uv_to_ray(plate_index, u, v, ray);
          byte col = with_margins || plate_index == ray_to_plate_index(ray) ? *data : 0xFE;

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

   globe.save.should = false;

    D_EnableBackBufferAccess();	// enable direct drawing of console to back

   for (i=0; i<globe.numplates; ++i) 
   {
      snprintf(pcxname, 32, "%s%d.pcx", globe.save.name, i);
      WritePCXplate(pcxname, i, globe.save.with_margins);

    Con_Printf("Wrote %s\n", pcxname);
   }

    D_DisableBackBufferAccess();	// for adapters that can't stay mapped in
    //  for linear writes all the time
}

static void F_Globe(void)
{
   if (Cmd_Argc() < 2) { // no globe name given
      Con_Printf("f_globe <name>: use a new globe\n");
      Con_Printf("Currently: %s\n", globe.name);
      return;
   }

   // trigger change
   globe.changed = true;

   // get name
   strcpy(globe.name, Cmd_Argv(1));

   // load globe
   globe.valid = LUA_load_globe();
   if (!globe.valid) {
      strcpy(globe.name,"");
      Con_Printf("not a valid globe\n");
   }
}

// autocompletion for globe names
static struct stree_root * F_GlobeArg(const char *arg)
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

void F_Init(void)
{
   lens_builder.working = false;
   lens_builder.seconds_per_frame = 1.0f / 60;

   rubix.enabled = false;

   F_InitLua();

   Cmd_AddCommand("fisheye", F_Fisheye);
   Cmd_AddCommand("f_dumppal", F_DumpPalette);
   Cmd_AddCommand("f_rubix", F_Rubix);
   Cmd_AddCommand("f_rubixgrid", F_RubixGrid);
   Cmd_AddCommand("f_cover", F_Cover);
   Cmd_AddCommand("f_contain", F_Contain);
   Cmd_AddCommand("f_fov", F_Fov);
   Cmd_AddCommand("f_vfov", F_VFov);
   Cmd_AddCommand("f_lens", F_Lens);
   Cmd_SetCompletion("f_lens", F_LensArg);
   Cmd_AddCommand("f_globe", F_Globe);
   Cmd_SetCompletion("f_globe", F_GlobeArg);
   Cmd_AddCommand("f_saveglobe", F_SaveGlobe);

   // defaults
   Cmd_ExecuteString("f_globe cube", src_command);
   Cmd_ExecuteString("f_lens panini", src_command);
   Cmd_ExecuteString("f_fov 180", src_command);
   Cmd_ExecuteString("f_rubixgrid 10 4 1", src_command);

   // create palette maps
   create_palmap();
}

void F_Shutdown(void)
{
   lua_close(lua);
}

// -----------------------------------
// Lua Functions
// -----------------------------------

static int CtoLUA_latlon_to_ray(lua_State *L)
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

static int CtoLUA_ray_to_latlon(lua_State *L)
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

static int CtoLUA_plate_to_ray(lua_State *L)
{
   int plate_index = luaL_checknumber(L,1);
   double u = luaL_checknumber(L,2);
   double v = luaL_checknumber(L,3);
   vec3_t ray;
   if (plate_index < 0 || plate_index >= globe.numplates) {
      lua_pushnil(L);
      return 1;
   }

   plate_uv_to_ray(plate_index,u,v,ray);
   lua_pushnumber(L, ray[0]);
   lua_pushnumber(L, ray[1]);
   lua_pushnumber(L, ray[2]);
   return 3;
}

static int LUAtoC_lens_inverse(double x, double y, vec3_t ray)
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

static int LUAtoC_lens_forward(vec3_t ray, double *x, double *y)
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

static int LUAtoC_globe_plate(vec3_t ray, int *plate)
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
static void LUA_clear_lens(void)
{
   CLEARVAR("map");
   CLEARVAR("max_fov");
   CLEARVAR("max_vfov");
   CLEARVAR("lens_width");
   CLEARVAR("lens_height");
   CLEARVAR("lens_inverse");
   CLEARVAR("lens_forward");
   CLEARVAR("onload");

   // set "numplates" var
   lua_pushinteger(lua, globe.numplates);
   lua_setglobal(lua, "numplates");
}

// used to clear the state when switching globes
static void LUA_clear_globe(void)
{
   CLEARVAR("plates");
   CLEARVAR("globe_plate");

   globe.numplates = 0;
}

#undef CLEARVAR

static qboolean lua_func_exists(const char* name)
{
   lua_getglobal(lua, name);
   int exists = lua_isfunction(lua,-1);
   lua_pop(lua, 1); // pop name
   return exists;
}

static qboolean LUA_load_globe(void)
{
   // clear Lua variables
   LUA_clear_globe();

   // set full filename
   char filename[100];
   sprintf(filename, "%s/../globes/%s.lua",com_gamedir,globe.name);

   // check if loaded correctly
   int errcode = 0;
   if ((errcode=luaL_loadfile(lua, filename))) {
      Con_Printf("could not loadfile (%d) \nERROR: %s", errcode, lua_tostring(lua,-1));
      lua_pop(lua,1); // pop error message
      return false;
   }
   else {
      if ((errcode=lua_pcall(lua, 0, 0, 0))) {
         Con_Printf("could not pcall (%d) \nERROR: %s", errcode, lua_tostring(lua,-1));
         lua_pop(lua,1); // pop error message
         return false;
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
      return false;
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
         return false;
      }

      // get forward vector elements
      for (j=0; j<3; ++j) {
         lua_rawgeti(lua, -1, j+1);
         if (!lua_isnumber(lua,-1))
         {
            Con_Printf("plate %d: forward vector: element %d not a number\n", i+1, j+1);
            lua_pop(lua, 4); // pop element, vector, plate, and plates
            return false;
         }
         globe.plates[i].forward[j] = lua_tonumber(lua,-1);
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
         return false;
      }

      // get up vector elements
      for (j=0; j<3; ++j) {
         lua_rawgeti(lua, -1, j+1);
         if (!lua_isnumber(lua,-1))
         {
            Con_Printf("plate %d: up vector: element %d not a number\n", i+1, j+1);
            lua_pop(lua, 4); // pop element, vector, plate, and plates
            return false;
         }
         globe.plates[i].up[j] = lua_tonumber(lua,-1);
         lua_pop(lua,1); // pop element
      }
      lua_pop(lua, 1); // pop up vector

      // calculate right vector (and correct up vector)
      CrossProduct(globe.plates[i].up, globe.plates[i].forward, globe.plates[i].right);
      CrossProduct(globe.plates[i].forward, globe.plates[i].right, globe.plates[i].up);

      // get fov
      lua_rawgeti(lua,-1,3);
      if (!lua_isnumber(lua,-1))
      {
         Con_Printf("plate %d: fov not a number\n", i+1);
      }
      globe.plates[i].fov = lua_tonumber(lua,-1) * M_PI / 180;
      lua_pop(lua, 1); // pop fov

      if (globe.plates[i].fov <= 0)
      {
         Con_Printf("plate %d: fov must > 0\n", i+1);
         return false;
      }

      // calculate distance to camera
      globe.plates[i].dist = 0.5/tan(globe.plates[i].fov/2);
   }
   lua_pop(lua, 1); // pop plates

   globe.numplates = i;

   return true;
}

static qboolean LUA_load_lens(void)
{
   // clear Lua variables
   LUA_clear_lens();

   // set full filename
   char filename[100];
   sprintf(filename,"%s/../lenses/%s.lua",com_gamedir, lens.name);

   // check if loaded correctly
   int errcode = 0;
   if ((errcode=luaL_loadfile(lua, filename))) {
      Con_Printf("could not loadfile (%d) \nERROR: %s", errcode, lua_tostring(lua,-1));
      lua_pop(lua,1); // pop error message
      return false;
   }
   else {
      if ((errcode=lua_pcall(lua, 0, 0, 0))) {
         Con_Printf("could not pcall (%d) \nERROR: %s", errcode, lua_tostring(lua,-1));
         lua_pop(lua,1); // pop error message
         return false;
      }
   }

   // clear current maps
   lens.map_type = MAP_NONE;
   lua_refs.lens_forward = lua_refs.lens_inverse = -1;

   // check if the inverse map function is provided
   lua_getglobal(lua, "lens_inverse");
   if (!lua_isfunction(lua,-1)) {
      Con_Printf("lens_inverse is not found\n");
      lua_pop(lua,1); // pop lens_inverse
   }
   else {
      lua_refs.lens_inverse = luaL_ref(lua, LUA_REGISTRYINDEX);
      lens.map_type = MAP_INVERSE;
   }

   // check if the forward map function is provided
   lua_getglobal(lua, "lens_forward");
   if (!lua_isfunction(lua,-1)) {
      Con_Printf("lens_forward is not found\n");
      lua_pop(lua,1); // pop lens_forward
   }
   else {
      lua_refs.lens_forward = luaL_ref(lua, LUA_REGISTRYINDEX);
      if (lens.map_type == MAP_NONE) {
         lens.map_type = MAP_FORWARD;
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
         lens.map_type = MAP_INVERSE;
      }
      else if (!strcmp(funcname, "lens_forward")) {
         lens.map_type = MAP_FORWARD;
      }
      else {
         Con_Printf("Unsupported map function: %s\n", funcname);
         lua_pop(lua, 1); // pop map
         return false;
      }
   }
   lua_pop(lua,1); // pop map

   lua_getglobal(lua, "max_fov");
   zoom.max_fov = (int)lua_isnumber(lua,-1) ? lua_tonumber(lua,-1) : 0;
   lua_pop(lua,1); // pop max_fov

   lua_getglobal(lua, "max_vfov");
   zoom.max_vfov = (int)lua_isnumber(lua,-1) ? lua_tonumber(lua,-1) : 0;
   lua_pop(lua,1); // pop max_vfov

   lua_getglobal(lua, "lens_width");
   lens.width = lua_isnumber(lua,-1) ? lua_tonumber(lua,-1) : 0;
   lua_pop(lua,1); // pop lens_width

   lua_getglobal(lua, "lens_height");
   lens.height = lua_isnumber(lua,-1) ? lua_tonumber(lua,-1) : 0;
   lua_pop(lua,1); // pop lens_height

   return true;
}

// -----------------------------------
// End Lua Functions
// -----------------------------------

static qboolean calcZoom(void)
{
   // clear lens scale
   lens.scale = -1;

   if (zoom.type == ZOOM_FOV || zoom.type == ZOOM_VFOV)
   {
      // check FOV limits
      if (zoom.max_fov <= 0 || zoom.max_vfov <= 0)
      {
         Con_Printf("max_fov & max_vfov not specified, try \"f_cover\"\n");
         return false;
      }
      else if (zoom.type == ZOOM_FOV && zoom.fov > zoom.max_fov) {
         Con_Printf("fov must be less than %d\n", zoom.max_fov);
         return false;
      }
      else if (zoom.type == ZOOM_VFOV && zoom.fov > zoom.max_vfov) {
         Con_Printf("vfov must be less than %d\n", zoom.max_vfov);
         return false;
      }

      // try to scale based on FOV using the forward map
      if (lua_refs.lens_forward != -1) {
         vec3_t ray;
         double x,y;
         double fovr = zoom.fov * M_PI / 180;
         if (zoom.type == ZOOM_FOV) {
            latlon_to_ray(0,fovr*0.5,ray);
            if (LUAtoC_lens_forward(ray,&x,&y)) {
               lens.scale = x / (lens.width_px * 0.5);
            }
            else {
               Con_Printf("ray_to_xy did not return a valid r value for determining FOV scale\n");
               return false;
            }
         }
         else if (zoom.type == ZOOM_VFOV) {
            latlon_to_ray(fovr*0.5,0,ray);
            if (LUAtoC_lens_forward(ray,&x,&y)) {
               lens.scale = y / (lens.height_px * 0.5);
            }
            else {
               Con_Printf("ray_to_xy did not return a valid r value for determining FOV scale\n");
               return false;
            }
         }
      }
      else
      {
         Con_Printf("Please specify a forward mapping function in your script for FOV scaling\n");
         return false;
      }
   }
   else if (zoom.type == ZOOM_CONTAIN || zoom.type == ZOOM_COVER) { // scale based on fitting

      double fit_width_scale = lens.width / lens.width_px;
      double fit_height_scale = lens.height / lens.height_px;

      qboolean width_provided = (lens.width > 0);
      qboolean height_provided = (lens.height > 0);

      if (!width_provided && height_provided) {
         lens.scale = fit_height_scale;
      }
      else if (width_provided && !height_provided) {
         lens.scale = fit_width_scale;
      }
      else if (!width_provided && !height_provided) {
         Con_Printf("neither lens_height nor lens_width are valid/specified.  Try f_fov instead.\n");
         return false;
      }
      else {
         double lens_aspect = lens.width / lens.height;
         double screen_aspect = (double)lens.width_px / lens.height_px;
         qboolean lens_wider = lens_aspect > screen_aspect;

         if (zoom.type == ZOOM_CONTAIN) {
            lens.scale = lens_wider ? fit_width_scale : fit_height_scale;
         }
         else if (zoom.type == ZOOM_COVER) {
            lens.scale = lens_wider ? fit_height_scale : fit_width_scale;
         }
      }
   }

   // validate scale
   if (lens.scale <= 0) {
      Con_Printf("init returned a scale of %f, which is  <= 0\n", lens.scale);
      return false;
   }

   return true;
}

// ----------------------------------------
// Lens Map Creation
// ----------------------------------------

static void set_lensmap_grid(int lx, int ly, int px, int py, int plate_index)
{
   // designate the palette for this pixel
   // This will set the palette index map such that a grid is shown

   // (This is a block)
   //    |----|----|----|
   //    |    |    |    |
   //    |    |    |    |
   //    |----|----|----|
   //    |    |XXXXXXXXX|
   //    |    |XXXXXXXXX|
   //    |----|XXXXXXXXX|
   //    |    |XXXXXXXXX|
   //    |    |XXXXXXXXX|
   //    |----|----|----|
   double block_size = (rubix.pad_size + rubix.cell_size);

   // (Total number of units across)
   //    ---------------------------------------------------
   //    |    |    |    |    |    |    |    |    |    |    |
   //    |    |    |    |    |    |    |    |    |    |    |
   //    |----|----|----|----|----|----|----|----|----|----|
   double num_units = rubix.numcells * block_size + rubix.pad_size;

   // (the size of one unit)
   double unit_size_px = (double)globe.platesize / num_units;

   // convert pixel coordinates to units
   double ux = (double)px/unit_size_px;
   double uy = (double)py/unit_size_px;

   int ongrid =
      fmod(ux,block_size) < rubix.pad_size ||
      fmod(uy,block_size) < rubix.pad_size;

   if (!ongrid)
      *LENSPIXELTINT(lx,ly) = plate_index;
}

// set a pixel on the lensmap from plate coordinates
static void set_lensmap_from_plate(int lx, int ly, int px, int py, int plate_index)
{
   // check valid lens coordinates
   if (lx < 0 || lx >= lens.width_px || ly < 0 || ly >= lens.height_px) {
      return;
   }

   // check valid plate coordinates
   if (px <0 || px >= globe.platesize || py < 0 || py >= globe.platesize) {
      return;
   }

   // increase the number of times this side is used
   globe.plates[plate_index].display = 1;

   // map the lens pixel to this cubeface pixel
   *LENSPIXEL(lx,ly) = GLOBEPIXEL(plate_index,px,py);

   set_lensmap_grid(lx,ly,px,py,plate_index);
}

// set a pixel on the lensmap from plate uv coordinates
static void set_lensmap_from_plate_uv(int lx, int ly, double u, double v, int plate_index)
{
   // convert to plate coordinates
   int px = (int)(u*globe.platesize);
   int py = (int)(v*globe.platesize);
   
   set_lensmap_from_plate(lx,ly,px,py,plate_index);
}

// retrieves the plate closest to the given ray
static int ray_to_plate_index(vec3_t ray)
{
   int plate_index = 0;

   if (lua_refs.globe_plate != -1) {
      // use user-defined plate selection function
      if (LUAtoC_globe_plate(ray, &plate_index)) {
         return plate_index;
      }
      return -1;
   }

   // maximum dotproduct 
   //  = minimum acos(dotproduct) 
   //  = minimum angle between vectors
   double max_dp = -2;

   int i;
   for (i=0; i<globe.numplates; ++i) {
      double dp = DotProduct(ray, globe.plates[i].forward);
      if (dp > max_dp) {
         max_dp = dp;
         plate_index = i;
      }
   }

   return plate_index;
}

static void plate_uv_to_ray(int plate_index, double u, double v, vec3_t ray)
{
   // transform to image coordinates
   u -= 0.5;
   v -= 0.5;
   v = -v;

   // clear ray
   ray[0] = ray[1] = ray[2] = 0;

   // get euclidean coordinate from texture uv
   VectorMA(ray, globe.plates[plate_index].dist, globe.plates[plate_index].forward, ray);
   VectorMA(ray, u, globe.plates[plate_index].right, ray);
   VectorMA(ray, v, globe.plates[plate_index].up, ray);

   VectorNormalize(ray);
}

static qboolean ray_to_plate_uv(int plate_index, vec3_t ray, double *u, double *v)
{
   // get ray in the plate's relative view frame
   double x = DotProduct(globe.plates[plate_index].right, ray);
   double y = DotProduct(globe.plates[plate_index].up, ray);
   double z = DotProduct(globe.plates[plate_index].forward, ray);

   // project ray to the texture
   double dist = 0.5 / tan(globe.plates[plate_index].fov/2);
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
   if (!ray_to_plate_uv(plate_index, ray, &u, &v)) {
      return;
   }

   // map lens pixel to plate pixel
   set_lensmap_from_plate_uv(lx,ly,u,v,plate_index);
}

static qboolean resume_lensmap_inverse(void)
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

      y = -(*ly-lens.height_px/2) * lens.scale;

      // calculate all the pixels in this row
      for(lx = 0;lx<lens.width_px;++lx)
      {
         x = (lx-lens.width_px/2) * lens.scale;

         // determine which light ray to follow
         vec3_t ray;
         int status = LUAtoC_lens_inverse(x,y,ray);
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
   plate_uv_to_ray(plate_index, u, v, ray);

   // map ray to image coordinates
   double x,y;
   int status = LUAtoC_lens_forward(ray,&x,&y);
   if (status == 0 || status == -1) { return status; }

   // map image to screen coordinates
   *lx = (int)(x/lens.scale + lens.width_px/2);
   *ly = (int)(-y/lens.scale + lens.height_px/2);

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

static qboolean resume_lensmap_forward(void)
{
   int *top = lens_builder.forward_state.top;
   int *bot = lens_builder.forward_state.bot;
   int *py = &(lens_builder.forward_state.py);
   int *plate_index = &(lens_builder.forward_state.plate_index);
   int platesize = globe.platesize;

   start_lens_builder_clock();
   for (; *plate_index < globe.numplates; ++(*plate_index))
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
            plate_uv_to_ray(*plate_index, u, v, ray);
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
   if (lens.map_type == MAP_FORWARD) {
      lens_builder.working = resume_lensmap_forward();
   }
   else if (lens.map_type == MAP_INVERSE) {
      lens_builder.working = resume_lensmap_inverse();
   }
}

static void create_lensmap_inverse(void)
{
   // initialize progress state
   lens_builder.inverse_state.ly = lens.height_px-1;

   resume_lensmap();
}

static void create_lensmap_forward(void)
{
   // initialize progress state
   int *rowa = malloc((globe.platesize+1)*sizeof(int[2]));
   int *rowb = malloc((globe.platesize+1)*sizeof(int[2]));
   lens_builder.forward_state.top = rowa;
   lens_builder.forward_state.bot = rowb;
   lens_builder.forward_state.py = globe.platesize-1;
   lens_builder.forward_state.plate_index = 0;

   resume_lensmap();
}

static void create_lensmap(void)
{
   lens_builder.working = false;

   // render nothing if current lens or globe is invalid
   if (!lens.valid || !globe.valid)
      return;

   // test if this lens can support the current fov
   if (!calcZoom()) {
      //Con_Printf("This lens could not be initialized.\n");
      return;
   }

   // clear the side counts
   int i;
   for (i=0; i<globe.numplates; i++) {
      globe.plates[i].display = 0;
   }

   // create lensmap
   if (lens.map_type == MAP_FORWARD) {
      Con_Printf("using forward map\n");
      create_lensmap_forward();
   }
   else if (lens.map_type == MAP_INVERSE) {
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
   byte **lmap = lens.pixels;
   byte *pmap = lens.pixel_tints;
   int x, y;
   for(y=0; y<lens.height_px; y++)
      for(x=0; x<lens.width_px; x++,lmap++,pmap++)
         if (*lmap) {
            int lx = x+scr_vrect.x;
            int ly = y+scr_vrect.y;
            if (rubix.enabled) {
               int i = *pmap;
               *VBUFFER(lx,ly) = i != 255 ? globe.plates[i].palette[**lmap] : **lmap;
            }
            else {
               *VBUFFER(lx,ly) = **lmap;
            }
         }
}

// render a specific plate
static void render_plate(int plate_index, vec3_t forward, vec3_t right, vec3_t up) 
{
   byte *pixels = GLOBEPIXEL(plate_index, 0, 0);

   // set camera orientation
   VectorCopy(forward, r_refdef.forward);
   VectorCopy(right, r_refdef.right);
   VectorCopy(up, r_refdef.up);

   // render view
   R_PushDlights();
   R_RenderView();

   // copy from vid buffer to cubeface, row by row
   byte *vbuffer = VBUFFER(scr_vrect.x,scr_vrect.y);
   int y;
   for(y = 0;y<globe.platesize;y++) {
      memcpy(pixels, vbuffer, globe.platesize);

      // advance to the next row
      vbuffer += vid.rowbytes;
      pixels += globe.platesize;
   }
}

void F_RenderView(void)
{
   static int pwidth = -1;
   static int pheight = -1;

   // update screen size
   lens.width_px = scr_vrect.width;
   lens.height_px = scr_vrect.height;
   #define MIN(a,b) ((a) < (b) ? (a) : (b))
   int platesize = globe.platesize = MIN(lens.height_px, lens.width_px);
   int area = lens.width_px * lens.height_px;
   int sizechange = (pwidth!=lens.width_px) || (pheight!=lens.height_px);

   // allocate new buffers if size changes
   if(sizechange)
   {
      if(globe.pixels) free(globe.pixels);
      if(lens.pixels) free(lens.pixels);
      if(lens.pixel_tints) free(lens.pixel_tints);

      globe.pixels = (byte*)malloc(platesize*platesize*MAX_PLATES*sizeof(byte));
      lens.pixels = (byte**)malloc(area*sizeof(byte*));
      lens.pixel_tints = (byte*)malloc(area*sizeof(byte));
      
      // the rude way
      if(!globe.pixels || !lens.pixels || !lens.pixel_tints) {
         Con_Printf("Quake-Lenses: could not allocate enough memory\n");
         exit(1); 
      }
   }

   // recalculate lens
   if (sizechange || zoom.changed || lens.changed || globe.changed) {
      memset(lens.pixels, 0, area*sizeof(byte*));
      memset(lens.pixel_tints, 255, area*sizeof(byte));

      // load lens again
      // (NOTE: this will be the second time this lens will be loaded in this frame if it has just changed)
      // (I'm just trying to force re-evaluation of lens variables that are dependent on globe variables (e.g. "lens_width = numplates" in debug.lua))
      lens.valid = LUA_load_lens();
      if (!lens.valid) {
         strcpy(lens.name,"");
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
   for (i=0; i<globe.numplates; ++i)
   {
      if (globe.plates[i].display) {

         // set view to change plate FOV
         fisheye_plate_fov = globe.plates[i].fov;
         R_ViewChanged(&vrect, sb_lines, vid.aspect);

         // compute absolute view vectors
         // right = x
         // top = y
         // forward = z

         vec3_t r = { 0,0,0};
         VectorMA(r, globe.plates[i].right[0], right, r);
         VectorMA(r, globe.plates[i].right[1], up, r);
         VectorMA(r, globe.plates[i].right[2], forward, r);

         vec3_t u = { 0,0,0};
         VectorMA(u, globe.plates[i].up[0], right, u);
         VectorMA(u, globe.plates[i].up[1], up, u);
         VectorMA(u, globe.plates[i].up[2], forward, u);

         vec3_t f = { 0,0,0};
         VectorMA(f, globe.plates[i].forward[0], right, f);
         VectorMA(f, globe.plates[i].forward[1], up, f);
         VectorMA(f, globe.plates[i].forward[2], forward, f);

         render_plate(i, f, r, u);
      }
   }

   // save plates upon request from the "saveglobe" command
   if (globe.save.should) {
      SaveGlobe();
   }

   // render our view
   Draw_TileClear(0, 0, vid.width, vid.height);
   render_lensmap();

   // store current values for change detection
   pwidth = lens.width_px;
   pheight = lens.height_px;

   // reset change flags
   lens.changed = globe.changed = zoom.changed = false;
}

// vim: et:ts=3:sts=3:sw=3
