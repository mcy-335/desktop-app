#ifndef PET_EXTRAS_H
#define PET_EXTRAS_H
#include "pet.h"
#include "growth_core.h"
#define CMD_EXTRAS 111
void extras_load(const wchar_t *path);
int extras_serialize(wchar_t *buffer,int capacity);
void extras_init(HWND owner,HFONT font,int dpi);
void extras_show(void);
void extras_close(void);
bool extras_update(bool notify);
void extras_manual_scene(void);
bool extras_dialog_message(MSG *message);
bool extras_ui_selftest(FILE *log);
void extras_caption(wchar_t *buffer,int capacity);
bool growth_selftest(FILE *log);
void pet_apply_scene_window(bool top,bool pass,bool game);
#endif
