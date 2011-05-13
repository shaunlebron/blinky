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

cvar_t l_hfov = {"hfov", "90", true};
cvar_t l_vfov = {"vfov", "-1", true};
cvar_t l_dfov = {"dfov", "-1", true};
cvar_t l_lens = {"lens", "rectilinear", true};

cvar_t l_fitmode = {"fitmode", "0", true};
#define FIT_NONE  0
#define FIT_H     1
#define FIT_V     2
#define FIT_BOTH  3

typedef unsigned char B;
static B *cubemap = NULL;  
static B **lensmap = NULL;

// top left coordinates of the screen in the vid buffer
static int left, top;

// size of the screen in the vid buffer
static int width, height, diag;

// size of each rendered cube face in the vid buffer
static int cubesize;

// desired FOV in radians
static double fov;

// fit sizes
static double hfit_size;
static double vfit_size;

// fit mode
static int fit;
static int hfit;
static int vfit;

// name of the current lens
#define MAX_LENS_LEN 50
static char lens[MAX_LENS_LEN];

// pointer to the screen dimension (width,height or diag) attached to the desired fov
static int* framesize;

// multiplier used to transform screen coordinates to lens coordinates
static double scale;

// the number of pixels used on each cube face
// used to only draw the cube faces in use
static int faceDisplay[] = {0,0,0,0,0,0};

// cubemap color display flag
static int colorcube = 0;

// maximum FOV width of the current lens
static double max_vfov;

// maximum FOV height of the current lens
static double max_hfov;

// indicates if the current lens is valid
static int valid_lens;

// lua reference map index (for storing a reference to the map function)
static int mapForwardIndex;
static int mapInverseIndex;

static int xyValidIndex;
static int rValidIndex;

// side count used to determine which cube faces to render
static int side_count[6];

// MAP SYMMETRIES
static int hsym,vsym;
#define NO_SYMMETRY 0
#define H_SYMMETRY 1
#define V_SYMMETRY 2

static int mapType;
#define MAP_NONE 0
#define MAP_FORWARD 1
#define MAP_INVERSE 2

static int mapCoord;
#define COORD_NONE      0
#define COORD_RADIAL    1
#define COORD_SPHERICAL 2
#define COORD_EUCLIDEAN 3
#define COORD_CUBEMAP   4

// cube faces
#define BOX_FRONT  0
#define BOX_RIGHT  1
#define BOX_BEHIND 2
#define BOX_LEFT   3
#define BOX_TOP    4
#define BOX_BOTTOM 5

// retrieves a pointer to a pixel in the video buffer
#define VBUFFER(x,y) (vid.buffer + (x) + (y)*vid.rowbytes)

// retrieves a pointer to a pixel in a designated cubemap face
#define CUBEFACE(side,x,y) (cubemap + (side)*cubesize*cubesize + (x) + (y)*cubesize)

// retrieves a pointer to a pixel in the lensmap
#define LENSMAP(x,y) (lensmap + (x) + (y)*width)

void L_Help(void)
{
   Con_Printf("\nQUAKE LENSES\n--------\n");
   Con_Printf("hfov <degrees>: Specify FOV in horizontal degrees\n");
   Con_Printf("vfov <degrees>: Specify FOV in vertical degrees\n");
   Con_Printf("dfov <degrees>: Specify FOV in diagonal degrees\n");
   Con_Printf("lens <name>: Change the lens\n");
   Con_Printf("\n---------\n");
   Con_Printf("colorcube: toggle paint cubemap\n");
   Con_Printf("\n---------\n");
   Con_Printf("Motion sick?  Try Stereographic or Panini\n");
}

void L_CaptureCubeMap(void)
{
   char filename[100];
   int i;
   sprintf(filename,"%s/cubemaps/cube00_top.pcx",com_gamedir);
   int len = strlen(filename);
   for (i=0; i<99; ++i)
   {
      filename[len-10] = i/10 + '0';
      filename[len-9] = i%10 + '0';
      if (Sys_FileTime(filename) == -1)
         break;
   }
   if (i == 100)
   {
      Con_Printf("Too many saved cubemaps, reached limit of 100\n");
      return;
   }

   extern void WritePCXfile(char *filename, byte *data, int width, int height, int rowbytes, byte *palette);

#define SET_FILE_FACE(face) sprintf(filename,"cubemaps/cube%02d_" face ".pcx",i);
#define WRITE_FILE(n) WritePCXfile(filename,cubemap+cubesize*cubesize*n,cubesize,cubesize,cubesize,host_basepal);

   SET_FILE_FACE("front"); WRITE_FILE(BOX_FRONT);
   SET_FILE_FACE("right"); WRITE_FILE(BOX_RIGHT);
   SET_FILE_FACE("behind"); WRITE_FILE(BOX_BEHIND);
   SET_FILE_FACE("left"); WRITE_FILE(BOX_LEFT);
   SET_FILE_FACE("top"); WRITE_FILE(BOX_TOP);
   SET_FILE_FACE("bottom"); WRITE_FILE(BOX_BOTTOM);

   Con_Printf("Saved cubemap to cube%02d_XXXX.pcx\n",i);
}

void L_ShowFovDeprecate(void)
{
   Con_Printf("Please use hfov instead\n");
}

void L_ColorCube(void)
{
   colorcube = colorcube ? 0 : 1;
   Con_Printf("Colored Cube is %s\n", colorcube ? "ON" : "OFF");
}

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
   int error = luaL_loadbuffer(lua, cmd, strlen(cmd), "jit.opt") ||
      lua_pcall(lua, 0, 0, 0);
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
      "pow = math.pow\n"

      "FRONT = 0\n"
      "RIGHT = 1\n"
      "BEHIND = 2\n"
      "LEFT = 3\n"
      "TOP = 4\n"
      "BOTTOM = 5\n";

   error = luaL_loadbuffer(lua, aliases, strlen(aliases), "aliases") ||
      lua_pcall(lua, 0, 0, 0);
   if (error) {
      fprintf(stderr, "%s", lua_tostring(lua, -1));
      lua_pop(lua, 1);  /* pop error message from the stack */
   }
}

void L_HFit(void)
{
   Cvar_SetValue("fitmode", FIT_H);
}

void L_VFit(void)
{
   Cvar_SetValue("fitmode", FIT_V);
}

void L_Fit(void)
{
   Cvar_SetValue("fitmode", FIT_BOTH);
}

void L_Init(void)
{
   L_InitLua();

   Cmd_AddCommand("lenses", L_Help);
   Cmd_AddCommand("savecube", L_CaptureCubeMap);
   Cmd_AddCommand("fov", L_ShowFovDeprecate);
   Cmd_AddCommand("colorcube", L_ColorCube);
   Cmd_AddCommand("hfit", L_HFit);
   Cmd_AddCommand("vfit", L_VFit);
   Cmd_AddCommand("fit", L_Fit);
   Cvar_RegisterVariable (&l_hfov);
   Cvar_RegisterVariable (&l_vfov);
   Cvar_RegisterVariable (&l_dfov);
   Cvar_RegisterVariable (&l_lens);
   Cvar_RegisterVariable (&l_fitmode);

}

void L_Shutdown(void)
{
   lua_close(lua);
}

/******** BEGIN LUA ******************/

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

int lua_xy_to_cubemap(double x, double y, int *side, double *u, double *v)
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

   *side = lua_tointeger(lua, -3);
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

int lua_cubemap_to_xy(int side, double u, double v, double *x, double *y)
{
   lua_rawgeti(lua, LUA_REGISTRYINDEX, mapForwardIndex);
   lua_pushinteger(lua, side);
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

int lua_lens_init(void)
{
   // call init if exists
   lua_getglobal(lua, "init");
   if (lua_isfunction(lua,-1)) {
      if (lua_pcall(lua, 0, 0, 0) != 0) {
         Con_Printf("could not call init \nERROR: %s\n", lua_tostring(lua,-1));
         return 0;
      }
   }

   // clear lens scale
   scale = -1;

   if (l_fitmode.value == FIT_NONE) // use FOV for scaling since no fit mode is selected
   {
      // check FOV limits
      if (max_hfov <= 0 || max_vfov <= 0)
      {
         Con_Printf("Please specify a positive max_hfov and max_vfov in your lens script\n");
         return 0;
      }

      if (width == *framesize && fov > max_hfov) {
         Con_Printf("Horizontal FOV must be less than %f\n", max_hfov * 180 / M_PI);
         return 0;
      }
      else if (height == *framesize && fov > max_vfov) {
         Con_Printf("Vertical FOV must be less than %f\n", max_vfov * 180/ M_PI);
         return 0;
      }

      // try to scale based on FOV using the forward map
      if (mapForwardIndex != -1) {
         if (mapCoord == COORD_RADIAL) {
            double r;
            if (lua_theta_to_r(fov*0.5, &r)) scale = r / (*framesize * 0.5);
            else {
               Con_Printf("theta_to_r did not return a valid r value for determining FOV scale\n");
               return 0;
            }
         }
         else if (mapCoord == COORD_SPHERICAL) {
            double x,y;
            if (*framesize == width) {
               if (lua_latlon_to_xy(0,fov*0.5,&x,&y)) scale = x / (*framesize * 0.5);
               else return 0;
            }
            else if (*framesize == height) {
               if (lua_latlon_to_xy(fov*0.5,0,&x,&y)) scale = y / (*framesize * 0.5);
               else return 0;
            }
            else {
               Con_Printf("latlon_to_xy does not support diagonal FOVs\n");
               return 0;
            }
         }
         else if (mapCoord == COORD_EUCLIDEAN) {
            Con_Printf("ray_to_xy currently not supported for FOV scaling\n");
            return 0;
         }
         else if (mapCoord == COORD_CUBEMAP) {
            Con_Printf("cubemap_to_xy currently not supported for FOV scaling\n");
            return 0;
         }
      }
      else
      {
         Con_Printf("Please specify a forward mapping function in your script for FOV scaling\n");
      }
   }
   else
   {
      if (hfit) {
         if (hfit_size <= 0)
         {
            Con_Printf("Cannot use hfit unless a positive hfit_size is in your script\n");
            return 0;
         }
         scale = hfit_size / width;
      }
      else if (vfit) {
         if (vfit_size <= 0)
         {
            Con_Printf("Cannot use vfit unless a positive vfit_size is in your script\n");
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
            Con_Printf("Please specify a positive vfit_size or hfit_size in your script\n");
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

void lua_lens_clear(void)
{
#define CLEARVAR(var) lua_pushnil(lua); lua_setglobal(lua, var);
   CLEARVAR("init");
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
   CLEARVAR("xy_to_cubemap");
   CLEARVAR("cubemap_to_xy");
   CLEARVAR("xy_isvalid");
   CLEARVAR("r_isvalid");
}

int lua_func_exists(const char* name)
{
   lua_getglobal(lua, name);
   int exists = lua_isfunction(lua,-1);
   lua_pop(lua,1);
   return exists;
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
      else if (!strcmp(funcname, "xy_to_cubemap")) { SETINV_ACTIVE(xy_to_cubemap,COORD_CUBEMAP); }
      else if (!strcmp(funcname, "cubemap_to_xy")) { SETFWD_ACTIVE(cubemap_to_xy,COORD_CUBEMAP); }
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
      else if (lua_func_exists( "xy_to_cubemap"))  { SETINV_ACTIVE(xy_to_cubemap, COORD_CUBEMAP); }
      else if (lua_func_exists( "cubemap_to_xy"))  { SETFWD_ACTIVE(cubemap_to_xy, COORD_CUBEMAP); }
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
      else if (mapCoord == COORD_CUBEMAP && lua_func_exists("cubemap_to_xy")){ SETFWD(cubemap_to_xy); }
      else {
         // TODO: notify if no matching forward function found (meaning no FOV scaling can be done)
      }
   }

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


/*************** END LUA ****************/

int clamp(int value, int min, int max)
{
   if (value < min)
      return min;
   if (value > max)
      return max;
   return value;
}

// set the (lx,ly) pixel on the lensmap to the (sx,sy,sz) view vector
void setLensPixelToRay(int lx, int ly, double sx, double sy, double sz)
{
   // determine which side of the box we need
   double abs_x = fabs(sx);
   double abs_y = fabs(sy);
   double abs_z = fabs(sz);			
   int side;
   double xs=0, ys=0;
   if (abs_x > abs_y) {
      if (abs_x > abs_z) { side = ((sx > 0.0) ? BOX_RIGHT : BOX_LEFT);   }
      else               { side = ((sz > 0.0) ? BOX_FRONT : BOX_BEHIND); }
   } else {
      if (abs_y > abs_z) { side = ((sy > 0.0) ? BOX_TOP : BOX_BOTTOM); }
      else               { side = ((sz > 0.0) ? BOX_FRONT : BOX_BEHIND); }
   }

#define T(x) (((x)*0.5) + 0.5)

   // scale up our vector [x,y,z] to the box
   switch(side) {
      case BOX_FRONT:  xs = T( sx /  sz); ys = T( -sy /  sz); break;
      case BOX_BEHIND: xs = T(-sx / -sz); ys = T( -sy / -sz); break;
      case BOX_LEFT:   xs = T( sz / -sx); ys = T( -sy / -sx); break;
      case BOX_RIGHT:  xs = T(-sz /  sx); ys = T( -sy /  sx); break;
      case BOX_BOTTOM: xs = T( sx /  -sy); ys = T( -sz / -sy); break;
      case BOX_TOP:    xs = T(sx /  sy); ys = T( sz / sy); break;
   }
   side_count[side]++;

   // convert to face coordinates
   int px = clamp((int)(xs*cubesize),0,cubesize-1);
   int py = clamp((int)(ys*cubesize),0,cubesize-1);

   // map lens pixel to cubeface pixel
   *LENSMAP(lx,ly) = CUBEFACE(side,px,py);
}

void create_lensmap_inverse()
{
   memset(side_count, 0, sizeof(side_count));

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

         // map the current window coordinate to a ray vector
         vec3_t ray = { 0, 0, 1};
         //if (lx==halfw && ly == halfh) {
            // FIXME: this is a workaround for strange dead pixel in the center
         //}
         if (mapCoord == COORD_RADIAL)
         {
            double r = sqrt(x*x+y*y);
            double theta;
            if (!lua_r_to_theta(r, &theta))
               continue;
            double s = sin(theta);
            ray[0] = x/r * s;
            ray[1] = y/r * s;
            ray[2] = cos(theta);
         }
         else if (mapCoord == COORD_SPHERICAL)
         {
            double lat,lon;
            if (!lua_xy_to_latlon(x,y,&lat,&lon))
               continue;

            double clat = cos(lat);
            ray[0] = sin(lon)*clat;
            ray[1] = sin(lat);
            ray[2] = cos(lon)*clat;
         }
         else if (mapCoord == COORD_EUCLIDEAN)
         {
            if (!lua_xy_to_ray(x,y,ray))
               continue;
         }
         else if (mapCoord == COORD_CUBEMAP)
         {
            int side;
            double u,v;
            if (!lua_xy_to_cubemap(x,y,&side,&u,&v))
               continue;

            side_count[side]++;
            int cx = clamp((int)(u*(cubesize-1)),0,cubesize-1);
            int cy = clamp((int)(v*(cubesize-1)),0,cubesize-1);
            *LENSMAP(lx,ly) = CUBEFACE(side,cx,cy);
            continue;
         }

         setLensPixelToRay(lx,ly,ray[0],ray[1],ray[2]);
         if (hsym) setLensPixelToRay(width-1-lx,ly,-ray[0],ray[1],ray[2]);
         if (vsym) setLensPixelToRay(lx,height-1-ly,ray[0],-ray[1],ray[2]);
         if (vsym && hsym) setLensPixelToRay(width-1-lx,height-1-ly,-ray[0],-ray[1],ray[2]);
      }
   }

   int i;
   for(i=0; i<6; ++i)
      faceDisplay[i] = (side_count[i] > 1);
}

void create_lensmap_forward()
{
   memset(side_count, 0, sizeof(side_count));
   int cx, cy;
   int side;
   double nz = cubesize*0.5;

   for (side=0; side<6; ++side)
   {
      int minx = 0;
      int miny = 0;
      int maxx = cubesize-1;
      int maxy = cubesize-1;

      if (hsym) {
         if (side == BOX_RIGHT) continue;
         if (side != BOX_LEFT) maxx = cubesize/2;
      }
      if (vsym) {
         if (side == BOX_BOTTOM) continue;
         if (side != BOX_TOP) maxy = cubesize/2;
      }

      for (cy=miny; cy<=maxy; ++cy)
      {
         double ny = -(cy-cubesize*0.5);
         for (cx=minx; cx<=maxx; ++cx)
         {
            double nx = cx-cubesize*0.5;
            vec3_t ray;
            if (side == BOX_FRONT) { ray[0] = nx; ray[1] = ny; ray[2] = nz; }
            else if (side == BOX_BEHIND) { ray[0] = -nx; ray[1] = ny; ray[2] = -nz; }
            else if (side == BOX_LEFT) { ray[0] = -nz; ray[1] = ny; ray[2] = nx; }
            else if (side == BOX_RIGHT) { ray[0] = nz; ray[1] = ny; ray[2] = -nx; }
            else if (side == BOX_TOP) { ray[0] = nx; ray[1] = nz; ray[2] = -ny; }
            else if (side == BOX_BOTTOM) { ray[0] = nx; ray[1] = -nz; ray[2] = ny; }

            double x,y;
            if (mapCoord == COORD_RADIAL)
            {
               double theta = acos(ray[2]/sqrt(ray[0]*ray[0]+ray[1]*ray[1]+ray[2]*ray[2]));
               double r;
               if (!lua_theta_to_r(theta, &r))
                  continue;

               double c = r/sqrt(ray[0]*ray[0]+ray[1]*ray[1]);
               x = ray[0]*c;
               y = ray[1]*c;
            }
            else if (mapCoord == COORD_SPHERICAL)
            {
               double lon = atan2(ray[0], ray[2]);
               double lat = atan2(ray[1], sqrt(ray[0]*ray[0]+ray[2]*ray[2]));
               if (!lua_latlon_to_xy(lat, lon, &x, &y))
                  continue;
            }
            else if (mapCoord == COORD_EUCLIDEAN)
            {
               if (!lua_ray_to_xy(ray, &x, &y))
                  continue;
            }
            else if (mapCoord == COORD_CUBEMAP)
            {
               double u = (double)cx / (cubesize-1);
               double v = (double)cy / (cubesize-1);
               if (!lua_cubemap_to_xy(side, u, v, &x, &y))
                  continue;
            }

            x /= scale;
            y /= scale;
            y *= -1;
            x += width*0.5;
            y += height*0.5;

            int lx = (int)x;
            int ly = (int)y;

            if (lx < 0 || lx >= width || ly < 0 || ly >= height)
               continue;

            side_count[side]++;
            *LENSMAP(lx,ly) = CUBEFACE(side,cx,cy);

            if (hsym) {
               int oppside = (side == BOX_LEFT) ? BOX_RIGHT : side;
               side_count[oppside]++;
               *LENSMAP(width-1-lx,ly) = CUBEFACE(oppside,cubesize-1-cx,cy);
            }
            if (vsym) {
               int oppside = (side == BOX_TOP) ? BOX_BOTTOM : side;
               side_count[oppside]++;
               *LENSMAP(lx,height-1-ly) = CUBEFACE(oppside,cx,cubesize-1-cy);
            }
            if (hsym && vsym) {
               int oppside = (side == BOX_TOP) ? BOX_BOTTOM : ((side == BOX_LEFT) ? BOX_RIGHT : side);
               side_count[oppside]++;
               *LENSMAP(width-1-lx,height-1-ly) = CUBEFACE(oppside,cubesize-1-cx,cubesize-1-cy);
            }
         }
      }
   }

   int i;
   for(i=0; i<6; ++i) {
      faceDisplay[i] = (side_count[i] > 1);
   }
}

void create_lensmap()
{
   // render nothing if current lens is invalid
   if (!valid_lens)
      return;

   // test if this lens can support the current fov
   if (!lua_lens_init())
   {
      Con_Printf("This lens could not be initialized.\n");
      return;
   }

   if (mapType == MAP_FORWARD)
      create_lensmap_forward();
   else if (mapType == MAP_INVERSE)
      create_lensmap_inverse();
}

// draw the lensmap to the vidbuffer
void render_lensmap()
{
   B **lmap = lensmap;
   int x, y;
   for(y=0; y<height; y++) 
      for(x=0; x<width; x++,lmap++) 
         if (*lmap)
            *VBUFFER(x+left, y+top) = **lmap;
}

// render a specific face on the cubemap
void render_cubeface(B* cubeface, vec3_t forward, vec3_t right, vec3_t up) 
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
   for(y = 0;y<cubesize;y++) {
      memcpy(cubeface, vbuffer, cubesize);

      // advance to the next row
      vbuffer += vid.rowbytes;
      cubeface += cubesize;
   }
}

void L_RenderView() 
{
   static int pwidth = -1;
   static int pheight = -1;
   static char plens[MAX_LENS_LEN] = "\0";
   static double phfov = -1;
   static double pvfov = -1;
   static double pdfov = -1;
   static int pcolorcube = -1;
   static int pfitmode = -1;

   // update screen size
   left = scr_vrect.x;
   top = scr_vrect.y;
   width = scr_vrect.width; 
   height = scr_vrect.height;
   cubesize = (width < height) ? width : height;
   diag = sqrt(width*width+height*height);
   int area = width*height;
   int sizechange = pwidth!=width || pheight!=height;

   // update lens
   strcpy(lens, l_lens.string);
   int lenschange = strcmp(plens,lens);
   if (lenschange)
   {
      valid_lens = lua_lens_load();
      if (!valid_lens)
      {
         strcpy(lens,"");
         Cvar_Set("lens",lens);
         Con_Printf("not a valid lens\n");
      }
   }

   // update FOV and framesize
   int fovchange = 1;
   if (l_hfov.value != phfov)
   {
      fov = l_hfov.value * M_PI / 180;
      framesize = &width;
      Cvar_SetValue("vfov", -1);
      Cvar_SetValue("dfov", -1);
   }
   else if (l_vfov.value != pvfov)
   {
      fov = l_vfov.value * M_PI / 180;
      framesize = &height;
      Cvar_SetValue("hfov", -1);
      Cvar_SetValue("dfov", -1);
   }
   else if (l_dfov.value != pdfov)
   {
      fov = l_dfov.value * M_PI / 180;
      framesize = &diag;
      Cvar_SetValue("hfov", -1);
      Cvar_SetValue("vfov", -1);
   }
   else 
   {
      fovchange = 0;
   }

   // check for fit change
   if (fovchange)
   {
      // assume the fit won't change in the same frame the FOV changes
      Cvar_SetValue("fitmode", FIT_NONE);
   }
   else
   {
      if (l_fitmode.value != pfitmode) // fitmode changed
      {
         if (l_fitmode.value > 0 && l_fitmode.value <= 3) // fitmode valid
         {
            fit = hfit = vfit = 0;
            if (l_fitmode.value == FIT_H)
               hfit = 1;
            else if (l_fitmode.value == FIT_V)
               vfit = 1;
            else if (l_fitmode.value == FIT_BOTH)
            {
               fit = 1;
            }

            // trigger change and clear fov's
            fovchange = 1;
            fov = 0;
            framesize = 0; // null pointer
            Cvar_SetValue("hfov", -1);
            Cvar_SetValue("vfov", -1);
            Cvar_SetValue("dfov", -1);
         }
         else
         {
            if (l_fitmode.value == FIT_NONE) {
               Con_Printf("You cannot set fit mode to NONE.  Use hfov,vfov, or dfov or use hfit/vfit.\n");
            }
            else {
               Con_Printf("%d is not a valid fitmode. Use (1,2,3) for (h,v,both)\n",(int)l_fitmode.value);
            }
            Cvar_SetValue("fitmode", pfitmode);
         }
      }
   }

   // allocate new buffers if size changes
   if(sizechange)
   {
      if(cubemap) free(cubemap);
      if(lensmap) free(lensmap);

      cubemap = (B*)malloc(cubesize*cubesize*6*sizeof(B));
      lensmap = (B**)malloc(area*sizeof(B*));
      if(!cubemap || !lensmap) exit(1); // the rude way
   }

   // recalculate lens
   if (sizechange || fovchange || lenschange) {
      memset(lensmap, 0, area*sizeof(B*));
      create_lensmap();
   }

   // get the orientations required to render the cube faces
   vec3_t front, right, up, back, left, down;
   AngleVectors(r_refdef.viewangles, front, right, up);
   VectorScale(front, -1, back);
   VectorScale(right, -1, left);
   VectorScale(up, -1, down);

   // render the environment onto a cube map
   int i;
   if (colorcube)
   {
      if (pcolorcube != colorcube)
      {
         B colors[6] = {242,243,244,245,250,255};
         for (i=0; i<6; ++i)
         {
            B* face = cubemap+cubesize*cubesize*i;
            memset(face,colors[i],cubesize*cubesize);
         }
      }
   }
   else
   {
      for (i=0; i<6; ++i)
         if (faceDisplay[i]) {
            B* face = cubemap+cubesize*cubesize*i;
            switch(i) {
               //                                     FORWARD  RIGHT   UP
               case BOX_BEHIND: render_cubeface(face, back,    left,   up);    break;
               case BOX_BOTTOM: render_cubeface(face, down,    right,  front); break;
               case BOX_TOP:    render_cubeface(face, up,      right,  back);  break;
               case BOX_LEFT:   render_cubeface(face, left,    front,  up);    break;
               case BOX_RIGHT:  render_cubeface(face, right,   back,   up);    break;
               case BOX_FRONT:  render_cubeface(face, front,   right,  up);    break;
            }
         }
   }

   // render our view
   Draw_TileClear(0, 0, vid.width, vid.height);
   render_lensmap();

   // store current values for change detection
   pwidth = width;
   pheight = height;
   strcpy(plens,lens);
   phfov = l_hfov.value;
   pvfov = l_vfov.value;
   pdfov = l_dfov.value;
   pcolorcube = colorcube;
   pfitmode = l_fitmode.value;
}

