#ifndef PET_STUDY_H
#define PET_STUDY_H
#include "pet.h"
#include "growth_core.h"
void study_growth_metrics(GrowthMetrics *metrics);
void study_load(const wchar_t *path,bool has_work);
int study_serialize(wchar_t *buffer,int capacity);
void study_show(HWND owner,HFONT font,int dpi);
void study_refresh(bool rebuild);
void study_close(void);
void study_set_locked(bool locked);
void study_begin_session(void);
void study_end_session(void);
void study_credit_time(double elapsed,double focused,bool completed);
void study_clock_reset(void);
void study_current_title(wchar_t *buffer,int capacity);
bool study_selftest(FILE *log);
bool study_ui_selftest(HWND owner,HFONT font,int dpi,FILE *log);
bool study_storage_selftest(const wchar_t *path,FILE *log);
bool study_dialog_message(MSG *message);
#endif
