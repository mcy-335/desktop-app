#ifndef LETTERPET_APPEARANCE_H
#define LETTERPET_APPEARANCE_H
#include "pet.h"
// Keep the themed tabletop footprint aligned with the original scene.
#define DIY_SCENE_SCALE 1.10
#define DIY_SCENE_Y_OFFSET (-.0216)
// Expand only the free side strips of the table; keys, mouse and hands stay fixed.
static inline double appearance_desk_x(double x,bool diy,bool inverse){
 double left=diy?.19:.13,right=diy?.79:.89,factor=diy?1.30:1.40;
 if(inverse)factor=1/factor;
 if(x<left)return left+(x-left)*factor;
 if(x>right)return right+(x-right)*factor;
 return x;
}

void appearance_init(const wchar_t *settings_path);
void appearance_close(void);
void appearance_show(void);
bool appearance_dialog_message(MSG *message);
bool appearance_active(void);
int appearance_character(void);
bool appearance_animate(double now);
bool appearance_load_sprite(const wchar_t *path,Image *image);
void appearance_tap(double now);
bool appearance_draw(uint32_t *pixels,int size,double now);
bool appearance_restore(void);
bool appearance_selftest(FILE *log);

#endif
