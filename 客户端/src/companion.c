#include "companion.h"
#include "companion_core.h"
#include "resources.h"
#include "study.h"
#include "extras.h"
#include "backup_io.h"
#include "dialogue.h"
#include "dialogue_core.h"
#include "focus_apps.h"
#include "rewards.h"
#include "ui_theme.h"
#include <commctrl.h>
#include <wtsapi32.h>
#define ID_TOGGLE 401
#define ID_CANCEL 402
#define ID_FEED 403
#define ID_PET 404
#define ID_APPLY 405
#define ID_QUIET 406
#define ID_RESOURCE 407
#define ID_STUDY 408
#define ID_EXTRAS 409
#define ID_FOCUS_APPS 412
static CompanionState state;
static ResourceRule rule;
static ResourceSample latest={-1,-1,-1,0};
static HWND owner,panel,bubble,status_label,reward_label,care_label,resource_label,work_edit,rest_edit,toggle_button,cancel_button,apply_button,quiet_check,resource_check;
static HFONT font;
static wchar_t data_path[32768],bubble_text[256],storage_error[120];
static double previous,last_update,last_save,bubble_until,last_feed=-1,last_pet=-1;
static bool quiet=false,resource_enabled=true,visible_now=true,registered_session,storage_reported;
static int dpi=96;
static bool test_storage=false,suppress_data_write=false,bubble_is_speech=false;
static bool focus_blocked;
static int px(int n){return MulDiv(n,dpi,96);}
static bool running(void){return state.phase==FOCUS_WORK||state.phase==FOCUS_REST;}
static bool active(void){return state.phase!=FOCUS_IDLE&&state.phase!=FOCUS_BREAK_READY;}
static void position_bubble(void){
 RECT r;GetWindowRect(owner,&r);MONITORINFO mi={sizeof(mi)};GetMonitorInfoW(MonitorFromRect(&r,MONITOR_DEFAULTTONEAREST),&mi);
 int w=px(320),h=px(96);HDC measure=GetDC(bubble);HFONT old_font=SelectObject(measure,font);RECT content={0,0,w-px(28),0};DrawTextW(measure,bubble_text,-1,&content,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(measure,old_font);ReleaseDC(bubble,measure);h=(int)clampd(content.bottom+px(24),px(96),px(240));int x=r.left+(r.right-r.left-w)/2,y=r.top-h-px(8);
 x=(int)clampd(x,mi.rcWork.left,fmax(mi.rcWork.left,mi.rcWork.right-w));
 if(y<mi.rcWork.top)y=r.bottom+px(8);
 y=(int)clampd(y,mi.rcWork.top,fmax(mi.rcWork.top,mi.rcWork.bottom-h));
 HWND panel=pet_foreground_panel(GetForegroundWindow());
 SetWindowPos(bubble,panel?panel:HWND_TOPMOST,x,y,w,h,SWP_NOACTIVATE|SWP_NOOWNERZORDER);
}
void companion_notice(const wchar_t *text){
 if(!bubble||quiet||settings.game||!visible_now||test_mode)return;
 bubble_is_speech=false;lstrcpynW(bubble_text,text,256);bubble_until=now_ms()+8000;position_bubble();InvalidateRect(bubble,NULL,TRUE);ShowWindow(bubble,SW_SHOWNOACTIVATE);
}
bool companion_speech_allowed(void){return bubble&&!quiet&&!settings.game&&visible_now&&!test_mode&&now_ms()>=bubble_until;}
bool companion_say(const wchar_t *text,bool manual){
 if(!bubble||quiet||settings.game||!visible_now||test_mode||!text||!*text)return false;
 double now=now_ms();if(now<bubble_until&&(!manual||!bubble_is_speech))return false;
 bubble_is_speech=true;lstrcpynW(bubble_text,text,256);bubble_until=now+8000;position_bubble();InvalidateRect(bubble,NULL,TRUE);ShowWindow(bubble,SW_SHOWNOACTIVATE);return true;
}
void companion_cancel_speech(void){if(bubble&&bubble_is_speech){bubble_until=0;bubble_is_speech=false;ShowWindow(bubble,SW_HIDE);}}
int companion_dialogue_context(void){SYSTEMTIME local;GetLocalTime(&local);return talk_context(local.wHour,state.phase,state.mood,state.energy);}
static void save_data(void){
 if(suppress_data_write||(test_mode&&!test_storage)||!data_path[0])return;
 extras_update(true);
 wchar_t text[65536],temp[32768];
 int count=swprintf(text,65536,L"\xfeff[companion]\r\nversion=1\r\nwork=%d\r\nrest=%d\r\nphase=%d\r\nremaining=%u\r\nmood=%d\r\nenergy=%d\r\naffinity=%d\r\ncompleted=%d\r\ncare_ms=%u\r\nfocus_reward_ms=%u\r\nquiet=%d\r\nresource=%d\r\n",state.work_minutes,state.rest_minutes,state.phase,(unsigned)ceil(state.remaining_ms),state.mood,state.energy,state.affinity,state.completed,(unsigned)state.care_ms,(unsigned)state.focus_reward_ms,quiet,resource_enabled);
 if(count>0){int extra=study_serialize(text+count,65536-count);count=extra<0?-1:count+extra;}
 if(count>0){int extra=extras_serialize(text+count,65536-count);count=extra<0?-1:count+extra;}
 if(count>0){int extra=dialogue_serialize(text+count,65536-count);count=extra<0?-1:count+extra;}
 swprintf(temp,32768,L"%ls.tmp",data_path);
 HANDLE file=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);DWORD wrote=0;bool ok=false;
 if(file!=INVALID_HANDLE_VALUE){DWORD bytes=(DWORD)(count*sizeof(wchar_t));ok=count>0&&WriteFile(file,text,bytes,&wrote,NULL)&&wrote==bytes&&FlushFileBuffers(file);CloseHandle(file);}
 if(ok)ok=MoveFileExW(temp,data_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
 if(!ok){DeleteFileW(temp);lstrcpyW(storage_error,L"保存失败：当前进度暂未写入磁盘。");if(!storage_reported){storage_reported=true;companion_notice(storage_error);}}
 else {storage_error[0]=0;storage_reported=false;}
 last_save=now_ms();
}
static void read_stored_fields(void){
 companion_defaults(&state);study_load(NULL,false);extras_load(NULL);dialogue_load(NULL);quiet=false;resource_enabled=true;
 if(GetPrivateProfileIntW(L"companion",L"version",0,data_path)!=1)return;
 #define READ(field,key) state.field=(int)GetPrivateProfileIntW(L"companion",L##key,state.field,data_path)
 READ(work_minutes,"work");READ(rest_minutes,"rest");READ(phase,"phase");READ(mood,"mood");READ(energy,"energy");READ(affinity,"affinity");READ(completed,"completed");
 #undef READ
 state.remaining_ms=(int)GetPrivateProfileIntW(L"companion",L"remaining",0,data_path);
 state.care_ms=(int)GetPrivateProfileIntW(L"companion",L"care_ms",0,data_path);
 state.focus_reward_ms=(int)GetPrivateProfileIntW(L"companion",L"focus_reward_ms",0,data_path);
 quiet=GetPrivateProfileIntW(L"companion",L"quiet",0,data_path)!=0;
 resource_enabled=GetPrivateProfileIntW(L"companion",L"resource",1,data_path)!=0;
 companion_validate(&state);companion_pause(&state);
 study_load(data_path,state.phase==FOCUS_PAUSED_WORK);extras_load(data_path);dialogue_load(data_path);
}
static void load_data(const wchar_t *settings_path){
 companion_defaults(&state);study_load(NULL,false);extras_load(NULL);dialogue_load(NULL);if(test_mode)return;
 if(!settings_path||wcslen(settings_path)>=32740){lstrcpyW(storage_error,L"无法定位存档目录，当前会话不保存。");return;}
 lstrcpynW(data_path,settings_path,32768);
 wchar_t *slash=wcsrchr(data_path,L'\\');
 if(!slash){data_path[0]=0;lstrcpyW(storage_error,L"无法定位存档目录，当前会话不保存。");return;}
 wcscpy(slash+1,L"companion.ini");
 read_stored_fields();
}

static void set_text_if_changed(HWND window,const wchar_t *text){
 wchar_t current[512]={0};
 GetWindowTextW(window,current,512);
 if(wcscmp(current,text))SetWindowTextW(window,text);
}
static void set_enabled_if_changed(HWND window,bool enabled){
 if((IsWindowEnabled(window)!=0)!=enabled)EnableWindow(window,enabled);
}
static void update_panel(void){
 study_set_locked(active());
 if(!panel)return;
 wchar_t caption[180],growth_text[120];extras_caption(growth_text,120);swprintf(caption,180,L"桌宠 · 专注与养成 · %ls",growth_text);set_text_if_changed(panel,caption);
 wchar_t text[512];const wchar_t *phase[]={L"等待开始",L"正在专注",L"专注已暂停",L"专注完成，可以开始休息",L"正在休息",L"休息已暂停"};
 int seconds=(int)ceil(state.remaining_ms/1000);
 wchar_t task_title[101];study_current_title(task_title,101);
 bool work=state.phase==FOCUS_WORK||state.phase==FOCUS_PAUSED_WORK;
 int effective=work?(int)((state.work_minutes*60000.0-state.remaining_ms)/1000):state.phase==FOCUS_BREAK_READY?state.work_minutes*60:0;
 const wchar_t *reason=state.phase==FOCUS_WORK&&focus_blocked?L"白名单外，计时暂停":phase[state.phase];
 if(work||state.phase==FOCUS_BREAK_READY)
  swprintf(text,512,L"有效专注 %02d:%02d / %02d:00\r\n%ls\r\n完成 %d 轮 · 任务：%.28ls",effective/60,effective%60,state.work_minutes,reason,state.completed,task_title);
 else swprintf(text,512,L"%ls    %02d:%02d\r\n累计完成 %d 次专注\r\n任务：%.28ls",reason,seconds/60,seconds%60,state.completed,task_title);
 set_text_if_changed(status_label,text);
 rewards_progress(text,512);set_text_if_changed(reward_label,text);
 swprintf(text,512,L"心情 %d / 100     精力 %d / 100     亲密度 %d / 100\r\n%ls",state.mood,state.energy,state.affinity,storage_error[0]?storage_error:L"有效专注每 10 分钟亲密度 +1，零头跨轮保留。");set_text_if_changed(care_label,text);
 const wchar_t *action=state.phase==FOCUS_IDLE?L"开始专注":state.phase==FOCUS_BREAK_READY?L"开始休息":running()?L"暂停":L"继续";set_text_if_changed(toggle_button,action);
 bool editable=!active();
 set_enabled_if_changed(work_edit,editable);set_enabled_if_changed(rest_edit,editable);
 set_enabled_if_changed(apply_button,editable);set_enabled_if_changed(cancel_button,state.phase!=FOCUS_IDLE);
 wchar_t cpu[24]=L"—",memory[24]=L"—",gpu[24]=L"—";
 bool fresh=latest.when>0&&now_ms()-latest.when<15000;
 if(fresh&&latest.cpu>=0)swprintf(cpu,24,L"%.0f%%",latest.cpu);
 if(fresh&&latest.memory>=0)swprintf(memory,24,L"%.0f%%",latest.memory);
 if(fresh&&latest.gpu>=0)swprintf(gpu,24,L"%.0f%%",latest.gpu);
 swprintf(text,512,L"CPU %ls   内存 %ls   GPU %ls\r\n%ls",cpu,memory,gpu,!resource_enabled?L"资源提醒已关闭":!fresh?L"等待有效资源采样…":rule.busy?L"电脑正在忙碌，持续高负载时提醒。":L"资源状态正常；短暂尖峰不会触发提醒。");set_text_if_changed(resource_label,text);
}
static bool read_minutes(HWND edit,int maximum,int *out){wchar_t text[24],*end;GetWindowTextW(edit,text,24);long n=wcstol(text,&end,10);if(!*text||*end||n<1||n>maximum)return false;*out=(int)n;return true;}
static HWND control(HWND parent,const wchar_t *cls,const wchar_t *text,DWORD style,int x,int y,int w,int h,int id){
 HWND c=CreateWindowW(cls,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),parent,(HMENU)(INT_PTR)id,pet_instance,NULL);SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;
}
void companion_pet(void){if(!owner)return;if(companion_care(&state,false,now_ms(),&last_pet)){save_data();dialogue_event(false);update_panel();}}
static LRESULT CALLBACK panel_proc(HWND w,UINT message,WPARAM wp,LPARAM lp){
 switch(message){
 case WM_CREATE:{
  control(w,L"STATIC",L"专注陪伴",0,24,18,146,30,0);
  control(w,L"BUTTON",L"应用白名单",WS_TABSTOP,180,14,144,34,ID_FOCUS_APPS);
  control(w,L"BUTTON",L"任务与学习统计",WS_TABSTOP,336,14,204,34,ID_STUDY);
  status_label=control(w,L"STATIC",L"",0,24,62,516,134,0);
  reward_label=control(w,L"STATIC",L"",0,24,202,516,46,0);
  control(w,L"STATIC",L"专注（1–180 分钟）",0,24,253,246,24,0);
  work_edit=control(w,L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_NUMBER,24,278,246,32,410);
  control(w,L"STATIC",L"休息（1–60 分钟）",0,294,253,246,24,0);
  rest_edit=control(w,L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_NUMBER,294,278,246,32,411);
  SendMessageW(work_edit,EM_SETLIMITTEXT,3,0);SendMessageW(rest_edit,EM_SETLIMITTEXT,2,0);
  wchar_t n[16];swprintf(n,16,L"%d",state.work_minutes);SetWindowTextW(work_edit,n);swprintf(n,16,L"%d",state.rest_minutes);SetWindowTextW(rest_edit,n);
  toggle_button=control(w,L"BUTTON",L"开始专注",WS_TABSTOP|BS_DEFPUSHBUTTON,24,321,164,36,ID_TOGGLE);
  cancel_button=control(w,L"BUTTON",L"结束并保留收益",WS_TABSTOP,200,321,164,36,ID_CANCEL);
  apply_button=control(w,L"BUTTON",L"应用时长",WS_TABSTOP,376,321,164,36,ID_APPLY);
  care_label=control(w,L"STATIC",L"",0,24,369,516,100,0);
  control(w,L"BUTTON",L"喂食",WS_TABSTOP,24,480,246,36,ID_FEED);control(w,L"BUTTON",L"抚摸",WS_TABSTOP,294,480,246,36,ID_PET);
  resource_label=control(w,L"STATIC",L"",0,24,529,516,56,0);
  quiet_check=control(w,L"BUTTON",L"安静模式（关闭提示气泡）",WS_TABSTOP|BS_AUTOCHECKBOX,24,593,516,26,ID_QUIET);
  resource_check=control(w,L"BUTTON",L"开启资源状态提醒",WS_TABSTOP|BS_AUTOCHECKBOX,24,625,516,26,ID_RESOURCE);
  SendMessageW(quiet_check,BM_SETCHECK,quiet?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(resource_check,BM_SETCHECK,resource_enabled?BST_CHECKED:BST_UNCHECKED,0);
  control(w,L"BUTTON",L"场景 成长与备份",WS_TABSTOP,24,670,516,36,ID_EXTRAS);
  return 0;
 }
 case WM_COMMAND:
  if(HIWORD(wp)!=BN_CLICKED)return 0;
  switch(LOWORD(wp)){
  case ID_EXTRAS:companion_extras_show();return 0;
  case ID_STUDY:companion_study_show();return 0;
  case ID_FOCUS_APPS:focus_apps_show(w);return 0;
  case ID_TOGGLE:companion_toggle_focus();return 0;
  case ID_CANCEL:companion_cancel(&state);focus_blocked=false;rewards_focus_time(0,false,false,true);rewards_tick();study_end_session();break;
  case ID_APPLY:{int work,rest;if(!read_minutes(work_edit,180,&work)||!read_minutes(rest_edit,60,&rest)){MessageBoxW(w,L"专注请输入 1–180，休息请输入 1–60 的整数分钟。",L"检查时长",MB_ICONINFORMATION);return 0;}state.work_minutes=work;state.rest_minutes=rest;break;}
  case ID_FEED:if(companion_care(&state,true,now_ms(),&last_feed)){live2d_click(now_ms());dialogue_event(true);}break;
  case ID_PET:companion_pet();live2d_click(now_ms());break;
  case ID_QUIET:extras_manual_scene();quiet=SendMessageW(quiet_check,BM_GETCHECK,0,0)==BST_CHECKED;if(quiet)ShowWindow(bubble,SW_HIDE);break;
  case ID_RESOURCE:extras_manual_scene();resource_enabled=SendMessageW(resource_check,BM_GETCHECK,0,0)==BST_CHECKED;
   memset(&rule,0,sizeof(rule));latest=(ResourceSample){-1,-1,-1,0};
   if(resource_enabled){if(!test_mode&&!resources_start(owner)){resource_enabled=false;SendMessageW(resource_check,BM_SETCHECK,BST_UNCHECKED,0);MessageBoxW(w,L"暂时无法启动资源采样，请稍后重试。",L"资源提醒",MB_ICONWARNING);}}else resources_stop(owner);break;
  default:return 0;
  }
  save_data();update_panel();return 0;
 case WM_CLOSE:ShowWindow(w,SW_HIDE);return 0;
 case WM_DESTROY:panel=NULL;return 0;
 }
 return DefWindowProcW(w,message,wp,lp);
}
static LRESULT CALLBACK bubble_proc(HWND w,UINT message,WPARAM wp,LPARAM lp){
 if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(message==WM_LBUTTONUP){bubble_until=0;bubble_is_speech=false;ShowWindow(w,SW_HIDE);return 0;}
 if(message==WM_PAINT){
  PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT r;GetClientRect(w,&r);
  ui_theme_fill(dc,&r,UI_SURFACE);
  RECT accent={r.left,r.top,r.left+px(4),r.bottom};ui_theme_fill(dc,&accent,UI_GOLD);
  HBRUSH edge=CreateSolidBrush(UI_BORDER);FrameRect(dc,&r,edge);DeleteObject(edge);
  SetBkMode(dc,TRANSPARENT);SetTextColor(dc,UI_INK);HFONT old=SelectObject(dc,font);
  InflateRect(&r,-px(15),-px(12));DrawTextW(dc,bubble_text,-1,&r,DT_WORDBREAK|DT_NOPREFIX);
  SelectObject(dc,old);EndPaint(w,&ps);return 0;
 }
 return DefWindowProcW(w,message,wp,lp);
}
void companion_init(HWND window,const wchar_t *settings_path){
 owner=window;HDC dc=GetDC(owner);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(owner,dc);
 font=ui_theme_font(px(15),FW_NORMAL,false);
 data_path[0]=storage_error[0]=0;storage_reported=suppress_data_write=false;
 quiet=false;resource_enabled=true;registered_session=false;bubble_is_speech=false;bubble_until=0;
 focus_blocked=false;
 last_feed=last_pet=-1;memset(&rule,0,sizeof(rule));latest=(ResourceSample){-1,-1,-1,0};
 load_data(settings_path);previous=last_save=last_update=now_ms();
 WNDCLASSW c={0};c.lpfnWndProc=bubble_proc;c.hInstance=pet_instance;c.lpszClassName=L"LetterPetC.CompanionBubble";c.hCursor=LoadCursor(NULL,IDC_ARROW);RegisterClassW(&c);
 bubble=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,c.lpszClassName,L"桌宠提醒",WS_POPUP|WS_BORDER,0,0,px(320),px(96),owner,NULL,pet_instance,NULL);
 dialogue_init(owner,font,dpi);
 extras_init(owner,font,dpi);
 if(!test_mode){registered_session=WTSRegisterSessionNotification(owner,NOTIFY_FOR_THIS_SESSION)!=0;if(resource_enabled&&!resources_start(owner))lstrcpyW(storage_error,L"资源采样未启动，可在面板关闭后重新开启。");}
}
void companion_show(void){
 if(!panel){WNDCLASSW c={0};c.lpfnWndProc=panel_proc;c.hInstance=pet_instance;c.lpszClassName=L"LetterPetC.CompanionPanel";c.hCursor=LoadCursor(NULL,IDC_ARROW);c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&c);
  DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN;RECT r={0,0,px(564),px(730)};AdjustWindowRect(&r,style,FALSE);
  MONITORINFO mi={sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&mi);int width=r.right-r.left,height=r.bottom-r.top;
  panel=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,c.lpszClassName,L"桌宠 · 专注与养成",style,mi.rcWork.left+(mi.rcWork.right-mi.rcWork.left-width)/2,mi.rcWork.top+(mi.rcWork.bottom-mi.rcWork.top-height)/2,width,height,NULL,NULL,pet_instance,NULL);
 }
 if(panel){ui_theme_apply(panel);ui_theme_role(status_label,UI_FOCUS_HERO);ui_theme_role(reward_label,UI_SURFACE_TEXT);ui_theme_role(care_label,UI_CARE_METRICS);ui_theme_role(resource_label,UI_SURFACE_TEXT);update_panel();pet_show_panel(panel,SW_SHOWNORMAL);}
}
void companion_tick(double now,bool visible,bool pet_menu_open){
 visible_now=visible;double dt=now-previous;previous=now;
 if(dt>10000){companion_pause(&state);dialogue_postpone();save_data();dt=0;}
 bool allowed=focus_apps_tick(state.phase==FOCUS_WORK,pet_menu_open);
 bool blocked=state.phase==FOCUS_WORK&&!allowed;
 if(blocked&&!focus_blocked)companion_notice(L"当前应用不在白名单内，已暂停有效专注计时。返回允许的应用后自动继续。");
 focus_blocked=blocked;
 double focused=companion_focus_elapsed(&state,dt,allowed);
 int event=companion_advance_focus(&state,dt,allowed);
 rewards_focus_time(focused,state.phase==FOCUS_WORK,state.phase==FOCUS_WORK||state.phase==FOCUS_PAUSED_WORK,allowed);
 study_credit_time(dt,focused,event==COMPANION_WORK_DONE);
 if(event==COMPANION_WORK_DONE)study_end_session();
 if(event==COMPANION_WORK_DONE){save_data();companion_notice(L"这轮有效专注已完成，奖励按有效时长累计。打开专注面板，开始休息吧。");}
 else if(event==COMPANION_REST_DONE){save_data();companion_notice(L"休息结束，精力恢复了。准备好后再开始下一轮吧。");}
 if(bubble){if(now>=bubble_until||!visible||quiet||settings.game)ShowWindow(bubble,SW_HIDE);else position_bubble();}
 if(now-last_update>=1000){if(extras_update(true))save_data();dialogue_tick(now);update_panel();last_update=now;}
 if(now-last_save>=60000)save_data();
}
void companion_suspend(void){companion_pause(&state);focus_blocked=false;rewards_focus_time(0,false,state.phase==FOCUS_PAUSED_WORK,true);rewards_tick();previous=now_ms();study_clock_reset();dialogue_postpone();memset(&rule,0,sizeof(rule));save_data();update_panel();if(bubble)ShowWindow(bubble,SW_HIDE);}
void companion_resource_message(LPARAM data){
 ResourceSample *sample=(ResourceSample*)data;if(!sample)return;
 if(resource_enabled){latest=*sample;if(resource_update(&rule,sample->cpu,sample->memory,sample->gpu,sample->when))companion_notice(L"电脑持续高负载，我也在忙碌中。可以打开资源面板查看占用。");}
 free(sample);
}
void companion_shutdown(void){
 if(!owner)return;dialogue_close();extras_close();study_close();resources_stop(owner);if(registered_session)WTSUnRegisterSessionNotification(owner);
 companion_pause(&state);save_data();if(panel)DestroyWindow(panel);if(bubble)DestroyWindow(bubble);bubble=NULL;
 DeleteObject(font);font=NULL;owner=NULL;
}
bool companion_dialog_message(MSG *message){return focus_apps_dialog_message(message)||dialogue_dialog_message(message)||extras_dialog_message(message)||study_dialog_message(message)||(panel&&IsWindowVisible(panel)&&IsDialogMessageW(panel,message));}
void companion_save(void){save_data();}
void companion_toggle_focus(void){if(state.phase==FOCUS_IDLE)study_begin_session();companion_toggle(&state);focus_blocked=false;rewards_focus_time(0,state.phase==FOCUS_WORK,state.phase==FOCUS_WORK||state.phase==FOCUS_PAUSED_WORK,true);rewards_tick();study_clock_reset();previous=now_ms();save_data();update_panel();}
void companion_extras_show(void){extras_show();}
void companion_growth_metrics(GrowthMetrics *m){memset(m,0,sizeof(*m));m->completed=(unsigned)state.completed;m->affinity=(unsigned)state.affinity;study_growth_metrics(m);}
bool companion_is_quiet(void){return quiet;}
bool companion_resources_enabled(void){return resource_enabled;}
const wchar_t *companion_data_path(void){return data_path;}
bool companion_storage_ready(void){return data_path[0]&&!storage_error[0]&&!suppress_data_write;}
void companion_discard_changes(void){suppress_data_write=true;}
bool companion_apply_preferences(bool quiet_value,bool resources_value){
 if(resources_value&&!test_mode&&!resources_start(owner))return false;
 if(!resources_value)resources_stop(owner);
 if(resource_enabled!=resources_value){memset(&rule,0,sizeof(rule));latest=(ResourceSample){-1,-1,-1,0};}
 quiet=quiet_value;resource_enabled=resources_value;
 if(quiet&&bubble)ShowWindow(bubble,SW_HIDE);
 if(panel){SendMessageW(quiet_check,BM_SETCHECK,quiet?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(resource_check,BM_SETCHECK,resource_enabled?BST_CHECKED:BST_UNCHECKED,0);update_panel();}
 return true;
}
void companion_study_show(void){study_set_locked(active());study_show(owner,font,dpi);}
bool companion_start_task(void){if(active())return false;companion_cancel(&state);companion_toggle_focus();companion_show();return true;}
static bool speech_priority_selftest(FILE *log){
 bool old_test=test_mode,old_quiet=quiet,old_game=settings.game,old_visible=visible_now;test_mode=false;quiet=false;settings.game=false;visible_now=true;bubble_until=0;
 bool ok=companion_say(L"普通闲聊",false)&&bubble_is_speech;
 ok=ok&&!companion_say(L"不应覆盖",false)&&companion_say(L"手动预览",true);
 companion_notice(L"专注完成等重要功能提醒");ok=ok&&!bubble_is_speech&&!companion_say(L"不能覆盖重要提醒",true);
 companion_cancel_speech();ok=ok&&bubble_until>now_ms()&&!bubble_is_speech;
 bubble_until=0;quiet=true;ok=ok&&!companion_say(L"安静时不显示",true);quiet=false;settings.game=true;ok=ok&&!companion_say(L"游戏时不显示",true);settings.game=false;visible_now=false;ok=ok&&!companion_say(L"隐藏时不显示",true);
 visible_now=true;wchar_t long_text[101];for(int i=0;i<100;i++)long_text[i]=L'字';long_text[100]=0;
 bool long_ok=companion_say(long_text,true);RECT bounds;GetWindowRect(bubble,&bounds);long_ok=long_ok&&bounds.bottom-bounds.top>px(96)&&bounds.bottom-bounds.top<=px(240);
 companion_cancel_speech();ok=ok&&bubble_until==0&&!bubble_is_speech;ShowWindow(bubble,SW_HIDE);
 test_mode=old_test;quiet=old_quiet;settings.game=old_game;visible_now=old_visible;bubble_until=0;
 fprintf(log,"%s: functional notices preempt chatter; quiet/game/hidden rules and speech cancellation\n",ok?"PASS":"FAIL");fprintf(log,"%s: long dialogue grows bubble within measured height bounds\n",long_ok?"PASS":"FAIL");return ok&&long_ok;
}
bool companion_ui_selftest(FILE *log){
 companion_init(pet_window,NULL);companion_show();
 bool ok=panel&&bubble&&work_edit&&rest_edit&&status_label&&care_label&&resource_label;
 if(ok){
  SetWindowTextW(work_edit,L"1");SetWindowTextW(rest_edit,L"2");SendMessageW(panel,WM_COMMAND,ID_APPLY,0);
  ok=state.work_minutes==1&&state.rest_minutes==2;
  SendMessageW(panel,WM_COMMAND,ID_TOGGLE,0);ok=ok&&state.phase==FOCUS_WORK&&!IsWindowEnabled(work_edit);
  HWND unchanged[]={panel,care_label,resource_label,toggle_button,work_edit,rest_edit,apply_button,cancel_button};
  for(size_t i=0;i<sizeof(unchanged)/sizeof(*unchanged);i++)ValidateRect(unchanged[i],NULL);
  ValidateRect(status_label,NULL);
  companion_advance(&state,1000);
  update_panel();
  wchar_t status[512]={0};GetWindowTextW(status_label,status,512);
  bool repaint_ok=wcsstr(status,L"有效专注 00:01 / 01:00")&&GetUpdateRect(status_label,NULL,FALSE);
  for(size_t i=0;i<sizeof(unchanged)/sizeof(*unchanged);i++)
   if(GetUpdateRect(unchanged[i],NULL,FALSE))repaint_ok=false;
  UpdateWindow(status_label);
  repaint_ok=repaint_ok&&!GetUpdateRect(status_label,NULL,FALSE);
  fprintf(log,"%s: focus tick updates and paints only the changed countdown\n",repaint_ok?"PASS":"FAIL");
  ok=ok&&repaint_ok;
  SendMessageW(panel,WM_COMMAND,ID_TOGGLE,0);ok=ok&&state.phase==FOCUS_PAUSED_WORK;
  SendMessageW(panel,WM_COMMAND,ID_TOGGLE,0);companion_suspend();ok=ok&&state.phase==FOCUS_PAUSED_WORK;
  SendMessageW(panel,WM_COMMAND,ID_CANCEL,0);ok=ok&&state.phase==FOCUS_IDLE;
  int before=state.energy;SendMessageW(panel,WM_COMMAND,ID_FEED,0);ok=ok&&state.energy>before;
  int mood=state.mood;SendMessageW(panel,WM_COMMAND,ID_PET,0);ok=ok&&state.mood>mood;
  SendMessageW(panel,WM_CLOSE,0,0);ok=ok&&!IsWindowVisible(panel);
 }
 bool tasks_ok=study_ui_selftest(owner,font,dpi,log);ok=ok&&tasks_ok&&state.phase==FOCUS_WORK;companion_cancel(&state);study_end_session();
 bool extras_ok=extras_ui_selftest(log);ok=ok&&extras_ok;
 bool dialogue_ok=dialogue_ui_selftest(log);ok=ok&&dialogue_ok;
 ok=speech_priority_selftest(log)&&ok;
 bool sampling_ok=resources_start(owner);double deadline=now_ms()+15000;
 while(sampling_ok&&latest.when==0&&now_ms()<deadline){MSG m;if(PeekMessageW(&m,owner,WM_RESOURCE_SAMPLE,WM_RESOURCE_SAMPLE,PM_REMOVE))companion_resource_message(m.lParam);else Sleep(10);}
 sampling_ok=sampling_ok&&latest.when>0;
 resources_stop(owner);
 fprintf(log,"%s: independent resource sampling with panel closed and clean worker stop\n",sampling_ok?"PASS":"FAIL");ok=ok&&sampling_ok;
 bool storage_ok=false;test_storage=true;
 swprintf(data_path,32768,L"%ls\\companion-storage-test.ini",app_dir);
 companion_defaults(&state);state.mood=81;state.energy=91;state.affinity=12;state.completed=7;
 companion_toggle(&state);state.remaining_ms=123456;state.focus_reward_ms=234567;quiet=true;resource_enabled=false;
 save_data();read_stored_fields();
 storage_ok=!storage_error[0]&&state.phase==FOCUS_PAUSED_WORK&&state.remaining_ms==123456&&state.focus_reward_ms==234567&&state.mood==81&&state.energy==91&&state.affinity==12&&state.completed==7&&quiet&&!resource_enabled;
 WritePrivateProfileStringW(L"companion",L"focus_reward_ms",NULL,data_path);read_stored_fields();
 storage_ok=storage_ok&&state.focus_reward_ms==0&&state.affinity==12;
 WritePrivateProfileStringW(L"companion",L"work",L"-100",data_path);WritePrivateProfileStringW(L"companion",L"energy",L"500",data_path);WritePrivateProfileStringW(L"companion",L"remaining",L"-99",data_path);read_stored_fields();
 storage_ok=storage_ok&&state.work_minutes==1&&state.energy==100&&state.phase==FOCUS_IDLE;
 WritePrivateProfileStringW(L"companion",L"version",L"999",data_path);read_stored_fields();storage_ok=storage_ok&&state.work_minutes==25&&state.completed==0;
 bool study_storage_ok=study_storage_selftest(data_path,log);storage_ok=storage_ok&&study_storage_ok;
 WritePrivateProfileStringW(L"companion",L"version",L"1",data_path);WritePrivateProfileStringW(L"companion",L"completed",L"99",data_path);
 companion_discard_changes();state.completed=0;save_data();bool restore_guard=GetPrivateProfileIntW(L"companion",L"completed",0,data_path)==99;suppress_data_write=false;
 fprintf(log,"%s: restored data cannot be overwritten by stale in-memory autosave\n",restore_guard?"PASS":"FAIL");storage_ok=storage_ok&&restore_guard;
 test_storage=false;data_path[0]=0;
 fprintf(log,"%s: atomic save, restart pause, care totals, corrupt values and schema recovery\n",storage_ok?"PASS":"FAIL");
 ok=ok&&storage_ok;
 fprintf(log,"%s: native panel controls, duration, pause, suspend, cancel, care, close\n",ok?"PASS":"FAIL");
 companion_shutdown();return ok;
}
