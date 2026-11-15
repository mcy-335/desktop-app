#include "timetable_core.h"
#include <stdlib.h>
#define CHECK(x,label) do{bool result=(x);fprintf(log,"%s: %s\n",result?"PASS":"FAIL",label);if(!result)failures++;}while(0)
bool course_selftest(FILE *log){
 int failures=0;Timetable *table=calloc(1,sizeof(*table));if(!table)return false;course_defaults(table);CourseOccurrence events[COURSE_MAX],next;
 int monday=course_day(2026,11,2);CourseClock now={monday,470};Course value={0};wcscpy(value.name,L"高等数学 😀");wcscpy(value.room,L"教学楼 A-201");value.days=1|4;value.start=480;value.end=570;value.lead=10;value.enabled=true;
 CHECK(course_weekday(monday)==0&&course_weekday(course_day(2026,11,3))==1,"Gregorian dates map to Monday-based weekly recurrence");
 CHECK(course_day(2000,2,29)&&!course_day(1900,2,29)&&!course_day(2026,2,29)&&course_day(2024,3,1)-course_day(2024,2,28)==2,"leap years and invalid calendar dates are handled");
 CHECK(course_parse_time(L"8:05",false)==485&&course_parse_time(L"23:59",false)==1439&&course_parse_time(L"24:00",true)==1440,"valid course times include short hours and midnight endings");
 CHECK(course_parse_time(L"24:00",false)==-1&&course_parse_time(L"12:60",false)==-1&&course_parse_time(L"8:5",false)==-1&&course_parse_time(L"-1:00",false)==-1,"invalid clock inputs are rejected");
 CHECK(table->enabled&&!table->sound&&!table->interrupt&&course_put(table,&value,0),"reminders default to enabled, silent and respecting quiet/game modes");
 uint32_t id=table->courses[0].id;CHECK(id&&table->count==1&&!wcscmp(table->courses[0].name,value.name),"Unicode names and stable course IDs survive insertion");
 CHECK(course_due(table,(CourseClock){monday,469},events,COURSE_MAX)==0,"reminders never fire before the advance reminder time");
 CHECK(course_due(table,now,events,COURSE_MAX)==1&&events[0].day==monday,"reminder becomes due exactly at the configured advance time");
 CHECK(course_due(table,(CourseClock){monday,484},events,COURSE_MAX)==1&&course_due(table,(CourseClock){monday,485},events,COURSE_MAX)==0,"restart or wake catches up until five minutes after start, then expires");
 CHECK(course_due(table,(CourseClock){monday+1,470},events,COURSE_MAX)==0&&course_due(table,(CourseClock){monday+2,470},events,COURSE_MAX)==1,"selected weekdays repeat without leaking to other days");
 table->courses[0].last_day=monday;CHECK(!course_due(table,now,events,COURSE_MAX)&&course_due(table,(CourseClock){monday+7,470},events,COURSE_MAX)==1,"persisted occurrence date prevents duplicates and allows the next week");
 CHECK(!course_due(table,(CourseClock){monday-7,470},events,COURSE_MAX),"moving the clock backwards cannot repeat an older delivered reminder");
 value.start=490;value.end=580;CHECK(course_put(table,&value,id)&&table->courses[0].last_day==monday,"editing preserves the reminder delivery stamp");
 value.days=1;CHECK(course_conflict(table,&value,0)==0&&course_conflict(table,&value,id)==-1,"overlap detection excludes the course being edited");
 value.start=580;value.end=600;CHECK(course_conflict(table,&value,0)==-1,"back-to-back classes do not conflict");
 value.days=2;value.start=500;value.end=550;CHECK(course_conflict(table,&value,0)==-1,"classes on different weekdays do not conflict");
 value.days=1;value.end=value.start;CHECK(!course_put(table,&value,0),"zero-length and overnight course intervals are rejected");value.end=550;
 value.days=0;CHECK(!course_put(table,&value,0),"a course must choose at least one weekday");value.days=1;
 value.lead=121;CHECK(!course_put(table,&value,0),"advance reminder is bounded to 0 through 120 minutes");value.lead=10;
 wcscpy(value.name,L"  ");CHECK(!course_put(table,&value,0),"blank course titles cannot be saved");wcscpy(value.name,L"物理");
 table->enabled=false;CHECK(!course_due(table,(CourseClock){monday+7,480},events,COURSE_MAX),"global reminder switch suppresses emission");table->enabled=true;
 table->courses[0].enabled=false;CHECK(!course_due(table,(CourseClock){monday+7,480},events,COURSE_MAX)&&!course_next(table,now,&next),"disabled courses neither remind nor appear as upcoming");table->courses[0].enabled=true;
 CHECK(course_next(table,(CourseClock){monday,510},&next)&&next.day==monday&&next.start<(int64_t)monday*1440+510,"currently running classes remain visible in the next-course summary");
 CHECK(course_next(table,(CourseClock){monday+2,600},&next)&&next.day==monday+7,"upcoming search crosses the weekly boundary");
 course_defaults(table);value.days=1;value.start=5;value.end=60;value.lead=10;course_put(table,&value,0);
 CHECK(course_due(table,(CourseClock){monday-1,1435},events,COURSE_MAX)==1&&events[0].day==monday,"early-Monday reminders can fire late Sunday across the week boundary");
 value.days=64;value.start=1438;value.end=1440;value.lead=0;course_put(table,&value,0);
 CHECK(course_due(table,(CourseClock){monday,1},events,COURSE_MAX)==1&&events[0].index==0,"a course ending at midnight cannot produce an expired catch-up reminder");
 CHECK(!course_due(table,(CourseClock){0,0},events,COURSE_MAX)&&!course_due(table,(CourseClock){monday,1440},events,COURSE_MAX),"invalid clock snapshots cannot trigger reminders");
 CHECK(course_delete(table,table->courses[0].id)&&table->count==1&&!course_delete(table,0),"deleting preserves other course IDs and rejects missing records");
 for(int i=table->count;i<COURSE_MAX;i++)course_put(table,&value,0);
 CHECK(table->count==COURSE_MAX&&!course_put(table,&value,0),"weekly schedule capacity is bounded at 128 courses");
 free(table);fprintf(log,"TIMETABLE CORE OVERALL: %s (%d failures)\n",failures?"FAIL":"PASS",failures);return !failures;
}
