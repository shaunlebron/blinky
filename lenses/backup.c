

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
   int (*mapForward)(vec3_t ray, double *x, double *y);
   int (*mapInverse)(double x, double y, vec3_t ray);
   int (*init)();
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

int equidistantFisheyeMapInverse(double x, double y, vec3_t ray)
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

int equisolidAngleFisheyeMapInverse(double x, double y, vec3_t ray)
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

int azStereographicMapInverse(double x, double y, vec3_t ray)
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

int rectilinearMapInverse(double x, double y, vec3_t ray)
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

int azOrthogonalMapInverse(double x, double y, vec3_t ray)
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

int equirectangularMapInverse(double x, double y, vec3_t ray)
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
int mercatorMapInverse(double x, double y, vec3_t ray)
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

int cylinderMapInverse(double x, double y, vec3_t ray)
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
int millerMapInverse(double x, double y, vec3_t ray)
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

int paniniMapInverse(double x, double y, vec3_t ray)
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
int gumbyCylinderMapInverse(double x, double y, vec3_t ray)
{
   paniniMapInverse(x,y,ray);
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

int gumbySphereMapInverse(double x, double y, vec3_t ray)
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

int hammerMapInverse(double x, double y, vec3_t ray)
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

int mollweideMapInverse(double x, double y, vec3_t ray)
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

int eckertIvMapInverse(double x, double y, vec3_t ray)
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

static double winkelLat0;
void winkelTripelGetXY(double lon, double lat, double *x, double *y)
{
   double clat = cos(lat);
   double temp = clat*cos(lon*0.5);
   double D = acos(temp);
   double C = 1 - temp*temp;
   temp = D/sqrt(C);

   *x = 0.5 * (2*temp*clat*sin(lon*0.5)+lon*cos(winkelLat0));
   *y = 0.5 * (temp*sin(lat) + lat);
}

int winkelTripelMapForward(vec3_t ray, double *x, double *y)
{
   double lon,lat;
   getRayLonLat(ray, &lon, &lat);
   winkelTripelGetXY(lon, lat, x, y);
   return 1;
}

int winkelTripelInit()
{
   // standard parallel chosen by Winkel(50 degrees 28')
   winkelLat0 = acos(2.0/M_PI);

   double w,h;
   if (*framesize == width)
   {
      if (fov > 2*M_PI)
         return 0;
      winkelTripelGetXY(HALF_FOV,0,&w,&h);
      scale = w / HALF_FRAME;
   }
   else if (*framesize == height)
   {
      if (fov > M_PI)
         return 0;
      winkelTripelGetXY(0,HALF_FOV,&w,&h);
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

#define LENS_FWD(name, desc) { name##MapForward, 0, name##Init, #name, desc }
#define LENS_INV(name, desc) { 0, name##MapInverse, name##Init, #name, desc }

static lens_t lenses[] = {
   LENS_INV(rectilinear, "Rectilinear"),
   LENS_INV(equidistantFisheye, "Equidistant Fisheye"),
   LENS_INV(equisolidAngleFisheye, "Equisolid-Angle Fisheye"),
   LENS_INV(azStereographic, "Stereographic"),
   LENS_INV(azOrthogonal, "Orthogonal"),
   LENS_INV(cylinder, "Cylinder"),
   LENS_INV(equirectangular, "Equirectangular"),
   LENS_INV(mercator, "Mercator"),
   LENS_INV(miller, "Miller"),
   LENS_INV(panini, "Panini"),
   LENS_INV(gumbyCylinder, "Gumby Cylinder"),
   LENS_INV(gumbySphere, "Gumby Sphere"),
   LENS_INV(hammer, "Hammer"),
   LENS_INV(mollweide, "Mollweide"),
   LENS_INV(eckertIv, "Eckert IV"),
   LENS_FWD(winkelTripel, "Winkel Tripel"),
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
