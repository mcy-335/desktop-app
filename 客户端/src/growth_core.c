#include "growth_core.h"
ScenePrefs scene_choose(SceneState *s,int scene,ScenePrefs current){
 if(scene<0||scene>3)return current;
 if(scene==0){ScenePrefs result=s->base_valid?s->base:current;s->scene=0;s->base_valid=false;return result;}
 if(s->scene==0||!s->base_valid){s->base=current;s->base_valid=true;}
 s->scene=scene;
 if(scene==1)return (ScenePrefs){true,false,false,false,true};
 if(scene==2)return (ScenePrefs){true,true,true,true,false};
 return (ScenePrefs){false,true,false,true,false};
}
void scene_custom(SceneState *s){s->scene=0;s->base_valid=false;}
unsigned growth_target(int i,const GrowthMetrics *m){static const unsigned goals[]={1,10,100,0,30,1,7,60};return i<0||i>=ACHIEVEMENT_COUNT?1:i==3?(m->goal_minutes?m->goal_minutes:120):goals[i];}
unsigned growth_value(int i,const GrowthMetrics *m){switch(i){case 0:case 1:case 2:return m->completed;case 3:case 7:return (unsigned)(m->today_ms/60000);case 4:return m->affinity;case 5:return m->done_tasks;case 6:return m->active_days;default:return 0;}}
unsigned growth_evaluate(GrowthState *s,const GrowthMetrics *m){unsigned before=s->unlocked;for(int i=0;i<ACHIEVEMENT_COUNT;i++)if(growth_value(i,m)>=growth_target(i,m))s->unlocked|=1u<<i;return s->unlocked&~before;}
uint64_t growth_xp(const GrowthMetrics *m){return (uint64_t)m->completed*20+(uint64_t)m->affinity*2;}
bool growth_title(GrowthState *s,int title){if(title<0||title>ACHIEVEMENT_COUNT||(title&&!(s->unlocked&(1u<<(title-1)))))return false;s->title=title;return true;}
