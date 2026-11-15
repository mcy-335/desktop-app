#include "growth_core.h"
#include <stdio.h>
bool growth_selftest(FILE *log){
 int failures=0;
 #define CHECK(condition,label) do{bool pass=(condition);fprintf(log,"%s: %s\n",pass?"PASS":"FAIL",label);if(!pass)failures++;}while(0)
 SceneState s={0};ScenePrefs initial={false,false,false,true,true};ScenePrefs p=scene_choose(&s,1,initial);
 CHECK(p.top&&!p.pass&&!p.game&&!p.quiet&&p.resources,"study preset applies all preferences");
 p=scene_choose(&s,2,p);CHECK(p.game&&p.pass&&p.top&&p.quiet&&!p.resources,"game preset reduces reminders and enables click-through");
 p=scene_choose(&s,3,p);CHECK(!p.top&&!p.game&&p.pass&&p.quiet&&!p.resources,"quiet preset disables reminders and resource sampling");
 p=scene_choose(&s,0,p);CHECK(!p.top&&!p.pass&&!p.game&&p.quiet&&p.resources&&!s.base_valid,"custom restores original preferences across multiple presets");
 scene_choose(&s,2,p);scene_custom(&s);CHECK(s.scene==0&&!s.base_valid,"manual preference changes leave preset mode");
 GrowthState g={0};GrowthMetrics m={0};m.goal_minutes=120;
 CHECK(!growth_evaluate(&g,&m)&&growth_xp(&m)==0,"new user has no fabricated achievements");
 m.completed=1;CHECK(growth_evaluate(&g,&m)==1&&g.unlocked==1,"first focus achievement unlocks at threshold");
 CHECK(!growth_evaluate(&g,&m),"unchanged progress does not repeatedly unlock achievements");
 CHECK(!growth_title(&g,2)&&growth_title(&g,1)&&g.title==1,"only unlocked titles can be equipped");
 m.completed=100;m.affinity=30;m.done_tasks=1;m.active_days=7;m.today_ms=120*60000ULL;
 CHECK(growth_evaluate(&g,&m)&&g.unlocked==255,"all achievement threshold types unlock correctly");
 CHECK(growth_xp(&m)==2060,"experience is deterministic from completed rounds and affinity");
 m=(GrowthMetrics){0};m.goal_minutes=120;growth_evaluate(&g,&m);CHECK(g.unlocked==255&&g.title==1,"achievements remain unlocked after daily reset or task deletion");
 CHECK(!growth_title(&g,9)&&!growth_title(&g,-1)&&growth_title(&g,0),"invalid title IDs are rejected and default remains available");
 fprintf(log,"GROWTH OVERALL: %s (%d failures)\n",failures?"FAIL":"PASS",failures);
 #undef CHECK
 return failures==0;
}
