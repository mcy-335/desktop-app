#include "study_core.h"
#include <string.h>
#include <wctype.h>
static uint64_t plus(uint64_t a,uint64_t b,uint64_t limit){return a>=limit||b>limit-a?limit:a+b;}
void study_defaults(StudyState *s){memset(s,0,sizeof(*s));s->next_id=1;s->goal=120;}
StudyTask *study_task(StudyState *s,uint32_t id){if(!id)return NULL;for(int i=0;i<s->count;i++)if(s->tasks[i].id==id)return &s->tasks[i];return NULL;}
static int month_days(int y,int m){static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};return days[m-1]+(m==2&&(y%400==0||(y%4==0&&y%100!=0)));}
bool study_date_valid(int date){int y=date/10000,m=date/100%100,d=date%100;return y>=1970&&y<=9999&&m>=1&&m<=12&&d>=1&&d<=month_days(y,m);}
int study_previous_date(int date){if(!study_date_valid(date))return 0;int y=date/10000,m=date/100%100,d=date%100;if(--d==0){if(--m==0){m=12;y--;}d=month_days(y,m);}return y*10000+m*100+d;}
bool study_add(StudyState *s,const wchar_t *title){
 if(s->count>=STUDY_TASKS||!title)return false;
 wchar_t clean[STUDY_TITLE];int n=0;
 for(;*title&&n<STUDY_TITLE-1;title++){wchar_t c=*title;if(c==L'\t')c=L' ';if(c>=32)clean[n++]=c;}
 while(n&&iswspace(clean[n-1]))n--;if(n&&clean[n-1]>=0xd800&&clean[n-1]<=0xdbff)n--;
 int start=0;while(start<n&&iswspace(clean[start]))start++;
 if(start==n)return false;clean[n]=0;
 while(!s->next_id||study_task(s,s->next_id))s->next_id++;
 StudyTask *t=&s->tasks[s->count++];memset(t,0,sizeof(*t));t->id=s->next_id++;wcscpy(t->title,clean+start);s->selected=t->id;return true;
}
bool study_delete(StudyState *s,uint32_t id){if(!id||s->active==id)return false;for(int i=0;i<s->count;i++)if(s->tasks[i].id==id){memmove(&s->tasks[i],&s->tasks[i+1],(s->count-i-1)*sizeof(StudyTask));s->count--;if(s->selected==id)s->selected=0;return true;}return false;}
bool study_mark(StudyState *s,uint32_t id){StudyTask *t=study_task(s,id);if(!t||s->active==id)return false;t->done=!t->done;if(t->done&&s->selected==id)s->selected=0;return true;}
bool study_select(StudyState *s,uint32_t id,bool locked){if(locked)return false;StudyTask *t=study_task(s,id);if(id&&(!t||t->done))return false;s->selected=id;return true;}
void study_begin(StudyState *s){StudyTask *t=study_task(s,s->selected);s->active=t&&!t->done?t->id:0;}
const StudyDay *study_day(const StudyState *s,int date){for(int i=0;i<STUDY_DAYS;i++)if(s->days[i].date==date)return &s->days[i];return NULL;}
void study_record(StudyState *s,int date,uint64_t milliseconds,bool completed){
 if(!milliseconds&&!completed)return;
 StudyTask *t=study_task(s,s->active);if(t){t->milliseconds=plus(t->milliseconds,milliseconds,1000000000000ULL);if(completed&&t->rounds<1000000)t->rounds++;}
 if(!study_date_valid(date))return;
 int slot=-1,oldest=0;for(int i=0;i<STUDY_DAYS;i++){if(s->days[i].date==date){slot=i;break;}if(s->days[i].date<s->days[oldest].date)oldest=i;}
 if(slot<0){slot=oldest;if(s->days[slot].date>date)return;s->days[slot]=(StudyDay){date,0,0};}
 StudyDay *d=&s->days[slot];d->milliseconds=plus(d->milliseconds,milliseconds,86400000ULL);if(completed&&d->rounds<1000000)d->rounds++;
}
void study_credit(StudyState *s,int date,int previous_date,uint64_t ms_today,uint64_t elapsed,uint64_t focused,bool completed){
 if(focused>elapsed)focused=elapsed;
 uint64_t before=date!=previous_date&&study_previous_date(date)==previous_date&&elapsed>ms_today?elapsed-ms_today:0;
 if(before>focused)before=focused;
 if(before)study_record(s,previous_date,before,completed&&before==focused);
 if(focused>before)study_record(s,date,focused-before,completed);
}
