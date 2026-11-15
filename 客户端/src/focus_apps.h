#ifndef PET_FOCUS_APPS_H
#define PET_FOCUS_APPS_H

#include "pet.h"

void focus_apps_init(const wchar_t *settings_path);
void focus_apps_show(HWND owner);
bool focus_apps_tick(bool focusing,bool pet_menu_open);
bool focus_apps_dialog_message(MSG *message);
void focus_apps_shutdown(void);
bool focus_apps_ui_selftest(FILE *log);

#endif
