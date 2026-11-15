#ifndef GROWTH_CORE_H
#define GROWTH_CORE_H
#include <stdbool.h>
#include <stdint.h>
#define ACHIEVEMENT_COUNT 8
typedef struct {bool top,pass,game,quiet,resources;} ScenePrefs;
typedef struct {int scene;bool base_valid;ScenePrefs base;} SceneState;
typedef struct {unsigned unlocked;int title;} GrowthState;
typedef struct {unsigned completed,affinity,done_tasks,active_days;uint64_t today_ms;unsigned goal_minutes;} GrowthMetrics;
ScenePrefs scene_choose(SceneState *state,int scene,ScenePrefs current);
void scene_custom(SceneState *state);
unsigned growth_evaluate(GrowthState *state,const GrowthMetrics *metrics);
unsigned growth_value(int index,const GrowthMetrics *metrics);
unsigned growth_target(int index,const GrowthMetrics *metrics);
uint64_t growth_xp(const GrowthMetrics *metrics);
bool growth_title(GrowthState *state,int title);
#endif
