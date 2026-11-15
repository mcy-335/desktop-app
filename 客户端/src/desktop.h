#ifndef PET_DESKTOP_H
#define PET_DESKTOP_H
#include "pet.h"
#define CMD_DESKTOP 112
void desktop_init(HWND owner,const wchar_t *settings_path);
bool desktop_tick(double now,bool intended,bool dragging,bool menu_open);
bool desktop_is_visible(void);
bool desktop_shell_surface(HWND window);
void desktop_user_visibility(bool visible);
void desktop_drag_begin(void);
void desktop_drag_end(void);
void desktop_position_changed(void);
void desktop_display_changed(void);
void desktop_show(void);
void desktop_shutdown(void);
void desktop_hotkey(WPARAM id);
bool desktop_dialog_message(MSG *message);
bool desktop_selftest(FILE *log);
bool desktop_ui_selftest(FILE *log);
void pet_desktop_action(int action);
void pet_desktop_position_saved(void);
#endif
