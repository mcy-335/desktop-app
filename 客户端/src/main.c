#include "pet.h"
#include "ui_theme.h"
#include "appearance.h"
#include "account.h"
#include "resource.h"
#include <shellapi.h>
#include <commctrl.h>
#include <wtsapi32.h>
#include "companion.h"
#include "study.h"
#include "extras.h"
#include "backup_io.h"
#include "desktop.h"
#include "focus_apps.h"
#include "rewards.h"
#include "dialogue.h"
#include "music.h"
#include "music_core.h"
#include "timetable.h"
#include "timetable_core.h"
HWND pet_window;HINSTANCE pet_instance;Settings settings={480,0,0,true,false,false,false,true};Model model;wchar_t app_dir[MAX_PATH];bool test_mode=false;
static wchar_t config_path[32768];static HANDLE instance_mutex;static NOTIFYICONDATAW tray;static UINT taskbar_created;static bool input_ok,dragging,moved,dirty=true;static POINT drag_cursor,drag_origin;static double blink_at,blink_until,expression_at,expression_until,pulses[3];static int expression;static double last_frame_ms,last_topmost_check;static unsigned long rendered_frames;
static bool preview_mode=false;static bool visible=true;static HMENU active_menu;static HFONT menu_font;
static HWND menu_window;
#ifdef PET_ARM_CANDIDATE
#define PET_MUTEX L"Local\\LetterPetC.ArmCandidate.v2"
#define PET_CLASS L"LetterPetC.ArmCandidate.Window"
#define PET_TITLE L"桌宠 · Live2D 手臂测试版"
#else
#define PET_MUTEX L"Local\\LetterPetC.Native.v1"
#define PET_CLASS L"LetterPetC.Window"
#define PET_TITLE L"桌宠"
#endif
enum {STATE_TIMER=1, INPUT_FRAME_TIMER=2, IDLE_FRAME_MS=33, INPUT_FRAME_MS=16, INPUT_BURST_MS=400};
#define WM_INPUT_FRAME (WM_APP+5)
static double last_frame_at,input_frames_until;
static bool input_frame_posted,input_timer_running;
static const wchar_t *session_exit_notice;

HICON pet_app_icon(void){return LoadIconW(pet_instance,MAKEINTRESOURCEW(IDI_LETTERPET));}

static void request_input_frame(double now){
 if(!IsWindowVisible(pet_window)||session_exit_notice)return;
 input_frames_until=now+INPUT_BURST_MS;
 if(!input_timer_running)input_timer_running=SetTimer(pet_window,INPUT_FRAME_TIMER,INPUT_FRAME_MS,NULL)!=0;
 if(!input_frame_posted&&(!last_frame_at||now-last_frame_at>=INPUT_FRAME_MS-2))
  input_frame_posted=PostMessageW(pet_window,WM_INPUT_FRAME,0,0)!=0;
}
static void input_key_event(UINT vk,bool down,double t,bool immediate){
 bool changed=model_key(&model,vk,down,t);
 live2d_key(vk,down);
 if(changed&&immediate)request_input_frame(t);
}
static void input_mouse_event(int b,bool down,double t,bool immediate){
 if(!model_mouse(&model,b,down,t))return;
 live2d_mouse(b,down);
 if(immediate)request_input_frame(t);
}
#define WM_TRAY (WM_APP+1)
#define WM_WAKE (WM_APP+2)
#define WM_ACCOUNT_END (WM_APP+4)

static void save_settings(void){if(test_mode)return;wchar_t buf[40];const wchar_t *names[]={L"size",L"x",L"y",L"topmost",L"clickthrough",L"game",L"live2d"};int values[]={settings.size,settings.x,settings.y,settings.topmost,settings.clickthrough,settings.game,settings.live2d};for(int i=0;i<7;i++){swprintf(buf,40,L"%d",values[i]);WritePrivateProfileStringW(L"pet",names[i],buf,config_path);}}
static void default_position(void){RECT area;SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);settings.x=area.left+24;settings.y=area.bottom-settings.size-24;}
static bool startup_enabled(void){HKEY key;bool yes=false;if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_READ,&key)==ERROR_SUCCESS){wchar_t value[MAX_PATH+4];DWORD n=sizeof(value),type;if(RegQueryValueExW(key,L"LetterPetC",NULL,&type,(BYTE*)value,&n)==ERROR_SUCCESS){wchar_t expected[MAX_PATH+4],exe[MAX_PATH];GetModuleFileNameW(NULL,exe,MAX_PATH);swprintf(expected,MAX_PATH+4,L"\"%ls\"",exe);yes=type==REG_SZ&&!wcscmp(value,expected);}RegCloseKey(key);}return yes;}
static void load_settings(void){wchar_t dir[32768];
#ifdef PET_ARM_CANDIDATE
 lstrcpyW(dir,app_dir);
#else
DWORD dir_len=GetEnvironmentVariableW(L"LOCALAPPDATA",dir,32768);if(!dir_len||dir_len>=32740)lstrcpyW(dir,app_dir);wcscat(dir,L"\\LetterPetC");
#endif
CreateDirectoryW(dir,NULL);swprintf(config_path,32768,L"%ls\\settings.ini",dir);settings.size=GetPrivateProfileIntW(L"pet",L"size",480,config_path);if(settings.size!=240&&settings.size!=360&&settings.size!=480&&settings.size!=600&&settings.size!=720)settings.size=480;default_position();settings.x=(int)GetPrivateProfileIntW(L"pet",L"x",settings.x,config_path);settings.y=(int)GetPrivateProfileIntW(L"pet",L"y",settings.y,config_path);RECT r={settings.x,settings.y,settings.x+settings.size,settings.y+settings.size};if(!MonitorFromRect(&r,MONITOR_DEFAULTTONULL))default_position();settings.topmost=GetPrivateProfileIntW(L"pet",L"topmost",1,config_path)!=0;settings.clickthrough=GetPrivateProfileIntW(L"pet",L"clickthrough",0,config_path)!=0;settings.game=GetPrivateProfileIntW(L"pet",L"game",0,config_path)!=0;settings.autostart=startup_enabled();settings.live2d=GetPrivateProfileIntW(L"pet",L"live2d",1,config_path)!=0;}
static bool wants_topmost(void){return settings.topmost||settings.game;}
static void place_pet(HWND order){SetWindowPos(pet_window,order,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);}
static bool window_above(HWND upper,HWND lower){
 for(HWND window=GetWindow(lower,GW_HWNDPREV);window;window=GetWindow(window,GW_HWNDPREV))if(window==upper)return true;
 return false;
}
static bool external_window_covers_pet(HWND foreground){
 DWORD pid=0;
 if(foreground)GetWindowThreadProcessId(foreground,&pid);
 if(!pid||pid==GetCurrentProcessId()||!window_above(foreground,pet_window))return false;
 RECT pet,bounds,overlap;
 return GetWindowRect(pet_window,&pet)&&GetWindowRect(foreground,&bounds)&&IntersectRect(&overlap,&pet,&bounds);
}
HWND pet_foreground_panel(HWND foreground){
 if(!foreground||foreground==pet_window||!IsWindowVisible(foreground)||
    GetWindowThreadProcessId(foreground,NULL)!=GetCurrentThreadId())return NULL;
 // Keep both a modal dialog and its parent panel above the pet.
 HWND panel=GetAncestor(foreground,GA_ROOTOWNER);
 return panel&&panel!=pet_window&&IsWindowVisible(panel)?panel:foreground;
}
static void sync_pet_order(HWND foreground,bool check_cover){
 if(!pet_window||active_menu)return;
 HWND panel=pet_foreground_panel(foreground);
 bool topmost=wants_topmost()&&!panel;
 bool current=(GetWindowLongPtrW(pet_window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
 if(topmost!=current||(topmost&&check_cover&&external_window_covers_pet(foreground)))
  place_pet(topmost?HWND_TOPMOST:HWND_NOTOPMOST);
 if(panel&&!(GetWindowLongPtrW(panel,GWL_EXSTYLE)&WS_EX_TOPMOST)&&window_above(pet_window,panel))place_pet(panel);
}
void pet_yield_to_panel(HWND panel){
 sync_pet_order(panel,false);
}
void pet_show_panel(HWND panel,int show){
 // Yield before activation, including when Windows denies a foreground request.
 if(pet_window&&IsWindow(pet_window)&&!active_menu&&
    (GetWindowLongPtrW(pet_window,GWL_EXSTYLE)&WS_EX_TOPMOST))place_pet(HWND_NOTOPMOST);
 ShowWindow(panel,show);
 SetForegroundWindow(panel);
 pet_yield_to_panel(panel);
}
static void behavior(void){
 LONG_PTR previous=GetWindowLongPtrW(pet_window,GWL_EXSTYLE),style=previous;
 if(settings.clickthrough||settings.game)style|=WS_EX_TRANSPARENT;else style&=~WS_EX_TRANSPARENT;
 if(previous!=style)SetWindowLongPtrW(pet_window,GWL_EXSTYLE,style);
 sync_pet_order(GetForegroundWindow(),false);
}
static void show_pet_menu_window(HWND window){
 if(!active_menu||!window||menu_window==window)return;
 menu_window=window;
 SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
}
static void lower_pet_for_menu(void){
 if(GetWindowLongPtrW(pet_window,GWL_EXSTYLE)&WS_EX_TOPMOST)place_pet(HWND_NOTOPMOST);
}
static void set_scale(int percent){int n=480*percent/100;if(n==settings.size)return;int x=settings.x+settings.size-n,y=settings.y+settings.size-n;if(!renderer_resize(n)){MessageBoxW(pet_window,L"无法分配缩放画面内存。",L"桌宠",MB_ICONERROR);PostQuitMessage(1);return;}settings.size=n;settings.x=x;settings.y=y;RECT r={x,y,x+n,y+n};MONITORINFO mi={sizeof(mi)};GetMonitorInfoW(MonitorFromRect(&r,MONITOR_DEFAULTTONEAREST),&mi);settings.x=(int)clampd(x,mi.rcWork.left,fmax(mi.rcWork.left,mi.rcWork.right-n));settings.y=(int)clampd(y,mi.rcWork.top,fmax(mi.rcWork.top,mi.rcWork.bottom-n));SetWindowPos(pet_window,NULL,settings.x,settings.y,n,n,SWP_NOZORDER|SWP_NOACTIVATE);desktop_position_changed();dirty=true;save_settings();}
static void set_startup(void){
#ifdef PET_ARM_CANDIDATE
 MessageBoxW(pet_window,L"独立测试版不修改开机启动设置。",L"手臂接入测试版",MB_OK);return;
#endif
 HKEY k;LONG result=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,NULL,0,KEY_SET_VALUE,NULL,&k,NULL);if(result==ERROR_SUCCESS){if(settings.autostart)result=RegDeleteValueW(k,L"LetterPetC");else{wchar_t exe[MAX_PATH],value[MAX_PATH+4];GetModuleFileNameW(NULL,exe,MAX_PATH);swprintf(value,MAX_PATH+4,L"\"%ls\"",exe);result=RegSetValueExW(k,L"LetterPetC",0,REG_SZ,(BYTE*)value,(DWORD)((wcslen(value)+1)*sizeof(wchar_t)));}RegCloseKey(k);}if(result==ERROR_SUCCESS||result==ERROR_FILE_NOT_FOUND)settings.autostart=startup_enabled();else MessageBoxW(pet_window,L"无法更改开机启动设置。",L"桌宠",MB_ICONWARNING);}
void pet_apply_scene_window(bool top,bool pass,bool game){settings.topmost=top;settings.clickthrough=pass;settings.game=game;behavior();save_settings();dirty=true;}
void pet_desktop_position_saved(void){save_settings();dirty=true;}
void pet_refresh_appearance(void){dirty=true;live2d_reset_clock();}
void pet_switch_profile(const wchar_t *path){companion_shutdown();timetable_shutdown();companion_init(pet_window,path);timetable_init(pet_window,path);}
bool pet_flush_profile(void){companion_save();return companion_storage_ready()&&timetable_flush();}
void pet_end_session(const wchar_t *notice){
 session_exit_notice=notice;
 PostMessageW(pet_window,WM_ACCOUNT_END,0,0);
}
void pet_desktop_action(int action){switch(action){case 0:pet_command(CMD_SHOW);break;case 1:pet_command(CMD_PASS);break;case 2:companion_toggle_focus();break;case 3:pet_command(CMD_GAME);break;}}
void pet_command(UINT id){if(id==CMD_TOP||id==CMD_PASS||id==CMD_GAME)extras_manual_scene();switch(id){case CMD_SHOW:visible=!desktop_is_visible();live2d_reset_clock();desktop_user_visibility(visible);dirty=true;break;case CMD_TOP:settings.topmost=!settings.topmost;behavior();break;case CMD_PASS:settings.clickthrough=!settings.clickthrough;if(settings.game)settings.game=false;behavior();break;case CMD_GAME:settings.game=!settings.game;behavior();break;case CMD_STARTUP:set_startup();break;case CMD_RESET:default_position();desktop_position_changed();SetWindowPos(pet_window,NULL,settings.x,settings.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);break;case CMD_TIMETABLE:timetable_show();break;case CMD_MUSIC:music_show();break;case CMD_DIALOGUE:dialogue_show();break;case CMD_DESKTOP:desktop_show();break;case CMD_EXTRAS:companion_extras_show();break;case CMD_STUDY:companion_study_show();break;case CMD_COMPANION:companion_show();break;case CMD_MONITOR:monitor_show();break;case CMD_APPEARANCE:appearance_show();break;case CMD_ACCOUNT:account_show();break;case CMD_LIVE2D:{if(appearance_active()){if(!appearance_restore())MessageBoxW(pet_window,L"无法保存原版设置。",L"外观 DIY",MB_ICONWARNING);break;}bool was_live2d=live2d_active();if(!live2d_available()){live2d_init(settings.size,false);if(!live2d_available()){MessageBoxW(pet_window,live2d_error(),L"Live2D 未就绪，保留原版互动模式",MB_ICONWARNING);break;}}settings.live2d=!was_live2d;live2d_reset_clock();break;}case CMD_QUIT:DestroyWindow(pet_window);return;default:if(id>=CMD_SCALE+50&&id<=CMD_SCALE+150)set_scale(id-CMD_SCALE);break;}save_settings();companion_save();dirty=true;}
typedef struct {UINT id;const wchar_t *text;bool checked;} MenuItem;
static MenuItem items[24];
void pet_menu(POINT point){
 if(active_menu)return;
 MenuItem menu[]={ {CMD_SHOW,desktop_is_visible()?L"隐藏桌宠":L"显示桌宠",false},{CMD_COMPANION,L"专注与养成",false},{CMD_STUDY,L"任务与学习统计",false},{CMD_EXTRAS,L"场景 成长与备份",false},{CMD_DESKTOP,L"桌面行为与快捷键",false},{CMD_DIALOGUE,L"角色台词",false},{CMD_MUSIC,L"本地音乐",false},{CMD_TIMETABLE,L"每周课程表",false},{CMD_MONITOR,L"查看进程 / CPU 占用",false},{CMD_APPEARANCE,L"外观 DIY",false},{CMD_ACCOUNT,L"我的账号",false},{CMD_LIVE2D,appearance_active()?L"恢复原版桌宠":live2d_active()?L"切换为原版键鼠互动":L"切换为 Live2D 手臂互动",!appearance_active()&&live2d_active()},{CMD_TOP,L"窗口置顶",settings.topmost},{CMD_PASS,L"鼠标穿透",settings.clickthrough},{CMD_GAME,L"游戏模式",settings.game},{CMD_STARTUP,L"开机启动",settings.autostart},{CMD_SCALE+50,L"大小 50%",settings.size==240},{CMD_SCALE+75,L"大小 75%",settings.size==360},{CMD_SCALE+100,L"大小 100%",settings.size==480},{CMD_SCALE+125,L"大小 125%",settings.size==600},{CMD_SCALE+150,L"大小 150%",settings.size==720},{CMD_RESET,L"重置位置",false},{CMD_QUIT,L"退出桌宠",false}};
 memcpy(items,menu,sizeof(menu));active_menu=CreatePopupMenu();if(!active_menu)return;
 for(size_t i=0;i<ARRAYSIZE(menu);i++)AppendMenuW(active_menu,MF_OWNERDRAW,items[i].id,(LPCWSTR)&items[i]);
 lower_pet_for_menu();
 SetForegroundWindow(pet_window);
 UINT cmd=TrackPopupMenu(active_menu,TPM_RETURNCMD|TPM_RIGHTBUTTON|TPM_LEFTALIGN,point.x,point.y,0,pet_window,NULL);
 DestroyMenu(active_menu);active_menu=NULL;menu_window=NULL;
 PostMessageW(pet_window,WM_NULL,0,0);
 if(cmd)pet_command(cmd);
 if(IsWindow(pet_window))behavior();
}
static void add_tray(void){memset(&tray,0,sizeof(tray));tray.cbSize=sizeof(tray);tray.hWnd=pet_window;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=WM_TRAY;tray.hIcon=pet_app_icon();lstrcpyW(tray.szTip,L"桌宠（右键打开菜单）");Shell_NotifyIconW(NIM_ADD,&tray);}
static void raw_input(HRAWINPUT handle){
 RAWINPUT input;
 UINT size=sizeof(input);
 if(GetRawInputData(handle,RID_INPUT,&input,&size,sizeof(input.header))==(UINT)-1)return;
 if(input.header.dwType==RIM_TYPEKEYBOARD){
  RAWKEYBOARD key=input.data.keyboard;
  UINT vk=key.VKey;
  bool down=!(key.Flags&RI_KEY_BREAK),ext=(key.Flags&RI_KEY_E0)!=0;
  if(vk==VK_SHIFT)vk=key.MakeCode==0x36?VK_RSHIFT:VK_LSHIFT;
  if(vk==VK_CONTROL)vk=ext?VK_RCONTROL:VK_LCONTROL;
  if(vk==VK_MENU)vk=ext?VK_RMENU:VK_LMENU;
  bool numpad=(vk==VK_RETURN&&ext)||(!ext&&(vk==VK_INSERT||vk==VK_DELETE||vk==VK_HOME||vk==VK_END||
                                          vk==VK_NEXT||vk==VK_PRIOR||(vk>=VK_LEFT&&vk<=VK_DOWN)));
  if(numpad||vk==255)return;
  double now=now_ms();
  if(vk==VK_SNAPSHOT&&!down)input_key_event(vk,true,now,true);
  input_key_event(vk,down,now,true);
 }else if(input.header.dwType==RIM_TYPEMOUSE){
  USHORT flags=input.data.mouse.usButtonFlags;
  const USHORT downs[]={RI_MOUSE_LEFT_BUTTON_DOWN,RI_MOUSE_RIGHT_BUTTON_DOWN,RI_MOUSE_MIDDLE_BUTTON_DOWN};
  const USHORT ups[]={RI_MOUSE_LEFT_BUTTON_UP,RI_MOUSE_RIGHT_BUTTON_UP,RI_MOUSE_MIDDLE_BUTTON_UP};
  if(!(flags&(downs[0]|downs[1]|downs[2]|ups[0]|ups[1]|ups[2])))return;
  double now=now_ms();
  for(int i=0;i<3;i++){
   if(flags&downs[i])input_mouse_event(i,true,now,true);
   if(flags&ups[i])input_mouse_event(i,false,now,true);
  }
 }
}
static void animate_frame(double now){
 if(session_exit_notice||!IsWindowVisible(pet_window))return;
 // Both timer and input messages can request this frame.
 if(last_frame_at&&now-last_frame_at<INPUT_FRAME_MS-2)return;
 dirty|=model_tick(&model,now);
 dirty|=appearance_active()?appearance_animate(now):live2d_active();
 dirty|=rewards_animate();
 if(now>=blink_at){blink_until=now+135;blink_at=now+3000+rand()%3500;dirty=true;}
 if(blink_until&&now>=blink_until){blink_until=0;dirty=true;}
 if(now>=expression_at){expression=2+rand()%6;expression_until=now+1800;expression_at=now+9000+rand()%14000;dirty=true;}
 if(expression_until&&now>=expression_until){expression_until=0;expression=0;dirty=true;}
 for(int i=0;i<3;i++)if(pulses[i]&&now-pulses[i]<900)dirty=true;
 if(!dirty)return;
 last_frame_at=now;
 renderer_draw(blink_until?1:expression,now,pulses);
 renderer_present(pet_window);
 last_frame_ms=now_ms()-now;rendered_frames++;dirty=false;
}
static void tick(void){
 account_poll();
 if(session_exit_notice)return;
 double now=now_ms();
 bool menu_open=active_menu!=NULL;
 bool effective=desktop_tick(now,visible,dragging,menu_open);
 companion_tick(now,effective,menu_open);rewards_tick();timetable_tick(now);
 if(!input_ok){
  for(int i=0;i<model.count;i++){
   bool held=(GetAsyncKeyState(model.keys[i].vk)&0x8000)!=0;
   if(held!=model.keys[i].held)input_key_event(model.keys[i].vk,held,now,false);
  }
  const UINT buttons[]={VK_LBUTTON,VK_RBUTTON,VK_MBUTTON};
  for(int i=0;i<3;i++){
   bool held=(GetAsyncKeyState(buttons[i])&0x8000)!=0;
   if(held!=model.mouse[i].held)input_mouse_event(i,held,now,false);
  }
 }
 bool check_cover=now-last_topmost_check>=250;
 sync_pet_order(GetForegroundWindow(),check_cover);
 if(check_cover)last_topmost_check=now;
 if(effective)animate_frame(now_ms());
 else{model_tick(&model,now);live2d_reset_clock();live2d_clear_input();dirty=true;}
}
static LRESULT CALLBACK window_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
 if(msg==taskbar_created&&taskbar_created){add_tray();return 0;}
 switch(msg){
 case WM_POWERBROADCAST:if(wp==PBT_APMSUSPEND){model_init(&model);live2d_clear_input();companion_suspend();music_suspend();timetable_power(true);}else if(wp==PBT_APMRESUMEAUTOMATIC||wp==PBT_APMRESUMESUSPEND)timetable_power(false);return TRUE;
 case WM_WTSSESSION_CHANGE:if(wp==WTS_SESSION_LOCK){model_init(&model);live2d_clear_input();companion_suspend();music_suspend();timetable_session(true);}else if(wp==WTS_SESSION_UNLOCK)timetable_session(false);return 0;
 case WM_RESOURCE_SAMPLE:companion_resource_message(lp);return 0;
 case WM_INPUT:raw_input((HRAWINPUT)lp);return DefWindowProcW(w,msg,wp,lp);
 case WM_INPUT_FRAME:input_frame_posted=false;animate_frame(now_ms());return 0;
 case WM_TIMER:
  if(wp==STATE_TIMER)tick();
  else if(wp==INPUT_FRAME_TIMER){
   double now=now_ms();
   if(now>=input_frames_until||!IsWindowVisible(w)){KillTimer(w,INPUT_FRAME_TIMER);input_timer_running=false;}
   else animate_frame(now);
  }
  return 0;
 case WM_HOTKEY:desktop_hotkey(wp);return 0;
 case WM_APP+3:MessageBoxW(w,live2d_error(),L"Live2D 渲染异常，已恢复原版互动",MB_ICONWARNING);return 0;
 case WM_ACCOUNT_END:if(session_exit_notice)DestroyWindow(w);return 0;
 case WM_WAKE:if(wp==10){account_show();return 0;}if(wp==9){appearance_show();return 0;}if(wp==8){timetable_show();return 0;}if(wp==7){music_show();return 0;}if(wp==6){dialogue_show();return 0;}if(wp==5){desktop_show();return 0;}if(wp==4){companion_extras_show();return 0;}if(wp==3){companion_study_show();return 0;}if(wp==2){companion_show();return 0;}if(wp==1){monitor_show();return 0;}visible=true;live2d_reset_clock();desktop_user_visibility(true);dirty=true;return 0;
 case WM_TRAY:if(lp==WM_RBUTTONUP){POINT p;GetCursorPos(&p);pet_menu(p);}else if(lp==WM_LBUTTONUP)pet_command(CMD_SHOW);return 0;
 case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
 case WM_NCHITTEST:{POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&p);return settings.clickthrough||settings.game||!renderer_hit(p.x,p.y)?HTTRANSPARENT:HTCLIENT;}
 case WM_LBUTTONDOWN:desktop_drag_begin();GetCursorPos(&drag_cursor);{RECT r;GetWindowRect(w,&r);drag_origin=(POINT){r.left,r.top};}dragging=true;moved=false;SetCapture(w);return 0;
 case WM_MOUSEMOVE:if(dragging){POINT p;GetCursorPos(&p);int dx=p.x-drag_cursor.x,dy=p.y-drag_cursor.y;if(abs(dx)+abs(dy)>4)moved=true;if(moved){settings.x=drag_origin.x+dx;settings.y=drag_origin.y+dy;SetWindowPos(w,NULL,settings.x,settings.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);}}return 0;
 case WM_LBUTTONUP:if(dragging){dragging=false;ReleaseCapture();if(moved)desktop_drag_end();save_settings();if(!moved){companion_pet();if(appearance_active()){appearance_tap(now_ms());dirty=true;return 0;}if(live2d_active()){live2d_click(now_ms());dirty=true;return 0;}int i=collectible_hit(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));if(i>=0)pulses[i]=now_ms();else{expression=2;expression_until=now_ms()+1600;}dirty=true;}}return 0;
 case WM_CAPTURECHANGED:if(dragging&&moved)desktop_drag_end();dragging=false;return 0;
 case WM_RBUTTONUP:{POINT p;GetCursorPos(&p);pet_menu(p);return 0;}
 case WM_ENTERIDLE:
  if(wp==MSGF_MENU&&active_menu){
   show_pet_menu_window((HWND)lp);
   return 0;
  }
  break;
 case WM_MEASUREITEM:{MEASUREITEMSTRUCT *mi=(void*)lp;if(mi->CtlType==ODT_MENU){mi->itemWidth=260;mi->itemHeight=30;return TRUE;}break;}
 case WM_DRAWITEM:{
  DRAWITEMSTRUCT *di=(void*)lp;
  if(di->CtlType!=ODT_MENU)break;
  MenuItem *item=(void*)di->itemData;
  if(!item)break;
  bool selected=(di->itemState&ODS_SELECTED)!=0;
  RECT r=di->rcItem;ui_theme_fill(di->hDC,&r,selected?UI_WINE:UI_SURFACE);
  COLORREF label=selected?ui_colors->on_primary:UI_INK;
  if(selected){RECT mark={r.left,r.top+3,r.left+3,r.bottom-3};ui_theme_fill(di->hDC,&mark,UI_GOLD==UI_WINE?label:UI_GOLD);}
  SetBkMode(di->hDC,TRANSPARENT);
  HFONT old=SelectObject(di->hDC,menu_font);
  if(item->checked){
   RECT check={r.left+9,r.top,r.left+32,r.bottom};SetTextColor(di->hDC,selected?label:UI_GOLD);
   DrawTextW(di->hDC,L"\u2713",1,&check,DT_SINGLELINE|DT_VCENTER|DT_CENTER);
  }
  r.left+=37;r.right-=10;SetTextColor(di->hDC,label);
  DrawTextW(di->hDC,item->text,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
  SelectObject(di->hDC,old);return TRUE;
 }
 case WM_DISPLAYCHANGE:desktop_display_changed();return 0;
 case WM_SETTINGCHANGE:if(wp==SPI_SETWORKAREA)desktop_display_changed();return 0;
 case WM_DPICHANGED:dirty=true;return 0;
 case WM_CLOSE:visible=false;desktop_user_visibility(false);return 0;
 case WM_DESTROY:rewards_shutdown();account_close();appearance_close();timetable_shutdown();music_shutdown();focus_apps_shutdown();desktop_shutdown();companion_shutdown();KillTimer(w,STATE_TIMER);KillTimer(w,INPUT_FRAME_TIMER);save_settings();Shell_NotifyIconW(NIM_DELETE,&tray);PostQuitMessage(0);return 0;
 }return DefWindowProcW(w,msg,wp,lp);
}
static bool input_frame_selftest(FILE *log){
 ShowWindow(pet_window,SW_SHOWNOACTIVATE);
 const double now=now_ms();
 const unsigned long before=rendered_frames;
 input_key_event('A',true,now,true);
 input_key_event('A',false,now,true);
 input_mouse_event(0,true,now,true);
 input_mouse_event(0,false,now,true);
 MSG message;
 bool posted=PeekMessageW(&message,pet_window,WM_INPUT_FRAME,WM_INPUT_FRAME,PM_REMOVE)!=0;
 if(posted)DispatchMessageW(&message);
 bool coalesced=!PeekMessageW(&message,pet_window,WM_INPUT_FRAME,WM_INPUT_FRAME,PM_REMOVE);
 bool painted=rendered_frames==before+1&&!input_frame_posted&&model.mouse[0].amount>.6;
 bool burst=input_timer_running&&input_frames_until>now;
 KillTimer(pet_window,INPUT_FRAME_TIMER);input_timer_running=false;
 live2d_clear_input();model_init(&model);
 ShowWindow(pet_window,SW_HIDE);
 bool ok=posted&&coalesced&&painted&&burst;
 fprintf(log,"%s: raw-input frame requests coalesce, present before the state timer, and start the high-rate burst.\n",ok?"PASS":"FAIL");
 return ok;
}
static int run_integration_test(void){
 wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\integration-test.log",app_dir);FILE *log=_wfopen(path,L"w");if(!log)return 2;
 bool ok=model_selftest(log);ok=live2d_input_selftest(log)&&ok;
 ok=input_frame_selftest(log)&&ok;int original=settings.size;
 for(int size=50;size<=150;size+=25){set_scale(size);RECT r;GetWindowRect(pet_window,&r);ok=ok&&r.right-r.left==settings.size&&r.bottom-r.top==settings.size;}
 int x=settings.x,y=settings.y;settings.x+=19;settings.y+=13;SetWindowPos(pet_window,NULL,settings.x,settings.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);RECT r;GetWindowRect(pet_window,&r);ok=ok&&r.left==x+19&&r.top==y+13;settings.x=x;settings.y=y;SetWindowPos(pet_window,NULL,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
 fprintf(log,"%s: host window resizing and position translation; no mouse injection or screenshots.\n",ok?"PASS":"FAIL");fprintf(log,"OVERALL: %s\n",ok?"PASS":"FAIL");fclose(log);(void)original;return ok?0:1;
}
static bool window_order_selftest(FILE *log){
 HWND popup=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"",WS_POPUP,0,0,120,80,NULL,NULL,pet_instance,NULL);
 if(!popup){fprintf(log,"FAIL: menu stacking fixture could not be created.\n");return false;}
 bool saved=settings.topmost;
 bool shown=IsWindowVisible(pet_window)!=0;
 ShowWindow(pet_window,SW_SHOWNOACTIVATE);
 ShowWindow(popup,SW_SHOWNOACTIVATE);
 settings.topmost=true;
 behavior();
 bool initial=(GetWindowLongPtrW(pet_window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
 active_menu=CreatePopupMenu();
 lower_pet_for_menu();
 behavior();
 window_proc(pet_window,WM_ENTERIDLE,MSGF_MENU,(LPARAM)popup);
 bool raised=active_menu&&!(GetWindowLongPtrW(pet_window,GWL_EXSTYLE)&WS_EX_TOPMOST)&&
    (GetWindowLongPtrW(popup,GWL_EXSTYLE)&WS_EX_TOPMOST)&&window_above(popup,pet_window);
 if(active_menu)DestroyMenu(active_menu);
 active_menu=NULL;menu_window=NULL;
 behavior();
 bool restored=(GetWindowLongPtrW(pet_window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
 settings.topmost=saved;behavior();DestroyWindow(popup);
 if(!shown)ShowWindow(pet_window,SW_HIDE);
 bool ok=initial&&raised&&restored;
 fprintf(log,"%s: menu stays above the pet and closing it restores the topmost band (initial=%d raised=%d restored=%d).\n",
         ok?"PASS":"FAIL",initial,raised,restored);
 return ok;
}
static bool panel_order_selftest(FILE *log){
 bool saved_top=settings.topmost,saved_game=settings.game;
 settings.topmost=true;settings.game=false;
 ShowWindow(pet_window,SW_SHOWNOACTIVATE);
 sync_pet_order(NULL,false);
 companion_init(pet_window,NULL);companion_show();
 music_init(pet_window,NULL);music_show();
 HWND focus=FindWindowW(L"LetterPetC.CompanionPanel",NULL);
 HWND music=FindWindowW(L"LetterPetC.MusicPanel",NULL);
 bool independent=focus&&music&&!GetWindow(focus,GW_OWNER)&&!GetWindow(music,GW_OWNER)&&
                  !(GetWindowLongPtrW(focus,GWL_EXSTYLE)&WS_EX_TOPMOST)&&
                  !(GetWindowLongPtrW(music,GWL_EXSTYLE)&WS_EX_TOPMOST);
 pet_show_panel(focus,SW_SHOWNORMAL);
 bool focus_above=focus&&window_above(focus,pet_window)&&
                  !(GetWindowLongPtrW(pet_window,GWL_EXSTYLE)&WS_EX_TOPMOST);
 bool picker_above=music&&music_picker_order_selftest(log);
 sync_pet_order(NULL,false);
 bool restored=(GetWindowLongPtrW(pet_window,GWL_EXSTYLE)&WS_EX_TOPMOST)&&
               !(GetWindowLongPtrW(focus,GWL_EXSTYLE)&WS_EX_TOPMOST)&&
               !(GetWindowLongPtrW(music,GWL_EXSTYLE)&WS_EX_TOPMOST);
 music_shutdown();companion_shutdown();ShowWindow(pet_window,SW_HIDE);
 settings.topmost=saved_top;settings.game=saved_game;sync_pet_order(NULL,false);
 bool ok=independent&&focus_above&&picker_above&&restored;
 fprintf(log,"%s: focus and music panels stay independent, the import dialog stays above the pet, and pet topmost resumes (panels=%d focus=%d dialog=%d restored=%d).\n",
         ok?"PASS":"FAIL",independent,focus_above,picker_above,restored);
 return ok;
}
static int run_selftest(void){wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\self-test.log",app_dir);FILE *log=_wfopen(path,L"w");if(!log){renderer_free();return 2;}bool want_live2d=settings.live2d;settings.live2d=false;bool ok=model_selftest(log)&&monitor_selftest(log);ok=rewards_selftest(log)&&ok;ok=renderer_rewards_selftest(log)&&ok;model_init(&model);double zero[3]={0};renderer_draw(0,1000,zero);swprintf(path,MAX_PATH,L"%ls\\test-idle.bmp",app_dir);ok=renderer_bmp(path)&&ok;
 double start=now_ms();for(int i=0;i<86;i++){model.tip=key_center(&model.keys[i],1);model.keys[i].amount=1;renderer_draw(0,1000,zero);if(i==0||i==15||model.keys[i].vk==VK_SPACE){swprintf(path,MAX_PATH,L"%ls\\test-key-%d.bmp",app_dir,i);ok=renderer_bmp(path)&&ok;}model.keys[i].amount=0;}fprintf(log,"Render 86 key poses: %.1f ms/frame at %d px\n",(now_ms()-start)/86,settings.size);
 for(int i=0;i<8;i++)renderer_draw(i,1000,zero);for(int n=240;n<=720;n+=120){ok=renderer_resize(n)&&ok;renderer_draw(0,1000,zero);}fprintf(log,"%s: assets, 86 rendered poses, 8 expressions, 5 sizes.\n",ok?"PASS":"FAIL");if(want_live2d){ok=live2d_selftest(log)&&ok;ok=appearance_selftest(log)&&ok;}settings.live2d=want_live2d;
 if(fflush(log)==EOF)ok=false;
 renderer_free();
 int written=fprintf(log,"OVERALL: %s\n",ok?"PASS":"FAIL");
 if(fclose(log)==EOF||written<0)return 2;
 return ok?0:1;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,LPWSTR command,int show){(void)previous;(void)show;pet_instance=instance;bool feature_test=wcsstr(command,L"--companion-test")!=NULL;bool feature_ui=wcsstr(command,L"--companion-ui-test")!=NULL;bool integration=wcsstr(command,L"--integration-test")!=NULL;bool open_appearance=wcsstr(command,L"--appearance")!=NULL;bool open_account=wcsstr(command,L"--account")!=NULL;test_mode=wcsstr(command,L"--timetable-preview")!=NULL||wcsstr(command,L"--music-preview")!=NULL||feature_test||feature_ui||integration||wcsstr(command,L"--self-test")!=NULL;preview_mode=wcsstr(command,L"--preview")!=NULL;GetModuleFileNameW(NULL,app_dir,MAX_PATH);wchar_t *slash=wcsrchr(app_dir,L'\\');if(slash)*slash=0;SetProcessDPIAware();srand((unsigned)GetTickCount());INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
 if(wcsstr(command,L"--timetable-preview"))return timetable_preview_test();
 if(wcsstr(command,L"--music-preview"))return music_preview_test();
 if(feature_test||feature_ui){
 wchar_t test_path[MAX_PATH];swprintf(test_path,MAX_PATH,L"%ls\\companion-test.log",app_dir);FILE *log=_wfopen(test_path,L"w");if(!log)return 2;
 bool ok=companion_selftest(log);ok=study_selftest(log)&&ok;ok=growth_selftest(log)&&ok;ok=backup_selftest(log)&&ok;ok=desktop_selftest(log)&&ok;ok=dialogue_selftest(log)&&ok;ok=music_selftest(log)&&ok;ok=course_selftest(log)&&ok;
 if(feature_ui){WNDCLASSW c={0};c.lpfnWndProc=DefWindowProcW;c.hInstance=instance;c.lpszClassName=L"LetterPetC.FeatureTest";RegisterClassW(&c);pet_window=CreateWindowW(c.lpszClassName,L"",WS_POPUP,0,0,480,480,NULL,NULL,instance,NULL);ok=pet_window&&ui_theme_selftest(log)&&ok;if(pet_window)ok=companion_ui_selftest(log)&&ok;if(pet_window)ok=desktop_ui_selftest(log)&&ok;if(pet_window)ok=window_order_selftest(log)&&ok;if(pet_window)ok=panel_order_selftest(log)&&ok;if(pet_window)ok=focus_apps_ui_selftest(log)&&ok;if(pet_window)ok=music_ui_selftest(log)&&ok;if(pet_window)ok=timetable_ui_selftest(log)&&ok;if(pet_window)DestroyWindow(pet_window);}
 fclose(log);ui_theme_release();return ok?0:1;
 }
 if(!test_mode){
  instance_mutex=CreateMutexW(NULL,FALSE,PET_MUTEX);
  if(!instance_mutex)return 1;
  if(GetLastError()==ERROR_ALREADY_EXISTS){
   HWND other=FindWindowW(PET_CLASS,NULL);
   if(other)PostMessageW(other,WM_WAKE,open_account?10:open_appearance?9:wcsstr(command,L"--timetable")?8:(wcsstr(command,L"--music")?7:(wcsstr(command,L"--dialogue")?6:(wcsstr(command,L"--desktop")?5:(wcsstr(command,L"--growth")?4:(wcsstr(command,L"--study")?3:(wcsstr(command,L"--companion")?2:(wcsstr(command,L"--monitor")?1:0))))))),0);
   else account_activate_startup();
   CloseHandle(instance_mutex);return 0;
  }
  load_settings();
  account_init(config_path);
 }
 const wchar_t *login_notice=NULL;
 for(;;){
  if(!test_mode&&!account_require_login(login_notice)){
   account_close();ui_theme_release();CloseHandle(instance_mutex);return 0;
  }
  session_exit_notice=NULL;
  visible=dirty=true;
  dragging=moved=input_timer_running=input_frame_posted=false;
  last_frame_at=input_frames_until=last_topmost_check=blink_until=expression_until=0;
  memset(pulses,0,sizeof(pulses));expression=0;
 if(wcsstr(command,L"--legacy"))settings.live2d=false;
 model_init(&model);if(!renderer_init()){MessageBoxW(NULL,L"无法加载桌宠素材。请保留 exe 旁的 assets 文件夹。",L"桌宠",MB_ICONERROR);renderer_free();return 1;}
 bool selftest=test_mode&&!integration;
 if(!selftest||settings.live2d)live2d_init(settings.size,selftest);
 if(selftest)return run_selftest();
 WNDCLASSW wc={0};wc.lpfnWndProc=window_proc;wc.hInstance=instance;wc.lpszClassName=PET_CLASS;wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.hIcon=pet_app_icon();RegisterClassW(&wc);pet_window=CreateWindowExW(WS_EX_LAYERED|(preview_mode?WS_EX_APPWINDOW:(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE)),wc.lpszClassName,PET_TITLE,WS_POPUP,settings.x,settings.y,settings.size,settings.size,NULL,NULL,instance,NULL);if(!pet_window){account_close();renderer_free();if(instance_mutex)CloseHandle(instance_mutex);return 1;}
 if(integration){int result=run_integration_test();DestroyWindow(pet_window);renderer_free();return result;}
 RAWINPUTDEVICE devices[2]={{1,6,RIDEV_INPUTSINK,pet_window},{1,2,RIDEV_INPUTSINK,pet_window}};input_ok=RegisterRawInputDevices(devices,2,sizeof(devices[0]))!=0;
 ui_theme_select(0);appearance_init(config_path);menu_font=ui_theme_font(15,FW_NORMAL,false);taskbar_created=RegisterWindowMessageW(L"TaskbarCreated");add_tray();desktop_init(pet_window,config_path);music_init(pet_window,config_path);
 focus_apps_init(config_path);
 companion_init(pet_window,account_profile_settings());timetable_init(pet_window,account_profile_settings());blink_at=now_ms()+3200;expression_at=now_ms()+12000;behavior();tick();if(settings.live2d&&!live2d_available()&&!appearance_active())PostMessageW(pet_window,WM_APP+3,0,0);SetTimer(pet_window,STATE_TIMER,IDLE_FRAME_MS,NULL);if(!login_notice&&wcsstr(command,L"--monitor"))monitor_show();if(!login_notice&&wcsstr(command,L"--companion"))companion_show();if(!login_notice&&wcsstr(command,L"--study"))companion_study_show();if(!login_notice&&wcsstr(command,L"--growth"))companion_extras_show();if(!login_notice&&wcsstr(command,L"--desktop"))desktop_show();if(!login_notice&&wcsstr(command,L"--dialogue"))dialogue_show();if(!login_notice&&wcsstr(command,L"--music"))music_show();if(!login_notice&&wcsstr(command,L"--timetable"))timetable_show();if(!login_notice&&open_appearance)appearance_show();if(!login_notice&&open_account)account_show();
 MSG msg={0};
 while(GetMessageW(&msg,NULL,0,0)>0){if(!account_dialog_message(&msg)&&!appearance_dialog_message(&msg)&&!timetable_dialog_message(&msg)&&!music_dialog_message(&msg)&&!desktop_dialog_message(&msg)&&!companion_dialog_message(&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
 pet_window=NULL;
 monitor_close();renderer_free();DeleteObject(menu_font);menu_font=NULL;ui_theme_release();
 if(!session_exit_notice||test_mode){
  if(instance_mutex)CloseHandle(instance_mutex);
  return (int)msg.wParam;
 }
 login_notice=session_exit_notice;
 }
}
