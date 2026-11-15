#include "music_core.h"
#include <stdlib.h>
#define CHECK(x,label) do {bool pass=(x);fprintf(log,"%s: %s\n",pass?"PASS":"FAIL",label);if(!pass)failures++;}while(0)
bool music_selftest(FILE *log){
 int failures=0;MusicLibrary *library=calloc(1,sizeof(*library));if(!library)return false;music_defaults(library);
 CHECK(library->volume==30&&library->count==0&&library->next_id==1,"music starts quiet with an empty library and no autoplay state");
 CHECK(music_format(L"D:\\歌曲\\中文.MP3")==1&&music_format(L"D:\\a.wav")==2&&!music_format(L"D:\\a.mp3.exe"),"only MP3 and WAV extensions are accepted");
 CHECK(!music_local_path(L"https://a/song.mp3")&&!music_local_path(L"\\\\server\\song.mp3")&&!music_local_path(L"C:\\x.mp3:stream")&&!music_local_path(L"..\\x.mp3"),"URLs, UNC paths, alternate streams and relative sources are rejected");
 CHECK(music_append(library,8,1,L"D:\\我的歌曲\\一起听 😀.mp3"),"Unicode and emoji song names enter the library");
 CHECK(!music_append(library,9,1,L"d:\\我的歌曲\\一起听 😀.MP3")&&!music_append(library,8,2,L"D:\\other.wav"),"duplicate source paths and duplicate IDs are rejected");
 CHECK(!music_append(library,10,2,L"D:\\wrong.mp3")&&!music_append(library,0,1,L"D:\\zero.mp3"),"invalid format metadata and zero IDs cannot be saved");
 wchar_t title[256];music_title(&library->tracks[0],title,256);CHECK(!wcscmp(title,L"一起听 😀"),"display names preserve Unicode without the file extension");
 CHECK(library->next_id==9&&music_find_id(library,8)==0,"restored IDs advance the next import ID");
 CHECK(music_next_index(3,2,MUSIC_ORDER,1,true)==-1,"ordered playback stops at the end of the list");
 CHECK(music_next_index(3,2,MUSIC_REPEAT_ALL,1,true)==0,"list repeat wraps after the final song");
 CHECK(music_next_index(3,1,MUSIC_REPEAT_ONE,1,true)==1,"single repeat repeats only on natural completion");
 CHECK(music_next_index(3,1,MUSIC_REPEAT_ONE,1,false)==2&&music_next_index(3,0,MUSIC_ORDER,-1,false)==2,"manual next and previous work across playback modes");
 CHECK(music_next_index(0,0,MUSIC_ORDER,1,true)==-1&&music_next_index(3,-1,0,1,false)==-1,"empty and invalid selections never produce a playable index");
 CHECK(music_append(library,9,2,L"D:\\other.wav")&&music_remove(library,0)&&library->count==1&&library->tracks[0].id==9,"removing a song preserves remaining track identities");
 CHECK(!music_remove(library,-1)&&!music_remove(library,2),"invalid removal leaves the library intact");
 for(int i=library->count;i<MUSIC_MAX;i++){wchar_t source[64];swprintf(source,64,L"D:\\song%d.mp3",i);music_append(library,(uint32_t)i+20,1,source);}
 CHECK(library->count==MUSIC_MAX&&!music_append(library,1000,1,L"D:\\overflow.mp3"),"music library is bounded at 128 songs");
 free(library);fprintf(log,"MUSIC CORE OVERALL: %s (%d failures)\n",failures?"FAIL":"PASS",failures);return !failures;
}
