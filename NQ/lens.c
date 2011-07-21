/*
   TO DO
   - publish Con_Printf to Lua
   - allow manual adjustment of map scale without specifying FOV
   - allow the viewing of the map scale due to the current FOV

   - add Lua interactive mode
   */
// lens.c -- player lens viewing

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

// the Lua state pointer
lua_State *lua;

// type to represent one pixel (one byte)
typedef unsigned char B;

// the environment cubemap
// a large array of pixels that hold all six rendered views
static B *platemap = NULL;  

// the lookup table
// an array of pointers to cubemap pixels
// (the view constructed by the lens)
static B **lensmap = NULL;

// how each pixel in the lensmap is colored
// an array of palette indices
// (used for displaying transparent colored overlays)
static B *palimap = NULL;

// top left coordinates of the view in the vid buffer
static int left, top;

// size of the view in the vid buffer
static int width, height, diag;

// desired FOV in radians
static double fov;

// specific desired FOVs in degrees
static double hfov, vfov, dfov;

// fit sizes
static double hfit_size;
static double vfit_size;

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

// indicates if the current globe supports forward lens mapping
static int globe_forward;

// pointer to the screen dimension (width,height or diag) attached to the desired fov
static int* framesize;

// scale determined from desired zoom level
// (multiplier used to transform lens coordinates to screen coordinates)
static double scale;

// cubemap color display flag
static int colorcube = 0;
static int colorcells = 5;
static int colorwfrac = 5;

// maximum FOV width of the current lens
static double max_vfov;

// maximum FOV height of the current lens
static double max_hfov;

// lua reference map index (for storing a reference to the map function)
static int mapForwardIndex;
static int mapInverseIndex;

static int xyValidIndex;
static int rValidIndex;

// change flags
static int lenschange;
static int globechange;
static int fovchange;

static int mapType;
#define MAP_NONE 0
#define MAP_FORWARD 1
#define MAP_INVERSE 2

static int mapCoord;
#define COORD_NONE      0
#define COORD_RADIAL    1
#define COORD_SPHERICAL 2
#define COORD_EUCLIDEAN 3
#define COORD_PLATE     4

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
   double forward[3];
   double right[3];
   double up[3];
   double fov;
} plate_t;

#define MAX_PLATES 6
plate_t plates[MAX_PLATES];
int numplates;

// the palettes for each cube face used by the rubix filter
static B palmap[MAX_PLATES][256];

// the number of pixels used on each cube face
// (used to skip the rendering of invisible cubefaces)
static int plate_tally[6];

// boolean flags set after looking at the final values of plate_tally
static int plate_display[] = {0,0,0,0,0,0};

// size of each rendered square plate in the vid buffer
static int platesize;

// find closest pallete index for color
int find_closest_pal_index(int r, int g, int b)
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

void create_palmap(void)
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

// -------------------------------------------
// Console Commands
// -------------------------------------------

void L_DumpPalette(void)
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

void L_ShowFovDeprecate(void)
{
   Con_Printf("Please use hfov instead\n");
}

void L_ColorCube(void)
{
   colorcube = colorcube ? 0 : 1;
   Con_Printf("Rubix is %s\n", colorcube ? "ON" : "OFF");
}

/* START CONVERSION LUA HELPER FUNCTIONS */
int lua_latlon_to_ray(lua_State *L);
int lua_ray_to_latlon(lua_State *L);

void latlon_to_ray(double lat, double lon, vec3_t ray)
{
   double clat = cos(lat);
   ray[0] = sin(lon)*clat;
   ray[1] = sin(lat);
   ray[2] = cos(lon)*clat;
}

void ray_to_latlon(vec3_t ray, double *lat, double *lon)
{
   *lon = atan2(ray[0], ray[2]);
   *lat = atan2(ray[1], sqrt(ray[0]*ray[0]+ray[2]*ray[2]));
}

/* END CONVERSION LUA HELPER FUNCTIONS */

void L_InitLua(void)
{
   // create Lua state
   lua = luaL_newstate();

   // open Lua standard libraries
   luaL_openlibs(lua);

   // initialize LuaJIT
   luaopen_jit(lua);

   // initialize LuaJIT optimizer 
   char *cmd = "require(\"jit.opt\").start()";
   int error = luaL_loadbuffer(lua, cmd, strlen(cmd), "jit.opt") || lua_pcall(lua, 0, 0, 0);
   if (error) {
      fprintf(stderr, "%s", lua_tostring(lua, -1));
      lua_pop(lua, 1);  /* pop error message from the stack */
   }

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

   error = luaL_loadbuffer(lua, aliases, strlen(aliases), "aliases") ||
      lua_pcall(lua, 0, 0, 0);
   if (error) {
      fprintf(stderr, "%s", lua_tostring(lua, -1));
      lua_pop(lua, 1);  /* pop error message from the stack */
   }

   lua_pushcfunction(lua, lua_latlon_to_ray);
   lua_setglobal(lua, "latlon_to_ray");

   lua_pushcfunction(lua, lua_ray_to_latlon);
   lua_setglobal(lua, "ray_to_latlon");
}

void clearFov(void)
{
   fit = hfit = vfit = 0;
   fov = hfov = vfov = dfov = 0;
   framesize = 0; // clear framesize pointer
   fovchange = 1; // trigger change
}

void L_HFit(void)
{
   clearFov();
   hfit = 1;
}

void L_VFit(void)
{
   clearFov();
   vfit = 1;
}

void L_Fit(void)
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
   else if (dfov != 0)
   {
      fprintf(f,"dfov %f\n", dfov);
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

   fprintf(f,"lens \"%s\"\n",lens);
   fprintf(f,"globe \"%s\"\n", globe);
}

void printActiveFov(void)
{
   Con_Printf("Currently: ");
   if (hfov != 0) {
      Con_Printf("hfov %d\n",(int)hfov);
   }
   else if (vfov != 0) {
      Con_Printf("vfov %d\n",(int)vfov);
   }
   else if (dfov != 0) {
      Con_Printf("dfov %d\n",(int)dfov);
   }
}

void L_HFov(void)
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

void L_VFov(void)
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

void L_DFov(void)
{
   if (Cmd_Argc() < 2) { // no fov given
      Con_Printf("dfov <degrees>: set diagonal FOV\n");
      printActiveFov();
      return;
   }

   clearFov();
   fovchange = 1;
   dfov = Q_atof(Cmd_Argv(1)); // will return 0 if not valid
   framesize = &diag;
   fov = dfov * M_PI / 180;
}

int lua_lens_load(void);

// lens command
void L_Lens(void)
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

void L_Globe(void)
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
   L_InitLua();

   Cmd_AddCommand("dumppal", L_DumpPalette);
   Cmd_AddCommand("savecube", L_CaptureCubeMap);
   Cmd_AddCommand("fov", L_ShowFovDeprecate);
   Cmd_AddCommand("rubix", L_ColorCube);
   Cmd_AddCommand("hfit", L_HFit);
   Cmd_AddCommand("vfit", L_VFit);
   Cmd_AddCommand("fit", L_Fit);
   Cmd_AddCommand("hfov", L_HFov);
   Cmd_AddCommand("vfov", L_VFov);
   Cmd_AddCommand("dfov", L_DFov);
   Cmd_AddCommand("lens", L_Lens);
   Cmd_SetCompletion("lens", L_LensArg);
   Cmd_AddCommand("globe", L_Globe);
   Cmd_SetCompletion("globe", L_GlobeArg);

   // default view state
   Cmd_ExecuteString("globe cube", src_command);
   Cmd_ExecuteString("lens rectilinear", src_command);
   Cmd_ExecuteString("hfov 90", src_command);

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

int lua_latlon_to_ray(lua_State *L)
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

int lua_ray_to_latlon(lua_State *L)
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

int lua_xy_isvalid(double x, double y)
{
   if (xyValidIndex == -1) {
      return 1;
   }

   lua_rawgeti(lua, LUA_REGISTRYINDEX, xyValidIndex);
   lua_pushnumber(lua,x);
   lua_pushnumber(lua,y);
   lua_call(lua, 2, 1);

   int valid = lua_toboolean(lua,-1);
   lua_pop(lua,1);
   return valid;
}

int lua_r_isvalid(double r)
{
   if (rValidIndex == -1)
      return 1;

   lua_rawgeti(lua, LUA_REGISTRYINDEX, rValidIndex);
   lua_pushnumber(lua,r);
   lua_call(lua, 1, 1);

   int valid = lua_toboolean(lua,-1);
   lua_pop(lua,1);
   return valid;
}

// Inverse Map Functions

int lua_xy_to_ray(double x, double y, vec3_t ray)
{
   if (!lua_xy_isvalid(x,y))
      return 0;

   lua_rawgeti(lua, LUA_REGISTRYINDEX, mapInverseIndex);
   lua_pushnumber(lua, x);
   lua_pushnumber(lua, y);
   lua_call(lua, 2, LUA_MULTRET);

   if (!lua_isnumber(lua, -1)) {
      lua_pop(lua,1);
      return 0;
   }

   ray[0] = lua_tonumber(lua, -3);
   ray[1] = lua_tonumber(lua, -2);
   ray[2] = lua_tonumber(lua, -1);
   lua_pop(lua,3);

   return 1;
}

int lua_r_to_theta(double r, double *theta)
{
   if (!lua_r_isvalid(r))
      return 0;

   lua_rawgeti(lua, LUA_REGISTRYINDEX, mapInverseIndex);
   lua_pushnumber(lua, r);
   lua_call(lua, 1, LUA_MULTRET);

   if (!lua_isnumber(lua, -1)) {
      lua_pop(lua,1);
      return 0;
   }

   *theta = lua_tonumber(lua,-1);
   lua_pop(lua,1);

   return 1;
}

int lua_xy_to_latlon(double x, double y, double *lat, double *lon)
{
   if (!lua_xy_isvalid(x,y))
      return 0;

   lua_rawgeti(lua, LUA_REGISTRYINDEX, mapInverseIndex);
   lua_pushnumber(lua, x);
   lua_pushnumber(lua, y);
   lua_call(lua, 2, LUA_MULTRET);

   if (!lua_isnumber(lua, -1)) {
      lua_pop(lua,1);
      return 0;
   }

   *lat = lua_tonumber(lua, -2);
   *lon = lua_tonumber(lua, -1);
   lua_pop(lua,2);

   return 1;
}

int lua_xy_to_plate(double x, double y, int *plate, double *u, double *v)
{
   if (!lua_xy_isvalid(x,y))
      return 0;

   lua_rawgeti(lua, LUA_REGISTRYINDEX, mapInverseIndex);
   lua_pushnumber(lua, x);
   lua_pushnumber(lua, y);
   lua_call(lua, 2, LUA_MULTRET);

   if (!lua_isnumber(lua, -1)) {
      lua_pop(lua,1);
      return 0;
   }

   *plate = lua_tointeger(lua, -3);
   *u = lua_tonumber(lua, -2);
   *v = lua_tonumber(lua, -1);
   lua_pop(lua,3);
   return 1;
}

// Forward Map Functions

int lua_ray_to_xy(vec3_t ray, double *x, double *y)
{
   lua_rawgeti(lua, LUA_REGISTRYINDEX, mapForwardIndex);
   lua_pushnumber(lua,ray[0]);
   lua_pushnumber(lua,ray[1]);
   lua_pushnumber(lua,ray[2]);
   lua_call(lua, 3, LUA_MULTRET);

   if (!lua_isnumber(lua, -1)) {
      lua_pop(lua,1);
      return 0;
   }

   *x = lua_tonumber(lua, -2);
   *y = lua_tonumber(lua, -1);
   lua_pop(lua,2);

   return 1;
}

int lua_latlon_to_xy(double lat, double lon, double *x, double *y)
{
   lua_rawgeti(lua, LUA_REGISTRYINDEX, mapForwardIndex);
   lua_pushnumber(lua,lat);
   lua_pushnumber(lua,lon);
   lua_call(lua, 2, LUA_MULTRET);

   if (!lua_isnumber(lua, -1)) {
      lua_pop(lua,1);
      return 0;
   }

   *x = lua_tonumber(lua, -2);
   *y = lua_tonumber(lua, -1);
   lua_pop(lua,2);
   return 1;
}

int lua_theta_to_r(double theta, double *r)
{
   lua_rawgeti(lua, LUA_REGISTRYINDEX, mapForwardIndex);
   lua_pushnumber(lua, theta);
   lua_call(lua, 1, LUA_MULTRET);

   if (!lua_isnumber(lua, -1))
   {
      lua_pop(lua,1);
      return 0;
   }

   *r = lua_tonumber(lua,-1);
   lua_pop(lua,1);
   return 1;
}

int lua_plate_to_xy(int plate, double u, double v, double *x, double *y)
{
   lua_rawgeti(lua, LUA_REGISTRYINDEX, mapForwardIndex);
   lua_pushinteger(lua, plate);
   lua_pushnumber(lua, u);
   lua_pushnumber(lua, v);
   lua_call(lua, 3, LUA_MULTRET);

   if (!lua_isnumber(lua, -1))
   {
      lua_pop(lua,1);
      return 0;
   }

   *x = lua_tonumber(lua,-2);
   *y = lua_tonumber(lua,-1);
   lua_pop(lua,2);
   return 1;
}

#define CLEARVAR(var) lua_pushnil(lua); lua_setglobal(lua, var);

// used to clear the state when switching lenses
void lua_lens_clear(void)
{
   CLEARVAR("map");
   CLEARVAR("max_hfov");
   CLEARVAR("max_vfov");
   CLEARVAR("hsym");
   CLEARVAR("vsym");
   CLEARVAR("hfit_size");
   CLEARVAR("vfit_size");
   CLEARVAR("xy_to_latlon");
   CLEARVAR("latlon_to_xy");
   CLEARVAR("r_to_theta");
   CLEARVAR("theta_to_r");
   CLEARVAR("xy_to_ray");
   CLEARVAR("ray_to_xy");
   CLEARVAR("xy_to_plate");
   CLEARVAR("plate_to_xy");
   CLEARVAR("xy_isvalid");
   CLEARVAR("r_isvalid");
}

// used to clear the state when switching globes
void lua_globe_clear(void)
{
   CLEARVAR("plates");
   CLEARVAR("ray_to_plate");
   CLEARVAR("plate_to_ray");

   numplates = 0;
}

#undef CLEARVAR

int lua_func_exists(const char* name)
{
   lua_getglobal(lua, name);
   int exists = lua_isfunction(lua,-1);
   lua_pop(lua,1);
   return exists;
}

int lua_globe_load(void)
{
   // clear Lua variables
   lua_globe_clear();

   // set full filename
   char filename[100];
   sprintf(filename, "%s/../globes/%s.lua",com_gamedir,globe);

   // check if loaded correctly
   if (luaL_dofile(lua, filename) != 0) {
      Con_Printf("could not load %s\nERROR: %s\n", globe, lua_tostring(lua,-1));
      lua_pop(lua, 1);
      return 0;
   }

   // check for the required function
   if (!lua_func_exists("ray_to_plate"))
   {
      Con_Printf("ray_to_plate function was not found\n");
      return 0;
   }

   // check if forward lens maps are supported by this globe
   globe_forward = lua_func_exists("plate_to_ray");

   // load plates array
   lua_getglobal(lua, "plates");
   if (!lua_istable(lua,-1) || lua_objlen(lua,-1) < 1)
   {
      Con_Printf("plates must be an array of one or more elements\n");
      lua_pop(lua, 1);
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
      if (!lua_istable(lua,-1) || lua_objlen(lua,-1) != 3 )
      {
         Con_Printf("plate %d: forward vector is not a 3d vector\n", i+1);
         lua_pop(lua,4); // plates, plate key, plate, forward
         return 0;
      }

      // get forward vector elements
      for (j=0; j<3; ++j) {
         lua_rawgeti(lua, -1, j+1);
         if (!lua_isnumber(lua,-1))
         {
            Con_Printf("plate %d: forward vector: element %d not a number\n", i+1, j+1);
            lua_pop(lua, 5); // plates, plate key, plate, forward, forward[i]
            return 0;
         }
         plates[i].forward[j] = lua_tonumber(lua,-1);
         lua_pop(lua,1); // forward[i]
      }
      lua_pop(lua,1); // forward

      // get up vector
      lua_rawgeti(lua, -1, 2);

      // verify table of length 3
      if (!lua_istable(lua,-1) || lua_objlen(lua,-1) != 3 )
      {
         Con_Printf("plate %d: up vector is not a 3d vector\n", i+1);
         lua_pop(lua,4); // plates, plate key, plate, up
         return 0;
      }

      // get up vector elements
      for (j=0; j<3; ++j) {
         lua_rawgeti(lua, -1, j+1);
         if (!lua_isnumber(lua,-1))
         {
            Con_Printf("plate %d: up vector: element %d not a number\n", i+1, j+1);
            lua_pop(lua, 5); // plates, plate key, plate, up, up[i]
            return 0;
         }
         plates[i].up[j] = lua_tonumber(lua,-1);
         lua_pop(lua,1); // up[i]
      }
      lua_pop(lua,1); // up

      // calculate right vector (and correct up vector)
      CrossProduct(plates[i].forward, plates[i].up, plates[i].right);
      VectorInverse(plates[i].right); // scale by -1 because we're using left hand coordinates
      CrossPRoduct(plates[i].forward, plates[i].right, plates[i].up);

      // get fov
      lua_rawgeti(lua,-1,3);
      if (!lua_isnumber(lua,-1))
      {
         Con_Printf("plate %d: fov not a number\n", i+1);
         lua_pop(lua,4); // plates, plate key, plate, fov
      }
      plates[i].fov = lua_tonumber(lua,-1) * M_PI / 180;
      lua_pop(lua,1); // fov

      if (plates[i].fov <= 0)
      {
         Con_Printf("plate %d: fov must > 0\n", i+1);
         lua_pop(lua, 3); // plates, plate key, plate
         return 0;
      }
   }

   // pop table key and table
   lua_pop(lua,2);
   numplates = i;

   return 1;
}

int lua_lens_load(void)
{
   // clear Lua variables
   lua_lens_clear();

   // set full filename
   char filename[100];
   sprintf(filename,"%s/../lenses/%s.lua",com_gamedir,lens);

   // check if loaded correctly
   if (luaL_dofile(lua, filename) != 0) {
      Con_Printf("could not load %s\nERROR: %s\n", lens, lua_tostring(lua,-1));
      lua_pop(lua, 1);
      return 0;
   }

   // clear current maps
   mapType = MAP_NONE;
   mapForwardIndex = mapInverseIndex = -1;
   mapCoord = COORD_NONE;

#define SETFWD(map) \
   lua_getglobal(lua,#map); \
   mapForwardIndex = luaL_ref(lua, LUA_REGISTRYINDEX);\

#define SETINV(map) \
   lua_getglobal(lua,#map); \
   mapInverseIndex = luaL_ref(lua, LUA_REGISTRYINDEX);\

#define SETFWD_ACTIVE(map,coord) \
   mapCoord = coord; \
   SETFWD(map); \
   mapType = MAP_FORWARD;

#define SETINV_ACTIVE(map,coord) \
   mapCoord = coord; \
   SETINV(map); \
   mapType = MAP_INVERSE;

   // get map function preference if provided
   lua_getglobal(lua, "map");
   if (lua_isstring(lua, -1))
   {
      // get desired map function name
      const char* funcname = lua_tostring(lua, -1);

      // check for valid map function name
      if (!strcmp(funcname, "xy_to_latlon"))       { SETINV_ACTIVE(xy_to_latlon, COORD_SPHERICAL); }
      else if (!strcmp(funcname, "latlon_to_xy"))  { SETFWD_ACTIVE(latlon_to_xy, COORD_SPHERICAL); }
      else if (!strcmp(funcname, "r_to_theta"))    { SETINV_ACTIVE(r_to_theta,   COORD_RADIAL);   }
      else if (!strcmp(funcname, "theta_to_r"))    { SETFWD_ACTIVE(theta_to_r,   COORD_RADIAL);   }
      else if (!strcmp(funcname, "xy_to_ray"))     { SETINV_ACTIVE(xy_to_ray,    COORD_EUCLIDEAN);    }
      else if (!strcmp(funcname, "ray_to_xy"))     { SETFWD_ACTIVE(ray_to_xy,    COORD_EUCLIDEAN);    }
      else if (!strcmp(funcname, "xy_to_plate"))   { SETINV_ACTIVE(xy_to_plate,  COORD_PLATE); }
      else if (!strcmp(funcname, "plate_to_xy"))   { SETFWD_ACTIVE(plate_to_xy,  COORD_PLATE); }
      else {
         Con_Printf("Unsupported map function: %s\n", funcname);
         lua_pop(lua, 1);
         return 0;
      }

      // check if the desired map function is provided
      lua_getglobal(lua, funcname);
      if (!lua_isfunction(lua,-1)) {
         Con_Printf("%s is not found\n", funcname);
         lua_pop(lua,1);
         return 0;
      }

      lua_pop(lua,1);
   }
   else
   {
      // deduce the map function if preference is not provided

      // search for provided map functions, choosing based on priority
      if (lua_func_exists( "r_to_theta"))          { SETINV_ACTIVE(r_to_theta,    COORD_RADIAL);   }
      else if (lua_func_exists( "xy_to_latlon"))   { SETINV_ACTIVE(xy_to_latlon,  COORD_SPHERICAL); }
      else if (lua_func_exists( "xy_to_ray"))      { SETINV_ACTIVE(xy_to_ray,     COORD_EUCLIDEAN);    }
      else if (lua_func_exists( "theta_to_r"))     { SETFWD_ACTIVE(theta_to_r,    COORD_RADIAL);   }
      else if (lua_func_exists( "latlon_to_xy"))   { SETFWD_ACTIVE(latlon_to_xy,  COORD_SPHERICAL); }
      else if (lua_func_exists( "ray_to_xy"))      { SETFWD_ACTIVE(ray_to_xy,     COORD_EUCLIDEAN);    }
      else if (lua_func_exists( "xy_to_plate"))    { SETINV_ACTIVE(xy_to_plate,   COORD_PLATE); }
      else if (lua_func_exists( "plate_to_xy"))    { SETFWD_ACTIVE(plate_to_xy,   COORD_PLATE); }
      else {
         Con_Printf("No map function provided\n");
         lua_pop(lua, 1);
         return 0;
      }
      lua_pop(lua,1);
   }

   // resolve a forward map function if not already defined
   if (mapForwardIndex == -1)
   {
      if (mapCoord == COORD_RADIAL && lua_func_exists("theta_to_r"))        { SETFWD(theta_to_r);   }
      else if (mapCoord == COORD_SPHERICAL && lua_func_exists("latlon_to_xy")) { SETFWD(latlon_to_xy); }
      else if (mapCoord == COORD_EUCLIDEAN && lua_func_exists("ray_to_xy"))    { SETFWD(ray_to_xy);    }
      else if (mapCoord == COORD_PLATE && lua_func_exists("plate_to_xy")){ SETFWD(plate_to_xy); }
      else {
         // TODO: notify if no matching forward function found (meaning no FOV scaling can be done)
      }
   }

#undef SETFWD
#undef SETINV
#undef SETFWD_ACTIVE
#undef SETINV_ACTIVE

   // set inverse coordinate checks
   rValidIndex = xyValidIndex = -1;
   if (mapInverseIndex != -1)
   {
      if (mapCoord == COORD_RADIAL) {
         if (lua_func_exists("r_isvalid")) {
            lua_getglobal(lua, "r_isvalid");
            rValidIndex = luaL_ref(lua, LUA_REGISTRYINDEX);
         }
      }
      else if (mapCoord != COORD_NONE) { // all other maps currently use x,y
         if (lua_func_exists("xy_isvalid")) {
            lua_getglobal(lua, "xy_isvalid");
            xyValidIndex = luaL_ref(lua, LUA_REGISTRYINDEX);
         }
      }
   }

   lua_getglobal(lua, "max_hfov");
   max_hfov = lua_isnumber(lua,-1) ? lua_tonumber(lua,-1) : 0;
   max_hfov *= M_PI / 180;
   lua_pop(lua,1);

   lua_getglobal(lua, "max_vfov");
   max_vfov = lua_isnumber(lua,-1) ? lua_tonumber(lua,-1) : 0;
   max_vfov *= M_PI / 180;
   lua_pop(lua,1);

   lua_getglobal(lua, "vsym");
   vsym = lua_isboolean(lua,-1) ? lua_toboolean(lua,-1) : 0;
   lua_pop(lua,1);

   lua_getglobal(lua, "hsym");
   hsym = lua_isboolean(lua,-1) ? lua_toboolean(lua,-1) : 0;
   lua_pop(lua,1);

   lua_getglobal(lua, "hfit_size");
   hfit_size = lua_isnumber(lua,-1) ? lua_tonumber(lua,-1) : 0;
   lua_pop(lua,1);

   lua_getglobal(lua, "vfit_size");
   vfit_size = lua_isnumber(lua,-1) ? lua_tonumber(lua,-1) : 0;
   lua_pop(lua,1);

   return 1;
}

// -----------------------------------
// End Lua Functions
// -----------------------------------

int determine_lens_scale(void)
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
      if (mapForwardIndex != -1) {
         if (mapCoord == COORD_RADIAL) {
            double r;
            if (lua_theta_to_r(fov*0.5, &r)) {
               scale = r / (*framesize * 0.5);
            }
            else {
               Con_Printf("theta_to_r did not return a valid r value for determining FOV scale\n");
               return 0;
            }
         }
         else if (mapCoord == COORD_SPHERICAL) {
            double x,y;
            if (*framesize == width) {
               if (lua_latlon_to_xy(0,fov*0.5,&x,&y)) {
                  scale = x / (*framesize * 0.5);
               }
               else {
                  Con_Printf("latlon_to_xy did not return a valid r value for determining FOV scale\n");
                  return 0;
               }
            }
            else if (*framesize == height) {
               if (lua_latlon_to_xy(fov*0.5,0,&x,&y)) {
                  scale = y / (*framesize * 0.5);
               }
               else {
                  Con_Printf("latlon_to_xy did not return a valid r value for determining FOV scale\n");
                  return 0;
               }
            }
            else {
               Con_Printf("latlon_to_xy does not support diagonal FOVs\n");
               return 0;
            }
         }
         else if (mapCoord == COORD_EUCLIDEAN) {
            vec3_t ray;
            double x,y;
            if (*framesize == width) {
               latlon_to_ray(0,fov*0.5,ray);
               if (lua_ray_to_xy(ray,&x,&y)) {
                  scale = x / (*framesize * 0.5);
               }
               else {
                  Con_Printf("ray_to_xy did not return a valid r value for determining FOV scale\n");
                  return 0;
               }
            }
            else if (*framesize == height) {
               latlon_to_ray(fov*0.5,0,ray);
               if (lua_ray_to_xy(ray,&x,&y)) {
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
         else if (mapCoord == COORD_PLATE) {
            Con_Printf("plate_to_xy currently not supported for FOV scaling\n");
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
         if (hfit_size <= 0)
         {
            //Con_Printf("Cannot use hfit unless a positive hfit_size is in your script\n");
            Con_Printf("hfit_size not specified.  Try hfov instead.\n");
            return 0;
         }
         scale = hfit_size / width;
      }
      else if (vfit) {
         if (vfit_size <= 0)
         {
            //Con_Printf("Cannot use vfit unless a positive vfit_size is in your script\n");
            Con_Printf("vfit_size not specified.  Try vfov instead.\n");
            return 0;
         }
         scale = vfit_size / height;
      }
      else if (fit) {
         if (hfit_size <= 0 && vfit_size > 0) {
            scale = vfit_size / height;
         }
         else if (vfit_size <=0 && hfit_size > 0) {
            scale = hfit_size / width;
         }
         else if (vfit_size <= 0 && hfit_size <= 0) {
            Con_Printf("vfit_size and hfit_size not specified.  Try hfov instead.\n");
            return 0;
         }
         else if (hfit_size / vfit_size > (double)width / height) {
            scale = hfit_size / width;
         }
         else {
            scale = vfit_size / height;
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

int clamp(int value, int min, int max)
{
   if (value < min) return min;
   if (value > max) return max;
   return value;
}

// ----------------------------------------
// Lens Map Creation
// ----------------------------------------

void set_lensmap_grid(int lx, int ly, int px, int py, int plate)
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
      *PALIMAP(lx,ly) = plate;
}

// set a pixel on the lensmap from plate coordinate
inline void set_lensmap_from_plate(int lx, int ly, double u, double v, int plate)
{
   // increase the number of times this side is used
   ++plate_tally[plate];

   // convert to plate coordinates
   int px = clamp((int)(u*platesize),0,platesize-1);
   int py = clamp((int)(v*platesize),0,platesize-1);

   // map the lens pixel to this cubeface pixel
   *LENSMAP(lx,ly) = PLATEMAP(plate,px,py);

   set_lensmap_grid(lx,ly,px,py,plate);
}

// set the (lx,ly) pixel on the lensmap to the (sx,sy,sz) view vector
void set_lensmap_from_ray(int lx, int ly, double sx, double sy, double sz)
{
   vec3_t ray = {sx,sy,sz};

   double u,v;
   int plate;
   if (!lua_ray_to_plate(ray,&plate,&u,&v)) {
      return;
   }

   // map lens pixel to cubeface pixel
   set_lensmap_from_plate(lx,ly,u,v,plate);
}

void create_lensmap_inverse()
{
   int halfw = width/2;
   int halfh = height/2;
   int maxx = hsym ? halfw : width;
   int maxy = vsym ? halfh : height;

   int lx,ly;

   for(ly = 0;ly<maxy;++ly) {
      double y = -(ly-halfh);
      y *= scale;
      for(lx = 0;lx<maxx;++lx) {
         double x = lx-halfw;
         x *= scale;

         // (use lens)
         // map the current window coordinate to a ray vector
         vec3_t ray = { 0, 0, 1};
         switch (mapCoord)
         {
            case COORD_RADIAL: {
               double r = sqrt(x*x+y*y);
               double theta;
               if (!lua_r_to_theta(r, &theta))
                  continue;
               double s = sin(theta);
               ray[0] = x/r * s;
               ray[1] = y/r * s;
               ray[2] = cos(theta);
               break;
            }

            case COORD_SPHERICAL: {
               double lat,lon;
               if (!lua_xy_to_latlon(x,y,&lat,&lon))
                  continue;
               latlon_to_ray(lat,lon,ray);
               break;
            }

            case COORD_EUCLIDEAN: {
               if (!lua_xy_to_ray(x,y,ray))
                  continue;
               break;
            }

            case COORD_PLATE: {
               int plate;
               double u;
               double v;
               if (lua_xy_to_plate(x,y,&plate,&u,&v))
                  set_lensmap_from_plate(lx, ly, u, v, plate);
               continue;
            }
         }

         // (use globe)
         set_lensmap_from_ray(lx,ly,ray[0],ray[1],ray[2]);
      }
   }
}

void create_lensmap_forward()
{
   int px, py;
   int plate;

   for (plate = 0; plate < numplates; ++plate)
   {
      for (py=0; py<platesize; ++py)
      {
         double v = -(py-platesize*0.5) / platesize;
         for (px=0; px < platesize; ++px)
         {
            double u = (px-platesize*0.5) / platesize;

            // (use globe)
            // get ray from plate coordinates
            vec3_t ray;
            if (!lua_plate_to_ray(plate, u, v, ray)) {
               continue;
            }

            // (use lens)
            // get image coordinates from ray
            double x,y;
            switch (mapCoord) 
            {
               case COORD_RADIAL: {
                  double theta = acos(ray[2]/sqrt(ray[0]*ray[0]+ray[1]*ray[1]+ray[2]*ray[2]));
                  double r;
                  if (!lua_theta_to_r(theta, &r))
                     continue;

                  double c = r/sqrt(ray[0]*ray[0]+ray[1]*ray[1]);
                  x = ray[0]*c;
                  y = ray[1]*c;
                  break;
               }

               case COORD_SPHERICAL: {
                  double lat,lon;
                  ray_to_latlon(ray, &lat, &lon);
                  if (!lua_latlon_to_xy(lat, lon, &x, &y))
                     continue;
                  break;
               }

               case COORD_EUCLIDEAN: {
                  if (!lua_ray_to_xy(ray, &x, &y))
                     continue;
                  break;
               }

               // TODO: add COORD_PLATE?
            }

            // transform from image coordinates to lens coordinates
            x /= scale;
            y /= scale;
            y = -y;
            x += width*0.5;
            y += height*0.5;

            int lx = (int)(x/scale + width/2);
            int ly = (int)(-y/scale + height/2);

            if (lx < 0 || lx >= width || ly < 0 || ly >= height)
               continue;

            set_lensmap_from_plate(lx,ly,u,v,plate);
         }
      }
   }
}

void create_lensmap()
{
   // render nothing if current lens or globe is invalid
   if (!valid_lens || !valid_globe)
      return;

   // test if this lens can support the current fov
   if (!determine_lens_scale()) {
      //Con_Printf("This lens could not be initialized.\n");
      return;
   }

   // clear the side counts
   memset(plate_tally, 0, sizeof(plate_tally));

   // create lensmap
   if (mapType == MAP_FORWARD && globe_forward)
      create_lensmap_forward();
   else if (mapType == MAP_INVERSE)
      create_lensmap_inverse();

   // update face display flags depending on tallied side counts
   int i;
   for(i=0; i<numplates; ++i) {
      plate_display[i] = (plate_tally[i] > 1);
   }
}

// draw the lensmap to the vidbuffer
void render_lensmap()
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
void render_plate(B* plate, vec3_t forward, vec3_t right, vec3_t up) 
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

void L_RenderView() 
{
   static int pwidth = -1;
   static int pheight = -1;

   // update screen size
   left = scr_vrect.x;
   top = scr_vrect.y;
   width = scr_vrect.width; 
   height = scr_vrect.height;
   platesize = (width < height) ? width : height;
   diag = sqrt(width*width+height*height);
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
      create_lensmap();
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

         B* plate = platemap+platesize*i;

         // set view to change plate FOV
         renderfov = plates[i].fov;
         R_ViewChanged(&vrect, sb_lines, vid.aspect);

         // compute absolute view vectors
         // right = x
         // top = y
         // forward = z

         plate_t *p = plates+i;

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

         render_plate(plate, f,r,u);
      }
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

