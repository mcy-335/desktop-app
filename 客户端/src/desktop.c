#include "desktop.h"
#include "ui_theme.h"
#include "desktop_core.h"
#include <commctrl.h>
#include <dwmapi.h>
#define HOTKEY_BASE 1200
#define ID_APPLY 701
#define ID_DEFAULTS 702
typedef struct {wchar_t device[32];int x,y;} MonitorPosition;
static HWND owner,panel,checks[3],edit_keys[DESK_KEYS],status_label;
static HFONT font;
static DeskHotkey keys[DESK_KEYS];
static bool registered[DESK_KEYS],avoid_enabled=true,fullscreen_enabled=true,snap_enabled=true,shown,initialized,test_storage,configuring;
static AvoidState avoidance;
static VisibilityState visibility;
static int dpi=96,position_count;
static MonitorPosition positions[16];
static wchar_t path[32768],last_device[32],status_text[256];
static double last_tick;
static const wchar_t *actions[]={L"显示 / 隐藏桌宠",L"切换鼠标穿透",L"开始 / 暂停专注",L"切换游戏模式"};
static int px(int n){return MulDiv(n,dpi,96);}
static void defaults(DeskHotkey *value){const unsigned letters[]={'H','P','F','G'};for(int i=0;i<DESK_KEYS;i++)value[i]=(DeskHotkey){MOD_CONTROL|MOD_ALT|MOD_SHIFT,letters[i]};}
static DeskRect area_of(HMONITOR monitor,MONITORINFOEXW *info){memset(info,0,sizeof(*info));info->cbSize=sizeof(*info);GetMonitorInfoW(monitor,(MONITORINFO*)info);return (DeskRect){info->rcWork.left,info->rcWork.top,info->rcWork.right,info->rcWork.bottom};}
static DeskPoint actual_position(void){RECT r;GetWindowRect(owner,&r);return (DeskPoint){r.left,r.top};}
static DeskRect home_area(MONITORINFOEXW *info){RECT r={settings.x,settings.y,settings.x+settings.size,settings.y+settings.size};return area_of(MonitorFromRect(&r,MONITOR_DEFAULTTONEAREST),info);}
static void move_to(DeskPoint p){SetWindowPos(owner,NULL,p.x,p.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);}
static bool save_config(void){
 if(!path[0]||(test_mode&&!test_storage))return true;
 wchar_t text[16384],temp[32768];int n=swprintf(text,16384,L"\xfeff[desktop]\r\nversion=1\r\navoid=%d\r\nfullscreen=%d\r\nsnap=%d\r\nlast_device=%ls\r\nmonitors=%d\r\n",avoid_enabled,fullscreen_enabled,snap_enabled,last_device,position_count);
 for(int i=0;i<DESK_KEYS&&n>0;i++){int count=swprintf(text+n,16384-n,L"key%d=%u\r\nmod%d=%u\r\n",i,keys[i].key,i,keys[i].modifiers);n=count<0?-1:n+count;}
 for(int i=0;i<position_count&&n>0;i++){int count=swprintf(text+n,16384-n,L"[monitor%d]\r\ndevice=%ls\r\nx=%d\r\ny=%d\r\n",i,positions[i].device,positions[i].x,positions[i].y);n=count<0?-1:n+count;}
 swprintf(temp,32768,L"%ls.tmp",path);HANDLE file=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);DWORD wrote=0;bool ok=false;
 if(file!=INVALID_HANDLE_VALUE){DWORD size=n>0?(DWORD)(n*sizeof(wchar_t)):0;ok=n>0&&WriteFile(file,text,size,&wrote,NULL)&&wrote==size&&FlushFileBuffers(file);CloseHandle(file);}
 if(ok)ok=MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;if(!ok)DeleteFileW(temp);return ok;
}
static void load_config(void){
 defaults(keys);avoid_enabled=fullscreen_enabled=snap_enabled=true;position_count=0;last_device[0]=0;
 if(!path[0]||GetPrivateProfileIntW(L"desktop",L"version",0,path)!=1)return;
 avoid_enabled=GetPrivateProfileIntW(L"desktop",L"avoid",1,path)!=0;fullscreen_enabled=GetPrivateProfileIntW(L"desktop",L"fullscreen",1,path)!=0;snap_enabled=GetPrivateProfileIntW(L"desktop",L"snap",1,path)!=0;
 GetPrivateProfileStringW(L"desktop",L"last_device",L"",last_device,32,path);
 DeskHotkey proposed[DESK_KEYS];for(int i=0;i<DESK_KEYS;i++){wchar_t key[20];swprintf(key,20,L"key%d",i);proposed[i].key=GetPrivateProfileIntW(L"desktop",key,keys[i].key,path);swprintf(key,20,L"mod%d",i);proposed[i].modifiers=GetPrivateProfileIntW(L"desktop",key,keys[i].modifiers,path);}
 if(desk_keys_valid(proposed))memcpy(keys,proposed,sizeof(keys));
 int count=(int)GetPrivateProfileIntW(L"desktop",L"monitors",0,path);if(count<0||count>16)count=0;
 for(int i=0;i<count;i++){wchar_t section[32];swprintf(section,32,L"monitor%d",i);MonitorPosition p={0};GetPrivateProfileStringW(section,L"device",L"",p.device,32,path);p.x=(int)GetPrivateProfileIntW(section,L"x",0,path);p.y=(int)GetPrivateProfileIntW(section,L"y",0,path);if(!p.device[0]||p.x<0||p.y<0||p.x>100000||p.y>100000)continue;positions[position_count++]=p;}
}
static bool register_set(const DeskHotkey *candidate,bool *active){bool ok=true;for(int i=0;i<DESK_KEYS;i++){active[i]=false;if(candidate[i].key){active[i]=RegisterHotKey(owner,HOTKEY_BASE+i,candidate[i].modifiers|MOD_NOREPEAT,candidate[i].key)!=0;if(!active[i])ok=false;}}return ok;}
static void unregister_set(bool *active){for(int i=0;i<DESK_KEYS;i++){if(active[i])UnregisterHotKey(owner,HOTKEY_BASE+i);active[i]=false;}}
static bool apply_keys(const DeskHotkey candidate[DESK_KEYS]){
 if(!desk_keys_valid(candidate)){lstrcpyW(status_text,L"快捷键需使用 Ctrl 或 Alt 搭配字母、数字或 F1–F24，且不能重复。显示/隐藏不能为空。");return false;}
 if(test_mode){memcpy(keys,candidate,sizeof(keys));return true;}
 for(int i=0;i<DESK_KEYS;i++)if(candidate[i].key){bool owned=false;for(int j=0;j<DESK_KEYS;j++)if(registered[j]&&desk_hotkey_equal(candidate[i],keys[j]))owned=true;if(!owned){int id=HOTKEY_BASE+20+i;if(!RegisterHotKey(owner,id,candidate[i].modifiers|MOD_NOREPEAT,candidate[i].key)){swprintf(status_text,256,L"“%ls”快捷键已被占用，原配置保留。",actions[i]);return false;}UnregisterHotKey(owner,id);}}
 DeskHotkey previous[DESK_KEYS];memcpy(previous,keys,sizeof(keys));unregister_set(registered);
 if(!register_set(candidate,registered)){unregister_set(registered);bool rolled_back=register_set(previous,registered);lstrcpyW(status_text,rolled_back?L"新快捷键注册失败，已恢复原快捷键。":L"快捷键注册遇到冲突，部分旧快捷键也不可用。请重新设置；托盘仍可操作。");return false;}
 memcpy(keys,candidate,sizeof(keys));return true;
}
static BOOL CALLBACK find_monitor(HMONITOR monitor,HDC dc,LPRECT rect,LPARAM data){(void)dc;(void)rect;MONITORINFOEXW info;area_of(monitor,&info);if(!_wcsicmp(info.szDevice,last_device)){*(HMONITOR*)data=monitor;return FALSE;}return TRUE;}
void desktop_display_changed(void){
 if(!owner)return;memset(&avoidance,0,sizeof(avoidance));HMONITOR monitor=NULL;
 if(last_device[0])EnumDisplayMonitors(NULL,NULL,find_monitor,(LPARAM)&monitor);
 if(!monitor){RECT r={settings.x,settings.y,settings.x+settings.size,settings.y+settings.size};monitor=MonitorFromRect(&r,MONITOR_DEFAULTTONEAREST);}
 MONITORINFOEXW info;DeskRect work=area_of(monitor,&info);DeskPoint target={settings.x,settings.y};
 for(int i=0;i<position_count;i++)if(!_wcsicmp(info.szDevice,positions[i].device)){target=(DeskPoint){work.left+positions[i].x,work.top+positions[i].y};break;}
 target=desk_clamp(work,settings.size,target);settings.x=target.x;settings.y=target.y;move_to(target);pet_desktop_position_saved();
}
void desktop_position_changed(void){
 if(!owner)return;memset(&avoidance,0,sizeof(avoidance));MONITORINFOEXW info;DeskRect work=home_area(&info);DeskPoint home=desk_clamp(work,settings.size,(DeskPoint){settings.x,settings.y});settings.x=home.x;settings.y=home.y;move_to(home);
 int index=-1;for(int i=0;i<position_count;i++)if(!_wcsicmp(positions[i].device,info.szDevice))index=i;
 if(index<0){if(position_count<16)index=position_count++;else index=0;}
 lstrcpynW(positions[index].device,info.szDevice,32);positions[index].x=home.x-work.left;positions[index].y=home.y-work.top;lstrcpynW(last_device,info.szDevice,32);
 if(!save_config())lstrcpyW(status_text,L"桌面设置保存失败，当前位置暂未写入磁盘。");pet_desktop_position_saved();
}
void desktop_drag_begin(void){avoidance.hovering=false;avoidance.leaving=false;}
void desktop_drag_end(void){
 if(!owner)return;DeskPoint p=actual_position();MONITORINFOEXW info;RECT rect={p.x,p.y,p.x+settings.size,p.y+settings.size};DeskRect work=area_of(MonitorFromRect(&rect,MONITOR_DEFAULTTONEAREST),&info);
 if(snap_enabled)p=desk_snap(work,settings.size,p,px(24));else p=desk_clamp(work,settings.size,p);settings.x=p.x;settings.y=p.y;desktop_position_changed();avoidance.cooldown=now_ms()+5000;
}
static bool own_foreground(void){DWORD pid=0;HWND foreground=GetForegroundWindow();if(foreground)GetWindowThreadProcessId(foreground,&pid);return pid==GetCurrentProcessId();}
bool desktop_shell_surface(HWND window){
 wchar_t cls[80]={0};GetClassNameW(window,cls,80);
 return !wcscmp(cls,L"Progman")||!wcscmp(cls,L"WorkerW")||!wcscmp(cls,L"Shell_TrayWnd")||!wcscmp(cls,L"Shell_SecondaryTrayWnd");
}
static bool covers_monitor(HWND window,const RECT *monitor){
 // Maximized windows include invisible resize borders in GetWindowRect.
 typedef HRESULT (WINAPI *GetDwmAttribute)(HWND,DWORD,PVOID,DWORD);
 HMODULE dwm=LoadLibraryExW(L"dwmapi.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
 RECT bounds;
 HRESULT result=E_FAIL;
 if(dwm){
  union {FARPROC raw;GetDwmAttribute call;} get;
  get.raw=GetProcAddress(dwm,"DwmGetWindowAttribute");
  if(get.raw)result=get.call(window,DWMWA_EXTENDED_FRAME_BOUNDS,&bounds,sizeof(bounds));
  FreeLibrary(dwm);
 }
 if(FAILED(result)){
  if(IsZoomed(window)||!GetWindowRect(window,&bounds))return false;
 }
 return bounds.left<=monitor->left+2&&bounds.top<=monitor->top+2&&
        bounds.right>=monitor->right-2&&bounds.bottom>=monitor->bottom-2;
}
static bool fullscreen_bounds(HWND window,HMONITOR monitor){
 MONITORINFO info={sizeof(info)};
 if(!GetMonitorInfoW(monitor,&info))return false;
 if(IsZoomed(window)&&!EqualRect(&info.rcMonitor,&info.rcWork))return false;
 return covers_monitor(window,&info.rcMonitor);
}
static bool foreground_fullscreen(HWND foreground){
 if(!foreground||foreground==owner||IsIconic(foreground)||!IsWindowVisible(foreground))return false;
 DWORD pid=0;GetWindowThreadProcessId(foreground,&pid);
 if(pid==GetCurrentProcessId())return false;
 if(desktop_shell_surface(foreground))return false;
 RECT home={settings.x,settings.y,settings.x+settings.size,settings.y+settings.size};HMONITOR target=MonitorFromRect(&home,MONITOR_DEFAULTTONEAREST);if(MonitorFromWindow(foreground,MONITOR_DEFAULTTONEAREST)!=target)return false;
 return fullscreen_bounds(foreground,target);
}
static bool desktop_should_show(HWND foreground,bool intended,bool menu_open){
 bool hide_fullscreen=fullscreen_enabled&&!settings.topmost&&!settings.game;
 bool full=hide_fullscreen&&!menu_open&&foreground_fullscreen(foreground);
 return desk_visible(&visibility,intended,full,hide_fullscreen);
}
static void sync_visibility(bool effective){
 if(shown!=effective||!!IsWindowVisible(owner)!=effective){
  shown=effective;
  ShowWindow(owner,shown?SW_SHOWNOACTIVATE:SW_HIDE);
  live2d_reset_clock();
 }
}
bool desktop_tick(double now,bool intended,bool dragging,bool menu_open){
 if(!initialized)return intended;if(now-last_tick<100)return shown;last_tick=now;
 bool effective=desktop_should_show(GetForegroundWindow(),intended,menu_open);
 sync_visibility(effective);
 if(!shown)return false;
 POINT cursor;GetCursorPos(&cursor);DeskPoint home={settings.x,settings.y},current=actual_position();
 bool interaction=dragging||menu_open||own_foreground()||(GetAsyncKeyState(VK_LBUTTON)&0x8000)||(GetAsyncKeyState(VK_RBUTTON)&0x8000)||(GetAsyncKeyState(VK_MBUTTON)&0x8000);
 if(desk_avoid(&avoidance,now,avoid_enabled&&!settings.clickthrough&&!settings.game,interaction,(DeskPoint){cursor.x,cursor.y},home,current,settings.size)){
  if(avoidance.moved){MONITORINFOEXW info;DeskRect work=home_area(&info);DeskPoint target=desk_escape(work,settings.size,(DeskPoint){cursor.x,cursor.y});if(desk_near((DeskPoint){cursor.x,cursor.y},target,settings.size,12)){avoidance.moved=false;avoidance.cooldown=now+5000;}else move_to(target);}
  else move_to(home);
 }
 return shown;
}
bool desktop_is_visible(void){return shown;}
void desktop_user_visibility(bool visible){if(!initialized)return;desk_reveal(&visibility,visible);memset(&avoidance,0,sizeof(avoidance));move_to((DeskPoint){settings.x,settings.y});last_tick=0;desktop_tick(now_ms(),visible,false,false);}
void desktop_hotkey(WPARAM id){if(!configuring&&id>=HOTKEY_BASE&&id<HOTKEY_BASE+DESK_KEYS)pet_desktop_action((int)id-HOTKEY_BASE);}
static WORD hotkey_word(DeskHotkey k){BYTE modifiers=0;if(k.modifiers&MOD_CONTROL)modifiers|=HOTKEYF_CONTROL;if(k.modifiers&MOD_ALT)modifiers|=HOTKEYF_ALT;if(k.modifiers&MOD_SHIFT)modifiers|=HOTKEYF_SHIFT;return MAKEWORD(k.key,modifiers);}
static DeskHotkey read_hotkey(HWND control){WORD word=(WORD)SendMessageW(control,HKM_GETHOTKEY,0,0);BYTE flags=HIBYTE(word);unsigned mod=0;if(flags&HOTKEYF_CONTROL)mod|=MOD_CONTROL;if(flags&HOTKEYF_ALT)mod|=MOD_ALT;if(flags&HOTKEYF_SHIFT)mod|=MOD_SHIFT;return (DeskHotkey){mod,LOBYTE(word)};}
static void fill_controls(void){SendMessageW(checks[0],BM_SETCHECK,avoid_enabled?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(checks[1],BM_SETCHECK,fullscreen_enabled?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(checks[2],BM_SETCHECK,snap_enabled?BST_CHECKED:BST_UNCHECKED,0);for(int i=0;i<DESK_KEYS;i++)SendMessageW(edit_keys[i],HKM_SETHOTKEY,hotkey_word(keys[i]),0);SetWindowTextW(status_label,status_text);}
static HWND child(HWND w,const wchar_t *cls,const wchar_t *text,DWORD style,int x,int y,int width,int height,int id){HWND c=CreateWindowW(cls,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(width),px(height),w,(HMENU)(INT_PTR)id,pet_instance,NULL);SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;}
static LRESULT CALLBACK desktop_proc(HWND w,UINT message,WPARAM wp,LPARAM lp){
 switch(message){
 case WM_CREATE:
  child(w,L"STATIC",L"桌面行为与快捷键",0,24,18,544,28,0);
  checks[0]=child(w,L"BUTTON",L"鼠标悬停 0.9 秒后暂时避让",WS_TABSTOP|BS_AUTOCHECKBOX,24,60,544,28,710);
  checks[1]=child(w,L"BUTTON",L"全屏时自动隐藏（窗口置顶或游戏模式下保持显示）",WS_TABSTOP|BS_AUTOCHECKBOX,24,98,544,28,711);
  checks[2]=child(w,L"BUTTON",L"拖动结束时吸附到屏幕工作区边缘",WS_TABSTOP|BS_AUTOCHECKBOX,24,136,544,28,712);
  for(int i=0;i<DESK_KEYS;i++){child(w,L"STATIC",actions[i],0,24,194+i*44,234,26,0);edit_keys[i]=child(w,HOTKEY_CLASSW,L"",WS_TABSTOP|WS_BORDER,274,190+i*44,294,30,720+i);}
  child(w,L"STATIC",L"点击快捷键框后直接按组合键；Backspace 可清空。\r\n使用 Ctrl 或 Alt 搭配字母、数字或 F1–F24，显示/隐藏必须设置。",0,24,380,544,52,0);
  child(w,L"BUTTON",L"应用设置",WS_TABSTOP|BS_DEFPUSHBUTTON,24,450,260,34,ID_APPLY);child(w,L"BUTTON",L"恢复默认",WS_TABSTOP,308,450,260,34,ID_DEFAULTS);
  status_label=child(w,L"STATIC",L"",0,24,504,544,64,0);return 0;
 case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED&&(LOWORD(wp)==ID_APPLY||LOWORD(wp)==ID_DEFAULTS)){
  DeskHotkey candidate[DESK_KEYS];if(LOWORD(wp)==ID_DEFAULTS)defaults(candidate);else for(int i=0;i<DESK_KEYS;i++)candidate[i]=read_hotkey(edit_keys[i]);
  if(!apply_keys(candidate)){if(configuring)unregister_set(registered);SetWindowTextW(status_label,status_text);MessageBoxW(w,status_text,L"快捷键未应用",MB_ICONINFORMATION);return 0;}
  if(configuring)unregister_set(registered);
  if(LOWORD(wp)==ID_DEFAULTS)avoid_enabled=fullscreen_enabled=snap_enabled=true;else{avoid_enabled=SendMessageW(checks[0],BM_GETCHECK,0,0)==BST_CHECKED;fullscreen_enabled=SendMessageW(checks[1],BM_GETCHECK,0,0)==BST_CHECKED;snap_enabled=SendMessageW(checks[2],BM_GETCHECK,0,0)==BST_CHECKED;}
  if(!avoid_enabled&&avoidance.moved){memset(&avoidance,0,sizeof(avoidance));move_to((DeskPoint){settings.x,settings.y});}last_tick=0;
  lstrcpyW(status_text,save_config()?L"设置已应用。避让不改变常驻位置，鼠标离开后约 3 秒恢复。":L"设置已在本次运行生效，但保存失败，请检查存档目录。" );fill_controls();return 0;
 }break;
 case WM_ACTIVATE:if(initialized){bool editing=LOWORD(wp)!=WA_INACTIVE;if(editing&&!configuring){configuring=true;unregister_set(registered);}else if(!editing&&configuring){configuring=false;if(!test_mode&&!register_set(keys,registered)){lstrcpyW(status_text,L"部分快捷键当前被占用，请重新设置。托盘菜单仍可使用。");if(status_label)SetWindowTextW(status_label,status_text);}}}return 0;
 case WM_CLOSE:ShowWindow(w,SW_HIDE);return 0;
 case WM_DESTROY:panel=NULL;return 0;
 }return DefWindowProcW(w,message,wp,lp);
}
void desktop_show(void){
 if(!owner)return;if(!panel){WNDCLASSW c={0};c.lpfnWndProc=desktop_proc;c.hInstance=pet_instance;c.lpszClassName=L"LetterPetC.DesktopPanel";c.hCursor=LoadCursor(NULL,IDC_ARROW);c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&c);DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;RECT r={0,0,px(592),px(590)};AdjustWindowRect(&r,style,FALSE);MONITORINFO info={sizeof(info)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&info);panel=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,c.lpszClassName,L"桌宠 · 桌面行为与快捷键",style,info.rcWork.left+max(0,(info.rcWork.right-info.rcWork.left-r.right+r.left)/2),info.rcWork.top+max(0,(info.rcWork.bottom-info.rcWork.top-r.bottom+r.top)/2),r.right-r.left,r.bottom-r.top,NULL,NULL,pet_instance,NULL);}
 if(panel){ui_theme_apply(panel);ui_theme_role(status_label,UI_SURFACE_TEXT);fill_controls();pet_show_panel(panel,SW_SHOWNORMAL);}
}
void desktop_init(HWND window,const wchar_t *settings_path){
 owner=window;initialized=true;shown=false;configuring=false;last_tick=0;memset(&avoidance,0,sizeof(avoidance));memset(&visibility,0,sizeof(visibility));path[0]=0;status_text[0]=0;HDC dc=GetDC(owner);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(owner,dc);font=ui_theme_font(px(15),FW_NORMAL,false);
 if(settings_path&&wcslen(settings_path)<32700){lstrcpynW(path,settings_path,32768);wchar_t *slash=wcsrchr(path,L'\\');if(slash)wcscpy(slash+1,L"desktop.ini");else path[0]=0;}
 load_config();if(!test_mode&&!register_set(keys,registered))lstrcpyW(status_text,L"部分快捷键已被其他程序占用，请在这里重新设置。托盘菜单仍可使用。");
 if(!test_mode)desktop_display_changed();
}
void desktop_shutdown(void){if(!owner)return;initialized=false;configuring=false;unregister_set(registered);if(panel)DestroyWindow(panel);DeleteObject(font);font=NULL;owner=NULL;}
bool desktop_dialog_message(MSG *m){return panel&&IsWindowVisible(panel)&&IsDialogMessageW(panel,m);}
static bool registration_selftest(void){
 HWND blocker=CreateWindowW(L"STATIC",L"",WS_POPUP,0,0,1,1,NULL,NULL,pet_instance,NULL);if(!blocker)return false;
 unsigned owned=0,blocked=0,free_key=0;DeskHotkey saved[DESK_KEYS];memcpy(saved,keys,sizeof(saved));bool was_test=test_mode;
 for(unsigned v=VK_F13;v<=VK_F24;v++)if(RegisterHotKey(owner,HOTKEY_BASE,7|MOD_NOREPEAT,v)){owned=v;break;}
 if(owned)for(unsigned v=VK_F13;v<=VK_F24;v++)if(v!=owned&&RegisterHotKey(blocker,1400,7|MOD_NOREPEAT,v)){blocked=v;break;}
 if(blocked)for(unsigned v=VK_F13;v<=VK_F24;v++)if(v!=owned&&v!=blocked&&RegisterHotKey(blocker,1403,7|MOD_NOREPEAT,v)){free_key=v;UnregisterHotKey(blocker,1403);break;}
 bool ok=owned&&blocked&&free_key;
 if(ok){memset(keys,0,sizeof(keys));keys[0]=(DeskHotkey){7,owned};registered[0]=true;test_mode=false;DeskHotkey candidate[DESK_KEYS]={{7,blocked},{0,0},{0,0},{0,0}};
  ok=!apply_keys(candidate)&&keys[0].key==owned&&registered[0]&&!RegisterHotKey(blocker,1401,7|MOD_NOREPEAT,owned);
  candidate[0].key=free_key;ok=apply_keys(candidate)&&ok&&keys[0].key==free_key&&registered[0];
  bool released=RegisterHotKey(blocker,1402,7|MOD_NOREPEAT,owned)!=0;ok=ok&&released;if(released)UnregisterHotKey(blocker,1402);
  unregister_set(registered);
 }else if(owned)UnregisterHotKey(owner,HOTKEY_BASE);
 test_mode=was_test;memcpy(keys,saved,sizeof(saved));if(blocked)UnregisterHotKey(blocker,1400);UnregisterHotKey(blocker,1401);DestroyWindow(blocker);return ok;
}
bool desktop_ui_selftest(FILE *log){
 desktop_init(pet_window,NULL);desktop_show();bool ok=panel&&checks[0]&&edit_keys[0]&&read_hotkey(edit_keys[0]).key=='H';
 if(ok){SendMessageW(checks[0],BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(panel,WM_COMMAND,ID_APPLY,0);ok=!avoid_enabled;SendMessageW(panel,WM_COMMAND,ID_DEFAULTS,0);ok=ok&&avoid_enabled&&snap_enabled&&fullscreen_enabled;}
 bool registration=registration_selftest();
 HMONITOR monitor=MonitorFromWindow(pet_window,MONITOR_DEFAULTTONEAREST);
 MONITORINFO area={sizeof(area)};
 bool geometry=GetMonitorInfoW(monitor,&area)!=0;
 if(geometry){
  RECT r=area.rcMonitor;
  HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"",WS_POPUP,
   r.left,r.top,r.right-r.left,r.bottom-r.top,NULL,NULL,pet_instance,NULL);
  HWND maximized=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"",WS_OVERLAPPEDWINDOW,
   r.left+20,r.top+20,320,240,NULL,NULL,pet_instance,NULL);
  geometry=window&&maximized&&fullscreen_bounds(window,monitor);
  if(maximized){
   ShowWindow(maximized,SW_SHOWMAXIMIZED);
   if(!EqualRect(&area.rcMonitor,&area.rcWork))geometry=geometry&&IsZoomed(maximized)&&!fullscreen_bounds(maximized,monitor);
   DestroyWindow(maximized);
  }
  if(window)DestroyWindow(window);
 }
 bool topmost=settings.topmost;
 settings.topmost=true;
 bool priority=desktop_should_show(NULL,true,false)&&!desktop_should_show(NULL,false,false);
 settings.topmost=topmost;
 fprintf(log,"%s: fullscreen window bounds and visibility priority.\n",geometry&&priority?"PASS":"FAIL");
 swprintf(path,32768,L"%ls\\desktop-storage-test.ini",app_dir);test_storage=true;avoid_enabled=false;positions[0]=(MonitorPosition){L"\\\\.\\DISPLAY9",42,73};position_count=1;lstrcpyW(last_device,positions[0].device);keys[0]=(DeskHotkey){MOD_CONTROL|MOD_ALT,VK_F15};bool stored=save_config();load_config();stored=stored&&!avoid_enabled&&keys[0].key==VK_F15&&position_count==1&&positions[0].x==42&&positions[0].y==73&&!wcscmp(last_device,L"\\\\.\\DISPLAY9");test_storage=false;
 fprintf(log,"%s: native behavior/hotkey controls and defaults\n",ok?"PASS":"FAIL");fprintf(log,"%s: real system hotkey conflict preserves previous configuration\n",registration?"PASS":"FAIL");fprintf(log,"%s: desktop preferences, hotkeys and per-monitor position persist\n",stored?"PASS":"FAIL");desktop_shutdown();return ok&&registration&&stored&&geometry&&priority;
}
