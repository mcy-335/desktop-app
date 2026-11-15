#ifndef PET_TIMETABLE_H
#define PET_TIMETABLE_H
#include "pet.h"
#define CMD_TIMETABLE 115
void timetable_init(HWND owner,const wchar_t *settings_path);
bool timetable_flush(void);
void timetable_show(void);
void timetable_tick(double now);
void timetable_session(bool locked);
void timetable_power(bool suspended);
void timetable_shutdown(void);
bool timetable_dialog_message(MSG *message);
bool timetable_ui_selftest(FILE *log);
int timetable_preview_test(void);
#endif
