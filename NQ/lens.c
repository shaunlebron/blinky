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

cvar_t hfov = {"hfov", "180", true};
cvar_t vfov = {"vfov", "180", true};
cvar_t dfov = {"dfov", "180", true};
cvar_t fov_anchor = {"fov_anchor", "0", true};
cvar_t lens = {"lens", "1", true};
cvar_t lens_grid = {"lens_grid", "1", true};

void L_Help()
{
   Con_Printf("hello");
}

void L_Init(void)
{
    Cmd_AddCommand("lens_help", L_Help);
	 Cvar_RegisterVariable (&hfov);
	 Cvar_RegisterVariable (&vfov);
	 Cvar_RegisterVariable (&dfov);
	 Cvar_RegisterVariable (&fov_anchor);
    Cvar_RegisterVariable (&lens);
    Cvar_RegisterVariable (&lens_grid);
}

typedef unsigned char B;

#define BOX_FRONT  0
#define BOX_BEHIND 2
#define BOX_LEFT   3
#define BOX_RIGHT  1
#define BOX_TOP    4
#define BOX_BOTTOM 5

#define FOV_HORIZONTAL 0
#define FOV_VERTICAL   1
#define FOV_DIAGONAL   2

static int width, height;
static double fov;

typedef int (*map_t)(double dx, double dy, vec3_t ray);

int fisheyeMap(double dx, double dy, vec3_t ray)
{
    double yaw = sqrt(dx*dx+dy*dy)*fov/((double)width);
    if (yaw > M_PI)
       return 0;

    double roll = -atan2(dy,dx);

    ray[0] = sin(yaw) * cos(roll);
    ray[1] = sin(yaw) * sin(roll);
    ray[2] = cos(yaw);

    return 1;
}

int cylinderMap(double dx, double dy, vec3_t ray)
{
    double az = dx*fov/(double)width;
    double el = -dy*fov/(double)width;
    if (el < -M_PI/2 || el > M_PI/2)
       return 0;

    if (az < -M_PI || az > M_PI)
       return 0;

    ray[0] = sin(az)*cos(el);
    ray[1] = sin(el);
    ray[2] = cos(az)*cos(el);

    return 1;
}

int perspectiveMap(double dx, double dy, vec3_t ray)
{
    double a = (double)width/2/tan(fov/2);

    double x = dx;
    double y = -dy;
    double z = a;

    double len = sqrt(x*x+y*y+z*z);

    ray[0] = x/len;
    ray[1] = y/len;
    ray[2] = z/len;

    return 1;
}

int stereographicMap(double dx, double dy, vec3_t ray)
{
    double r = (double)width/(4*tan(fov/4));
    dx /= r;
    dy /= r;

    double t = 4 / (dx*dx + dy*dy + 4);

    ray[0] = dx * t;
    ray[1] = -dy * t;
    ray[2] = 2*t-1;

    return 1;
}

int equisolidMap(double dx, double dy, vec3_t ray)
{
   double cfov = fov <= 2*M_PI ? fov : 2*M_PI;
   double r = (double)width/4 / sin(cfov/4);
   dx /= r;
   dy /= r;

   double len = sqrt(dx*dx+dy*dy);
   if (len > 2)
      return 0;

   double a = asin(len/2);

   vec3_t in = { dx, dy, 0};
   vec3_t axis = { dy/len, -dx/len, 0};
   
   RotatePointAroundVector(ray, axis, in, -a*180/M_PI);
   ray[1] *= -1;
   ray[2] += 1;

   return 1;
}

static map_t maps[] = {
   perspectiveMap,
   fisheyeMap,
   cylinderMap,
   stereographicMap,
   equisolidMap
};

void lookuptable(B **buf, B *scrp, int map) {
  int x, y;
  for(y = 0;y<height;y++) for(x = 0;x<width;x++) {
    double dx = x-width/2;
    double dy = -(y-height/2);

    // map the current window coordinate to a ray vector
    vec3_t ray;
    if (!maps[map](dx,dy,ray))
    {
       *buf++ = 0;
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
      if (abs_y > abs_z) { side = ((sy > 0.0) ? BOX_BOTTOM : BOX_TOP); }
      else               { side = ((sz > 0.0) ? BOX_FRONT : BOX_BEHIND); }
    }

    #define R(x) (((x)/2) + 0.5)

    // scale up our vector [x,y,z] to the box
    switch(side) {
      case BOX_FRONT:  xs = R( sx /  sz); ys = R( sy /  sz); break;
      case BOX_BEHIND: xs = R(-sx / -sz); ys = R( sy / -sz); break;
      case BOX_LEFT:   xs = R( sz / -sx); ys = R( sy / -sx); break;
      case BOX_RIGHT:  xs = R(-sz /  sx); ys = R( sy /  sx); break;
      case BOX_BOTTOM: xs = R( sx /  sy); ys = R( sz / -sy); break;
      case BOX_TOP:    xs = R(-sx /  sy); ys = R( sz / -sy); break;
    }

    int px = (int)(xs*width);
    int py = (int)(ys*height);

    if (px < 0) px = 0;
    if (px >= width) px = width - 1;
    if (py < 0) py = 0;
    if (py >= height) py = height - 1;

    *buf++ = scrp + (px + py*width) + side*width*height;
  };
};

void renderlookup(B **offs, B* bufs) {
  int x, y;
  for(y = 0;y<scr_vrect.height;y++) {
    for(x = 0;x<scr_vrect.width;x++,offs++) 
       if (*offs)
         vid.buffer[scr_vrect.x+x+(y+scr_vrect.y)*vid.rowbytes] = **offs;
  }
}

void renderside(B* bufs, int side, vec3_t forward, vec3_t right, vec3_t up) {
  int y;
  VectorCopy(forward, r_refdef.forward);
  VectorCopy(right, r_refdef.right);
  VectorCopy(up, r_refdef.up);

  B *p = vid.buffer + scr_vrect.x + scr_vrect.y*vid.rowbytes;
  R_PushDlights();
  R_RenderView();
  for(y = 0;y<scr_vrect.height;y++) {
     memcpy(bufs, p, scr_vrect.width);
     p += vid.rowbytes;
     bufs += scr_vrect.width;
  }
}

//extern int istimedemo;

void updateFovs()
{
   // TODO: fix this; vertical|diagonal FOVs rely on knowledge of the type of projection (this only applies to horizontal gnomonic)

   // previous FOV values
   static double ph = -1;
   static double pv = -1;
   static double pd = -1;

   // set the most recently changed FOV as the anchor
   if (ph == -1 && pv == -1 && pd == -1)
   {
      // just starting, so don't change anchor
   }
   else if (ph != hfov.value)
   {
      Cvar_SetValue("fov_anchor", FOV_HORIZONTAL);
      Con_Printf("setting horizontal anchor");
   }
   else if (pv != vfov.value)
   {
      Cvar_SetValue("fov_anchor", FOV_VERTICAL);
      Con_Printf("setting vertical anchor");
   }
   else if (pd != dfov.value)
   {
      Cvar_SetValue("fov_anchor", FOV_DIAGONAL);
      Con_Printf("setting diagonal anchor");
   }
   
   // calculate focal length based on the fov anchor
   double diag = sqrt(width*width + height*height);
   int anchor = (int)fov_anchor.value;
   double focal_len;
   #define FOCAL(range,f) (double)(range)/2/tan(f.value/360*M_PI)
   switch (anchor)
   {
      case FOV_VERTICAL: focal_len = FOCAL(height, vfov); break;
      case FOV_DIAGONAL: focal_len = FOCAL(diag, dfov); break;
      case FOV_HORIZONTAL: 
      default: focal_len = FOCAL(width, hfov);
   }

   // update all FOVs to reflect new change
   #define FOV(range, focal) atan2((double)(range)/2,(focal))*360/M_PI
   Cvar_SetValue("hfov", FOV(width, focal_len));
   Cvar_SetValue("vfov", FOV(height, focal_len));
   Cvar_SetValue("dfov", FOV(diag, focal_len));

   // update previous values for change detection
   ph = hfov.value;
   pv = vfov.value;
   pd = dfov.value;
}

extern void R_SetupFrame(void);
extern void R_DrawViewModel(void);
extern cvar_t r_drawviewmodel;

void L_RenderView() {

  static int pwidth = -1;
  static int pheight = -1;
  static int pmap = -1;
  static float pfovd = -1;
  static int pviews = -1;

  // update screen size
  width = scr_vrect.width; 
  height = scr_vrect.height;
  int scrsize = width*height;
  int views = 6;

  // update FOVs
  fov = hfov.value * M_PI / 180;
  double fovd = hfov.value;

  // update Map
  int map = (int)lens.value;
  int numMaps = sizeof(maps) / sizeof(map_t);
  if (map < 0 || map >= numMaps)
  {
     map = pmap == -1 ? 0 : pmap;
     Cvar_SetValue("lens", map);
  }

  static B *scrbufs = NULL;  
  static B **offs = NULL;

  if(pwidth!=width || pheight!=height || pfovd!=fovd || pmap!=map) {

    if(scrbufs) free(scrbufs);
    if(offs) free(offs);

    scrbufs = (B*)malloc(scrsize*6); // front|right|back|left|top|bottom
    offs = (B**)malloc(scrsize*sizeof(B*));
    if(!scrbufs || !offs) exit(1); // the rude way

    lookuptable(offs,scrbufs, map);
  }

  if(views!=pviews) {
    int i;
    for(i = 0;i<scrsize*6;i++) scrbufs[i] = 0;
  }

  // get the directions of all the cube map faces
  vec3_t forward, right, up, back, left, down;
  AngleVectors(r_refdef.viewangles, forward, right, up);
  VectorScale(forward, -1, back);
  VectorScale(right, -1, left);
  VectorScale(up, -1, down);

  switch(views) {
    case 6:  renderside(scrbufs+scrsize*2, BOX_BEHIND, back, left, up);
    case 5:  renderside(scrbufs+scrsize*5, BOX_BOTTOM, down, right, forward);
    case 4:  renderside(scrbufs+scrsize*4, BOX_TOP, up, right, back);
    case 3:  renderside(scrbufs+scrsize*3, BOX_LEFT, left, forward, up);
    case 2:  renderside(scrbufs+scrsize,   BOX_RIGHT, right, back, up);
    default: renderside(scrbufs,           BOX_FRONT, forward, right, up);
  }

  Draw_TileClear(0, 0, vid.width, vid.height);
  renderlookup(offs,scrbufs);

    pwidth = width;
    pheight = height;
    pfovd = fovd;
    pmap = map;
    pviews = views;

  /*
  static int demonum = 0;
  char framename[100];
  if(istimedemo) { 
    sprintf(framename,"anim/ani%05d.pcx",demonum++);
    //Con_Printf("attempting to write %s\n",framename);
    WritePCXfile(framename,vid.buffer,vid.width,vid.height,vid.rowbytes,host_basepal);
  } else {
    demonum = 0;
  }
  */
}

