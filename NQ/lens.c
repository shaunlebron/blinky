// lens.c -- player lens viewing

#include "bspfile.h"
#include "client.h"
#include "cmd.h"
#include "console.h"
#include "cvar.h"
#include "draw.h"
#include "host.h"
#include "quakedef.h"
#include "screen.h"
#include "view.h"
#include "lens.h"

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

void fisheyeMap(int width, int height, double fov, double dx, double dy, double *sx, double *sy, double *sz)
{
    double yaw = sqrt(dx*dx+dy*dy)*fov/((double)width);
    double roll = -atan2(dy,dx);

    *sx = sin(yaw) * cos(roll);
    *sy = sin(yaw) * sin(roll);
    *sz = cos(yaw);
}

void cylinderMap(int width, int height, double fov, double dx, double dy, double *sx, double *sy, double *sz)
{
    // create forward vector
    *sx = 0;
    *sy = 0;
    *sz = 1;

    double az = dx*fov/(double)width;
    double el = -dy*fov/(double)width;

    *sx = sin(az)*cos(el);
    *sy = sin(el);
    *sz = cos(az)*cos(el);
}

void perspectiveMap(int width, int height, double fov, double dx, double dy, double *sx, double *sy, double *sz)
{
    // create forward vector
    *sx = 0;
    *sy = 0;
    *sz = 1;

    double a = (double)width/2/tan(fov/2);

    double x = dx;
    double y = -dy;
    double z = a;

    double len = sqrt(x*x+y*y+z*z);

    *sx = x/len;
    *sy = y/len;
    *sz = z/len;
}

void stereographicMap(int width, int height, double fov, double dx, double dy, double *sx, double *sy, double *sz)
{
    double diam = (double)width/2 / tan(fov/4);
    double rad = diam/2;

    double t = 2*rad*diam / (dx*dx + dy*dy + diam*diam);

    *sx = dx * t / rad;
    *sy = -dy * t / rad;
    *sz = (-rad + diam * t) / rad;
}

void rendercopy(int *dest) {
  int *p = (int*)vid.buffer;
  int pad = 3;
  int x, y;
  int color = -1;
  R_PushDlights();
  R_RenderView();
  int border = (int)lens_grid.value;
  for(y = 0;y<vid.height;y++) {
    for(x = 0;x<(vid.width/4);x++,dest++) {
      int isborder = y<=pad || y+pad >= vid.height || x <= pad || x+pad>=vid.width/4;
      *dest = (border && isborder) ? color : p[x];
    } 

    p += (vid.rowbytes/4);
  };
};

void renderlookup(B **offs, B* bufs) {
  B *p = (B*)vid.buffer;
  int x, y;
  for(y = 0;y<vid.height;y++) {
    for(x = 0;x<vid.width;x++,offs++) p[x] = **offs;
    p += vid.rowbytes;
  };
};

void lookuptable(B **buf, int width, int height, B *scrp, double fov, int map) {
  int x, y;
  for(y = 0;y<height;y++) for(x = 0;x<width;x++) {
    double dx = x-width/2;
    double dy = -(y-height/2);

    // map the current window coordinate to a ray vector
    double sx, sy, sz;
    switch (map)
    {
       case 0: perspectiveMap(width, height, fov, dx, dy, &sx, &sy, &sz); break;
       case 1: fisheyeMap(width, height, fov, dx, dy, &sx, &sy, &sz); break;
       case 2: cylinderMap(width, height, fov, dx, dy, &sx, &sy, &sz); break;
       case 3: 
       default: stereographicMap(width, height, fov, dx, dy, &sx, &sy, &sz);
    }

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

void renderside(B* bufs, int side, vec3_t forward, vec3_t right, vec3_t up) {
  VectorCopy(forward, r_refdef.forward);
  VectorCopy(right, r_refdef.right);
  VectorCopy(up, r_refdef.up);
  rendercopy((int *)bufs);
};

//extern int istimedemo;

void updateFovs(int width, int height)
{
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
   }
   else if (pv != vfov.value)
   {
      Cvar_SetValue("fov_anchor", FOV_VERTICAL);
   }
   else if (pd != dfov.value)
   {
      Cvar_SetValue("fov_anchor", FOV_DIAGONAL);
   }
   
   // calculate focal length based on the fov anchor
   double diag = sqrt(width*width + height*height);
   int anchor = (int)fov_anchor.value;
   double focal_len;
   #define FOCAL(range,fov) (double)(range)/2/tan(fov.value/360*M_PI)
   switch (anchor)
   {
      case FOV_VERTICAL: focal_len = FOCAL(height, vfov); break;
      case FOV_DIAGONAL: focal_len = FOCAL(diag, dfov); break;
      case FOV_HORIZONTAL: 
      default: focal_len = FOCAL(width, hfov);
   }

   // update all FOVs to reflect new change
   #define FOV(range, focal) atan2((range)/2,(focal))*360/M_PI
   Cvar_SetValue("hfov", FOV(width, focal_len));
   Cvar_SetValue("vfov", FOV(height, focal_len));
   Cvar_SetValue("dfov", FOV(diag, focal_len));

   // update previous values for change detection
   ph = hfov.value;
   pv = vfov.value;
   pd = dfov.value;
}

void L_RenderView() {

  int width = vid.width; //r_refdef.vrect.width;
  int height = vid.height; //r_refdef.vrect.height;
  int scrsize = width*height;
  int views = 6;

  updateFovs(width, height);
  double fov = hfov.value;
  int map = (int)lens.value;

  static int pwidth = -1;
  static int pheight = -1;
  static int pmap = -1;
  static float pfov = -1;
  static int pviews = -1;

  static B *scrbufs = NULL;  
  static B **offs = NULL;

  if(pwidth!=width || pheight!=height || pfov!=fov || pmap!=map) {

    if(scrbufs) free(scrbufs);
    if(offs) free(offs);

    scrbufs = (B*)malloc(scrsize*6); // front|right|back|left|top|bottom
    offs = (B**)malloc(scrsize*sizeof(B*));
    if(!scrbufs || !offs) exit(1); // the rude way

    pwidth = width;
    pheight = height;
    pfov = fov;
    pmap = map;

    lookuptable(offs,width,height,scrbufs,fov*M_PI/180.0, map);
  };

  if(views!=pviews) {
    int i;
    pviews = views;
    for(i = 0;i<scrsize*6;i++) scrbufs[i] = 0;
  };

  // get the directions of all the cube map faces
  vec3_t forward, right, up, back, left, down;
  AngleVectors(r_refdef.viewangles, forward, right, up);
  VectorScale(forward, -1, back);
  VectorScale(right, -1, left);
  VectorScale(up, -1, down);

  r_refdef.useViewVectors = 1;

  switch(views) {
    case 6:  renderside(scrbufs+scrsize*2, BOX_BEHIND, back, left, up);
    case 5:  renderside(scrbufs+scrsize*5, BOX_BOTTOM, down, right, forward);
    case 4:  renderside(scrbufs+scrsize*4, BOX_TOP, up, right, back);
    case 3:  renderside(scrbufs+scrsize*3, BOX_LEFT, left, forward, up);
    case 2:  renderside(scrbufs+scrsize,   BOX_RIGHT, right, back, up);
    default: renderside(scrbufs,           BOX_FRONT, forward, right, up);
  };

  r_refdef.useViewVectors = 0;
  renderlookup(offs,scrbufs);

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
};

