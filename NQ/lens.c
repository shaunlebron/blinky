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

static int left, top;
static int width, height, diag;
static double fov;
static int lens;
static int* framesize;
static double scale;
static int faceDisplay[] = {0,0,0,0,0,0};
static int cube = 0;
static int colorcube = 0;
static int cube_rows;
static int cube_cols;
static char cube_order[MAX_CUBE_ORDER];

// retrieves a pointer to a pixel in the video buffer
#define VBUFFER(x,y) (vid.buffer + (x) + (y)*vid.rowbytes)

// retrieves a pointer to a pixel in a designated cubemap face
#define CUBEFACE(side,x,y) (cubemap + (side)*width*height + (x) + (y)*width)

// retrieves a pointer to a pixel in the lensmap
#define LENSMAP(x,y) (lensmap + (x) + (y)*width)

void L_Help();

void L_CaptureCubeMap()
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
#define WRITE_FILE(n) WritePCXfile(filename,cubemap+width*height*n,width,height,width,host_basepal);

   SET_FILE_FACE("front"); WRITE_FILE(BOX_FRONT);
   SET_FILE_FACE("right"); WRITE_FILE(BOX_RIGHT);
   SET_FILE_FACE("behind"); WRITE_FILE(BOX_BEHIND);
   SET_FILE_FACE("left"); WRITE_FILE(BOX_LEFT);
   SET_FILE_FACE("top"); WRITE_FILE(BOX_TOP);
   SET_FILE_FACE("bottom"); WRITE_FILE(BOX_BOTTOM);

   Con_Printf("Saved cubemap to cube%02d_XXXX.pcx\n",i);
}

void L_ShowFovDeprecate()
{
   Con_Printf("Please use hfov instead\n");
}

void L_NextLens();
void L_PrevLens();

void L_IncFov()
{
   int value = (int)l_hfov.value;
   if (value >= 360)
      return;
   Cvar_SetValue("hfov", value + 45);
}

void L_DecFov()
{
   int value = (int)l_hfov.value;
   if (value <= 90)
      return;
   Cvar_SetValue("hfov", value - 45);
}

void L_Cube()
{
   cube = cube ? 0 : 1;
   Con_Printf("Cube is %s\n", cube ? "ON" : "OFF");
}

void L_ColorCube()
{
   colorcube = colorcube ? 0 : 1;
   Con_Printf("Colored Cube is %s\n", colorcube ? "ON" : "OFF");
}

void L_Init(void)
{
    Cmd_AddCommand("lenses", L_Help);
    Cmd_AddCommand("savecube", L_CaptureCubeMap);
    Cmd_AddCommand("fov", L_ShowFovDeprecate);
    Cmd_AddCommand("nextlens", L_NextLens);
    Cmd_AddCommand("prevlens", L_PrevLens);
    Cmd_AddCommand("incfov", L_IncFov);
    Cmd_AddCommand("decfov", L_DecFov);
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
#define CalcCylinderRay \
   ray[0] = sin(lon)*cos(lat); \
   ray[1] = sin(lat); \
   ray[2] = cos(lon)*cos(lat);

typedef struct
{
   int (*map)(double x, double y, vec3_t ray);
   int (*focal)();
   const char* name;
   const char* desc;
} lens_t;

void getRayLonLat(vec3_t ray, double *lon, double *lat)
{
   VectorNormalize(ray);
   double x=ray[0], y=ray[1], z=ray[2];
   *lon = atan2(x,z);
   *lat = atan2(y,sqrt(x*x+z*z));
}


/***************************************************
             START LENS DEFINITIONS
*************************************************/

int equidistantFisheyeMap(double x, double y, vec3_t ray)
{
   // r = f*theta

   double r = R;
   double el = r;

   if (el > M_PI)
      return 0;

   CalcRay;
   return 1;
}

int equidistantFisheyeInit()
{
   scale = HALF_FOV / HALF_FRAME;
   return 1;
}

int equisolidAngleFisheyeMap(double x, double y, vec3_t ray)
{
   // r = 2*f*sin(theta/2)

   double r = R;
   double maxr = 2/* *sin(M_PI/2)*/;
   if (r > maxr)
      return 0;

   double el = 2*asin(r/2);

   CalcRay;
   return 1;
}


int equisolidAngleFisheyeInit()
{
   if (HALF_FOV > M_PI)
      return 0;

   scale = 2*sin(HALF_FOV/2) / HALF_FRAME;
   return 1;
}

int azStereographicMap(double x, double y, vec3_t ray)
{
   // r = 2f*tan(theta/2)

   double r = R;
   double el = 2*atan2(r,2);

   if (el > M_PI)
      return 0;

   CalcRay;
   return 1;
}

int azStereographicInit()
{
   if (HALF_FOV > M_PI)
      return 0;

   scale = 2*tan(HALF_FOV/2) / HALF_FRAME;
   return 1;
}

int rectilinearMap(double x, double y, vec3_t ray)
{
   // r = f*tan(theta)

   double r = R;
   double el = atan2(r,1);

   CalcRay;
   return 1;
}

int rectilinearInit()
{
   if (HALF_FOV > M_PI/2)
      return 0;

   scale = tan(HALF_FOV) / HALF_FRAME;
   return 1;
}

int azOrthogonalMap(double x, double y, vec3_t ray)
{
   // r = f*sin(theta)
   
   double r = R;
   //double maxr = f*sin(M_PI/2);
   if (r > 1)
      return 0;

   double el = asin(r);

   CalcRay;
   return 1;
}

int azOrthogonalInit()
{
   if (HALF_FOV > M_PI/2)
      return 0;

   scale = sin(HALF_FOV) / HALF_FRAME;
   return 1;
}

int equirectangularMap(double x, double y, vec3_t ray)
{
   double lon = x;
   double lat = y;
   if (fabs(lat) > M_PI/2 || fabs(lon) > M_PI)
      return 0;
   CalcCylinderRay;
   return 1;
}

int equirectangularInit()
{
   scale = fov/(2*HALF_FRAME);
   return 1;
}

static double mercatorHeight;
int mercatorMap(double x, double y, vec3_t ray)
{
   if (fabs(y) > mercatorHeight)
      return 0;
   
   double lon = x;
   double lat = atan(sinh(y));
   if (fabs(lat) > M_PI/2 || fabs(lon) > M_PI)
      return 0;
   CalcCylinderRay;
   return 1;
}

int mercatorInit()
{
   scale = fov/(2*HALF_FRAME);
   mercatorHeight = log(tan(M_PI/4 + M_PI/2/2));
   return 1;
}

int cylinderMap(double x, double y, vec3_t ray)
{
   double lon = x;
   double lat = atan(y);
   if (fabs(lat) > M_PI/2 || fabs(lon) > M_PI)
      return 0;
   CalcCylinderRay;
   return 1;
}

int cylinderInit()
{
   scale = fov/(2*HALF_FRAME);
   return 1;
}

static double millerHeight;
int millerMap(double x, double y, vec3_t ray)
{
   if (fabs(y) > millerHeight)
      return 0;
   
   double lon = x;
   double lat = 5.0/4*atan(sinh(4.0/5*y));
   if (fabs(lat) > M_PI/2 || fabs(lon) > M_PI)
      return 0;
   CalcCylinderRay;
   return 1;
}

int millerInit()
{
   scale = fov/(2*HALF_FRAME);
   millerHeight = 5/4*log(tan(M_PI/4 + 2*(M_PI/2)/5));
   return 1;
}

int paniniMap(double x, double y, vec3_t ray)
{
   double t = 4/(x*x+4);
   ray[0] = x*t;
   ray[1] = y*t;
   ray[2] = -1+2*t;
   return 1;
}

int paniniInit()
{
   scale = 2*tan(HALF_FOV/2) / HALF_FRAME;
   return 1;
}

static double gumbyScale;
int gumbyCylinderMap(double x, double y, vec3_t ray)
{
   paniniMap(x,y,ray);
   double lon,lat;
   getRayLonLat(ray,&lon,&lat);
   lat /= gumbyScale;
   lon /= gumbyScale;
   CalcCylinderRay;
   return 1;
}

int gumbyCylinderInit()
{
   gumbyScale = 0.75;
   double r = HALF_FRAME / tan(HALF_FOV/2*gumbyScale) / 2;
   scale = 1/r;
   return 1;
}

int gumbySphereMap(double x, double y, vec3_t ray)
{
   // r = 2f*tan(theta/2)

   double r = R;
   double el = 2*atan2(r,2);
   el /= gumbyScale;

   if (el > M_PI)
      return 0;

   CalcRay;
   return 1;
}

int gumbySphereInit()
{
   if (HALF_FOV > M_PI)
      return 0;

   gumbyScale = 0.75;
   scale = 2*tan(HALF_FOV/2*gumbyScale) / HALF_FRAME;
   return 1;
}

int hammerMap(double x, double y, vec3_t ray)
{
   if (x*x/8+y*y/2 > 1)
      return 0;

   double z = sqrt(1-0.0625*x*x-0.25*y*y);
   double lon = 2*atan(z*x/(2*(2*z*z-1)));
   double lat = asin(z*y);
   CalcCylinderRay;
   return 1;
}

int hammerInit()
{
   if (*framesize == width)
   {
      if (fov > 2*M_PI)
         return 0;
      double w = 2*sqrt(2)*sin(HALF_FOV/2)/sqrt(1+cos(HALF_FOV/2));
      scale = w / HALF_FRAME;
   }
   else if (*framesize == height)
   {
      if (fov > M_PI)
         return 0;
      double h = sqrt(2)*sin(HALF_FOV)/sqrt(1+cos(HALF_FOV));
      scale = h / HALF_FRAME;
   }
   else
   {
      // TODO: find an equation for the diagonal...
      return 0;
   }
   return 1;
}

int mollweideMap(double x, double y, vec3_t ray)
{
   if (x*x/8+y*y/2 > 1)
      return 0;

   double t = asin(y/sqrt(2));
   double lon = M_PI*x/(2*sqrt(2)*cos(t));
   double lat = asin((2*t+sin(2*t))/M_PI);
   CalcCylinderRay;
   return 1;
}

double computeMollweideTheta(double lat)
{
   double t = lat;
   double dt;
   do
   {
      dt = -(t + sin(t) - M_PI*sin(lat))/(1+cos(t));
      t += dt;
   }
   while (dt > 0.001);
   return t/2;
}

int mollweideInit()
{
   if (*framesize == width)
   {
      if (fov > 2*M_PI)
         return 0;
      double t = computeMollweideTheta(0);
      double w = 2*sqrt(2)/M_PI*HALF_FOV*cos(t);
      scale = w / HALF_FRAME;
   }
   else if (*framesize == height)
   {
      if (fov > M_PI)
         return 0;
      double t = computeMollweideTheta(HALF_FOV);
      double h = sqrt(2)*sin(t);
      scale = h / HALF_FRAME;
   }
   else
   {
      // TODO: find an equation for the diagonal...
      return 0;
   }
   return 1;
}

static double eckertIvMaxY;
static double eckertIvLastY;
static double eckertIvMaxX;

double computeEckertIvTheta(double lat)
{
   double t = lat/2;
   double dt;
   do
   {
      dt = -(t + sin(t)*cos(t) + 2*sin(t) - (2+M_PI/2)*sin(lat))/(2*cos(t)*(1+cos(t)));
      t += dt;
   }
   while (dt > 0.001);
   return t;
}

int eckertIvMap(double x, double y, vec3_t ray)
{
   if (fabs(y) > eckertIvMaxY)
      return 0;

   double t = asin(y/2*sqrt((4+M_PI)/M_PI));
   double lat = asin((t+sin(t)*cos(t)+2*sin(t))/(2+M_PI/2));

   if (y != eckertIvLastY)
   {
      double t2 = computeEckertIvTheta(fabs(lat));
      eckertIvMaxX = 2/sqrt(M_PI*(4+M_PI))*M_PI*(1+cos(t2));
      eckertIvLastY = y;
   }

   if (fabs(x) > eckertIvMaxX)
      return 0;

   double lon = sqrt(M_PI*(4+M_PI))*x/(2*(1+cos(t)));

   CalcCylinderRay;
   return 1;
}

int eckertIvInit()
{
   double t = computeEckertIvTheta(M_PI/2);
   eckertIvMaxY = 2*sqrt(M_PI/(4+M_PI))*sin(t);

   if (*framesize == width)
   {
      if (fov > 2*M_PI)
         return 0;
      t = computeEckertIvTheta(0);
      double w = 2/sqrt(M_PI*(4+M_PI))*HALF_FOV*(1+cos(t));
      scale = w / HALF_FRAME;
   }
   else if (*framesize == height)
   {
      if (fov > M_PI)
         return 0;
      t = computeEckertIvTheta(HALF_FOV);
      double h = 2*sqrt(M_PI/(4+M_PI))*sin(t);
      scale = h / HALF_FRAME;
   }
   else
   {
      // TODO: find an equation for the diagonal...
      return 0;
   }
   return 1;
}

/***************************************************
             END LENS DEFINITIONS
*************************************************/

#define LENS(name, desc) { name##Map, name##Init, #name, desc }

static lens_t lenses[] = {
   LENS(rectilinear, "Rectilinear"),
   LENS(equidistantFisheye, "Equidistant Fisheye"),
   LENS(equisolidAngleFisheye, "Equisolid-Angle Fisheye"),
   LENS(azStereographic, "Stereographic"),
   LENS(azOrthogonal, "Orthogonal"),
   LENS(cylinder, "Cylinder"),
   LENS(equirectangular, "Equirectangular"),
   LENS(mercator, "Mercator"),
   LENS(miller, "Miller"),
   LENS(panini, "Panini"),
   LENS(gumbyCylinder, "Gumby Cylinder"),
   LENS(gumbySphere, "Gumby Sphere"),
   LENS(hammer, "Hammer"),
   LENS(mollweide, "Mollweide"),
   LENS(eckertIv, "Eckert IV"),
};

void PrintLensType()
{
   int i = (int)l_lens.value;
   Con_Printf("lens %d: %s\n",i, lenses[i].desc);
}

#define NUM_LENSES (sizeof(lenses)/sizeof(lens_t))

void L_NextLens()
{
   Cvar_SetValue("lens", ((int)l_lens.value+1)%NUM_LENSES);
}

void L_PrevLens()
{
   Cvar_SetValue("lens", ((int)l_lens.value-1)%NUM_LENSES);
}


void L_Help()
{
   Con_Printf("\nQUAKE LENSES\n--------\n");
   Con_Printf("hfov <degrees>: Specify FOV in horizontal degrees\n");
   Con_Printf("vfov <degrees>: Specify FOV in vertical degrees\n");
   Con_Printf("dfov <degrees>: Specify FOV in diagonal degrees\n");
   Con_Printf("lens <#>: Change the lens\n");
   Con_Printf("nextlens : goes to next lens\n");
   Con_Printf("prevlens : goes to previous lens\n");
   int i;
   for (i=0; i<sizeof(lenses)/sizeof(lens_t); ++i)
      Con_Printf("   %d: %s\n", i, lenses[i].desc);
   Con_Printf("\n---------\n");
   Con_Printf("cube: toggle cubemap\n");
   Con_Printf("colorcube: toggle paint cubemap\n");
   Con_Printf("\n---------\n");
   Con_Printf("Motion sick?  Try Stereographic or Panini\n");
}

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
               int fx = clamp(width*x/size,0,width-1);
               int fy = clamp(height*y/size,0,height-1);
               *LENSMAP(lx,ly) = CUBEFACE(face,fx,fy);
            }
      }
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

  int side_count[] = {0,0,0,0,0,0};

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

    x0 *= scale;
    y0 *= scale;

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
     faceDisplay[x] = (side_count[x] > width);
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
  for(y = 0;y<height;y++) {
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
     PrintLensType();
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
           B* face = cubemap+area*i;
           memset(face,colors[i],area);
        }
     }
  }
  else
  {
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
  pcube = cube;
  pcube_rows = cube_rows;
  pcube_cols = cube_cols;
  strcpy(pcube_order, cube_order);
  pcolorcube = colorcube;
}

