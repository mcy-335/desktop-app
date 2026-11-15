#include "desktop_core.h"
#include <stdint.h>
static int bounded(int value,int lo,int hi){return value<lo?lo:value>hi?hi:value;}
DeskPoint desk_clamp(DeskRect w,int size,DeskPoint p){int right=w.right-size,bottom=w.bottom-size;return (DeskPoint){bounded(p.x,w.left,right<w.left?w.left:right),bounded(p.y,w.top,bottom<w.top?w.top:bottom)};}
DeskPoint desk_snap(DeskRect w,int size,DeskPoint p,int threshold){
 p=desk_clamp(w,size,p);int right=w.right-size,bottom=w.bottom-size;
 if(p.x-w.left<=threshold)p.x=w.left;else if(right-p.x<=threshold)p.x=right;
 if(p.y-w.top<=threshold)p.y=w.top;else if(bottom-p.y<=threshold)p.y=bottom;
 return desk_clamp(w,size,p);
}
bool desk_near(DeskPoint p,DeskPoint origin,int size,int margin){return (int64_t)p.x>=((int64_t)origin.x-margin)&&(int64_t)p.x<((int64_t)origin.x+size+margin)&&(int64_t)p.y>=((int64_t)origin.y-margin)&&(int64_t)p.y<((int64_t)origin.y+size+margin);}
DeskPoint desk_escape(DeskRect w,int size,DeskPoint cursor){
 DeskPoint candidates[]={{w.left,w.top},{w.right-size,w.top},{w.left,w.bottom-size},{w.right-size,w.bottom-size}};DeskPoint best=desk_clamp(w,size,candidates[0]);int64_t farthest=-1;
 for(int i=0;i<4;i++){DeskPoint p=desk_clamp(w,size,candidates[i]);int64_t x=(int64_t)p.x+size/2-cursor.x,y=(int64_t)p.y+size/2-cursor.y,dist=x*x+y*y;if(dist>farthest){farthest=dist;best=p;}}
 return best;
}
bool desk_avoid(AvoidState *s,double now,bool enabled,bool interaction,DeskPoint cursor,DeskPoint home,DeskPoint current,int size){
 if(!enabled){bool changed=s->moved;s->moved=false;s->hovering=s->leaving=false;return changed;}
 if(interaction){s->hovering=s->leaving=false;return false;}
 if(!s->moved){
  if(now<s->cooldown||!desk_near(cursor,current,size,12)){s->hovering=false;return false;}
  if(!s->hovering){s->hovering=true;s->hover_since=now;}
  if(now-s->hover_since>=900){s->moved=true;s->hovering=false;s->leaving=false;return true;}
 }else{
  if(desk_near(cursor,home,size,32)||desk_near(cursor,current,size,32)){s->leaving=false;return false;}
  if(!s->leaving){s->leaving=true;s->leave_since=now;}
  if(now-s->leave_since>=3000){s->moved=false;s->leaving=false;s->cooldown=now+5000;return true;}
 }
 return false;
}
bool desk_visible(VisibilityState *s,bool intended,bool fullscreen,bool hide_fullscreen){
 if(!fullscreen)s->reveal=false;s->full_hidden=fullscreen&&hide_fullscreen&&!s->reveal;return intended&&!s->full_hidden;
}
void desk_reveal(VisibilityState *s,bool show){s->reveal=show;}
bool desk_hotkey_equal(DeskHotkey a,DeskHotkey b){return a.key==b.key&&a.modifiers==b.modifiers;}
bool desk_keys_valid(const DeskHotkey keys[DESK_KEYS]){
 if(!keys[DESK_SHOW].key)return false;
 for(int i=0;i<DESK_KEYS;i++){
  unsigned key=keys[i].key,mod=keys[i].modifiers;
  if(!key){if(mod)return false;continue;}
  if(!((key>='A'&&key<='Z')||(key>='0'&&key<='9')||(key>=0x70&&key<=0x87)))return false;
  if((mod&~7u)||!(mod&3u))return false;
  for(int j=0;j<i;j++)if(keys[j].key&&desk_hotkey_equal(keys[i],keys[j]))return false;
 }
 return true;
}
