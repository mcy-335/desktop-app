#ifndef PET_TIMETABLE_CORE_H
#define PET_TIMETABLE_CORE_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#define COURSE_MAX 128
#define COURSE_NAME 65
#define COURSE_ROOM 81
typedef struct {
 uint32_t id;
 wchar_t name[COURSE_NAME],room[COURSE_ROOM];
 int days,start,end,lead,last_day;
 bool enabled;
} Course;
typedef struct {
 Course courses[COURSE_MAX];int count;
 uint32_t next_id;
 bool enabled,sound,interrupt;
} Timetable;
typedef struct {int day,minute;} CourseClock;
typedef struct {int index,day;int64_t start,expires;} CourseOccurrence;
void course_defaults(Timetable *table);
int course_day(int year,int month,int day);
int course_weekday(int day);
int course_parse_time(const wchar_t *text,bool end);
bool course_valid(const Course *course);
int course_find(const Timetable *table,uint32_t id);
bool course_put(Timetable *table,const Course *value,uint32_t id);
bool course_delete(Timetable *table,uint32_t id);
int course_conflict(const Timetable *table,const Course *value,uint32_t except_id);
int course_due(const Timetable *table,CourseClock now,CourseOccurrence *events,int capacity);
bool course_next(const Timetable *table,CourseClock now,CourseOccurrence *event);
bool course_selftest(FILE *log);
#endif
