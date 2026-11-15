#include "companion_core.h"
#include <math.h>
#include <string.h>
static int bounded(int n,int lo,int hi){return n<lo?lo:n>hi?hi:n;}
void companion_defaults(CompanionState *s){memset(s,0,sizeof(*s));s->work_minutes=25;s->rest_minutes=5;s->mood=70;s->energy=80;s->affinity=0;}
void companion_validate(CompanionState *s){
 s->work_minutes=bounded(s->work_minutes,1,180);s->rest_minutes=bounded(s->rest_minutes,1,60);
 s->mood=bounded(s->mood,0,100);s->energy=bounded(s->energy,0,100);s->affinity=bounded(s->affinity,0,100);s->completed=bounded(s->completed,0,1000000);
 if(s->phase<FOCUS_IDLE||s->phase>FOCUS_PAUSED_REST)s->phase=FOCUS_IDLE;
 double limit=(s->phase==FOCUS_REST||s->phase==FOCUS_PAUSED_REST?s->rest_minutes:s->work_minutes)*60000.0;
 if(s->phase==FOCUS_WORK||s->phase==FOCUS_PAUSED_WORK||s->phase==FOCUS_REST||s->phase==FOCUS_PAUSED_REST){
  if(!isfinite(s->remaining_ms)||s->remaining_ms<=0||s->remaining_ms>limit){s->phase=FOCUS_IDLE;s->remaining_ms=0;}
 }else s->remaining_ms=0;
 if(!isfinite(s->care_ms)||s->care_ms<0||s->care_ms>=1800000)s->care_ms=0;
 if(!isfinite(s->focus_reward_ms)||s->focus_reward_ms<0||s->focus_reward_ms>=600000)s->focus_reward_ms=0;
}
void companion_pause(CompanionState *s){if(s->phase==FOCUS_WORK)s->phase=FOCUS_PAUSED_WORK;else if(s->phase==FOCUS_REST)s->phase=FOCUS_PAUSED_REST;}
void companion_toggle(CompanionState *s){
 switch(s->phase){
 case FOCUS_IDLE:s->phase=FOCUS_WORK;s->remaining_ms=s->work_minutes*60000.0;break;
 case FOCUS_BREAK_READY:s->phase=FOCUS_REST;s->remaining_ms=s->rest_minutes*60000.0;break;
 case FOCUS_PAUSED_WORK:s->phase=FOCUS_WORK;break;
 case FOCUS_PAUSED_REST:s->phase=FOCUS_REST;break;
 default:companion_pause(s);break;
 }
}
void companion_cancel(CompanionState *s){s->phase=FOCUS_IDLE;s->remaining_ms=0;}
double companion_focus_elapsed(const CompanionState *s,double elapsed,bool allowed){
 return s->phase==FOCUS_WORK&&allowed&&isfinite(elapsed)&&elapsed>0?fmin(elapsed,s->remaining_ms):0;
}
int companion_advance(CompanionState *s,double elapsed){return companion_advance_focus(s,elapsed,true);}
int companion_advance_focus(CompanionState *s,double elapsed,bool allowed){
 if(!isfinite(elapsed)||elapsed<=0)return COMPANION_NONE;
 s->care_ms+=elapsed;
 if(s->care_ms>=1800000){int steps=(int)fmin(100,floor(s->care_ms/1800000));s->care_ms=fmod(s->care_ms,1800000);s->mood=bounded(s->mood-steps,0,100);s->energy=bounded(s->energy-steps,0,100);}
 if(s->phase!=FOCUS_WORK&&s->phase!=FOCUS_REST)return COMPANION_NONE;
 if(s->phase==FOCUS_WORK){
  double focused=companion_focus_elapsed(s,elapsed,allowed);
  s->focus_reward_ms+=focused;
  int points=(int)floor(s->focus_reward_ms/600000);
  s->focus_reward_ms=fmod(s->focus_reward_ms,600000);
  s->affinity=bounded(s->affinity+points,0,100);
  elapsed=focused;
 }
 s->remaining_ms=fmax(0,s->remaining_ms-elapsed);
 if(s->remaining_ms>0)return COMPANION_NONE;
 if(s->phase==FOCUS_WORK){s->phase=FOCUS_BREAK_READY;s->completed=bounded(s->completed+1,0,1000000);s->mood=bounded(s->mood+5,0,100);s->energy=bounded(s->energy-3,0,100);return COMPANION_WORK_DONE;}
 s->phase=FOCUS_IDLE;s->energy=bounded(s->energy+10,0,100);return COMPANION_REST_DONE;
}
bool companion_care(CompanionState *s,bool feed,double now,double *last_action){
 if(*last_action>=0&&now-*last_action<10000)return false;
 *last_action=now;
 if(feed)s->energy=bounded(s->energy+12,0,100);else s->mood=bounded(s->mood+10,0,100);
 s->affinity=bounded(s->affinity+1,0,100);return true;
}
static bool valid(double n){return isfinite(n)&&n>=0&&n<=100;}
bool resource_update(ResourceRule *r,double cpu,double memory,double gpu,double now){
 bool known=valid(cpu)||valid(memory)||valid(gpu);
 bool high=(valid(cpu)&&cpu>=85)||(valid(memory)&&memory>=90)||(valid(gpu)&&gpu>=90);
 bool low=known&&(!valid(cpu)||cpu<65)&&(!valid(memory)||memory<80)&&(!valid(gpu)||gpu<70);
 if(r->last_sample>0&&now-r->last_sample>15000)r->tracking=false;
 r->last_sample=now;
 if(!known){r->tracking=false;return false;}
 if(low)r->busy=false;
 if(!high){r->tracking=false;return false;}
 if(!r->tracking){r->tracking=true;r->high_since=now;}
 if(now-r->high_since<30000)return false;
 r->busy=true;
 if(r->last_notice>0&&now-r->last_notice<300000)return false;
 r->last_notice=now;return true;
}
