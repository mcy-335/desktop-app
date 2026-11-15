#include "music_core.h"
#include <string.h>
#include <wctype.h>
void music_defaults(MusicLibrary *library){memset(library,0,sizeof(*library));library->volume=30;library->next_id=1;}
int music_format(const wchar_t *path){const wchar_t *ext=path?wcsrchr(path,L'.'):NULL;if(!ext)return 0;return !_wcsicmp(ext,L".mp3")?1:!_wcsicmp(ext,L".wav")?2:0;}
bool music_local_path(const wchar_t *path){
 if(!path||wcslen(path)<4||wcslen(path)>=MUSIC_PATH||!iswalpha(path[0])||path[1]!=L':'||path[2]!=L'\\')return false;
 for(const wchar_t *p=path+3;*p;p++)if(*p<32||*p==L':'||*p==L'"')return false;
 return true;
}
int music_find_source(const MusicLibrary *library,const wchar_t *source){for(int i=0;i<library->count;i++)if(!_wcsicmp(library->tracks[i].source,source))return i;return -1;}
int music_find_id(const MusicLibrary *library,uint32_t id){for(int i=0;i<library->count;i++)if(library->tracks[i].id==id)return i;return -1;}
bool music_append(MusicLibrary *library,uint32_t id,int format,const wchar_t *source){
 if(library->count>=MUSIC_MAX||!id||id>0x7ffffffe||format<1||format>2||!music_local_path(source)||music_format(source)!=format||music_find_id(library,id)>=0||music_find_source(library,source)>=0)return false;
 MusicTrack *track=&library->tracks[library->count++];track->id=id;track->format=format;wcscpy(track->source,source);if(library->next_id<=id)library->next_id=id+1;return true;
}
bool music_remove(MusicLibrary *library,int index){if(index<0||index>=library->count)return false;memmove(&library->tracks[index],&library->tracks[index+1],(library->count-index-1)*sizeof(MusicTrack));library->count--;return true;}
int music_next_index(int count,int current,int mode,int direction,bool ended){
 if(count<=0||current<0||current>=count)return -1;
 if(ended&&mode==MUSIC_REPEAT_ONE)return current;
 int next=current+(direction<0?-1:1);
 if(next>=0&&next<count)return next;
 if(!ended||mode==MUSIC_REPEAT_ALL)return direction<0?count-1:0;
 return -1;
}
void music_title(const MusicTrack *track,wchar_t *text,int capacity){if(capacity<1)return;const wchar_t *name=wcsrchr(track->source,L'\\');name=name?name+1:track->source;size_t n=wcslen(name);const wchar_t *dot=wcsrchr(name,L'.');if(dot)n=(size_t)(dot-name);if(n>=(size_t)capacity)n=capacity-1;wmemcpy(text,name,n);text[n]=0;}
