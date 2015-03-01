#ifndef FISHEYE_H_
#define FISHEYE_H_

extern qboolean fisheye_enabled;

void F_Init(void);
void F_Shutdown(void);
void F_RenderView(void);
void F_WriteConfig(FILE *f);

#endif
