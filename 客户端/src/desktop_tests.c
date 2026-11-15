#include "desktop_core.h"
#include <stdio.h>
bool desktop_selftest(FILE *log){
 int failures=0;
 #define CHECK(condition,label) do{bool pass=(condition);fprintf(log,"%s: %s\n",pass?"PASS":"FAIL",label);if(!pass)failures++;}while(0)
 DeskRect work={-1920,0,0,1040};DeskPoint p=desk_snap(work,480,(DeskPoint){-1910,550},24);
 CHECK(p.x==-1920&&p.y==560,"snap respects negative monitor coordinates and work-area bottom");
 p=desk_snap(work,480,(DeskPoint){-900,250},24);CHECK(p.x==-900&&p.y==250,"positions far from edges do not snap");
 p=desk_clamp((DeskRect){0,0,320,200},480,(DeskPoint){-100,900});CHECK(p.x==0&&p.y==0,"oversized pet stays anchored on small work area");
 p=desk_escape((DeskRect){0,0,1920,1040},480,(DeskPoint){100,100});CHECK(p.x==1440&&p.y==560,"avoidance selects a distant corner on the same monitor");
 AvoidState a={0};DeskPoint home={0,0},near={100,100},away={700,700},moved={1440,560};
 CHECK(!desk_avoid(&a,1000,true,false,near,home,home,480),"hover does not immediately move the pet");
 CHECK(!desk_avoid(&a,1800,true,false,near,home,home,480)&&desk_avoid(&a,1900,true,false,near,home,home,480)&&a.moved,"hover dwell triggers one avoidance move");
 CHECK(!desk_avoid(&a,2000,true,true,away,home,moved,480)&&a.moved,"clicking, dragging or menus suspend automatic movement");
 CHECK(!desk_avoid(&a,2100,true,false,away,home,moved,480)&&desk_avoid(&a,5100,true,false,away,home,moved,480)&&!a.moved,"pet returns after cursor leaves both locations for three seconds");
 CHECK(!desk_avoid(&a,5200,true,false,near,home,home,480),"cooldown prevents immediate repeated avoidance");
 a=(AvoidState){.moved=true};CHECK(desk_avoid(&a,6000,false,false,away,home,moved,480)&&!a.moved,"disabling avoidance restores the home position");
 VisibilityState v={0};CHECK(!desk_visible(&v,true,true,true)&&v.full_hidden,"fullscreen hides an intended-visible pet");
 CHECK(!desk_visible(&v,false,false,true),"leaving fullscreen never overrides manual hiding");
 CHECK(desk_visible(&v,true,true,false),"fullscreen keeps the pet visible when hiding is disabled");
 desk_reveal(&v,true);CHECK(desk_visible(&v,true,true,true),"explicit show can reveal during fullscreen");
 desk_visible(&v,true,false,true);CHECK(!desk_visible(&v,true,true,true),"fullscreen reveal expires after fullscreen ends");
 DeskHotkey keys[]={{7,'H'},{7,'P'},{7,'F'},{7,'G'}};
 CHECK(desk_keys_valid(keys),"default shortcuts are valid and unique");
 keys[1]=keys[0];CHECK(!desk_keys_valid(keys),"duplicate shortcuts are rejected");keys[1]=(DeskHotkey){7,'P'};
 keys[2]=(DeskHotkey){4,'F'};CHECK(!desk_keys_valid(keys),"shift-only shortcuts cannot consume normal typing");
 keys[2]=(DeskHotkey){0,0};CHECK(desk_keys_valid(keys),"optional shortcut actions can be disabled");
 keys[0]=(DeskHotkey){0,0};CHECK(!desk_keys_valid(keys),"show/hide shortcut remains available");
 keys[0]=(DeskHotkey){3,0x87};CHECK(desk_keys_valid(keys),"Ctrl/Alt function keys including F24 are supported");
 fprintf(log,"DESKTOP OVERALL: %s (%d failures)\n",failures?"FAIL":"PASS",failures);
 #undef CHECK
 return failures==0;
}
