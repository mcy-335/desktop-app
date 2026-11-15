#include "music.h"
#include "ui_theme.h"
#include "music_core.h"
#include <commctrl.h>
#include <commdlg.h>
#include <cderr.h>
#include <mmsystem.h>
#include <digitalv.h>
#include <stdarg.h>
#define ID_IMPORT 901
#define ID_REMOVE 902
#define ID_PLAY 903
#define ID_STOP 904
#define ID_PREV 905
#define ID_NEXT 906
#define ID_MODE 907
#define ID_SEEK 908
#define ID_VOLUME 909
#define ID_CANCEL_IMPORT 910
#define MUSIC_LIMIT_BYTES (100ULL*1024*1024)
#define CONFIG_CAP (MUSIC_MAX*(MUSIC_PATH*4+100)+256)
typedef struct {
 MusicLibrary result;
 wchar_t paths[MUSIC_MAX][MUSIC_PATH];
 wchar_t root[MUSIC_PATH];
 int requested,added,duplicates,failed,limited,compatible;
 volatile LONG cancel;
} ImportJob;
static MusicLibrary library;
static HWND owner,window,song_list,now_label,time_label,status_label,seek_bar,volume_bar,volume_label,mode_combo,play_button,import_button,remove_button,cancel_button;
static HFONT font;
static int dpi=96;
static wchar_t root[MUSIC_PATH],config[MUSIC_PATH],status[512];
static bool writable,playing,paused,seeking,refreshing;
static uint32_t current_id;
static DWORD duration;
static MCIDEVICEID device;
static HANDLE worker;
static ImportJob *job;
static int px(int n){return MulDiv(n,dpi,96);}
static void message(const wchar_t *format,...){va_list args;va_start(args,format);vswprintf(status,512,format,args);va_end(args);if(status_label)SetWindowTextW(status_label,status);}
static void managed_path(const wchar_t *directory,const MusicTrack *track,wchar_t *path){swprintf(path,MUSIC_PATH,L"%ls\\song-%08X.%ls",directory,track->id,track->format==1?L"mp3":L"wav");}
static bool append_text(wchar_t *text,int *length,const wchar_t *format,...){va_list args;va_start(args,format);int n=vswprintf(text+*length,CONFIG_CAP-*length,format,args);va_end(args);if(n<0)return false;*length+=n;return true;}
static bool save_library(const MusicLibrary *value){
 if(!writable||!config[0])return false;
 wchar_t *text=calloc(CONFIG_CAP,sizeof(wchar_t));if(!text)return false;int n=0;
 bool ok=append_text(text,&n,L"\xfeff[music]\r\nversion=1\r\ncount=%d\r\nvolume=%d\r\nmode=%d\r\nnext_id=%u\r\n",value->count,value->volume,value->mode,value->next_id);
 for(int i=0;i<value->count&&ok;i++){
  const MusicTrack *track=&value->tracks[i];ok=append_text(text,&n,L"[track%d]\r\nid=%u\r\nformat=%d\r\nsource_hex=",i,track->id,track->format);
  for(const wchar_t *p=track->source;*p&&ok;p++)ok=append_text(text,&n,L"%04x",(unsigned)*p);
  if(ok)ok=append_text(text,&n,L"\r\n");
 }
 wchar_t temp[MUSIC_PATH];swprintf(temp,MUSIC_PATH,L"%ls.tmp",config);
 HANDLE file=ok?CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL):INVALID_HANDLE_VALUE;
 if(file!=INVALID_HANDLE_VALUE){DWORD written=0,bytes=n*sizeof(wchar_t);ok=WriteFile(file,text,bytes,&written,NULL)&&written==bytes&&FlushFileBuffers(file);CloseHandle(file);}else ok=false;
 if(ok)ok=MoveFileExW(temp,config,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
 if(!ok)DeleteFileW(temp);free(text);return ok;
}
static unsigned number(const wchar_t *section,const wchar_t *key,unsigned fallback,unsigned maximum){
 wchar_t text[32],*end;GetPrivateProfileStringW(section,key,L"",text,32,config);if(!text[0])return fallback;
 for(const wchar_t *p=text;*p;p++)if(*p<L'0'||*p>L'9')return fallback;
 unsigned long long n=wcstoull(text,&end,10);return *end||n>maximum?fallback:(unsigned)n;
}
static bool decode_source(const wchar_t *hex,wchar_t *text){size_t n=wcslen(hex);if(!n||n%4||n/4>=MUSIC_PATH)return false;for(size_t i=0;i<n/4;i++){unsigned v=0;for(int j=0;j<4;j++){wchar_t c=hex[i*4+j];int h=c>=L'0'&&c<=L'9'?c-L'0':c>=L'a'&&c<=L'f'?c-L'a'+10:-1;if(h<0)return false;v=v*16+h;}if(v<32)return false;text[i]=(wchar_t)v;}text[n/4]=0;return music_local_path(text);}
static void load_library(void){
 music_defaults(&library);writable=true;
 if(GetFileAttributesW(config)==INVALID_FILE_ATTRIBUTES)return;
 if(number(L"music",L"version",0,0x7fffffff)!=1){writable=false;message(L"音乐库版本无法识别，已停止修改该音乐库。请保留原文件。");return;}
 library.volume=(int)number(L"music",L"volume",30,100);library.mode=(int)number(L"music",L"mode",0,2);
 unsigned count=number(L"music",L"count",0,MUSIC_MAX),next=number(L"music",L"next_id",1,0x7fffffff);
 int invalid=0;
 for(unsigned i=0;i<count;i++){
  wchar_t section[24],hex[MUSIC_PATH*4],source[MUSIC_PATH];swprintf(section,24,L"track%u",i);GetPrivateProfileStringW(section,L"source_hex",L"",hex,MUSIC_PATH*4,config);
  if(!decode_source(hex,source)||!music_append(&library,number(section,L"id",0,0x7ffffffe),(int)number(section,L"format",0,2),source))invalid++;
 }
 if(next>library.next_id)library.next_id=next;
 if(invalid)message(L"已跳过 %d 条损坏的音乐记录，其他歌曲仍可使用。",invalid);
}
static MCIERROR media_open(const wchar_t *path,MCIDEVICEID *id,DWORD *length){
 MCI_DGV_OPEN_PARMSW open={0};open.lpstrDeviceType=L"mpegvideo";open.lpstrElementName=(LPWSTR)path;
 MCIERROR error=mciSendCommandW(0,MCI_OPEN,MCI_OPEN_TYPE|MCI_OPEN_ELEMENT|MCI_WAIT,(DWORD_PTR)&open);if(error)return error;
 MCI_DGV_SET_PARMS set={0};set.dwTimeFormat=MCI_FORMAT_MILLISECONDS;
 error=mciSendCommandW(open.wDeviceID,MCI_SET,MCI_SET_TIME_FORMAT|MCI_WAIT,(DWORD_PTR)&set);
 MCI_DGV_STATUS_PARMSW value={0};value.dwItem=MCI_STATUS_LENGTH;
 if(!error)error=mciSendCommandW(open.wDeviceID,MCI_STATUS,MCI_STATUS_ITEM|MCI_WAIT,(DWORD_PTR)&value);
 if(!error&&!value.dwReturn)error=MCIERR_INVALID_FILE;
 if(error){mciSendCommandW(open.wDeviceID,MCI_CLOSE,MCI_WAIT,0);return error;}
 *id=open.wDeviceID;*length=(DWORD)value.dwReturn;return 0;
}
static MCIERROR media_volume(int value){MCI_DGV_SETAUDIO_PARMSW audio={0};audio.dwItem=MCI_DGV_SETAUDIO_VOLUME;audio.dwValue=value*10;return mciSendCommandW(device,MCI_SETAUDIO,MCI_DGV_SETAUDIO_ITEM|MCI_DGV_SETAUDIO_VALUE|MCI_WAIT,(DWORD_PTR)&audio);}
static DWORD media_status(DWORD item,bool *ok){MCI_DGV_STATUS_PARMSW value={0};value.dwItem=item;MCIERROR error=mciSendCommandW(device,MCI_STATUS,MCI_STATUS_ITEM|MCI_WAIT,(DWORD_PTR)&value);if(ok)*ok=!error;return error?0:(DWORD)value.dwReturn;}
static void media_error(MCIERROR error){wchar_t description[256]=L"系统无法读取此音频。";mciGetErrorStringW(error,description,256);message(L"无法播放：%ls 请检查音频是否完整、是否为受支持的 MP3 / WAV。",description);}
static void stop_playback(void){if(device)mciSendCommandW(device,MCI_CLOSE,MCI_WAIT,0);device=0;current_id=0;duration=0;playing=paused=false;}
static void refresh(bool rebuild);
static bool start_track(int index){
 if(index<0||index>=library.count){message(L"请先导入并选择一首歌曲。");return false;}
 stop_playback();wchar_t path[MUSIC_PATH];managed_path(root,&library.tracks[index],path);
 MCIERROR error=media_open(path,&device,&duration);
 if(!error)error=media_volume(library.volume);
 MCI_PLAY_PARMS play={0};if(!error)error=mciSendCommandW(device,MCI_PLAY,0,(DWORD_PTR)&play);
 if(error){stop_playback();media_error(error);refresh(false);return false;}
 current_id=library.tracks[index].id;playing=true;paused=false;message(L"正在播放。关闭此窗口后仍会继续；点击“停止”或退出桌宠可结束播放。");
 ListView_SetItemState(song_list,index,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);refresh(false);return true;
}
static void toggle_play(void){
 if(!device){int selected=ListView_GetNextItem(song_list,-1,LVNI_SELECTED);start_track(selected>=0?selected:0);return;}
 MCIERROR error;
 if(playing)error=mciSendCommandW(device,MCI_PAUSE,MCI_WAIT,0);
 else{MCI_PLAY_PARMS play={0};error=mciSendCommandW(device,MCI_PLAY,0,(DWORD_PTR)&play);}
 if(error){media_error(error);return;}playing=!playing;paused=!playing;message(playing?L"已继续播放。":L"已暂停。再次点击播放可继续。");refresh(false);
}
static void switch_track(int direction){int index=music_find_id(&library,current_id);if(index<0)index=ListView_GetNextItem(song_list,-1,LVNI_SELECTED);if(index<0){start_track(0);return;}start_track(music_next_index(library.count,index,library.mode,direction,false));}
static void seek_to(DWORD position){
 if(!device)return;bool resume=playing;MCI_SEEK_PARMS seek={0};seek.dwTo=min(position,duration?duration-1:0);
 MCIERROR error=mciSendCommandW(device,MCI_SEEK,MCI_TO|MCI_WAIT,(DWORD_PTR)&seek);
 if(!error&&resume){MCI_PLAY_PARMS play={0};error=mciSendCommandW(device,MCI_PLAY,0,(DWORD_PTR)&play);}
 if(error){stop_playback();media_error(error);}refresh(false);
}
static DWORD CALLBACK copy_progress(LARGE_INTEGER total,LARGE_INTEGER transferred,LARGE_INTEGER stream_size,LARGE_INTEGER stream_transferred,DWORD stream,DWORD reason,HANDLE source,HANDLE destination,LPVOID data){
 (void)total;(void)transferred;(void)stream_size;(void)stream_transferred;(void)stream;(void)reason;(void)source;(void)destination;
 return InterlockedCompareExchange(&((ImportJob*)data)->cancel,0,0)?PROGRESS_CANCEL:PROGRESS_CONTINUE;
}
static bool cancelled(ImportJob *value){return InterlockedCompareExchange(&value->cancel,0,0)!=0;}
static bool probe_audio(const wchar_t *path){
 MCIDEVICEID probe=0;DWORD length=0;
 if(media_open(path,&probe,&length))return false;
 mciSendCommandW(probe,MCI_CLOSE,MCI_WAIT,0);
 return true;
}

// ID3v2 stores the leading tag size as four seven-bit bytes, excluding its header.
static bool skip_id3_header(HANDLE file,LONGLONG file_size){
 BYTE header[10];DWORD read=0;
 if(!ReadFile(file,header,sizeof(header),&read,NULL)||read!=sizeof(header)||memcmp(header,"ID3",3))return false;
 BYTE version=header[3],flags=header[5];
 if(version<2||version>4||header[4]==0xff)return false;
 BYTE allowed_flags=version==2?0xc0:version==3?0xe0:0xf0;
 if(flags&~allowed_flags)return false;
 DWORD body_size=0;
 for(int i=6;i<10;i++){
  if(header[i]&0x80)return false;
  body_size=(body_size<<7)|header[i];
 }
 bool has_footer=version==4&&(flags&0x10);
 LARGE_INTEGER offset={.QuadPart=sizeof(header)+(LONGLONG)body_size};
 if(offset.QuadPart+(has_footer?10:0)>=file_size||!SetFilePointerEx(file,offset,NULL,FILE_BEGIN))return false;
 if(!has_footer)return true;
 BYTE footer[10];
 return ReadFile(file,footer,sizeof(footer),&read,NULL)&&read==sizeof(footer)&&
        !memcmp(footer,"3DI",3)&&!memcmp(footer+3,header+3,7);
}
static bool import_mp3_compatibly(const wchar_t *target,ImportJob *value){
 wchar_t retry[MUSIC_PATH];
 if(swprintf(retry,MUSIC_PATH,L"%ls.compat.mp3",target)<0)return false;
 HANDLE input=CreateFileW(target,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,NULL);
 if(input==INVALID_HANDLE_VALUE)return false;
 LARGE_INTEGER size;
 HANDLE output=INVALID_HANDLE_VALUE;
 if(GetFileSizeEx(input,&size)&&size.QuadPart<=MUSIC_LIMIT_BYTES&&
    skip_id3_header(input,size.QuadPart)&&!cancelled(value))
  output=CreateFileW(retry,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
 bool copied=output!=INVALID_HANDLE_VALUE;
 if(copied){
  BYTE buffer[32768];DWORD read=0,written=0;
  do{
   copied=!cancelled(value)&&ReadFile(input,buffer,sizeof(buffer),&read,NULL);
   if(copied&&read)copied=WriteFile(output,buffer,read,&written,NULL)&&written==read;
  }while(copied&&read);
  CloseHandle(output);
 }
 CloseHandle(input);
 if(output==INVALID_HANDLE_VALUE)return false;
 bool replaced=copied&&!cancelled(value)&&probe_audio(retry)&&!cancelled(value)&&
               MoveFileExW(retry,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
 if(!replaced)DeleteFileW(retry);
 return replaced;
}
static DWORD WINAPI import_worker(LPVOID data){
 ImportJob *value=data;
 for(int i=0;i<value->requested&&!cancelled(value);i++){
  wchar_t source[MUSIC_PATH],target[MUSIC_PATH];DWORD n=GetFullPathNameW(value->paths[i],MUSIC_PATH,source,NULL);
  if(!n||n>=MUSIC_PATH||!music_local_path(source)||!music_format(source)){value->failed++;continue;}
  wchar_t drive[]={source[0],L':',L'\\',0};UINT kind=GetDriveTypeW(drive);
  if(kind!=DRIVE_FIXED&&kind!=DRIVE_REMOVABLE&&kind!=DRIVE_CDROM&&kind!=DRIVE_RAMDISK){value->failed++;continue;}
  if(music_find_source(&value->result,source)>=0){value->duplicates++;continue;}
  if(value->result.count>=MUSIC_MAX){value->limited++;continue;}
  WIN32_FILE_ATTRIBUTE_DATA info;
  if(!GetFileAttributesExW(source,GetFileExInfoStandard,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||!(((uint64_t)info.nFileSizeHigh<<32)|info.nFileSizeLow)||(((uint64_t)info.nFileSizeHigh<<32)|info.nFileSizeLow)>MUSIC_LIMIT_BYTES){value->failed++;continue;}
  MusicTrack track={0};track.format=music_format(source);
  do{track.id=value->result.next_id++;if(track.id==0||track.id>0x7ffffffe)break;managed_path(value->root,&track,target);}while(GetFileAttributesW(target)!=INVALID_FILE_ATTRIBUTES);
  if(!track.id||track.id>0x7ffffffe){value->failed++;continue;}
  if(!CopyFileExW(source,target,copy_progress,value,NULL,COPY_FILE_FAIL_IF_EXISTS)){if(!cancelled(value))value->failed++;continue;}
  DWORD attributes=GetFileAttributesW(target);
  if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_READONLY))
   SetFileAttributesW(target,attributes&~FILE_ATTRIBUTE_READONLY);
  bool playable=!cancelled(value)&&probe_audio(target);
  bool compatible=!playable&&track.format==1&&!cancelled(value)&&import_mp3_compatibly(target,value);
  if((!playable&&!compatible)||cancelled(value)||!music_append(&value->result,track.id,track.format,source)){
   DeleteFileW(target);if(!cancelled(value))value->failed++;continue;
  }
  value->added++;
  if(compatible)value->compatible++;
 }
 return 0;
}
static void discard_imports(ImportJob *value){for(int i=library.count;i<value->result.count;i++){wchar_t path[MUSIC_PATH];managed_path(root,&value->result.tracks[i],path);DeleteFileW(path);}}
static void collect_import(void){
 if(!worker||WaitForSingleObject(worker,0)!=WAIT_OBJECT_0)return;CloseHandle(worker);worker=NULL;
 // Preferences may change while files are copied; preserve the latest controls.
 job->result.volume=library.volume;job->result.mode=library.mode;
 if(job->added&&!save_library(&job->result)){discard_imports(job);message(L"音乐库保存失败，本批导入未生效。原歌曲文件保留。请检查存档目录空间及权限。");}
 else{
  library=job->result;
  wchar_t detail[100]=L"最多 128 首，每首不超过 100 MB。";
  if(job->compatible)swprintf(detail,ARRAYSIZE(detail),L"其中 %d 首使用去除文件头标签的音乐库副本，原文件保留。",job->compatible);
  message(L"%ls导入 %d 首；重复 %d 首；失败 %d 首；超过容量 %d 首。%ls",
          cancelled(job)?L"已取消后续导入。已完成部分：":L"",job->added,job->duplicates,job->failed,job->limited,detail);
 }
 free(job);job=NULL;refresh(true);
}
static bool begin_import(const wchar_t paths[][MUSIC_PATH],int count,int omitted){
 if(worker||!writable||count<1)return false;
 job=calloc(1,sizeof(*job));if(!job){message(L"内存不足，无法导入歌曲。");return false;}
 job->result=library;wcscpy(job->root,root);job->requested=min(count,MUSIC_MAX);job->limited=omitted;
 for(int i=0;i<job->requested;i++)wcscpy(job->paths[i],paths[i]);
 worker=CreateThread(NULL,0,import_worker,job,0,NULL);
 if(!worker){free(job);job=NULL;message(L"无法启动导入，请稍后重试。");return false;}
 message(L"正在复制并检查歌曲，完成后自动加入列表。可以取消后续导入；原文件保留。");refresh(false);return true;
}
static void choose_songs(void){
 wchar_t *buffer=calloc(65536,sizeof(wchar_t));wchar_t (*paths)[MUSIC_PATH]=calloc(MUSIC_MAX,sizeof(*paths));if(!buffer||!paths){free(buffer);free(paths);return;}
 OPENFILENAMEW dialog={0};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;dialog.lpstrFilter=L"本地音频（MP3、WAV）\0*.mp3;*.wav\0MP3 音乐\0*.mp3\0WAV 音频\0*.wav\0\0";dialog.lpstrFile=buffer;dialog.nMaxFile=65536;dialog.lpstrTitle=L"选择要导入的本地歌曲（可多选）";dialog.Flags=OFN_EXPLORER|OFN_ALLOWMULTISELECT|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT;
 pet_yield_to_panel(window);
 if(GetOpenFileNameW(&dialog)){
  int count=0,omitted=0;const wchar_t *name=buffer+wcslen(buffer)+1;
  if(!*name){if(wcslen(buffer)<MUSIC_PATH){wcscpy(paths[0],buffer);count=1;}else omitted++;}
  else for(;*name;name+=wcslen(name)+1){if(count>=MUSIC_MAX||wcslen(buffer)+wcslen(name)+2>=MUSIC_PATH){omitted++;continue;}swprintf(paths[count++],MUSIC_PATH,L"%ls\\%ls",buffer,name);}
  if(count)begin_import((const wchar_t (*)[MUSIC_PATH])paths,count,omitted);else message(L"所选歌曲路径过长，无法导入。请放到较短的本地路径后重试。");
 }else{DWORD error=CommDlgExtendedError();if(error)message(error==FNERR_BUFFERTOOSMALL?L"选中的文件过多，请分批导入。":L"无法打开文件选择窗口，请重试。");}
 free(buffer);free(paths);
}
static bool picker_seen,picker_above;
static void CALLBACK check_picker_order(HWND panel,UINT message,UINT_PTR id,DWORD time){
 (void)message;(void)time;
 HWND picker=GetLastActivePopup(panel);
 if(!picker||picker==panel||!IsWindowVisible(picker))return;
 wchar_t type[32];
 if(!GetClassNameW(picker,type,ARRAYSIZE(type))||wcscmp(type,L"#32770"))return;
 KillTimer(panel,id);
 picker_seen=true;
 bool above_picker=false,above_panel=false;
 for(HWND w=GetWindow(pet_window,GW_HWNDPREV);w;w=GetWindow(w,GW_HWNDPREV)){
  above_picker|=w==picker;
  above_panel|=w==panel;
 }
 picker_above=above_picker&&above_panel&&!(GetWindowLongPtrW(pet_window,GWL_EXSTYLE)&WS_EX_TOPMOST)&&
              !(GetWindowLongPtrW(picker,GWL_EXSTYLE)&WS_EX_TOPMOST);
 PostMessageW(picker,WM_COMMAND,IDCANCEL,0);
}
bool music_picker_order_selftest(FILE *log){
 if(!window)return false;
 picker_seen=picker_above=false;
 UINT_PTR timer=SetTimer(window,92,40,check_picker_order);
 if(timer){
  choose_songs();
  KillTimer(window,timer);
 }
 fprintf(log,"%s: real Windows music file picker opened above pet and closed via Cancel (opened=%d order=%d).\n",
         picker_seen&&picker_above?"PASS":"FAIL",picker_seen,picker_above);
 return picker_seen&&picker_above;
}
static bool remove_selected(bool confirm){
 int index=ListView_GetNextItem(song_list,-1,LVNI_SELECTED);if(worker||index<0||index>=library.count||!writable)return false;
 if(confirm&&MessageBoxW(window,L"从列表移除这首歌曲，并删除桌宠音乐库中的副本？\n你原来的歌曲文件会保留。",L"移除歌曲",MB_YESNO|MB_ICONQUESTION)!=IDYES)return false;
 MusicTrack track=library.tracks[index];MusicLibrary *next=malloc(sizeof(*next));if(!next)return false;*next=library;music_remove(next,index);
 if(!save_library(next)){free(next);message(L"保存失败，歌曲仍在列表中。");return false;}
 if(current_id==track.id)stop_playback();library=*next;free(next);
 wchar_t path[MUSIC_PATH];managed_path(root,&track,path);bool deleted=DeleteFileW(path)!=0||GetLastError()==ERROR_FILE_NOT_FOUND;
 message(deleted?L"已移除歌曲；原文件保留。":L"已移出列表，但音乐库副本暂时无法删除。原文件保留。");refresh(true);return true;
}
static void refresh(bool rebuild){
 if(!window)return;refreshing=true;
 if(rebuild){int selected=ListView_GetNextItem(song_list,-1,LVNI_SELECTED);uint32_t selected_id=0;if(selected>=0){LVITEMW item={0};item.mask=LVIF_PARAM;item.iItem=selected;ListView_GetItem(song_list,&item);selected_id=(uint32_t)item.lParam;}
  ListView_DeleteAllItems(song_list);
  for(int i=0;i<library.count;i++){wchar_t title[256],path[MUSIC_PATH];music_title(&library.tracks[i],title,256);LVITEMW item={0};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=i;item.pszText=title;item.lParam=library.tracks[i].id;ListView_InsertItem(song_list,&item);managed_path(root,&library.tracks[i],path);ListView_SetItemText(song_list,i,1,GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES?L"文件缺失":library.tracks[i].format==1?L"MP3":L"WAV");if(library.tracks[i].id==(selected_id?selected_id:current_id))ListView_SetItemState(song_list,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}
 }
 wchar_t text[384],title[256];int current=music_find_id(&library,current_id);
 if(current>=0){music_title(&library.tracks[current],title,256);swprintf(text,384,L"%ls：%ls",playing?L"正在播放":L"已暂停",title);}else swprintf(text,384,L"本地音乐库：%d / 128 首（重新启动后不会自动播放）",library.count);SetWindowTextW(now_label,text);
 SetWindowTextW(play_button,playing?L"暂停":paused?L"继续播放":L"播放");
 EnableWindow(import_button,writable&&!worker&&library.count<MUSIC_MAX);EnableWindow(remove_button,writable&&!worker&&library.count>0);EnableWindow(cancel_button,worker!=NULL);
 SendMessageW(mode_combo,CB_SETCURSEL,library.mode,0);EnableWindow(mode_combo,writable);
 SendMessageW(volume_bar,TBM_SETPOS,TRUE,library.volume);swprintf(text,384,L"音量 %d%%",library.volume);SetWindowTextW(volume_label,text);
 EnableWindow(seek_bar,device!=0);if(!device){SendMessageW(seek_bar,TBM_SETPOS,TRUE,0);SetWindowTextW(time_label,L"00:00 / 00:00");}
 SetWindowTextW(status_label,status);refreshing=false;
}
static void poll_playback(void){
 collect_import();if(!device)return;bool ok;DWORD mode=media_status(MCI_STATUS_MODE,&ok);
 if(!ok){stop_playback();message(L"播放设备不可用，已停止。请检查音频输出设备后重新播放。");refresh(false);return;}
 DWORD position=media_status(MCI_STATUS_POSITION,&ok);if(!ok)return;
 if(!seeking)SendMessageW(seek_bar,TBM_SETPOS,TRUE,duration?(LPARAM)((uint64_t)position*1000/duration):0);
 wchar_t text[80];swprintf(text,80,L"%02u:%02u / %02u:%02u",position/60000,(position/1000)%60,duration/60000,(duration/1000)%60);SetWindowTextW(time_label,text);
 if(playing&&mode==MCI_MODE_STOP){
  int index=music_find_id(&library,current_id),next=music_next_index(library.count,index,library.mode,1,true);
  bool ended=duration&&position+400>=duration;stop_playback();
  if(!ended)message(L"音频提前停止，请检查文件或播放设备。");else if(next>=0){start_track(next);return;}else message(L"列表已播放完成。");refresh(false);
 }
}
static HWND child(HWND w,const wchar_t *cls,const wchar_t *text,DWORD style,int x,int y,int width,int height,int id){HWND c=CreateWindowW(cls,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(width),px(height),w,(HMENU)(INT_PTR)id,pet_instance,NULL);SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;}
static LRESULT CALLBACK music_proc(HWND w,UINT message_id,WPARAM wp,LPARAM lp){
 switch(message_id){
 case WM_CREATE:
  now_label=child(w,L"STATIC",L"本地音乐",SS_PATHELLIPSIS,20,16,600,28,0);
  child(w,L"STATIC",L"导入会复制到桌宠音乐库；支持 MP3 / WAV，原文件保留。",0,20,50,600,24,0);
  song_list=child(w,WC_LISTVIEWW,L"歌曲列表",WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,20,82,600,196,920);ListView_SetExtendedListViewStyle(song_list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
  {LVCOLUMNW c={0};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=L"歌曲";c.cx=px(478);ListView_InsertColumn(song_list,0,&c);c.pszText=L"格式 / 状态";c.cx=px(100);ListView_InsertColumn(song_list,1,&c);}
  import_button=child(w,L"BUTTON",L"导入歌曲…",WS_TABSTOP,20,290,190,32,ID_IMPORT);remove_button=child(w,L"BUTTON",L"移除所选歌曲",WS_TABSTOP,224,290,190,32,ID_REMOVE);cancel_button=child(w,L"BUTTON",L"取消导入",WS_TABSTOP,428,290,192,32,ID_CANCEL_IMPORT);
  time_label=child(w,L"STATIC",L"00:00 / 00:00",SS_CENTER,20,332,600,24,0);seek_bar=child(w,TRACKBAR_CLASSW,L"播放进度",WS_TABSTOP|TBS_HORZ|TBS_NOTICKS,20,360,600,28,ID_SEEK);SendMessageW(seek_bar,TBM_SETRANGE,TRUE,MAKELPARAM(0,1000));
  child(w,L"BUTTON",L"上一首",WS_TABSTOP,20,402,136,34,ID_PREV);play_button=child(w,L"BUTTON",L"播放",WS_TABSTOP,174,402,138,34,ID_PLAY);child(w,L"BUTTON",L"停止",WS_TABSTOP,330,402,138,34,ID_STOP);child(w,L"BUTTON",L"下一首",WS_TABSTOP,486,402,134,34,ID_NEXT);
  child(w,L"STATIC",L"播放方式",0,20,454,92,26,0);mode_combo=child(w,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,112,448,170,150,ID_MODE);const wchar_t *modes[]={L"顺序播放",L"单曲循环",L"列表循环"};for(int i=0;i<3;i++)SendMessageW(mode_combo,CB_ADDSTRING,0,(LPARAM)modes[i]);
  volume_label=child(w,L"STATIC",L"音量 30%",0,302,454,104,26,0);volume_bar=child(w,TRACKBAR_CLASSW,L"音乐音量",WS_TABSTOP|TBS_HORZ|TBS_NOTICKS,410,448,210,32,ID_VOLUME);SendMessageW(volume_bar,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));
  status_label=child(w,L"STATIC",L"",0,20,498,600,62,0);SetTimer(w,1,200,NULL);return 0;
 case WM_TIMER:poll_playback();return 0;
 case WM_NOTIFY:{NMHDR *header=(void*)lp;if(header->hwndFrom==song_list&&header->code==NM_DBLCLK){int selected=ListView_GetNextItem(song_list,-1,LVNI_SELECTED);if(selected>=0)start_track(selected);}return 0;}
 case WM_COMMAND:
  if(LOWORD(wp)==ID_MODE&&HIWORD(wp)==CBN_SELCHANGE&&!refreshing){int old=library.mode;library.mode=(int)SendMessageW(mode_combo,CB_GETCURSEL,0,0);if(!save_library(&library)){library.mode=old;message(L"播放方式保存失败，请检查音乐库存档目录。");}refresh(false);return 0;}
  if(HIWORD(wp)!=BN_CLICKED)return 0;
  switch(LOWORD(wp)){
   case ID_IMPORT:choose_songs();break;case ID_REMOVE:remove_selected(!test_mode);break;
   case ID_PLAY:toggle_play();break;case ID_STOP:stop_playback();message(L"已停止播放。");refresh(false);break;
   case ID_PREV:switch_track(-1);break;case ID_NEXT:switch_track(1);break;
   case ID_CANCEL_IMPORT:if(job){InterlockedExchange(&job->cancel,1);message(L"正在取消后续导入，已完成的歌曲会保留。");}break;
  }return 0;
 case WM_HSCROLL:
  if((HWND)lp==volume_bar){int old=library.volume,value=(int)SendMessageW(volume_bar,TBM_GETPOS,0,0);MCIERROR error=device?media_volume(value):0;
   if(error){media_error(error);SendMessageW(volume_bar,TBM_SETPOS,TRUE,old);return 0;}library.volume=value;wchar_t text[48];swprintf(text,48,L"音量 %d%%",value);SetWindowTextW(volume_label,text);
   if(LOWORD(wp)!=TB_THUMBTRACK&&!save_library(&library))message(L"音量已生效，但保存失败。请检查音乐库存档目录。");return 0;
  }
  if((HWND)lp==seek_bar){seeking=LOWORD(wp)==TB_THUMBTRACK;if(!seeking&&device){int value=(int)SendMessageW(seek_bar,TBM_GETPOS,0,0);seek_to((DWORD)((uint64_t)duration*value/1000));}return 0;}break;
 case WM_CLOSE:ShowWindow(w,SW_HIDE);return 0;
 case WM_DESTROY:KillTimer(w,1);window=NULL;return 0;
 }return DefWindowProcW(w,message_id,wp,lp);
}
void music_init(HWND parent,const wchar_t *settings_path){
 owner=parent;root[0]=config[0]=status[0]=0;writable=false;music_defaults(&library);
 HDC dc=GetDC(owner);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(owner,dc);font=ui_theme_font(px(15),FW_NORMAL,false);
 if(settings_path&&wcslen(settings_path)<MUSIC_PATH-96){wcscpy(root,settings_path);wchar_t *slash=wcsrchr(root,L'\\');if(slash){wcscpy(slash+1,L"Music");if(CreateDirectoryW(root,NULL)||GetLastError()==ERROR_ALREADY_EXISTS){swprintf(config,MUSIC_PATH,L"%ls\\library.ini",root);load_library();}}}
 if(!config[0])message(L"无法创建本地音乐库。请检查存档目录权限或路径长度。");else if(!status[0])message(L"先导入歌曲，再选择播放。音乐列表、音量和播放方式会自动保存。");
}
void music_show(void){
 if(!owner)return;
 if(!window){WNDCLASSW cls={0};cls.lpfnWndProc=music_proc;cls.hInstance=pet_instance;cls.lpszClassName=L"LetterPetC.MusicPanel";cls.hCursor=LoadCursor(NULL,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&cls);
  DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;RECT rect={0,0,px(640),px(576)};AdjustWindowRect(&rect,style,FALSE);MONITORINFO info={sizeof(info)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&info);int width=rect.right-rect.left,height=rect.bottom-rect.top;
  window=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,cls.lpszClassName,L"桌宠 · 本地音乐",style,info.rcWork.left+max(0,(info.rcWork.right-info.rcWork.left-width)/2),info.rcWork.top+max(0,(info.rcWork.bottom-info.rcWork.top-height)/2),width,height,NULL,NULL,pet_instance,NULL);
 }
 if(window){ui_theme_apply(window);ui_theme_role(now_label,UI_TITLE);ui_theme_role(time_label,UI_READOUT);ui_theme_role(status_label,UI_SURFACE_TEXT);refresh(true);pet_show_panel(window,SW_SHOWNORMAL);}
}
void music_suspend(void){if(device&&playing){MCIERROR error=mciSendCommandW(device,MCI_PAUSE,MCI_WAIT,0);if(error)stop_playback();else{playing=false;paused=true;}message(L"锁屏或休眠已暂停音乐，请手动继续播放。");refresh(false);}}
void music_shutdown(void){
 if(worker){InterlockedExchange(&job->cancel,1);WaitForSingleObject(worker,INFINITE);collect_import();}
 stop_playback();if(window)DestroyWindow(window);window=NULL;owner=NULL;DeleteObject(font);font=NULL;
 song_list=now_label=time_label=status_label=seek_bar=volume_bar=volume_label=mode_combo=play_button=import_button=remove_button=cancel_button=NULL;
}
bool music_dialog_message(MSG *msg){return window&&IsWindowVisible(window)&&IsDialogMessageW(window,msg);}
int music_preview_test(void){
 wchar_t directory[MUSIC_PATH],settings_path[MUSIC_PATH];swprintf(directory,MUSIC_PATH,L"%ls\\music-preview",app_dir);CreateDirectoryW(directory,NULL);swprintf(settings_path,MUSIC_PATH,L"%ls\\settings.ini",directory);
 HWND host=CreateWindowW(L"STATIC",L"",WS_POPUP,0,0,1,1,NULL,NULL,pet_instance,NULL);if(!host)return 1;
 music_init(host,settings_path);music_show();ULONGLONG deadline=GetTickCount64()+60000;MSG msg;
 while(window&&IsWindowVisible(window)&&GetTickCount64()<deadline){while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){if(!music_dialog_message(&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}MsgWaitForMultipleObjects(0,NULL,FALSE,100,QS_ALLINPUT);}
 music_shutdown();DestroyWindow(host);return 0;
}

// Test files and silent PCM audio are confined to a separate directory beside the test executable.
static bool create_silent_wav(const wchar_t *path){
 FILE *file=_wfopen(path,L"wb");if(!file)return false;
 const uint32_t bytes=16000;unsigned char header[44]={'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0x40,0x1f,0,0,0x80,0x3e,0,0,2,0,16,0,'d','a','t','a'};uint32_t size=bytes+36;memcpy(header+4,&size,4);memcpy(header+40,&bytes,4);bool ok=fwrite(header,1,44,file)==44;short samples[8000]={0};ok=fwrite(samples,1,bytes,file)==bytes&&ok;fclose(file);return ok;
}
static bool wait_import(void){DWORD deadline=GetTickCount()+15000;while(worker&&GetTickCount()<deadline){if(WaitForSingleObject(worker,50)==WAIT_OBJECT_0)collect_import();}return !worker;}
static bool create_large_id3_fixture(const wchar_t *sample,const wchar_t *path){
 FILE *input=_wfopen(sample,L"rb");if(!input)return false;
 BYTE mp3[32768];size_t count=fread(mp3,1,sizeof(mp3),input);fclose(input);
 if(count<10||count==sizeof(mp3)||memcmp(mp3,"ID3",3))return false;
 DWORD tag_size=0;
 for(int i=6;i<10;i++)tag_size=(tag_size<<7)|mp3[i];
 DWORD audio_start=10+tag_size;
 if(audio_start>=count)return false;
 const unsigned large_tag=150241;
 BYTE header[10]={'I','D','3',3,0,0,0,(large_tag>>14)&0x7f,(large_tag>>7)&0x7f,large_tag&0x7f};
 BYTE *padding=calloc(large_tag,1);
 if(!padding)return false;
 FILE *output=_wfopen(path,L"wb");
 bool ok=output&&fwrite(header,1,sizeof(header),output)==sizeof(header)&&
         fwrite(padding,1,large_tag,output)==large_tag&&
         fwrite(mp3+audio_start,1,count-audio_start,output)==count-audio_start;
 if(output&&fclose(output))ok=false;
 free(padding);
 return ok;
}
bool music_ui_selftest(FILE *log){
 wchar_t testdir[MUSIC_PATH],settings_path[MUSIC_PATH],paths[3][MUSIC_PATH],copied[MUSIC_PATH];swprintf(testdir,MUSIC_PATH,L"%ls\\music-test-%lu",app_dir,GetCurrentProcessId());bool created=CreateDirectoryW(testdir,NULL)!=0;if(!created)return false;swprintf(settings_path,MUSIC_PATH,L"%ls\\settings.ini",testdir);swprintf(paths[0],MUSIC_PATH,L"%ls\\中文歌曲 😀.wav",testdir);swprintf(paths[1],MUSIC_PATH,L"%ls\\损坏的音频.mp3",testdir);swprintf(paths[2],MUSIC_PATH,L"%ls\\本地 MP3 测试.mp3",testdir);bool fixture=create_silent_wav(paths[0]);FILE *bad=_wfopen(paths[1],L"wb");if(bad){fputs("invalid audio",bad);fclose(bad);}else fixture=false;
 music_init(pet_window,settings_path);music_show();bool ui=window&&song_list&&seek_bar&&volume_bar&&mode_combo&&play_button;
 bool imported=fixture&&begin_import((const wchar_t (*)[MUSIC_PATH])paths,2,0)&&wait_import()&&library.count==1&&library.tracks[0].format==2;
 fprintf(log,"%s: background import copies Unicode WAV and rejects corrupt audio\n",imported?"PASS":"FAIL");
 bool duplicate=imported&&begin_import((const wchar_t (*)[MUSIC_PATH])paths,1,0)&&wait_import()&&library.count==1;
 fprintf(log,"%s: repeated import keeps a single song\n",duplicate?"PASS":"FAIL");
 bool codec=false,seek=false,ended=false,stored=false,removed=false,cancel=false;
 if(imported){
  managed_path(root,&library.tracks[0],copied);stored=GetFileAttributesW(copied)!=INVALID_FILE_ATTRIBUTES&&GetFileAttributesW(paths[0])!=INVALID_FILE_ATTRIBUTES;
  library.volume=0;save_library(&library);load_library();stored=stored&&library.volume==0&&library.count==1&&!wcscmp(library.tracks[0].source,paths[0])&&!device;
  ListView_SetItemState(song_list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);SendMessageW(window,WM_COMMAND,ID_PLAY,0);codec=playing&&device&&duration>=950&&duration<=1050;if(!codec)fprintf(log,"INFO: media start playing=%d device=%u duration=%lu status=%ls\n",playing,device,duration,status);
  if(codec){SendMessageW(window,WM_COMMAND,ID_PLAY,0);codec=codec&&paused&&!playing;seek_to(400);bool ok;DWORD position=media_status(MCI_STATUS_POSITION,&ok);seek=ok&&position>=350&&position<=500&&paused;SendMessageW(window,WM_COMMAND,ID_PLAY,0);codec=codec&&playing;music_suspend();codec=codec&&paused&&!playing;SendMessageW(window,WM_COMMAND,ID_PLAY,0);SendMessageW(window,WM_CLOSE,0,0);codec=codec&&playing&&!IsWindowVisible(window);library.mode=MUSIC_ORDER;seek_to(duration-10);Sleep(150);poll_playback();ended=!device&&!playing;}
  library.mode=MUSIC_REPEAT_ALL;SendMessageW(mode_combo,CB_SETCURSEL,MUSIC_REPEAT_ALL,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(ID_MODE,CBN_SELCHANGE),0);load_library();stored=stored&&library.mode==MUSIC_REPEAT_ALL&&library.volume==0;
  uint32_t id=library.tracks[0].id;WritePrivateProfileStringW(L"track0",L"id",L"0",config);load_library();stored=stored&&library.count==0;library.next_id=id;music_append(&library,id,2,paths[0]);save_library(&library);refresh(true);ListView_SetItemState(song_list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
  removed=remove_selected(false)&&library.count==0&&GetFileAttributesW(copied)==INVALID_FILE_ATTRIBUTES&&GetFileAttributesW(paths[0])!=INVALID_FILE_ATTRIBUTES;load_library();removed=removed&&library.count==0;
  ImportJob *cancel_job=calloc(1,sizeof(*cancel_job));if(cancel_job){cancel_job->result=library;wcscpy(cancel_job->root,root);wcscpy(cancel_job->paths[0],paths[0]);cancel_job->requested=1;cancel_job->cancel=1;import_worker(cancel_job);cancel=cancel_job->added==0&&cancel_job->result.count==0;free(cancel_job);}
 }
 fprintf(log,"%s: native music controls and silent WAV play/pause/resume/lock/background playback\n",ui&&codec?"PASS":"FAIL");fprintf(log,"%s: paused seeking retains position and ordered completion stops playback\n",seek&&ended?"PASS":"FAIL");fprintf(log,"%s: library and Unicode source persistence, preferences and corrupt entry recovery\n",stored?"PASS":"FAIL");fprintf(log,"%s: removing a song deletes only its managed copy and persists removal\n",removed?"PASS":"FAIL");fprintf(log,"%s: cancelled import never adds an incomplete track\n",cancel?"PASS":"FAIL");
 bool mp3=false,loops=false,atomic=false;
 wchar_t sample[MUSIC_PATH],source_set[2][MUSIC_PATH],moved[MUSIC_PATH];swprintf(sample,MUSIC_PATH,L"%ls\\tests\\fixtures\\silence.mp3",app_dir);wcscpy(source_set[0],paths[2]);wcscpy(source_set[1],paths[0]);swprintf(moved,MUSIC_PATH,L"%ls.moved",paths[2]);
 if(CopyFileW(sample,paths[2],TRUE)&&begin_import((const wchar_t (*)[MUSIC_PATH])source_set,2,0)&&wait_import()&&library.count==2){
  bool independent=MoveFileW(paths[2],moved)!=0;mp3=independent&&start_track(0)&&playing&&duration>=900&&duration<=1400;
  if(mp3){
   uint32_t mp3_id=current_id;SendMessageW(window,WM_COMMAND,ID_NEXT,0);bool next=current_id==library.tracks[1].id;SendMessageW(window,WM_COMMAND,ID_PREV,0);bool previous=current_id==mp3_id;
   library.mode=MUSIC_REPEAT_ONE;seek_to(duration-10);Sleep(180);poll_playback();bool single=playing&&current_id==mp3_id;
   library.mode=MUSIC_REPEAT_ALL;start_track(1);seek_to(duration-10);Sleep(180);poll_playback();loops=next&&previous&&single&&playing&&current_id==mp3_id;
   toggle_play();bool ok;seek_to(400);DWORD position=media_status(MCI_STATUS_POSITION,&ok);mp3=mp3&&paused&&ok&&position>=300&&position<=550;stop_playback();
  }
  if(independent)MoveFileW(moved,paths[2]);
  wchar_t saved_config[MUSIC_PATH];wcscpy(saved_config,config);swprintf(config,MUSIC_PATH,L"%ls\\does-not-exist\\library.ini",testdir);atomic=!save_library(&library);wcscpy(config,saved_config);load_library();atomic=atomic&&library.count==2;
  WritePrivateProfileStringW(L"music",L"version",L"99",config);load_library();atomic=atomic&&!writable&&!save_library(&library);WritePrivateProfileStringW(L"music",L"version",L"1",config);load_library();atomic=atomic&&writable&&library.count==2;
 }
 fprintf(log,"%s: real silent MP3 import/play/paused seek and playback after original source moves\n",mp3?"PASS":"FAIL");fprintf(log,"%s: native previous/next and natural single/list repeat with real audio\n",loops?"PASS":"FAIL");fprintf(log,"%s: failed atomic save preserves library and future schema refuses overwrites\n",atomic?"PASS":"FAIL");
 swprintf(paths[2],MUSIC_PATH,L"%ls\\large-id3.mp3",testdir);
 wcscpy(source_set[0],paths[2]);
 bool large_id3=create_large_id3_fixture(sample,paths[2])&&
                begin_import((const wchar_t (*)[MUSIC_PATH])source_set,1,0)&&wait_import();
 int track=music_find_source(&library,paths[2]);
 large_id3=large_id3&&track>=0&&start_track(track)&&playing&&duration>=900&&duration<=1400;
 fprintf(log,"%s: MP3 with large leading ID3 metadata imports and plays.\n",large_id3?"PASS":"FAIL");
 music_shutdown();return imported&&duplicate&&ui&&codec&&seek&&ended&&stored&&removed&&cancel&&mp3&&loops&&atomic&&large_id3;
}
