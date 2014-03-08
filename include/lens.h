#ifndef LENS_H_
#define LENS_H_

extern qboolean fisheye_enabled;

void L_Init(void);
void L_Shutdown(void);
void L_RenderView(void);
void L_WriteConfig(FILE *f);

#endif
