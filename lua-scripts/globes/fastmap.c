// fast mode
int fastmode;
double fast_maxfov;
double renderfov;

// retrieves a pointer to a pixel in the fastmap (just the 2nd cubeface)
#define FASTMAP(x,y) CUBEFACE(1,x,y)


// cube faces
#define BOX_FRONT  0
#define BOX_RIGHT  1
#define BOX_BEHIND 2
#define BOX_LEFT   3
#define BOX_TOP    4
#define BOX_BOTTOM 5

int ray_to_fastmap(vec3_t ray, int* side, int* x, int* y)
{
   double sx = ray[0], sy=ray[1], sz=ray[2];

   // exit if pointing behind us
   if (sz <= 0) {
      return 0;
   }

   // get screen distance
   double dist = (cubesize/2) / tan(fast_maxfov/2);
   int boundsize = (int)(2 * dist * tan(M_PI/4));

   // project vector to the screen
   double fx = sx / sz * dist;
   double fy = sy / sz * dist;

   if (abs(fx) < boundsize/2 && abs(fy) < boundsize/2) {
      // scale up
      fx = fx / (boundsize/2) * cubesize/2;
      fy = fy / (boundsize/2) * cubesize/2;
      *side = 0;
   }
   else {
      *side = 1;
   }

   // transform to screen coordinates
   fx += cubesize/2;
   fy = -fy;
   fy += cubesize/2;

   *x = (int)fx;
   *y = (int)fy;

   // outside the fastmap
   if (*x < 0 || *x >= cubesize || *y < 0 || *y >= cubesize) {
      return 0;
   }

   return 1;
}
