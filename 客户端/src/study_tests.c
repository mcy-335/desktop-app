#include "study_core.h"
#include <stdio.h>
#include <string.h>
bool study_selftest(FILE *log){
 int failures=0;
 #define CHECK(value,label) do{bool pass=(value);fprintf(log,"%s: %s\n",pass?"PASS":"FAIL",label);if(!pass)failures++;}while(0)
 StudyState s;study_defaults(&s);
 CHECK(!study_add(&s,L" \t\r\n"),"empty task titles are rejected");
 CHECK(study_add(&s,L"  软件工程报告  ")&&!wcscmp(s.tasks[0].title,L"软件工程报告"),"Unicode task title is trimmed");
 uint32_t first=s.selected;study_begin(&s);study_record(&s,20261102,60000,false);
 CHECK(s.tasks[0].milliseconds==60000&&s.tasks[0].rounds==0,"partial focus is credited without a completed round");
 study_add(&s,L"复习");uint32_t second=s.selected;
 CHECK(!study_select(&s,second,true)&&!study_delete(&s,first)&&!study_mark(&s,first),"active task is protected from retargeting, deletion and completion");
 study_record(&s,20261102,30000,true);
 CHECK(s.tasks[0].milliseconds==90000&&s.tasks[0].rounds==1&&s.tasks[1].milliseconds==0,"selected task changes cannot redirect an active session");
 s.active=0;CHECK(study_mark(&s,first)&&!study_select(&s,first,false),"completed tasks cannot start a focus session");
 CHECK(study_mark(&s,first)&&study_select(&s,first,false),"completed task can be reopened");
 study_select(&s,0,false);study_begin(&s);study_record(&s,20261102,5000,true);
 const StudyDay *day=study_day(&s,20261102);
 CHECK(day&&day->milliseconds==95000&&day->rounds==2&&s.tasks[0].milliseconds==90000,"free focus contributes to daily totals without modifying tasks");
 CHECK(study_delete(&s,first)&&study_day(&s,20261102)->milliseconds==95000,"deleting a task preserves daily history");
 study_defaults(&s);study_add(&s,L"跨午夜任务");study_begin(&s);
 study_credit(&s,20261103,20261102,2000,5000,5000,true);
 CHECK(study_day(&s,20261102)->milliseconds==3000&&study_day(&s,20261103)->milliseconds==2000&&study_day(&s,20261103)->rounds==1,"focus interval splits across local midnight");
 CHECK(s.tasks[0].milliseconds==5000&&s.tasks[0].rounds==1,"midnight split counts the task round once");
 study_defaults(&s);study_credit(&s,20261103,20261102,2000,5000,1000,true);
 CHECK(study_day(&s,20261102)&&study_day(&s,20261102)->milliseconds==1000&&study_day(&s,20261102)->rounds==1&&!study_day(&s,20261103),"completion before midnight excludes timer overshoot");
 CHECK(study_previous_date(20260301)==20260228&&study_previous_date(20240301)==20240229&&study_previous_date(20260101)==20251231,"calendar handles leap years and year rollover");
 CHECK(!study_date_valid(20260229)&&!study_date_valid(20261301)&&study_date_valid(20240229),"invalid dates are rejected");
 study_defaults(&s);for(int i=0;i<STUDY_TASKS;i++)study_add(&s,L"任务");
 CHECK(s.count==STUDY_TASKS&&!study_add(&s,L"额外任务"),"task capacity is bounded");
 uint32_t previous=s.tasks[1].id;study_delete(&s,s.tasks[0].id);study_add(&s,L"新任务");CHECK(s.tasks[0].id==previous&&s.tasks[s.count-1].id!=previous,"task IDs survive row deletion and insertion");
 study_defaults(&s);int date=20261231,oldest=0;for(int i=0;i<STUDY_DAYS;i++){study_record(&s,date,1000,false);oldest=date;date=study_previous_date(date);}study_record(&s,20270101,2000,false);
 CHECK(study_day(&s,20270101)&&!study_day(&s,oldest),"bounded daily history retains the newest dates");
 study_record(&s,20270101,UINT64_MAX,true);CHECK(study_day(&s,20270101)->milliseconds==86400000,"daily time cannot overflow");
 fprintf(log,"STUDY OVERALL: %s (%d failures)\n",failures?"FAIL":"PASS",failures);
 #undef CHECK
 return failures==0;
}
