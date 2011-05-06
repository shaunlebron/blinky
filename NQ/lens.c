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
cvar_t l_cube_rows = {"cube_rows", "3"};
cvar_t l_cube_cols = {"cube_cols", "4"};
cvar_t l_cube_order = {"cube_order", "9499" "3012" "9599"};

typedef unsigned char B;
static B *cubemap = NULL;  
static B **lensmap = NULL;

#define BOX_FRONT  0
#define BOX_RIGHT  1
#define BOX_BEHIND 2
#define BOX_LEFT   3
#define BOX_TOP    4
#define BOX_BOTTOM 5

#define MAX_CUBE_ORDER 20
#define MAX_LENS_LEN 50

// top left coordinates of the screen in the vid buffer
static int left, top;

// size of the screen in the vid buffer
static int width, height, diag;

// size of each rendered cube face in the vid buffer
static int cubesize;

// desired FOV in radians
static double fov;

// name of the current lens
static char lens[MAX_LENS_LEN];

// pointer to the screen dimension (width,height or diag) attached to the desired fov
static int* framesize;

// multiplier used to transform screen coordinates to lens coordinates
static double scale;

// the number of pixels used on each cube face
// used to only draw the cube faces in use
static int faceDisplay[] = {0,0,0,0,0,0};

// cubemap display flag
static int cube = 0;

// cubemap color display flag
static int colorcube = 0;

// number of rows in the cubemap display
static int cube_rows;

// number of columns in cubemap display
static int cube_cols;

// order of the cubemap faces in the cubemap table in row major order
static char cube_order[MAX_CUBE_ORDER];

// maximum FOV width of the current lens
static double maxFovWidth;

// maximum FOV height of the current lens
static double maxFovHeight;

// indicates if the current lens is valid
static int valid_lens;

// inverse map function
typedef int (*mapInverseFunc)(double x, double y, vec3_t ray);
static mapInverseFunc mapInverse;

// forward map function
typedef int (*mapForwardFunc)(vec3_t ray, double *x, double *y);
static mapForwardFunc mapForward;

// lua reference map index (for storing a reference to the map function)
static int map_index;

// side count used to determine which cube faces to render
static int side_count[6];

// MAP SYMMETRIES
static int mapSymmetry;
#define NO_SYMMETRY 0
#define H_SYMMETRY 1
#define V_SYMMETRY 2

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
   Con_Printf("cube: toggle cubemap\n");
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

void L_Cube(void)
{
   cube = cube ? 0 : 1;
   Con_Printf("Cube is %s\n", cube ? "ON" : "OFF");
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
      "pow = math.pow\n";
   error = luaL_loadbuffer(lua, aliases, strlen(aliases), "aliases") ||
      lua_pcall(lua, 0, 0, 0);
   if (error) {
      fprintf(stderr, "%s", lua_tostring(lua, -1));
      lua_pop(lua, 1);  /* pop error message from the stack */
   }
}

void L_Init(void)
{
   L_InitLua();

   Cmd_AddCommand("lenses", L_Help);
   Cmd_AddCommand("savecube", L_CaptureCubeMap);
   Cmd_AddCommand("fov", L_ShowFovDeprecate);
   Cmd_AddCommand("cube", L_Cube);
   Cmd_AddCommand("colorcube", L_ColorCube);
   Cvar_RegisterVariable (&l_hfov);
   Cvar_RegisterVariable (&l_vfov);
   Cvar_RegisterVariable (&l_dfov);
   Cvar_RegisterVariable (&l_lens);
   Cvar_RegisterVariable (&l_cube_rows);
   Cvar_RegisterVariable (&l_cube_cols);
   Cvar_RegisterVariable (&l_cube_order);
}

void L_Shutdown(void)
{
   lua_close(lua);
}

/******** BEGIN LUA ******************/

// Inverse Map Functions

int xy_to_ray(double x, double y, vec3_t ray)
{
   lua_rawgeti(lua, LUA_REGISTRYINDEX, map_index);
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

int r_to_theta(double x, double y, vec3_t ray)
{
   double r = sqrt(x*x+y*y);

   lua_rawgeti(lua, LUA_REGISTRYINDEX, map_index);
   lua_pushnumber(lua, r);
   lua_call(lua, 1, LUA_MULTRET);

   if (!lua_isnumber(lua, -1)) {
      lua_pop(lua,1);
      return 0;
   }

   double el = lua_tonumber(lua,-1);
   lua_pop(lua,1);

   double s = sin(el);
   double c = cos(el);

   ray[0] = x/r * s;
   ray[1] = y/r * s;
   ray[2] = c;

   return 1;
}

int xy_to_latlon(double x, double y, vec3_t ray)
{
   lua_rawgeti(lua, LUA_REGISTRYINDEX, map_index);
   lua_pushnumber(lua, x);
   lua_pushnumber(lua, y);
   lua_call(lua, 2, LUA_MULTRET);

   if (!lua_isnumber(lua, -1)) {
      lua_pop(lua,1);
      return 0;
   }

   double lat = lua_tonumber(lua, -2);
   double lon = lua_tonumber(lua, -1);
   lua_pop(lua,2);

   double clat = cos(lat);

   ray[0] = sin(lon)*clat;
   ray[1] = sin(lat);
   ray[2] = cos(lon)*clat;

   return 1;
}

// Forward Map Functions

int ray_to_xy(vec3_t ray, double *x, double *y)
{
   lua_rawgeti(lua, LUA_REGISTRYINDEX, map_index);
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

int latlon_to_xy(vec3_t ray, double *x, double *y)
{
   double rx=ray[0], ry=ray[1], rz=ray[2];
   double lon = atan2(rx, rz);
   double lat = atan2(ry, sqrt(rx*rx+rz*rz));

   lua_rawgeti(lua, LUA_REGISTRYINDEX, map_index);
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

int theta_to_r(vec3_t ray, double *x, double *y)
{
   double len = sqrt(ray[0]*ray[0]+ray[1]*ray[1]+ray[2]*ray[2]);
   double theta = acos(ray[2]/len);

   lua_rawgeti(lua, LUA_REGISTRYINDEX, map_index);
   lua_pushnumber(lua, theta);
   lua_call(lua, 1, LUA_MULTRET);

   if (!lua_isnumber(lua, -1))
   {
      lua_pop(lua,1);
      return 0;
   }

   double r = lua_tonumber(lua,-1);
   double c = r/sqrt(ray[0]*ray[0]+ray[1]*ray[1]);
   *x = ray[0]*c;
   *y = ray[1]*c;
   return 1;
}

int lua_lens_init(void)
{
   // call init
   lua_getglobal(lua, "init");
   lua_pushnumber(lua, fov);
   lua_pushinteger(lua, width);
   lua_pushinteger(lua, height);
   lua_pushinteger(lua, *framesize);
   if (lua_pcall(lua, 4, 1, 0) != 0)
   {
      Con_Printf("could not call init \nERROR: %s\n", lua_tostring(lua,-1));
      lua_pop(lua,1);
      return 0;
   }

   if (!lua_isnumber(lua,-1))
   {
      lua_pop(lua,1);
      Con_Printf("init did not return a number\n");
      return 0;
   }

   // get scale
   scale = lua_tonumber(lua, -1);
   lua_pop(lua,1);
   if (scale <= 0)
   {
      Con_Printf("init returned a scale of %f, which is  <= 0\n", scale);
      return 0;
   }

   // check FOV limits
   if (width == *framesize && fov > maxFovWidth) {
      Con_Printf("Horizontal FOV must be less than %f\n", maxFovWidth);
      return 0;
   }
   else if (height == *framesize && fov > maxFovHeight) {
      Con_Printf("Vertical FOV must be less than %f\n", maxFovHeight);
      return 0;
   }

   return 1;
}

void lua_lens_clear(void)
{
#define CLEARVAR(var) lua_pushnil(lua); lua_setglobal(lua, var);
   CLEARVAR("init");
   CLEARVAR("map");
   CLEARVAR("maxFovWidth");
   CLEARVAR("maxFovHeight");
   CLEARVAR("horizontalSymmetry");
   CLEARVAR("verticalSymmetry");
   CLEARVAR("verticalSymmetry");
   CLEARVAR("xy_to_latlon");
   CLEARVAR("latlon_to_xy");
   CLEARVAR("r_to_theta");
   CLEARVAR("theta_to_r");
   CLEARVAR("xy_to_ray");
   CLEARVAR("ray_to_xy");
}

int lua_lens_load(void)
{
   lua_lens_clear();

   // set full filename
   char filename[100];
   sprintf(filename,"%s/../lenses/%s.lua",com_gamedir,lens);

   // check if loaded correctly
   if (luaL_dofile(lua, filename) != 0)
   {
      Con_Printf("could not load %s\nERROR: %s\n", lens, lua_tostring(lua,-1));
      lua_pop(lua, 1);
      return 0;
   }

   // get appropriate map function
   mapInverse = 0;
   mapForward = 0;
   lua_getglobal(lua, "map");
   if (lua_isstring(lua, -1))
   {
      const char* funcname = lua_tostring(lua, -1);
      if (!strcmp(funcname, "xy_to_latlon")) { mapInverse = xy_to_latlon; }
      else if (!strcmp(funcname, "latlon_to_xy")) { mapForward = latlon_to_xy; }
      else if (!strcmp(funcname, "r_to_theta")) { mapInverse = r_to_theta; }
      else if (!strcmp(funcname, "theta_to_r")) { mapForward = theta_to_r; }
      else if (!strcmp(funcname, "xy_to_ray")) { mapInverse = xy_to_ray; }
      else if (!strcmp(funcname, "ray_to_xy")) { mapForward = ray_to_xy; }
      else {
         Con_Printf("Unsupported map function: %s\n", funcname);
         lua_pop(lua, 1);
         return 0;
      }
      lua_getglobal(lua, funcname);
      map_index = luaL_ref(lua, LUA_REGISTRYINDEX);
      lua_pop(lua,1);
   }
   else
   {
      Con_Printf("Invalid map function\n");
      lua_pop(lua, 1);
      return 0;
   }

   // FOV limits

   lua_getglobal(lua, "maxFovWidth");
   maxFovWidth = lua_isnumber(lua,-1) ? lua_tonumber(lua,-1) : 2*M_PI;
   lua_pop(lua,1);

   lua_getglobal(lua, "maxFovHeight");
   maxFovHeight = lua_isnumber(lua,-1) ? lua_tonumber(lua,-1) : 2*M_PI;
   lua_pop(lua,1);

   // Map Symmetry options

   mapSymmetry = 0;

   lua_getglobal(lua, "verticalSymmetry");
   if (lua_isboolean(lua,-1) ? lua_toboolean(lua,-1) : 1)
      mapSymmetry |= V_SYMMETRY;
   lua_pop(lua,1);

   lua_getglobal(lua, "horizontalSymmetry");
   if (lua_isboolean(lua,-1) ? lua_toboolean(lua,-1) : 1)
      mapSymmetry |= H_SYMMETRY;
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

void create_cubefold()
{
   // get size of each square cell
   int xsize = width / cube_cols;
   int ysize = height / cube_rows;
   int size = (xsize < ysize) ? xsize : ysize;

   // get top left position of the first row and first column
   int left = (width - size*cube_cols)/2;
   int top = (height - size*cube_rows)/2;

   int r,c;
   for (r=0; r<cube_rows; ++r)
   {
      int rowy = top + size*r;
      for (c=0; c<cube_cols; ++c)
      {
         int colx = left + size*c;
         int face = (int)(cube_order[c+r*cube_cols] - '0');
         if (face > 5)
            continue;

         int x,y;
         for (y=0;y<size;++y)
            for (x=0;x<size;++x)
            {
               int lx = clamp(colx+x,0,width-1);
               int ly = clamp(rowy+y,0,height-1);
               int fx = clamp(cubesize*x/size,0,cubesize-1);
               int fy = clamp(cubesize*y/size,0,cubesize-1);
               *LENSMAP(lx,ly) = CUBEFACE(face,fx,fy);
            }
      }
   }
}

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

   int hsym = mapSymmetry & H_SYMMETRY;
   int vsym = mapSymmetry & V_SYMMETRY;

   int halfw = width/2;
   int halfh = height/2;
   int maxx = hsym ? halfw : width;
   int maxy = vsym ? halfh : height;

   int x, y;

   for(y = 0;y<maxy;++y) {
      double y0 = -(y-halfh);
      y0 *= scale;
      for(x = 0;x<maxx;++x) {
         double x0 = x-halfw;
         x0 *= scale;

         // map the current window coordinate to a ray vector
         vec3_t ray = { 0, 0, 1};
         if (x==halfw && y == halfh) {
            // FIXME: this is a workaround for strange dead pixel in the center
         }
         else if (!mapInverse(x0,y0,ray)) {
            // pixel is outside projection
            continue;
         }

         setLensPixelToRay(x,y,ray[0],ray[1],ray[2]);
         if (hsym) setLensPixelToRay(width-1-x,y,-ray[0],ray[1],ray[2]);
         if (vsym) setLensPixelToRay(x,height-1-y,ray[0],-ray[1],ray[2]);
         if (vsym && hsym) setLensPixelToRay(width-1-x,height-1-y,-ray[0],-ray[1],ray[2]);
      }
   }

   for(x=0; x<6; ++x)
      faceDisplay[x] = (side_count[x] > 1);
}

void fill_forward_holes()
{
   int hsym = mapSymmetry & H_SYMMETRY;
   int vsym = mapSymmetry & V_SYMMETRY;

   if (hsym && vsym)
   {
      for (int x=0; x<=width/2; ++x)
      {
         for (int y=0; y<height/2; ++y)
         {
            if (*LENSMAP(x,y) == 0)
            {
               // TODO: insert hole filling algorithm
            }
         }
      }
   }
}

void create_lensmap_forward()
{
   memset(side_count, 0, sizeof(side_count));
   int x,y;
   int side;
   double nz = cubesize*0.5;

   int hsym = mapSymmetry & H_SYMMETRY;
   int vsym = mapSymmetry & V_SYMMETRY;

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

      for (y=miny; y<=maxy; ++y)
      {
         double ny = -(y-cubesize*0.5);
         for (x=minx; x<=maxx; ++x)
         {
            double nx = x-cubesize*0.5;
            vec3_t ray;
            if (side == BOX_FRONT) { ray[0] = nx; ray[1] = ny; ray[2] = nz; }
            else if (side == BOX_BEHIND) { ray[0] = -nx; ray[1] = ny; ray[2] = -nz; }
            else if (side == BOX_LEFT) { ray[0] = -nz; ray[1] = ny; ray[2] = nx; }
            else if (side == BOX_RIGHT) { ray[0] = nz; ray[1] = ny; ray[2] = -nx; }
            else if (side == BOX_TOP) { ray[0] = nx; ray[1] = nz; ray[2] = -ny; }
            else if (side == BOX_BOTTOM) { ray[0] = nx; ray[1] = -nz; ray[2] = ny; }

            double x0,y0;
            if (!mapForward(ray, &x0, &y0)) {
               continue;
            }

            double invscale = 1/scale;
            x0 *= invscale;
            y0 *= invscale;

            y0 *= -1;

            x0 += width*0.5;
            y0 += height*0.5;

            int lx = (int)x0;
            int ly = (int)y0;

            if (lx < 0 || lx >= width || ly < 0 || ly >= height)
               continue;

            side_count[side]++;
            *LENSMAP(lx,ly) = CUBEFACE(side,x,y);

            if (hsym) {
               int oppside = (side == BOX_LEFT) ? BOX_RIGHT : side;
               side_count[oppside]++;
               *LENSMAP(width-1-lx,ly) = CUBEFACE(oppside,cubesize-1-x,y);
            }
            if (vsym) {
               int oppside = (side == BOX_TOP) ? BOX_BOTTOM : side;
               side_count[oppside]++;
               *LENSMAP(lx,height-1-ly) = CUBEFACE(oppside,x,cubesize-1-y);
            }
            if (hsym && vsym) {
               int oppside = (side == BOX_TOP) ? BOX_BOTTOM : ((side == BOX_LEFT) ? BOX_RIGHT : side);
               side_count[oppside]++;
               *LENSMAP(width-1-lx,height-1-ly) = CUBEFACE(oppside,cubesize-1-x,cubesize-1-y);
            }
         }
      }
   }

   for(x=0; x<6; ++x) {
      faceDisplay[x] = (side_count[x] > 1);
   }
}

void create_lensmap()
{
   if (cube)
   {
      // set all faces to display
      memset(faceDisplay,1,6*sizeof(int));

      // create lookup table for unfolded cubemap
      create_cubefold();
      return;
   }

   // render nothing if current lens is invalid
   if (!valid_lens)
      return;

   // test if this lens can support the current fov
   if (!lua_lens_init())
   {
      Con_Printf("This lens could not be loaded.\n");
      return;
   }

   if (mapForward)
      create_lensmap_forward();
   else if (mapInverse)
      create_lensmap_inverse();
}

void render_lensmap()
{
   B **lmap = lensmap;
   int x, y;
   for(y=0; y<height; y++) 
      for(x=0; x<width; x++,lmap++) 
         if (*lmap)
            *VBUFFER(x+left, y+top) = **lmap;
}

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
   static int pcube = -1;
   static int pcube_rows = -1;
   static int pcube_cols = -1;
   static char pcube_order[MAX_CUBE_ORDER];
   static int pcolorcube = -1;

   // update cube settings
   cube_rows = (int)l_cube_rows.value;
   cube_cols = (int)l_cube_cols.value;
   strcpy(cube_order, l_cube_order.string);
   int cubechange = cube != pcube || cube_rows!=pcube_rows || cube_cols!=pcube_cols || strcmp(cube_order,pcube_order);

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
   if (sizechange || fovchange || lenschange || cubechange) {
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
   pcube = cube;
   pcube_rows = cube_rows;
   pcube_cols = cube_cols;
   strcpy(pcube_order, cube_order);
   pcolorcube = colorcube;
}

