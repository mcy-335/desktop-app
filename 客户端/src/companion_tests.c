#include "companion_core.h"
#include <stdio.h>
#include <math.h>
bool companion_selftest(FILE *log){
 int failures=0;
 #define CHECK(value,label) do{bool pass=(value);fprintf(log,"%s: %s\n",pass?"PASS":"FAIL",label);if(!pass)failures++;}while(0)
 CompanionState s;companion_defaults(&s);s.work_minutes=1;s.rest_minutes=1;companion_toggle(&s);
 companion_advance(&s,20000);companion_pause(&s);double paused=s.remaining_ms;companion_advance(&s,90000);
 CHECK(s.phase==FOCUS_PAUSED_WORK&&s.remaining_ms==paused,"paused focus does not consume time");
 companion_toggle(&s);int event=companion_advance(&s,paused);
 CHECK(event==COMPANION_WORK_DONE&&s.phase==FOCUS_BREAK_READY&&s.completed==1&&s.affinity==0&&s.focus_reward_ms==60000,"short round keeps affinity remainder without a flat completion bonus");
 CHECK(companion_advance(&s,100000)==COMPANION_NONE&&s.completed==1,"repeated ticks cannot duplicate rewards");
 companion_validate(&s);CHECK(s.phase==FOCUS_BREAK_READY,"break-ready survives save validation");
 companion_toggle(&s);CHECK(s.phase==FOCUS_REST,"break starts only on explicit action");
 CHECK(companion_advance(&s,60000)==COMPANION_REST_DONE&&s.phase==FOCUS_IDLE,"break completion returns to idle");
 companion_toggle(&s);companion_cancel(&s);CHECK(s.phase==FOCUS_IDLE&&s.remaining_ms==0&&s.completed==1,"cancel gives no completion reward");
 double last=-1;s.energy=99;s.affinity=99;
 CHECK(companion_care(&s,true,1000,&last)&&s.energy==100&&s.affinity==100,"care clamps values at upper bound");
 CHECK(!companion_care(&s,true,2000,&last)&&companion_care(&s,true,11000,&last),"care cooldown blocks spam and expires");
 s.mood=s.energy=0;companion_advance(&s,3600000);CHECK(s.mood==0&&s.energy==0,"care decay cannot become negative");
 s.phase=FOCUS_WORK;s.remaining_ms=NAN;s.work_minutes=-10;s.rest_minutes=999;s.completed=-1;companion_validate(&s);
 CHECK(s.phase==FOCUS_IDLE&&s.work_minutes==1&&s.rest_minutes==60&&s.completed==0,"invalid stored values recover safely");
 s.remaining_ms=60000;s.phase=FOCUS_WORK;companion_validate(&s);companion_pause(&s);CHECK(s.phase==FOCUS_PAUSED_WORK,"restored work resumes paused");
 companion_defaults(&s);companion_toggle(&s);
 double credited=companion_focus_elapsed(&s,360000,true);
 companion_advance_focus(&s,360000,true);
 CHECK(credited==360000&&s.remaining_ms==1140000&&s.focus_reward_ms==360000&&s.affinity==0,"six minutes of reading counts without keyboard or mouse input");
 credited=companion_focus_elapsed(&s,300000,false);
 companion_advance_focus(&s,300000,false);
 CHECK(credited==0&&s.remaining_ms==1140000&&s.focus_reward_ms==360000,"disallowed application freezes timer and both reward inputs");
 companion_pause(&s);companion_advance_focus(&s,300000,true);
 CHECK(s.remaining_ms==1140000&&s.focus_reward_ms==360000,"manual pause and session suspension preserve earned time without crediting the gap");
 companion_toggle(&s);companion_advance_focus(&s,240000,true);
 CHECK(s.affinity==1&&s.focus_reward_ms==0&&s.remaining_ms==900000,"returning to allowed focus earns one affinity per ten effective minutes");
 companion_cancel(&s);
 CHECK(s.affinity==1&&s.completed==0,"ending a partial round keeps earned affinity without incrementing completed rounds");
 companion_toggle(&s);companion_advance_focus(&s,420000,true);companion_cancel(&s);
 companion_toggle(&s);companion_advance_focus(&s,180000,true);
 CHECK(s.affinity==2&&s.focus_reward_ms==0&&s.completed==0,"seven-minute and three-minute partial rounds combine into one affinity point");
 credited=companion_focus_elapsed(&s,9999999,true);companion_advance_focus(&s,9999999,true);
 CHECK(credited==1320000&&s.affinity==4&&s.focus_reward_ms==120000&&s.completed==1,"completion clamps oversized ticks to remaining effective time with no extra bonus");
 companion_advance(&s,600000);
 CHECK(s.affinity==4&&s.focus_reward_ms==120000&&s.completed==1,"idle after completion cannot duplicate affinity or remainder");
 s.focus_reward_ms=NAN;companion_validate(&s);
 CHECK(s.focus_reward_ms==0,"invalid affinity remainder recovers safely");
 ResourceRule r={0};bool notice=false;
 for(int t=1000;t<=26000;t+=5000)notice|=resource_update(&r,95,30,-1,t);
 CHECK(!notice&&!r.busy,"short resource spike is ignored");
 CHECK(resource_update(&r,95,30,-1,31000)&&r.busy,"sustained CPU load generates one notice");
 CHECK(!resource_update(&r,95,30,-1,36000),"resource notices obey cooldown");
 resource_update(&r,75,50,-1,41000);CHECK(r.busy,"hysteresis holds busy state at intermediate load");
 resource_update(&r,20,40,-1,46000);CHECK(!r.busy,"lower threshold restores normal state");
 r=(ResourceRule){0};for(int t=1000;t<=41000;t+=5000)notice=resource_update(&r,-1,-1,NAN,t);
 CHECK(!notice&&!r.busy&&!r.tracking,"unknown GPU and invalid data never become zero or high load");
 resource_update(&r,95,20,10,50000);resource_update(&r,95,20,10,90000);CHECK(!r.busy,"missing samples reset sustained-load timer");
 r=(ResourceRule){0};for(int t=1000;t<=31000;t+=5000)notice=resource_update(&r,10,30,95,t);
 CHECK(notice&&r.busy,"valid sustained GPU load also triggers feedback");
 fprintf(log,"COMPANION OVERALL: %s (%d failures)\n",failures?"FAIL":"PASS",failures);
 #undef CHECK
 return failures==0;
}
