#include "timetable_core.h"
#include <string.h>
#include <wctype.h>
void course_defaults(Timetable *table){memset(table,0,sizeof(*table));table->next_id=1;table->enabled=true;}
int course_day(int year,int month,int day){
 static const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
 if(year<1||year>9999||month<1||month>12)return 0;
 bool leap=year%4==0&&(year%100!=0||year%400==0);
 if(day<1||day>lengths[month-1]+(month==2&&leap))return 0;
 int y=year-1,n=y*365+y/4-y/100+y/400+day;
 for(int m=1;m<month;m++)n+=lengths[m-1]+(m==2&&leap);
 return n;
}
int course_weekday(int day){return day>0?(day-1)%7:-1;}
int course_parse_time(const wchar_t *text,bool end){
 if(!text)return -1;size_t n=wcslen(text);if(n!=4&&n!=5)return -1;size_t colon=n-3;
 if(text[colon]!=L':')return -1;int hour=0,minute=0;
 for(size_t i=0;i<colon;i++){if(text[i]<L'0'||text[i]>L'9')return -1;hour=hour*10+text[i]-L'0';}
 for(size_t i=colon+1;i<n;i++){if(text[i]<L'0'||text[i]>L'9')return -1;minute=minute*10+text[i]-L'0';}
 if(minute>59||hour>24||(hour==24&&(!end||minute)))return -1;return hour*60+minute;
}
static bool valid_text(const wchar_t *text,int capacity,bool required){
 size_t n=0;bool content=false;
 for(;n<(size_t)capacity&&text[n];n++){if(text[n]<32)return false;if(!iswspace(text[n]))content=true;}
 return n<(size_t)capacity&&(!required||content);
}
bool course_valid(const Course *course){return course&&valid_text(course->name,COURSE_NAME,true)&&valid_text(course->room,COURSE_ROOM,false)&&course->days>0&&course->days<=127&&course->start>=0&&course->start<1440&&course->end>course->start&&course->end<=1440&&course->lead>=0&&course->lead<=120&&course->last_day>=0&&course->last_day<=3652059;}
int course_find(const Timetable *table,uint32_t id){for(int i=0;i<table->count;i++)if(table->courses[i].id==id)return i;return -1;}
bool course_put(Timetable *table,const Course *value,uint32_t id){
 if(!course_valid(value))return false;int index=id?course_find(table,id):-1;
 if(id&&index<0)return false;
 if(!id){if(table->count>=COURSE_MAX||!table->next_id||table->next_id>0x7ffffffe)return false;id=table->next_id++;index=table->count++;}
 int last_day=table->courses[index].last_day;table->courses[index]=*value;table->courses[index].id=id;
 // Editing a course never repeats a reminder already delivered for that date.
 if(last_day>table->courses[index].last_day)table->courses[index].last_day=last_day;return true;
}
bool course_delete(Timetable *table,uint32_t id){int index=course_find(table,id);if(index<0)return false;memmove(&table->courses[index],&table->courses[index+1],(table->count-index-1)*sizeof(Course));memset(&table->courses[--table->count],0,sizeof(Course));return true;}
int course_conflict(const Timetable *table,const Course *value,uint32_t except_id){
 if(!value->enabled)return -1;
 for(int i=0;i<table->count;i++){const Course *other=&table->courses[i];if(other->id!=except_id&&other->enabled&&(other->days&value->days)&&value->start<other->end&&other->start<value->end)return i;}return -1;
}
int course_due(const Timetable *table,CourseClock now,CourseOccurrence *events,int capacity){
 if(!table->enabled||now.day<1||now.day>3652059||now.minute<0||now.minute>=1440||capacity<1)return 0;
 int count=0;int64_t current=(int64_t)now.day*1440+now.minute;
 for(int i=0;i<table->count;i++){
  const Course *course=&table->courses[i];if(!course->enabled)continue;
  for(int offset=-1;offset<=1;offset++){
   int day=now.day+offset;if(day<1||day>3652059||course->last_day>=day||!(course->days&(1<<course_weekday(day))))continue;
   int64_t start=(int64_t)day*1440+course->start,expiry=(int64_t)day*1440+(course->end<course->start+5?course->end:course->start+5);
   if(current>=start-course->lead&&current<expiry&&count<capacity)events[count++]=(CourseOccurrence){i,day,start,expiry};
  }
 }
 for(int i=1;i<count;i++){CourseOccurrence value=events[i];int j=i;while(j>0&&events[j-1].start>value.start){events[j]=events[j-1];j--;}events[j]=value;}
 return count;
}
bool course_next(const Timetable *table,CourseClock now,CourseOccurrence *event){
 if(now.day<1||now.day>3652059||now.minute<0||now.minute>=1440)return false;
 bool found=false;int64_t current=(int64_t)now.day*1440+now.minute;
 for(int i=0;i<table->count;i++){
  const Course *course=&table->courses[i];if(!course->enabled)continue;
  for(int offset=0;offset<=7;offset++){
   int day=now.day+offset;if(day>3652059||!(course->days&(1<<course_weekday(day))))continue;
   int64_t start=(int64_t)day*1440+course->start,end=(int64_t)day*1440+course->end;
   if(end>current&&(!found||start<event->start)){*event=(CourseOccurrence){i,day,start,end};found=true;}
  }
 }
 return found;
}
