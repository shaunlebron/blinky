// retrieves a pointer to a pixel in a designated cubemap face
#define CUBEFACE(side,x,y) (cubemap + (side)*cubesize*cubesize + (x) + (y)*cubesize)

// MAP SYMMETRIES
static int hsym,vsym;
#define NO_SYMMETRY 0
#define H_SYMMETRY 1
#define V_SYMMETRY 2

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

#undef SET_FILE_FACE
#undef WRITE_FILE

   Con_Printf("Saved cubemap to cube%02d_XXXX.pcx\n",i);
}

void ray_to_cubemap(vec3_t ray, int *side, double *u, double *v)
{
   +X = RIGHT
   -X = LEFT
   +Y = TOP
   -Y = BOTTOM
   +Z = FRONT
   -Z = BEHIND
   // determine which side of the box we need
   double sx = ray[0], sy=ray[1], sz=ray[2];
   double abs_x = fabs(sx);
   double abs_y = fabs(sy);
   double abs_z = fabs(sz);			
   if (abs_x > abs_y) {
      if (abs_x > abs_z) { *side = ((sx > 0.0) ? BOX_RIGHT : BOX_LEFT);   }
      else               { *side = ((sz > 0.0) ? BOX_FRONT : BOX_BEHIND); }
   } else {
      if (abs_y > abs_z) { *side = ((sy > 0.0) ? BOX_TOP : BOX_BOTTOM); }
      else               { *side = ((sz > 0.0) ? BOX_FRONT : BOX_BEHIND); }
   }

#define T(x) (((x)*0.5) + 0.5)

   // get u,v coordinates
   switch(*side) {
      case BOX_FRONT:  *u = T( sx /  sz); *v = T( -sy /  sz); break;
      case BOX_BEHIND: *u = T(-sx / -sz); *v = T( -sy / -sz); break;
      case BOX_LEFT:   *u = T( sz / -sx); *v = T( -sy / -sx); break;
      case BOX_RIGHT:  *u = T(-sz /  sx); *v = T( -sy /  sx); break;
      case BOX_BOTTOM: *u = T( sx /  -sy); *v = T( -sz / -sy); break;
      case BOX_TOP:    *u = T(sx /  sy); *v = T( sz / sy); break;
   }

#undef T
}
