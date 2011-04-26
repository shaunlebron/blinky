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

#include <lua5.1/lua.h>
#include <lua5.1/lauxlib.h>
#include <lua5.1/lualib.h>

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

#define FOV_HORIZONTAL 0
#define FOV_VERTICAL   1
#define FOV_DIAGONAL   2

#define MAX_CUBE_ORDER 20
#define MAX_LENS_LEN 50

static int left, top;
static int width, height, diag;
static int cubesize;
static double fov;
static char lens[MAX_LENS_LEN];
static int* framesize;
static double scale;
static int faceDisplay[] = {0,0,0,0,0,0};
static int cube = 0;
static int colorcube = 0;
static int cube_rows;
static int cube_cols;
static char cube_order[MAX_CUBE_ORDER];

static int map;
static int valid_lens;

// retrieves a pointer to a pixel in the video buffer
#define VBUFFER(x,y) (vid.buffer + (x) + (y)*vid.rowbytes)

// retrieves a pointer to a pixel in a designated cubemap face
#define CUBEFACE(side,x,y) (cubemap + (side)*cubesize*cubesize + (x) + (y)*cubesize)
// retrieves a pointer to a pixel in the lensmap
#define LENSMAP(x,y) (lensmap + (x) + (y)*width)

void L_Help(void);

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
   else
   {
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

void L_Init(void)
{
    lua = luaL_newstate();
    luaL_openlibs(lua);

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

/******** BEGIN LUA ******************/

int lua_lens_inverse(double x, double y, vec3_t ray)
{
   // call inverse
   lua_getglobal(lua, "inverse");
   lua_pushnumber(lua, x);
   lua_pushnumber(lua, y);
   lua_call(lua, 2, 1);

   if (!lua_istable(lua, -1))
   {
      lua_pop(lua,1);
      return 0;
   }
   
   lua_rawgeti(lua, -1, 1);
   ray[0] = lua_tonumber(lua, -1);
   
   lua_rawgeti(lua, -2, 2);
   ray[1] = lua_tonumber(lua, -1);

   lua_rawgeti(lua, -3, 3);
   ray[2] = lua_tonumber(lua, -1);
   lua_pop(lua,4);

   return 1;
}

int lua_lens_forward(vec3_t ray, double *x, double *y)
{
   // call forward
   lua_getglobal(lua,"forward");
   lua_pushnumber(lua,ray[0]);
   lua_pushnumber(lua,ray[1]);
   lua_pushnumber(lua,ray[2]);
   lua_call(lua, 3, 1);

   if (!lua_istable(lua, -1))
   {
      lua_pop(lua,1);
      return 0;
   }

   lua_rawgeti(lua, -1, 1);
   *x = lua_tonumber(lua, -1);

   lua_rawgeti(lua, -2, 2);
   *y = lua_tonumber(lua, -1);

   lua_pop(lua,3);

   return 1;
}

static void stackDump (lua_State *L) {
   int i;
   int top = lua_gettop(L);
   for (i = 1; i <= top; i++) { /* repeat for each level */
      int t = lua_type(L, i);
      switch (t) {
         case LUA_TSTRING: { /* strings */
                              Con_Printf("’%s’", lua_tostring(L, i));
                              break;
                           }
         case LUA_TBOOLEAN: { /* booleans */
                               Con_Printf(lua_toboolean(L, i) ? "true" : "false");
                               break;
                            }
         case LUA_TNUMBER: { /* numbers */
                              Con_Printf("%g", lua_tonumber(L, i));
                              break;
                           }
         default: { /* other values */
                     Con_Printf("%s", lua_typename(L, t));
                     break;
                  }
      }
      Con_Printf(" "); /* put a separator */
   }
   Con_Printf("\n"); /* end the listing */
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

   // check return value
   int result = lua_tointeger(lua, -1);
   lua_pop(lua,1);
   if (result == 0)
      return 0;

   // load scale
   lua_getglobal(lua, "scale");
   scale = lua_tonumber(lua,-1);
   lua_pop(lua,1);

   return 1;
}

void lua_lens_clear(void)
{
   lua_pushnil(lua); lua_setglobal(lua, "inverse");
   lua_pushnil(lua); lua_setglobal(lua, "init");
   lua_pushnil(lua); lua_setglobal(lua, "forward");
   lua_pushnil(lua); lua_setglobal(lua, "scale");
   lua_pushnil(lua); lua_setglobal(lua, "map");
}

int lua_lens_load(void)
{
   lua_lens_clear();

   // set full filename
   char filename[100];
   sprintf(filename,"%s/lenses/%s.lua",com_gamedir,lens);

   // check if loaded correctly
   if (luaL_dofile(lua, filename) != 0)
   {
      Con_Printf("could not load %s\nERROR: %s\n", lens, lua_tostring(lua,-1));
      lua_pop(lua, 1);
      return 0;
   }

   // load mapForward mapInverse flags
   lua_getglobal(lua, "map");
   map = lua_tointeger(lua,-1);
   lua_pop(lua,1);

   // TODO: return 0 if all required functions are not present
   
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

void create_cubefold(B **lensmap, B *cubemap)
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

void create_lensmap_inverse(B **lensmap, B *cubemap)
{
  int side_count[] = {0,0,0,0,0,0};

  int x, y;
  for(y = 0;y<height;y++) 
   for(x = 0;x<width;x++,lensmap++) {
    double x0 = x-width/2;
    double y0 = -(y-height/2);

    x0 *= scale;
    y0 *= scale;

    // map the current window coordinate to a ray vector
    vec3_t ray = { 0, 0, 1};
    if (x==width/2 && y == height/2)
    {
       // FIXME: this is a workaround for strange dead pixel in the center
    }
    else if (!lua_lens_inverse(x0,y0,ray))
    {
       // pixel is outside projection
       continue;
    }

    // determine which side of the box we need
    double sx = ray[0], sy = ray[1], sz = ray[2];
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

    #define T(x) (((x)/2) + 0.5)

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
    *lensmap = CUBEFACE(side,px,py);
  }

  //Con_Printf("cubemap side usage count:\n");
  for(x=0; x<6; ++x)
  {
     //Con_Printf("   %d: %d\n",x,side_count[x]);
     faceDisplay[x] = (side_count[x] > 1);
  }
  //Con_Printf("rendering %d views\n",views);
}

void create_lensmap_forward(B **lensmap, B *cubemap)
{
   // create a ray
   // give to map
   // divide coordinates by scale
   // negate y
   // x += width/2
   // y += height/2
   // clamp x and y
   int side_count[] = {0,0,0,0,0,0};
   int x,y;
   int side;
   double nz = cubesize*0.5;
   for (side=0; side<6; ++side)
   {
      for (y=0; y<cubesize; ++y)
      {
         double ny = -(y-cubesize*0.5);
         for (x=0; x<cubesize; ++x)
         {
            double nx = x-cubesize*0.5;
            vec3_t ray;
            if (side == BOX_FRONT)
            {
               ray[0] = nx;
               ray[1] = ny;
               ray[2] = nz;
            }
            else if (side == BOX_BEHIND)
            {
               ray[0] = -nx;
               ray[1] = ny;
               ray[2] = -nz;
            }
            else if (side == BOX_LEFT)
            {
               ray[0] = -nz;
               ray[1] = ny;
               ray[2] = nx;
            }
            else if (side == BOX_RIGHT)
            {
               ray[0] = nz;
               ray[1] = ny;
               ray[2] = -nx;
            }
            else if (side == BOX_TOP)
            {
               ray[0] = nx;
               ray[1] = nz;
               ray[2] = -ny;
            }
            else if (side == BOX_BOTTOM)
            {
               ray[0] = nx;
               ray[1] = -nz;
               ray[2] = ny;
            }

            double x0,y0;
            if (!lua_lens_forward(ray, &x0, &y0))
            {
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
         }
      }
   }

   //Con_Printf("cubemap side usage count:\n");
   for(x=0; x<6; ++x)
   {
      //Con_Printf("   %d: %d\n",x,side_count[x]);
      faceDisplay[x] = (side_count[x] > 1);
   }
}

void create_lensmap(B **lensmap, B *cubemap)
{
  if (cube)
  {
     // set all faces to display
     memset(faceDisplay,1,6*sizeof(int));

     // create lookup table for unfolded cubemap
     create_cubefold(lensmap,cubemap);
     return;
  }

  if (!valid_lens)
     return;

  // test if this lens can support the current fov
  if (!lua_lens_init())
  {
     Con_Printf("This lens cannot handle the current FOV.\n");
     return;
  }

  if (map)
     create_lensmap_forward(lensmap,cubemap);
  else 
     create_lensmap_inverse(lensmap,cubemap);
}

void render_lensmap(B **lensmap)
{
  int x, y;
  for(y=0; y<height; y++) 
    for(x=0; x<width; x++,lensmap++) 
       if (*lensmap)
          *VBUFFER(x+left, y+top) = **lensmap;
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
    create_lensmap(lensmap,cubemap);
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
  render_lensmap(lensmap);

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

