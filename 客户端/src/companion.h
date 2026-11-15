#ifndef COMPANION_H
#define COMPANION_H
#include "pet.h"
#include "growth_core.h"
#define CMD_COMPANION 109
#define CMD_STUDY 110
#define WM_RESOURCE_SAMPLE (WM_APP+24)
void companion_init(HWND owner,const wchar_t *settings_path);
void companion_show(void);
void companion_study_show(void);
void companion_save(void);
void companion_toggle_focus(void);
void companion_extras_show(void);
void companion_growth_metrics(GrowthMetrics *metrics);
bool companion_is_quiet(void);
bool companion_resources_enabled(void);
bool companion_apply_preferences(bool quiet,bool resources);
const wchar_t *companion_data_path(void);
bool companion_storage_ready(void);
void companion_discard_changes(void);
bool companion_start_task(void);
void companion_tick(double now,bool visible,bool pet_menu_open);
void companion_suspend(void);
void companion_shutdown(void);
void companion_resource_message(LPARAM data);
void companion_pet(void);
void companion_notice(const wchar_t *text);
void companion_cancel_speech(void);
bool companion_selftest(FILE *log);
bool companion_ui_selftest(FILE *log);
bool companion_dialog_message(MSG *message);
#endif
