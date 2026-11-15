#ifndef PET_DESKTOP_CORE_H
#define PET_DESKTOP_CORE_H
#include <stdbool.h>
typedef struct {int left,top,right,bottom;} DeskRect;
typedef struct {int x,y;} DeskPoint;
typedef struct {unsigned modifiers,key;} DeskHotkey;
#define DESK_KEYS 4
enum {DESK_SHOW,DESK_PASS,DESK_FOCUS,DESK_GAME};
typedef struct {bool moved,hovering,leaving;double hover_since,leave_since,cooldown;} AvoidState;
typedef struct {bool full_hidden,reveal;} VisibilityState;
DeskPoint desk_clamp(DeskRect work,int size,DeskPoint point);
DeskPoint desk_snap(DeskRect work,int size,DeskPoint point,int threshold);
bool desk_near(DeskPoint point,DeskPoint origin,int size,int margin);
DeskPoint desk_escape(DeskRect work,int size,DeskPoint cursor);
bool desk_avoid(AvoidState *state,double now,bool enabled,bool interaction,DeskPoint cursor,DeskPoint home,DeskPoint current,int size);
bool desk_visible(VisibilityState *state,bool intended,bool fullscreen,bool hide_fullscreen);
void desk_reveal(VisibilityState *state,bool show);
bool desk_keys_valid(const DeskHotkey keys[DESK_KEYS]);
bool desk_hotkey_equal(DeskHotkey a,DeskHotkey b);
#endif
