#ifndef PET_DIALOGUE_CORE_H
#define PET_DIALOGUE_CORE_H
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>
#define TALK_MAX_LINES 32
#define TALK_TEXT 101
typedef enum {TALK_GENERAL,TALK_MORNING,TALK_DAY,TALK_EVENING,TALK_NIGHT,TALK_FOCUS,TALK_PAUSED,TALK_REST,TALK_LOW_MOOD,TALK_LOW_ENERGY,TALK_PET,TALK_FEED,TALK_CATEGORIES} TalkCategory;
typedef struct {uint32_t id;int category;wchar_t text[TALK_TEXT];} TalkLine;
typedef struct {bool enabled,builtins;int interval,count;uint32_t next_id;TalkLine lines[TALK_MAX_LINES];} TalkCatalog;
typedef struct {uint32_t random;int last_id;double next;wchar_t last_text[TALK_TEXT];} TalkRuntime;
void talk_defaults(TalkCatalog *catalog);
bool talk_add(TalkCatalog *catalog,int category,const wchar_t *text);
bool talk_edit(TalkCatalog *catalog,uint32_t id,int category,const wchar_t *text);
bool talk_delete(TalkCatalog *catalog,uint32_t id);
TalkLine *talk_line(TalkCatalog *catalog,uint32_t id);
int talk_context(int hour,int phase,int mood,int energy);
int talk_choose(const TalkCatalog *catalog,TalkRuntime *runtime,int category,wchar_t *output,int capacity);
void talk_expand(const wchar_t *text,const wchar_t *task,wchar_t *output,int capacity);
bool talk_ready(TalkRuntime *runtime,double now,int interval,bool enabled,bool allowed);
void talk_schedule(TalkRuntime *runtime,double now,int interval,bool displayed);
#endif
