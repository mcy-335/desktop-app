#ifndef PET_MUSIC_H
#define PET_MUSIC_H
#include "pet.h"
#define CMD_MUSIC 114
void music_init(HWND owner,const wchar_t *settings_path);
void music_show(void);
void music_suspend(void);
void music_shutdown(void);
bool music_dialog_message(MSG *message);
bool music_ui_selftest(FILE *log);
bool music_picker_order_selftest(FILE *log);
int music_preview_test(void);
#endif
