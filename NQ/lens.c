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
#include "screen.h"
#include "sys.h"
#include "view.h"

cvar_t l_hfov = {"hfov", "90", true};
cvar_t l_vfov = {"vfov", "-1", true};
cvar_t l_dfov = {"dfov", "-1", true};
cvar_t l_lens = {"lens", "0", true};
cvar_t l_lens_grid = {"lens_grid", "0", true};
cvar_t l_lens_grid_space = {"lens_grid_space", "10", true};
cvar_t l_lens_grid_width = {"lens_grid_width", "2", true};
cvar_t l_lens_grid_color = {"lens_grid_color", "10", true};
cvar_t l_cube = {"cube", "0", true};
cvar_t l_cube_rows = {"cube_rows", "3", true};
cvar_t l_cube_cols = {"cube_cols", "3", true};
cvar_t l_cube_order = {"cube_order", "949301952", true};

typedef unsigned char B;

#define BOX_FRONT  0
#define BOX_RIGHT  1
#define BOX_BEHIND 2
#define BOX_LEFT   3
#define BOX_TOP    4
#define BOX_BOTTOM 5

#define FOV_HORIZONTAL 0
#define FOV_VERTICAL   1
#define FOV_DIAGONAL   2

static int left, top;
static int width, height, diag;
static double fov;
static int lens;
static int* framesize;
static double focal;
static int faceDisplay[] = {0,0,0,0,0,0};

// retrieves a pointer to a pixel in the video buffer
#define VBUFFER(x,y) (vid.buffer + (x) + (y)*vid.rowbytes)

// retrieves a pointer to a pixel in a designated cubemap face
#define CUBEFACE(side,x,y) (cubemap + (side)*width*height + (x) + (y)*width)

void L_Help();

void L_Init(void)
{
    Cmd_AddCommand("lenses", L_Help);
	 Cvar_RegisterVariable (&l_hfov);
	 Cvar_RegisterVariable (&l_vfov);
	 Cvar_RegisterVariable (&l_dfov);
    Cvar_RegisterVariable (&l_lens);
    Cvar_RegisterVariable (&l_lens_grid);
    Cvar_RegisterVariable (&l_lens_grid_space);
    Cvar_RegisterVariable (&l_lens_grid_width);
    Cvar_RegisterVariable (&l_lens_grid_color);
    Cvar_RegisterVariable (&l_cube);
    Cvar_RegisterVariable (&l_cube_rows);
    Cvar_RegisterVariable (&l_cube_cols);
    Cvar_RegisterVariable (&l_cube_order);
}

// FISHEYE HELPERS
#define HALF_FRAME ((double)(*framesize)/2)
#define HALF_FOV (fov/2)
#define R (sqrt(x*x+y*y))
#define CalcRay \
   double s = sin(el); \
   double c = cos(el); \
   ray[0] = x/r * s; \
   ray[1] = y/r * s; \
   ray[2] = c;

typedef struct
{
   int (*map)(double x, double y, vec3_t ray);
   int (*focal)();
   const char* name;
   const char* desc;
} lens_t;

int fisheyeMap(double x, double y, vec3_t ray)
{
   // r = f*tan(t2) where sin(theta)/sin(t2) = 1.33
   // this is an actual fisheye (i.e. what a fish would see when viewing the world from underwater)

   // index of refraction of water
   double index = 1.33;
   
   // get elevation
   double r = R;
   double len = sqrt(r*r+focal*focal);
   double s2 = r/len;
   double s1 = index*s2;
   double el = asin(s1);

   CalcRay;
   return 1;
}

int fisheyeFocal()
{
   if (HALF_FOV > M_PI/2)
      return 0;

   // index of refraction of water
   double index = 1.33;

   // get focal length
   double s2 = 1/index*sin(HALF_FOV);
   double c2 = sqrt(1-s2*s2);
   focal = HALF_FRAME * c2 / s2;
   return 1;
}

int equidistantMap(double x, double y, vec3_t ray)
{
   // r = f*theta

   double r = R;
   double el = r/focal;

   if (el > M_PI)
      return 0;

   CalcRay;
   return 1;
}

int equidistantFocal()
{
   focal = HALF_FRAME / HALF_FOV;
   return 1;
}

int equisolidMap(double x, double y, vec3_t ray)
{
   // r = 2*f*sin(theta/2)

   double r = R;
   double maxr = 2*focal/* *sin(M_PI/2)*/;
   if (r > maxr)
      return 0;

   double el = 2*asin(r/(2*focal));

   CalcRay;
   return 1;
}

int equisolidFocal()
{
   if (HALF_FOV > M_PI)
      return 0;

   focal = HALF_FRAME / (2*sin(HALF_FOV/2));
   return 1;
}

int stereographicMap(double x, double y, vec3_t ray)
{
   // r = 2f*tan(theta/2)

   double r = R;
   double el = 2*atan2(r,2*focal);

   if (el > M_PI)
      return 0;

   CalcRay;
   return 1;
}

int stereographicFocal()
{
   if (HALF_FOV > M_PI)
      return 0;

   focal = HALF_FRAME / (2 * tan(HALF_FOV/2));
   return 1;
}

int gnomonicMap(double x, double y, vec3_t ray)
{
   // r = f*tan(theta)

   double r = R;
   double el = atan2(r,focal);

   CalcRay;
   return 1;
}

int gnomonicFocal()
{
   if (HALF_FOV > M_PI/2)
      return 0;

   focal = HALF_FRAME / tan(HALF_FOV);
   return 1;
}

int orthogonalMap(double x, double y, vec3_t ray)
{
   // r = f*sin(theta)

   double r = R;
   //double maxr = f*sin(M_PI/2);
   if (r > focal)
      return 0;

   double el = asin(r/focal);

   CalcRay;
   return 1;
}

int orthogonalFocal()
{
   if (HALF_FOV > M_PI/2)
      return 0;

   focal = HALF_FRAME / sin(HALF_FOV);
   return 1;
}

int equirectangularMap(double x, double y, vec3_t ray)
{
    double az = x*fov/(2*HALF_FRAME);
    double el = y*fov/(2*HALF_FRAME);
    if (el < -M_PI/2 || el > M_PI/2 || az < -M_PI || az > M_PI)
       return 0;

    ray[0] = sin(az)*cos(el);
    ray[1] = sin(el);
    ray[2] = cos(az)*cos(el);

    return 1;
}

int equirectangularFocal()
{
   return 1;
}

#define LENS(name, desc) { name##Map, name##Focal, #name, desc }

static lens_t lenses[] = {
   LENS(gnomonic, "standard perspective"),
   LENS(equidistant, "sphere unwrapped onto a circle"),
   LENS(equisolid, "mirror ball"),
   LENS(stereographic, "sphere viewed from its surface"),
   LENS(orthogonal, "hemisphere flattened"),
   LENS(equirectangular, "sphere unwrapped around cylinder"),
   LENS(fisheye, "viewing the sky from underwater")
};

void L_Help()
{
   Con_Printf("QUAKE LENSES\n--------\n");
   Con_Printf("hfov <degrees>: Specify FOV in horizontal degrees\n");
   Con_Printf("vfov <degrees>: Specify FOV in vertical degrees\n");
   Con_Printf("dfov <degrees>: Specify FOV in diagonal degrees\n");
   Con_Printf("lens <#>: Change the lens\n");
   int i;
   for (i=0; i<sizeof(lenses)/sizeof(lens_t); ++i)
      Con_Printf("   %d: %20s - %s\n", i, lenses[i].name, lenses[i].desc);
}

void create_lensmap(B **lensmap, B *cubemap)
{
  int side_count[] = {0,0,0,0,0,0};

  if ((int)l_cube.value)
  {

     return;
  }

  // lens' focal length impossible to compute from desired FOV
  if (!lenses[lens].focal())
  {
     Con_Printf("This lens cannot handle the current FOV.\n");
     return;
  }

  int x, y;
  for(y = 0;y<height;y++) 
   for(x = 0;x<width;x++,lensmap++) {
    double x0 = x-width/2;
    double y0 = -(y-height/2);

    // map the current window coordinate to a ray vector
    vec3_t ray = { 0, 0, 1};
    if (x==width/2 && y == height/2)
    {
    }
    else if (!lenses[lens].map(x0,y0,ray))
    {
       // pixel is outside projection
       continue;
    }

    // FIXME: strange negative y anomaly
    ray[1] *= -1;

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
      if (abs_y > abs_z) { side = ((sy > 0.0) ? BOX_BOTTOM : BOX_TOP); }
      else               { side = ((sz > 0.0) ? BOX_FRONT : BOX_BEHIND); }
    }

    #define T(x) (((x)/2) + 0.5)

    // scale up our vector [x,y,z] to the box
    switch(side) {
      case BOX_FRONT:  xs = T( sx /  sz); ys = T( sy /  sz); break;
      case BOX_BEHIND: xs = T(-sx / -sz); ys = T( sy / -sz); break;
      case BOX_LEFT:   xs = T( sz / -sx); ys = T( sy / -sx); break;
      case BOX_RIGHT:  xs = T(-sz /  sx); ys = T( sy /  sx); break;
      case BOX_BOTTOM: xs = T( sx /  sy); ys = T( sz / -sy); break;
      case BOX_TOP:    xs = T(-sx /  sy); ys = T( sz / -sy); break;
    }
    side_count[side]++;

    // convert to face coordinates
    int px = (int)(xs*width);
    int py = (int)(ys*height);

    // clamp coordinates
    if (px < 0) px = 0;
    if (px >= width) px = width - 1;
    if (py < 0) py = 0;
    if (py >= height) py = height - 1;

    // map lens pixel to cubeface pixel
    *lensmap = CUBEFACE(side,px,py);
  }

  //Con_Printf("cubemap side usage count:\n");
  for(x=0; x<6; ++x)
  {
     //Con_Printf("   %d: %d\n",x,side_count[x]);
     if (side_count[x] > width)
        faceDisplay[x] = 1;
  }
  //Con_Printf("rendering %d views\n",views);

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
  int gridspace = (int)l_lens_grid_space.value;
  int gridwidth = (int)l_lens_grid_width.value;
  B gridcolor = (B)l_lens_grid_color.value;
  int gridoffset;
  int grid;
  for(y = 0;y<height;y++) {
     grid = 0;
     if ((int)l_lens_grid.value)
     {
        for (gridoffset=0; gridoffset<gridwidth; ++gridoffset)
           if ((y-gridoffset) % gridspace == 0)
           {
              grid = 1;
              break;
           }
     }
     if (grid)
        memset(cubeface, gridcolor, width);
     else
        memcpy(cubeface, vbuffer, width);
     
     // advance to the next row
     vbuffer += vid.rowbytes;
     cubeface += width;
  }
}

void L_RenderView() 
{
  static int pwidth = -1;
  static int pheight = -1;
  static int plens = -1;
  static double phfov = -1;
  static double pvfov = -1;
  static double pdfov = -1;
  static int pcube = -1;
  static int pcube_rows = -1;
  static int pcube_cols = -1;
  static char *pcube_order = 0;

  static B *cubemap = NULL;  
  static B **lensmap = NULL;

  // update screen size
  left = scr_vrect.x;
  top = scr_vrect.y;
  width = scr_vrect.width; 
  height = scr_vrect.height;
  diag = sqrt(width*width+height*height);
  int area = width*height;
  int sizechange = pwidth!=width || pheight!=height;

  // update lens
  lens = (int)l_lens.value;
  int numLenses = sizeof(lenses) / sizeof(lens_t);
  if (lens < 0 || lens >= numLenses)
  {
     lens = plens == -1 ? 0 : plens;
     Cvar_SetValue("lens", lens);
     Con_Printf("not a valid lens\n");
  }
  int lenschange = plens!=lens;
  if (lenschange)
  {
     Con_Printf("lens %d: %s - %s\n",lens, lenses[lens].name, lenses[lens].desc);
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

    cubemap = (B*)malloc(area*6*sizeof(B));
    lensmap = (B**)malloc(area*sizeof(B*));
    if(!cubemap || !lensmap) exit(1); // the rude way
  }

  // recalculate lens
  if (sizechange || fovchange || lenschange) {
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
  for (i=0; i<6; ++i)
     if (faceDisplay[i]) {
        B* face = cubemap+area*i;
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

  // render our view
  Draw_TileClear(0, 0, vid.width, vid.height);
  render_lensmap(lensmap);

  // store current values for change detection
  pwidth = width;
  pheight = height;
  plens = lens;
  phfov = l_hfov.value;
  pvfov = l_vfov.value;
  pdfov = l_dfov.value;
}

