#ifndef PET_DIALOGUE_H
#define PET_DIALOGUE_H
#include "pet.h"
#define CMD_DIALOGUE 113
void dialogue_load(const wchar_t *path);
int dialogue_serialize(wchar_t *buffer,int capacity);
void dialogue_init(HWND owner,HFONT font,int dpi);
void dialogue_tick(double now);
void dialogue_postpone(void);
void dialogue_event(bool feed);
void dialogue_show(void);
void dialogue_close(void);
bool dialogue_dialog_message(MSG *message);
bool dialogue_selftest(FILE *log);
bool dialogue_ui_selftest(FILE *log);
int companion_dialogue_context(void);
bool companion_speech_allowed(void);
bool companion_say(const wchar_t *text,bool manual);
#endif
