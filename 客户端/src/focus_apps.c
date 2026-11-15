#define COBJMACROS
#include "focus_apps.h"
#include "backup_io.h"
#include "desktop.h"
#include "ui_theme.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>

enum {APP_LIMIT=32, PATH_CAP=32768, CANDIDATE_LIMIT=1024};
enum {FOREGROUND_GRACE_MS=3000, LOOKUP_RETRY_MS=500, MINIMIZE_ATTEMPT_LIMIT=2};
enum {ID_ENABLE=2001, ID_ADD_APP, ID_REMOVE_APP, ID_APPS};
enum {ID_APP_SEARCH=2020, ID_APP_CHOICES, ID_APP_BROWSE, ID_APP_REFRESH};
typedef struct {
 bool enabled;
 unsigned count;
 wchar_t *paths[APP_LIMIT];
} FocusApps;
typedef struct {
 wchar_t *name,*path;
 bool running,shortcut;
} AppChoice;
typedef struct {
 AppChoice rows[CANDIDATE_LIMIT];
 unsigned count;
} AppCatalog;
typedef struct {
 AppCatalog catalog;
 HANDLE thread,stop;
} AppScan;

static FocusApps apps;
static wchar_t config[PATH_CAP],windows_dir[PATH_CAP];
static HWND panel,enable_check,list,add_button,remove_button,status_label;
static HWND chooser,search_box,choices,choose_button,choice_status;
static AppCatalog catalog;
static AppScan *scan;
static HWND last_foreground;
static DWORD last_pid;
typedef enum {FOREGROUND_UNKNOWN,FOREGROUND_EXEMPT,FOREGROUND_ALLOWED,FOREGROUND_BLOCKED} ForegroundRule;
static ForegroundRule last_rule;
static ULONGLONG lookup_at,minimize_at;
static unsigned minimize_attempts;
static bool minimize_queued;
static bool outside_pending;
static ULONGLONG outside_since;
static int dpi=96;

static const wchar_t *file_name(const wchar_t *path){
 const wchar_t *slash=wcsrchr(path,L'\\');
 return slash?slash+1:path;
}
static void normalize_path(wchar_t *path){
 if(!_wcsnicmp(path,L"\\\\?\\UNC\\",8)){
  wmemmove(path+2,path+8,wcslen(path+8)+1);
  path[0]=path[1]=L'\\';
 }else if(!wcsncmp(path,L"\\\\?\\",4)&&path[4]&&path[5]==L':'){
  wmemmove(path,path+4,wcslen(path+4)+1);
 }
}
static bool valid_app_path(const wchar_t *path){
 size_t length=wcslen(path);
 if(length<7||wcspbrk(path,L"\r\n"))return false;
 if(!(path[1]==L':'&&path[2]==L'\\')&&!(path[0]==L'\\'&&path[1]==L'\\'))return false;
 return !_wcsicmp(path+length-4,L".exe");
}
static bool contains_app(const wchar_t *path){
 for(unsigned i=0;i<apps.count;i++){
  if(CompareStringOrdinal(apps.paths[i],-1,path,-1,TRUE)==CSTR_EQUAL)return true;
 }
 return false;
}
void focus_apps_init(const wchar_t *settings_path){
 config[0]=0;
 UINT length=GetWindowsDirectoryW(windows_dir,PATH_CAP);
 if(!length||length>=PATH_CAP)windows_dir[0]=0;
 if(!settings_path||wcslen(settings_path)>=PATH_CAP-64)return;
 wcscpy(config,settings_path);
 wchar_t *slash=wcsrchr(config,L'\\');
 if(!slash){config[0]=0;return;}
 wcscpy(slash+1,L"focus-apps.ini");
 if(GetPrivateProfileIntW(L"focus",L"version",0,config)!=1)return;
 unsigned count=GetPrivateProfileIntW(L"focus",L"count",0,config);
 if(count>APP_LIMIT)return;
 for(unsigned i=0;i<count;i++){
  wchar_t key[20],path[PATH_CAP];
  swprintf(key,20,L"app%u",i);
  DWORD used=GetPrivateProfileStringW(L"focus",key,L"",path,PATH_CAP,config);
  normalize_path(path);
  if(used>=PATH_CAP-1||!valid_app_path(path)||contains_app(path))continue;
  wchar_t *saved=_wcsdup(path);
  if(saved)apps.paths[apps.count++]=saved;
 }
 apps.enabled=apps.count>0&&apps.count==count&&GetPrivateProfileIntW(L"focus",L"enabled",0,config)==1;
}
static bool save_config(const FocusApps *next){
 if(!config[0])return false;
 size_t capacity=100;
 for(unsigned i=0;i<next->count;i++)capacity+=wcslen(next->paths[i])+20;
 wchar_t *text=malloc(capacity*sizeof(*text));
 if(!text)return false;
 int used=swprintf(text,capacity,L"\xfeff[focus]\r\nversion=1\r\nenabled=%d\r\ncount=%u\r\n",next->enabled,next->count);
 for(unsigned i=0;i<next->count&&used>=0;i++){
  int written=swprintf(text+used,capacity-(size_t)used,L"app%u=%ls\r\n",i,next->paths[i]);
  used=written<0?-1:used+written;
 }
 bool ok=used>0&&atomic_write(config,(const BYTE*)text,(DWORD)used*sizeof(*text));
 free(text);
 return ok;
}
static void forget_foreground(void){
 last_foreground=NULL;last_pid=0;last_rule=FOREGROUND_UNKNOWN;
 lookup_at=minimize_at=0;minimize_attempts=0;minimize_queued=false;
}

static bool shell_app(const wchar_t *path){
 size_t length=wcslen(windows_dir);
 if(!length||wcslen(path)<=length||path[length]!=L'\\'||
    CompareStringOrdinal(path,(int)length,windows_dir,(int)length,TRUE)!=CSTR_EQUAL)return false;
 const wchar_t *name=file_name(path);
 const wchar_t *system_apps[]={L"StartMenuExperienceHost.exe",L"ShellExperienceHost.exe",L"SearchHost.exe",
  L"SearchApp.exe",L"SearchUI.exe",L"TextInputHost.exe",L"LockApp.exe",L"Taskmgr.exe"};
 for(size_t i=0;i<sizeof(system_apps)/sizeof(*system_apps);i++){
  if(CompareStringOrdinal(name,-1,system_apps[i],-1,TRUE)==CSTR_EQUAL)return true;
 }
 return false;
}
static BOOL CALLBACK find_app_window(HWND child,LPARAM argument){
 wchar_t type[80]={0};
 GetClassNameW(child,type,80);
 if(IsWindowVisible(child)&&!wcscmp(type,L"Windows.UI.Core.CoreWindow")){
  GetWindowThreadProcessId(child,(DWORD*)argument);
  return FALSE;
 }
 return TRUE;
}
static bool app_image(HWND foreground,DWORD pid,wchar_t *path){
 wchar_t type[80]={0};
 GetClassNameW(foreground,type,80);
 if(!wcscmp(type,L"ApplicationFrameWindow")){
  pid=0;
  EnumChildWindows(foreground,find_app_window,(LPARAM)&pid);
  if(!pid)return false;
 }
 HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
 if(!process)return false;
 DWORD length=PATH_CAP;
 bool found=QueryFullProcessImageNameW(process,0,path,&length)!=0;
 CloseHandle(process);
 if(found)normalize_path(path);
 return found;
}
static void free_catalog(AppCatalog *items){
 for(unsigned i=0;i<items->count;i++){free(items->rows[i].name);free(items->rows[i].path);}
 items->count=0;
}
static void add_choice(AppCatalog *items,wchar_t *path,const wchar_t *name,bool running){
 normalize_path(path);
 if(!valid_app_path(path)||shell_app(path))return;
 for(unsigned i=0;i<items->count;i++){
  AppChoice *row=&items->rows[i];
  if(CompareStringOrdinal(path,-1,row->path,-1,TRUE)!=CSTR_EQUAL)continue;
  if(running)row->running=true;
  else if(!row->shortcut){
   wchar_t *label=_wcsdup(name);
   if(label){free(row->name);row->name=label;row->shortcut=true;}
  }
  return;
 }
 if(items->count==CANDIDATE_LIMIT)return;
 AppChoice row={_wcsdup(name),_wcsdup(path),running,!running};
 if(row.name&&row.path)items->rows[items->count++]=row;
 else{free(row.name);free(row.path);}
}
static bool shortcut_target(const wchar_t *link_path,wchar_t *path){
 IShellLinkW *link=NULL;
 HRESULT hr=CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkW,(void**)&link);
 if(FAILED(hr))return false;
 IPersistFile *file=NULL;
 hr=IShellLinkW_QueryInterface(link,&IID_IPersistFile,(void**)&file);
 if(SUCCEEDED(hr)){
  hr=IPersistFile_Load(file,link_path,STGM_READ);
  IPersistFile_Release(file);
 }
 wchar_t target[PATH_CAP]=L"";
 if(SUCCEEDED(hr))hr=IShellLinkW_GetPath(link,target,PATH_CAP,NULL,SLGP_RAWPATH);
 IShellLinkW_Release(link);
 if(hr!=S_OK||!target[0]||wcslen(target)>=PATH_CAP-1)return false;
 DWORD length=ExpandEnvironmentStringsW(target,path,PATH_CAP);
 if(!length||length>PATH_CAP)return false;
 normalize_path(path);
 return valid_app_path(path);
}
static bool executable_exists(const wchar_t *path){
 DWORD attr=GetFileAttributesW(path);
 return attr!=INVALID_FILE_ATTRIBUTES&&!(attr&FILE_ATTRIBUTE_DIRECTORY);
}
static bool scan_cancelled(const AppScan *job){return job->stop&&WaitForSingleObject(job->stop,0)==WAIT_OBJECT_0;}
static BOOL CALLBACK running_window(HWND window,LPARAM arg){
 AppScan *job=(AppScan*)arg;
 if(scan_cancelled(job)||job->catalog.count==CANDIDATE_LIMIT)return FALSE;
 DWORD pid=0;GetWindowThreadProcessId(window,&pid);
 if(!pid||pid==GetCurrentProcessId())return TRUE;
 LONG_PTR style=GetWindowLongPtrW(window,GWL_EXSTYLE);
 wchar_t title[2];
 if(!IsWindowVisible(window)||!GetWindowTextW(window,title,ARRAYSIZE(title))||GetWindow(window,GW_OWNER)||
    (style&(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE))||desktop_shell_surface(window))return TRUE;
 wchar_t path[PATH_CAP];
 if(app_image(window,pid,path))add_choice(&job->catalog,path,file_name(path),true);
 return TRUE;
}
static void scan_shortcuts(AppScan *job,const wchar_t *directory,unsigned depth){
 if(depth>10||scan_cancelled(job)||job->catalog.count==CANDIDATE_LIMIT)return;
 wchar_t path[MAX_PATH];
 if(swprintf(path,ARRAYSIZE(path),L"%ls\\*",directory)<0)return;
 WIN32_FIND_DATAW found;
 HANDLE files=FindFirstFileW(path,&found);
 if(files==INVALID_HANDLE_VALUE)return;
 do{
  if(scan_cancelled(job)||job->catalog.count==CANDIDATE_LIMIT)break;
  if(found.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)continue;
  wchar_t *name=found.cFileName;
  if(!wcscmp(name,L".")||!wcscmp(name,L"..")||swprintf(path,ARRAYSIZE(path),L"%ls\\%ls",directory,name)<0)continue;
  if(found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){
   if(_wcsicmp(name,L"Startup"))scan_shortcuts(job,path,depth+1);
  }else{
   wchar_t *suffix=wcsrchr(name,L'.');
   if(!suffix||_wcsicmp(suffix,L".lnk"))continue;
   wchar_t target[PATH_CAP];
   if(shortcut_target(path,target)&&target[0]!=L'\\'&&executable_exists(target)){
    *suffix=0;
    add_choice(&job->catalog,target,name,false);
   }
  }
 }while(FindNextFileW(files,&found));
 FindClose(files);
}
static int compare_choices(const void *a,const void *b){
 const AppChoice *left=a,*right=b;
 if(left->running!=right->running)return left->running?-1:1;
 int order=CompareStringEx(LOCALE_NAME_USER_DEFAULT,NORM_IGNORECASE,left->name,-1,right->name,-1,NULL,NULL,0);
 if(order!=CSTR_EQUAL&&order)return order-CSTR_EQUAL;
 return _wcsicmp(left->path,right->path);
}
static DWORD WINAPI collect_choices(void *argument){
 AppScan *job=argument;
 EnumWindows(running_window,(LPARAM)job);
 HRESULT initialized=CoInitializeEx(NULL,COINIT_MULTITHREADED);
 if(SUCCEEDED(initialized)){
  const int folders[]={CSIDL_PROGRAMS,CSIDL_COMMON_PROGRAMS};
  for(int i=0;i<ARRAYSIZE(folders)&&!scan_cancelled(job);i++){
   wchar_t directory[MAX_PATH];
   if(SUCCEEDED(SHGetFolderPathW(NULL,folders[i],NULL,SHGFP_TYPE_CURRENT,directory))&&directory[0]!=L'\\')
    scan_shortcuts(job,directory,0);
  }
  CoUninitialize();
 }
 qsort(job->catalog.rows,job->catalog.count,sizeof(*job->catalog.rows),compare_choices);
 return 0;
}
static bool interaction_surface(HWND window){
 if(!window)return false;
 wchar_t type[80];
 if(!GetClassNameW(window,type,ARRAYSIZE(type)))return false;
 const wchar_t *classes[]={L"#32768",L"Shell_TrayWnd",L"Shell_SecondaryTrayWnd",
  L"TrayNotifyWnd",L"NotifyIconOverflowWindow",
  L"TopLevelWindowForOverflowXamlIsland",L"TaskListThumbnailWnd",L"MultitaskingViewFrame"};
 for(int i=0;i<ARRAYSIZE(classes);i++)if(!wcscmp(type,classes[i]))return true;
 if(wcscmp(type,L"XamlExplorerHostIslandWindow"))return false;
 DWORD pid=0,shell_pid=0;
 GetWindowThreadProcessId(window,&pid);
 GetWindowThreadProcessId(GetShellWindow(),&shell_pid);
 return pid&&pid==shell_pid;
}
static ForegroundRule foreground_rule(HWND foreground,DWORD pid){
 if(pid==GetCurrentProcessId())return FOREGROUND_ALLOWED;
 LONG_PTR style=GetWindowLongPtrW(foreground,GWL_EXSTYLE);
 if(!pid||desktop_shell_surface(foreground)||interaction_surface(foreground)||(style&WS_EX_NOACTIVATE)||
    ((style&WS_EX_TOOLWINDOW)&&GetWindow(foreground,GW_OWNER)))return FOREGROUND_EXEMPT;
 wchar_t path[PATH_CAP];
 if(!app_image(foreground,pid,path))return FOREGROUND_UNKNOWN;
 if(shell_app(path))return FOREGROUND_EXEMPT;
 return contains_app(path)?FOREGROUND_ALLOWED:FOREGROUND_BLOCKED;
}
static bool thread_interacting(DWORD thread){
 GUITHREADINFO info={.cbSize=sizeof(info)};
 return thread&&GetGUIThreadInfo(thread,&info)&&
        (info.flags&(GUI_INMENUMODE|GUI_POPUPMENUMODE|GUI_SYSTEMMENUMODE|GUI_INMOVESIZE));
}
static bool foreground_interaction(DWORD thread,bool pet_menu_open){
 if(pet_menu_open||thread_interacting(thread))return true;
 // The taskbar can own a menu without becoming the foreground window.
 HWND taskbar=FindWindowW(L"Shell_TrayWnd",NULL);
 if(taskbar&&thread_interacting(GetWindowThreadProcessId(taskbar,NULL)))return true;
 POINT cursor;
 if(!GetCursorPos(&cursor))return false;
 HWND pointed=WindowFromPoint(cursor);
 return interaction_surface(pointed)||interaction_surface(GetAncestor(pointed,GA_ROOT));
}
static bool minimize_pending(void){
 return !minimize_queued&&minimize_attempts<MINIMIZE_ATTEMPT_LIMIT;
}
static bool minimize_due(ULONGLONG now,bool interacting){
 if(interacting)minimize_at=now+FOREGROUND_GRACE_MS;
 return minimize_pending()&&now>=minimize_at;
}
static void record_minimize_result(ULONGLONG now,bool queued){
 minimize_queued=queued;
 minimize_attempts++;
 minimize_at=now+FOREGROUND_GRACE_MS;
}
static bool focus_time_allowed(ForegroundRule rule,ULONGLONG now){
 if(rule==FOREGROUND_ALLOWED||rule==FOREGROUND_EXEMPT){outside_pending=false;return true;}
 // Keep one grace period across consecutive disallowed apps, independent of HWND changes.
 if(!outside_pending){outside_pending=true;outside_since=now;}
 return now-outside_since<FOREGROUND_GRACE_MS;
}
bool focus_apps_tick(bool focusing,bool pet_menu_open){
 if(!focusing||!apps.enabled||!apps.count||test_mode){forget_foreground();outside_pending=false;return true;}
 HWND foreground=GetForegroundWindow();
 if(!foreground||IsIconic(foreground)||!IsWindowVisible(foreground)){forget_foreground();return focus_time_allowed(FOREGROUND_UNKNOWN,GetTickCount64());}
 DWORD pid=0,thread=GetWindowThreadProcessId(foreground,&pid);
 ULONGLONG now=GetTickCount64();
 if(foreground!=last_foreground||pid!=last_pid){
  forget_foreground();
  last_foreground=foreground;last_pid=pid;
  minimize_at=now+FOREGROUND_GRACE_MS;
 }
 if(last_rule==FOREGROUND_UNKNOWN&&now>=lookup_at){
  last_rule=foreground_rule(foreground,pid);
  lookup_at=now+LOOKUP_RETRY_MS;
 }
 if(last_rule==FOREGROUND_BLOCKED&&minimize_pending()&&
    minimize_due(now,foreground_interaction(thread,pet_menu_open))){
  // Recheck the app and act on this window only, even when it has an owner.
  last_rule=foreground_rule(foreground,pid);
  DWORD current_pid=0;
  if(last_rule==FOREGROUND_BLOCKED&&GetForegroundWindow()==foreground&&
     GetWindowThreadProcessId(foreground,&current_pid)==thread&&current_pid==pid&&
     IsWindowVisible(foreground)&&!IsIconic(foreground)){
   bool queued=minimize_attempts==0?ShowWindowAsync(foreground,SW_MINIMIZE)!=0:
                                   PostMessageW(foreground,WM_SYSCOMMAND,SC_MINIMIZE,0)!=0;
   record_minimize_result(now,queued);
  }
 }
 return focus_time_allowed(last_rule,now);
}

static int px(int value){return MulDiv(value,dpi,96);}
static HWND child(HWND owner,const wchar_t *kind,const wchar_t *title,DWORD style,int x,int y,int width,int height,int id){
 return CreateWindowW(kind,title,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(width),px(height),owner,(HMENU)(INT_PTR)id,pet_instance,NULL);
}
static void update_controls(void){
 SendMessageW(enable_check,BM_SETCHECK,apps.enabled?BST_CHECKED:BST_UNCHECKED,0);
 EnableWindow(enable_check,config[0]&&apps.count>0);
 EnableWindow(add_button,config[0]&&apps.count<APP_LIMIT);
 EnableWindow(remove_button,config[0]&&ListView_GetNextItem(list,-1,LVNI_SELECTED)>=0);
}
static void refresh_list(void){
 ListView_DeleteAllItems(list);
 for(unsigned i=0;i<apps.count;i++){
  LVITEMW item={.mask=LVIF_TEXT|LVIF_PARAM,.iItem=(int)i,.pszText=(wchar_t*)file_name(apps.paths[i]),.lParam=(LPARAM)i};
  int index=ListView_InsertItem(list,&item);
  if(index>=0)ListView_SetItemText(list,index,1,apps.paths[i]);
 }
 update_controls();
}
static bool commit(const FocusApps *next){
 if(!save_config(next)){
  SetWindowTextW(status_label,L"保存失败，请检查本机设置目录后重试。");
  update_controls();return false;
 }
 apps=*next;forget_foreground();
 refresh_list();
 SetWindowTextW(status_label,apps.count?L"设置已保存。":L"名单为空，限制已关闭。");
 return true;
}
static bool add_app_path(const wchar_t *path,HWND feedback){
 if(apps.count>=APP_LIMIT){SetWindowTextW(feedback,L"白名单最多保存 32 个应用。");return false;}
 if(!valid_app_path(path)||!executable_exists(path)){
  SetWindowTextW(feedback,L"找不到此应用的可执行文件。请刷新列表，或手动选择程序。");return false;
 }
 if(contains_app(path)){SetWindowTextW(feedback,L"该程序已在白名单中。");return false;}
 wchar_t *added=_wcsdup(path);
 if(!added){SetWindowTextW(feedback,L"内存不足，无法添加应用。");return false;}
 FocusApps next=apps;
 next.paths[next.count++]=added;
 if(commit(&next))return true;
 free(added);SetWindowTextW(feedback,L"保存失败，请检查本机设置目录后重试。");return false;
}
static bool browse_app(HWND owner){
 wchar_t path[PATH_CAP]=L"";
 OPENFILENAMEW picker={.lStructSize=sizeof(picker),.hwndOwner=owner,
  .lpstrFilter=L"程序和快捷方式 (*.exe;*.lnk)\0*.exe;*.lnk\0\0",.lpstrFile=path,.nMaxFile=PATH_CAP,
  .lpstrTitle=L"选择专注期间允许使用的程序",
  .Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT};
 pet_yield_to_panel(owner);
 if(!GetOpenFileNameW(&picker)){
  if(CommDlgExtendedError())SetWindowTextW(choice_status,L"无法打开文件选择窗口，请重试。");
  return false;
 }
 const wchar_t *suffix=wcsrchr(path,L'.');
 if(suffix&&!_wcsicmp(suffix,L".lnk")){
  HRESULT initialized=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
  wchar_t target[PATH_CAP];
  bool found=(SUCCEEDED(initialized)||initialized==RPC_E_CHANGED_MODE)&&shortcut_target(path,target);
  if(SUCCEEDED(initialized))CoUninitialize();
  if(!found){SetWindowTextW(choice_status,L"此快捷方式没有可加入白名单的程序。");return false;}
  wcscpy(path,target);
 }
 normalize_path(path);
 return add_app_path(path,choice_status);
}
static AppChoice *selected_choice(void){
 LVITEMW item={.mask=LVIF_PARAM,.iItem=ListView_GetNextItem(choices,-1,LVNI_SELECTED)};
 if(item.iItem<0||!ListView_GetItem(choices,&item)||(unsigned)item.lParam>=catalog.count)return NULL;
 return &catalog.rows[item.lParam];
}
static bool matches_search(const wchar_t *value,const wchar_t *filter){
 return !*filter||FindNLSStringEx(LOCALE_NAME_USER_DEFAULT,FIND_FROMSTART|NORM_IGNORECASE,value,-1,filter,-1,NULL,NULL,NULL,0)>=0;
}
static void refresh_choices(void){
 if(!choices||!choice_status||!choose_button)return;
 wchar_t filter[128];GetWindowTextW(search_box,filter,ARRAYSIZE(filter));
 SendMessageW(choices,WM_SETREDRAW,FALSE,0);
 ListView_DeleteAllItems(choices);
 int shown=0;
 for(unsigned i=0;i<catalog.count;i++){
  AppChoice *app=&catalog.rows[i];
  if(contains_app(app->path)||(!matches_search(app->name,filter)&&!matches_search(app->path,filter)))continue;
  LVITEMW item={.mask=LVIF_TEXT|LVIF_PARAM,.iItem=shown,.pszText=app->name,.lParam=i};
  int row=ListView_InsertItem(choices,&item);
  if(row>=0){
   ListView_SetItemText(choices,row,1,app->running?L"当前打开":L"开始菜单");
   ListView_SetItemText(choices,row,2,app->path);
   shown++;
  }
 }
 SendMessageW(choices,WM_SETREDRAW,TRUE,0);
 InvalidateRect(choices,NULL,TRUE);
 EnableWindow(choose_button,FALSE);
 wchar_t message[160];
 if(shown)swprintf(message,ARRAYSIZE(message),L"%d 个可添加应用。选中一项后双击，或点「加入白名单」。",shown);
 else wcscpy(message,*filter?L"没有匹配的应用。试试应用名或程序名。":L"没有可添加的应用。可以先打开应用，再刷新列表。");
 SetWindowTextW(choice_status,message);
}
static void finish_scan(void){
 AppScan *job=scan;
 if(!job||WaitForSingleObject(job->thread,0)!=WAIT_OBJECT_0)return;
 scan=NULL;CloseHandle(job->thread);CloseHandle(job->stop);
 if(chooser){
  free_catalog(&catalog);catalog=job->catalog;job->catalog.count=0;
  KillTimer(chooser,1);
  EnableWindow(GetDlgItem(chooser,ID_APP_REFRESH),TRUE);
  refresh_choices();
 }
 free_catalog(&job->catalog);free(job);
}
static void start_scan(void){
 finish_scan();
 if(scan)return;
 AppScan *job=calloc(1,sizeof(*job));
 if(job){
  job->stop=CreateEventW(NULL,TRUE,FALSE,NULL);
  if(job->stop&&SetTimer(chooser,1,75,NULL))job->thread=CreateThread(NULL,0,collect_choices,job,0,NULL);
  if(job->thread){
   scan=job;
   EnableWindow(GetDlgItem(chooser,ID_APP_REFRESH),FALSE);
   SetWindowTextW(choice_status,L"正在读取当前打开的应用和开始菜单…");return;
  }
  if(job->stop)CloseHandle(job->stop);
  free(job);
 }
 KillTimer(chooser,1);
 SetWindowTextW(choice_status,L"无法读取应用列表。请刷新，或使用「浏览文件」。");
}
static LRESULT CALLBACK chooser_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
 switch(message){
 case WM_CREATE:{
  child(window,L"STATIC",L"选择应用",0,20,14,560,28,0);
  search_box=child(window,L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,20,54,524,32,ID_APP_SEARCH);
  SendMessageW(search_box,EM_SETLIMITTEXT,127,0);
  SendMessageW(search_box,EM_SETCUEBANNER,FALSE,(LPARAM)L"搜索应用名或程序路径");
  child(window,L"BUTTON",L"刷新",WS_TABSTOP,556,54,104,32,ID_APP_REFRESH);
  choices=child(window,WC_LISTVIEWW,L"可添加应用",WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,20,100,640,252,ID_APP_CHOICES);
  choice_status=child(window,L"STATIC",L"",0,20,362,640,46,0);
  child(window,L"BUTTON",L"浏览文件…",WS_TABSTOP,20,418,132,34,ID_APP_BROWSE);
  child(window,L"BUTTON",L"取消",WS_TABSTOP,398,418,104,34,IDCANCEL);
  choose_button=child(window,L"BUTTON",L"加入白名单",WS_TABSTOP|BS_DEFPUSHBUTTON,514,418,146,34,IDOK);
  if(!search_box||!choices||!choice_status||!choose_button)return -1;
  ListView_SetExtendedListViewStyle(choices,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_INFOTIP);
  const wchar_t *titles[]={L"应用",L"来源",L"程序路径"};
  const int widths[]={218,100,300};
  for(int i=0;i<ARRAYSIZE(widths);i++){
   LVCOLUMNW column={.mask=LVCF_WIDTH|LVCF_TEXT,.cx=px(widths[i]),.pszText=(wchar_t*)titles[i]};
   ListView_InsertColumn(choices,i,&column);
  }
  EnableWindow(choose_button,FALSE);
  return 0;
 }
 case WM_TIMER:if(wp==1){finish_scan();return 0;}break;
 case WM_NOTIFY:
  if(lp&&((NMHDR*)lp)->hwndFrom==choices){
   if(((NMHDR*)lp)->code==LVN_GETINFOTIPW){
    NMLVGETINFOTIPW *tip=(NMLVGETINFOTIPW*)lp;
    LVITEMW item={.mask=LVIF_PARAM,.iItem=tip->iItem};
    if(ListView_GetItem(choices,&item)&&(unsigned)item.lParam<catalog.count)
     lstrcpynW(tip->pszText,catalog.rows[item.lParam].path,tip->cchTextMax);
    return 0;
   }
   if(((NMHDR*)lp)->code==LVN_ITEMCHANGED)EnableWindow(choose_button,selected_choice()!=NULL);
   if(((NMHDR*)lp)->code==NM_DBLCLK&&((NMITEMACTIVATE*)lp)->iItem>=0){
    PostMessageW(window,WM_COMMAND,IDOK,0);return 0;
   }
  }
  break;
 case WM_COMMAND:
  if(LOWORD(wp)==ID_APP_SEARCH&&HIWORD(wp)==EN_CHANGE){
   if(catalog.count||!scan)refresh_choices();
   return 0;
  }
  if(HIWORD(wp)!=BN_CLICKED)break;
  switch(LOWORD(wp)){
   case ID_APP_REFRESH:start_scan();return 0;
   case ID_APP_BROWSE:if(browse_app(window))DestroyWindow(window);return 0;
   case IDOK:{
    AppChoice *selected=selected_choice();
    if(selected&&add_app_path(selected->path,choice_status))DestroyWindow(window);
    return 0;
   }
   case IDCANCEL:DestroyWindow(window);return 0;
  }
  break;
 case WM_CLOSE:DestroyWindow(window);return 0;
 case WM_DESTROY:
  KillTimer(window,1);free_catalog(&catalog);
  chooser=search_box=choices=choose_button=choice_status=NULL;
  if(panel){EnableWindow(panel,TRUE);SetActiveWindow(panel);SetFocus(add_button);}
  return 0;
 }
 return DefWindowProcW(window,message,wp,lp);
}
static void show_chooser(HWND owner){
 if(chooser){pet_show_panel(chooser,SW_RESTORE);return;}
 WNDCLASSW cls={.lpfnWndProc=chooser_proc,.hInstance=pet_instance,.lpszClassName=L"LetterPet.FocusAppChooser",
  .hCursor=LoadCursorW(NULL,IDC_ARROW),.hbrBackground=(HBRUSH)(COLOR_WINDOW+1)};
 RegisterClassW(&cls);
 DWORD style=WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,ex=WS_EX_CONTROLPARENT;
 RECT size={0,0,px(680),px(468)},parent;
 AdjustWindowRectEx(&size,style,FALSE,ex);GetWindowRect(owner,&parent);
 MONITORINFO area={sizeof(area)};
 if(!GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&area))SystemParametersInfoW(SPI_GETWORKAREA,0,&area.rcWork,0);
 int width=size.right-size.left,height=size.bottom-size.top;
 int x=max(area.rcWork.left,min(area.rcWork.right-width,(parent.left+parent.right-width)/2));
 int y=max(area.rcWork.top,min(area.rcWork.bottom-height,(parent.top+parent.bottom-height)/2));
 chooser=CreateWindowExW(ex,cls.lpszClassName,L"桌宠 · 添加白名单应用",style,x,y,width,height,owner,NULL,pet_instance,NULL);
 if(!chooser){SetWindowTextW(status_label,L"无法打开应用选择窗口。");return;}
 EnableWindow(owner,FALSE);
 ui_theme_apply(chooser);ui_theme_role(choice_status,UI_SURFACE_TEXT);
 pet_show_panel(chooser,SW_SHOW);SetFocus(search_box);
 if(scan){
  if(SetTimer(chooser,1,75,NULL)){
   EnableWindow(GetDlgItem(chooser,ID_APP_REFRESH),FALSE);
   SetWindowTextW(choice_status,L"正在读取应用列表…");
  }else SetWindowTextW(choice_status,L"无法刷新应用列表，可使用「浏览文件」。");
 }
 else start_scan();
}
static void remove_app(void){
 LVITEMW item={.mask=LVIF_PARAM,.iItem=ListView_GetNextItem(list,-1,LVNI_SELECTED)};
 if(item.iItem<0||!ListView_GetItem(list,&item)||(unsigned)item.lParam>=apps.count)return;
 unsigned selected=(unsigned)item.lParam;
 FocusApps next=apps;
 wchar_t *removed=next.paths[selected];
 next.count--;
 memmove(next.paths+selected,next.paths+selected+1,(next.count-selected)*sizeof(*next.paths));
 next.paths[next.count]=NULL;
 if(!next.count)next.enabled=false;
 if(commit(&next))free(removed);
}
static LRESULT CALLBACK panel_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
 switch(message){
 case WM_CREATE:{
  child(window,L"STATIC",L"应用白名单",0,24,16,596,30,0);
  enable_check=child(window,L"BUTTON",L"专注时最小化名单外的应用窗口",WS_TABSTOP|BS_AUTOCHECKBOX,24,55,596,28,ID_ENABLE);
  list=child(window,WC_LISTVIEWW,L"",WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,24,97,596,185,ID_APPS);
  add_button=child(window,L"BUTTON",L"添加应用…",WS_TABSTOP,24,298,140,34,ID_ADD_APP);
  remove_button=child(window,L"BUTTON",L"移除所选",WS_TABSTOP,176,298,140,34,ID_REMOVE_APP);
  child(window,L"BUTTON",L"关闭",WS_TABSTOP,480,298,140,34,IDCANCEL);
  status_label=child(window,L"STATIC",L"",0,24,347,596,40,0);
  if(!enable_check||!list||!add_button||!remove_button||!status_label)return -1;
  ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_INFOTIP);
  LVCOLUMNW column={.mask=LVCF_WIDTH|LVCF_TEXT,.cx=px(160),.pszText=L"程序"};
  ListView_InsertColumn(list,0,&column);
  column.cx=px(410);column.pszText=L"程序路径";ListView_InsertColumn(list,1,&column);
  return 0;
 }
 case WM_NOTIFY:
  if(lp&&((NMHDR*)lp)->hwndFrom==list&&((NMHDR*)lp)->code==LVN_ITEMCHANGED)update_controls();
  break;
 case WM_COMMAND:
  if(HIWORD(wp)!=BN_CLICKED)break;
  switch(LOWORD(wp)){
   case ID_ENABLE:{
    FocusApps next=apps;
    next.enabled=apps.count&&SendMessageW(enable_check,BM_GETCHECK,0,0)==BST_CHECKED;
    commit(&next);return 0;
   }
   case ID_ADD_APP:show_chooser(window);return 0;
   case ID_REMOVE_APP:remove_app();return 0;
   case IDCANCEL:DestroyWindow(window);return 0;
  }
  break;
 case WM_CLOSE:DestroyWindow(window);return 0;
 case WM_DESTROY:panel=enable_check=list=add_button=remove_button=status_label=NULL;return 0;
 }
 return DefWindowProcW(window,message,wp,lp);
}
void focus_apps_show(HWND owner){
 if(panel){pet_show_panel(panel,SW_RESTORE);return;}
 HDC dc=GetDC(owner);
 dpi=dc?GetDeviceCaps(dc,LOGPIXELSX):96;
 if(dc)ReleaseDC(owner,dc);
 WNDCLASSW cls={.lpfnWndProc=panel_proc,.hInstance=pet_instance,.lpszClassName=L"LetterPet.FocusApps",
  .hCursor=LoadCursorW(NULL,IDC_ARROW),.hbrBackground=(HBRUSH)(COLOR_WINDOW+1)};
 RegisterClassW(&cls);
 DWORD style=WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,ex=WS_EX_CONTROLPARENT;
 RECT size={0,0,px(644),px(396)};
 AdjustWindowRectEx(&size,style,FALSE,ex);
 MONITORINFO area={sizeof(area)};
 if(!GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&area)){
  SystemParametersInfoW(SPI_GETWORKAREA,0,&area.rcWork,0);
 }
 int width=size.right-size.left,height=size.bottom-size.top;
 panel=CreateWindowExW(ex,cls.lpszClassName,L"桌宠 · 应用白名单",style,
  area.rcWork.left+max(0,(area.rcWork.right-area.rcWork.left-width)/2),
  area.rcWork.top+max(0,(area.rcWork.bottom-area.rcWork.top-height)/2),
  width,height,owner,NULL,pet_instance,NULL);
 if(!panel)return;
 ui_theme_apply(panel);ui_theme_role(status_label,UI_SURFACE_TEXT);refresh_list();
 SetWindowTextW(status_label,apps.count?L"仅在专注计时时生效。暂停或休息时可以正常切换应用。":L"先添加应用，再开启限制。");
 pet_show_panel(panel,SW_SHOWNORMAL);
}
bool focus_apps_dialog_message(MSG *message){
 if(chooser&&IsWindowVisible(chooser)){
  if(message->hwnd!=chooser&&!IsChild(chooser,message->hwnd))return false;
  if(message->message==WM_KEYDOWN&&(message->hwnd==chooser||IsChild(chooser,message->hwnd))){
   if(message->wParam==VK_ESCAPE){DestroyWindow(chooser);return true;}
   if(message->wParam==VK_RETURN&&(GetFocus()==search_box||GetFocus()==choices)){
    if(!selected_choice()&&ListView_GetItemCount(choices)==1)
     ListView_SetItemState(choices,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    SendMessageW(chooser,WM_COMMAND,IDOK,0);return true;
   }
  }
  return IsDialogMessageW(chooser,message)!=0;
 }
 return panel&&IsWindowVisible(panel)&&IsDialogMessageW(panel,message);
}
void focus_apps_shutdown(void){
 if(chooser)DestroyWindow(chooser);
 if(scan){
  SetEvent(scan->stop);
  WaitForSingleObject(scan->thread,INFINITE);
  finish_scan();
 }
 if(panel)DestroyWindow(panel);
 for(unsigned i=0;i<apps.count;i++)free(apps.paths[i]);
 apps=(FocusApps){0};forget_foreground();outside_pending=false;
}
bool focus_apps_ui_selftest(FILE *log){
 wchar_t dir[MAX_PATH],settings_path[MAX_PATH],exe[MAX_PATH];
 if(swprintf(dir,ARRAYSIZE(dir),L"%ls\\focus-apps-test-%lu",app_dir,GetCurrentProcessId())<0||
    swprintf(settings_path,ARRAYSIZE(settings_path),L"%ls\\settings.ini",dir)<0||
    !GetModuleFileNameW(NULL,exe,ARRAYSIZE(exe))||!CreateDirectoryW(dir,NULL)){
  fprintf(log,"FAIL: could not create isolated allowlist directory.\n");return false;
 }
 focus_apps_init(settings_path);
 focus_apps_show(pet_window);
 if(panel)SendMessageW(panel,WM_COMMAND,ID_ADD_APP,0);
 bool ready=chooser&&scan&&WaitForSingleObject(scan->thread,10000)==WAIT_OBJECT_0;
 bool filtered=false,saved=false;
 bool shortcut=false;
 int shown=-1;
 unsigned hits=0;
 if(ready){
  finish_scan();
  wchar_t link_path[MAX_PATH+16],target[PATH_CAP];
  swprintf(link_path,ARRAYSIZE(link_path),L"%ls\\fixture.lnk",dir);
  HRESULT initialized=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
  IShellLinkW *link=NULL;
  if((SUCCEEDED(initialized)||initialized==RPC_E_CHANGED_MODE)&&
     SUCCEEDED(CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkW,(void**)&link))){
   IPersistFile *file=NULL;
   if(SUCCEEDED(IShellLinkW_SetPath(link,exe))&&SUCCEEDED(IShellLinkW_QueryInterface(link,&IID_IPersistFile,(void**)&file))){
    shortcut=SUCCEEDED(IPersistFile_Save(file,link_path,TRUE))&&shortcut_target(link_path,target)&&!_wcsicmp(target,exe);
    IPersistFile_Release(file);
   }
   IShellLinkW_Release(link);
  }
  if(SUCCEEDED(initialized))CoUninitialize();
  DeleteFileW(link_path);
  add_choice(&catalog,exe,L"LetterPet QA application",false);
  add_choice(&catalog,exe,L"duplicate",true);
  for(unsigned i=0;i<catalog.count;i++)if(!_wcsicmp(catalog.rows[i].path,exe))hits++;
  SetWindowTextW(search_box,L"lEtTeRpEt qA APPLICATION");
  shown=ListView_GetItemCount(choices);
  filtered=hits==1&&shown==1&&!IsWindowEnabled(panel);
  if(filtered){
   ListView_SetItemState(choices,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
   SendMessageW(chooser,WM_COMMAND,IDOK,0);
   saved=!chooser&&apps.count==1&&contains_app(exe)&&ListView_GetItemCount(list)==1&&IsWindowEnabled(panel);
  }
 }
 focus_apps_shutdown();
 if(saved){
  focus_apps_init(settings_path);
  saved=apps.count==1&&contains_app(exe)&&!apps.enabled;
  focus_apps_shutdown();
 }
 DeleteFileW(config);RemoveDirectoryW(dir);config[0]=0;
 bool direct=foreground_rule(pet_window,GetCurrentProcessId())==FOREGROUND_ALLOWED&&
             foreground_rule(NULL,0)==FOREGROUND_EXEMPT;
 WNDCLASSW tray_class={.lpfnWndProc=DefWindowProcW,.hInstance=pet_instance,
                         .lpszClassName=L"TopLevelWindowForOverflowXamlIsland"};
 ATOM registered=RegisterClassW(&tray_class);
 HWND tray_popup=registered?CreateWindowW(tray_class.lpszClassName,L"",WS_POPUP,0,0,10,10,
                                          NULL,NULL,pet_instance,NULL):NULL;
 bool surfaces=tray_popup&&interaction_surface(tray_popup)&&!interaction_surface(pet_window);
 if(tray_popup)DestroyWindow(tray_popup);
 if(registered)UnregisterClassW(tray_class.lpszClassName,pet_instance);
 forget_foreground();
 minimize_at=1000+FOREGROUND_GRACE_MS;
 bool stable=!minimize_due(minimize_at-1,false)&&minimize_due(minimize_at,false);
 bool protected=foreground_interaction(GetCurrentThreadId(),true)&&
                !minimize_due(1400,true)&&!minimize_due(5000,true)&&
                !minimize_due(5000+FOREGROUND_GRACE_MS-1,false)&&
                minimize_due(5000+FOREGROUND_GRACE_MS,false);
 record_minimize_result(6000,false);
 bool retry=!minimize_due(6000+FOREGROUND_GRACE_MS-1,false)&&
             minimize_due(6000+FOREGROUND_GRACE_MS,false);
 record_minimize_result(7000,false);
 retry=retry&&!minimize_due(9000,false);
 forget_foreground();
 retry=retry&&minimize_due(9000,false);
 record_minimize_result(9000,true);
 retry=retry&&!minimize_due(10000,false);
 forget_foreground();
 bool grace=focus_time_allowed(FOREGROUND_BLOCKED,1000)&&
            focus_time_allowed(FOREGROUND_UNKNOWN,3999)&&
            !focus_time_allowed(FOREGROUND_BLOCKED,4000)&&
            !focus_time_allowed(FOREGROUND_BLOCKED,5000)&&
            focus_time_allowed(FOREGROUND_ALLOWED,6000)&&
            focus_time_allowed(FOREGROUND_BLOCKED,7000)&&
            focus_time_allowed(FOREGROUND_EXEMPT,8000)&&!outside_pending;
 outside_pending=false;
 fprintf(log,"%s: app chooser search, deduplication, selection and reload (opened=%d duplicate count=%u shown=%d saved=%d).\n",
         filtered&&saved?"PASS":"FAIL",ready,hits,shown,saved);
 fprintf(log,"%s: a Start menu shortcut resolves to the executable path used by the allowlist.\n",shortcut?"PASS":"FAIL");
 fprintf(log,"%s: tray overflow popup is recognized as a protected interaction surface.\n",surfaces?"PASS":"FAIL");
 fprintf(log,"%s: blocked foreground waits for stability and a menu interaction postpones minimization.\n",
         stable&&protected?"PASS":"FAIL");
 fprintf(log,"%s: blocked foreground stops after a queued request or two failed attempts, and LetterPet stays allowed.\n",
         retry&&direct?"PASS":"FAIL");
 fprintf(log,"%s: three-second accounting grace survives blocked-app switches and resumes on allowed/system surfaces.\n",grace?"PASS":"FAIL");
 return filtered&&saved&&shortcut&&surfaces&&stable&&protected&&retry&&direct&&grace;
}
