#ifndef COMPANION_CORE_H
#define COMPANION_CORE_H
#include <stdbool.h>
#include <stdint.h>
typedef enum {FOCUS_IDLE,FOCUS_WORK,FOCUS_PAUSED_WORK,FOCUS_BREAK_READY,FOCUS_REST,FOCUS_PAUSED_REST} FocusPhase;
typedef struct {
 FocusPhase phase;
 int work_minutes,rest_minutes,mood,energy,affinity,completed;
 double remaining_ms,care_ms,focus_reward_ms;
} CompanionState;
typedef struct {bool busy,tracking;double high_since,last_sample,last_notice;} ResourceRule;
enum {COMPANION_NONE,COMPANION_WORK_DONE,COMPANION_REST_DONE};
void companion_defaults(CompanionState *s);
void companion_validate(CompanionState *s);
void companion_toggle(CompanionState *s);
void companion_pause(CompanionState *s);
void companion_cancel(CompanionState *s);
int companion_advance(CompanionState *s,double elapsed);
double companion_focus_elapsed(const CompanionState *s,double elapsed,bool allowed);
int companion_advance_focus(CompanionState *s,double elapsed,bool allowed);
bool companion_care(CompanionState *s,bool feed,double now,double *last_action);
bool resource_update(ResourceRule *r,double cpu,double memory,double gpu,double now);
#endif
