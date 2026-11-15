#ifndef STUDY_CORE_H
#define STUDY_CORE_H
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>
#define STUDY_TASKS 64
#define STUDY_DAYS 90
#define STUDY_TITLE 101
typedef struct {uint32_t id;bool done;wchar_t title[STUDY_TITLE];uint64_t milliseconds;unsigned rounds;} StudyTask;
typedef struct {int date;uint64_t milliseconds;unsigned rounds;} StudyDay;
typedef struct {StudyTask tasks[STUDY_TASKS];int count;uint32_t next_id,selected,active;int goal;StudyDay days[STUDY_DAYS];} StudyState;
void study_defaults(StudyState *s);
StudyTask *study_task(StudyState *s,uint32_t id);
bool study_add(StudyState *s,const wchar_t *title);
bool study_delete(StudyState *s,uint32_t id);
bool study_mark(StudyState *s,uint32_t id);
bool study_select(StudyState *s,uint32_t id,bool locked);
void study_begin(StudyState *s);
void study_record(StudyState *s,int date,uint64_t milliseconds,bool completed);
void study_credit(StudyState *s,int date,int previous_date,uint64_t ms_today,uint64_t elapsed,uint64_t focused,bool completed);
const StudyDay *study_day(const StudyState *s,int date);
bool study_date_valid(int date);
int study_previous_date(int date);
#endif
