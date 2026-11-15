#include "extras.h"
#include "ui_theme.h"
#include "companion.h"
#include "study.h"
#include "backup_io.h"
#include <commctrl.h>
#include <commdlg.h>
#define ID_SCENE 601
#define ID_TITLE 602
#define ID_EXPORT 603
#define ID_IMPORT 604
static SceneState scene;
static GrowthState growth;
static GrowthMetrics metrics;
static HWND owner,window,scene_combo,title_combo,summary,list;
static HFONT font;
static int dpi=96;
static bool changing;
static const wchar_t *names[]={L"初次专注",L"十轮同行",L"百轮坚持",L"今日达标",L"默契伙伴",L"任务完成者",L"七日坚持",L"专注一小时"};
static const wchar_t *conditions[]={L"累计完成 1 轮专注",L"累计完成 10 轮专注",L"累计完成 100 轮专注",L"今日专注达到每日目标",L"亲密度达到 30",L"至少一个任务标记完成",L"最近七天每天专注至少 1 分钟",L"单日专注达到 60 分钟"};
static int px(int n){return MulDiv(n,dpi,96);}
void extras_caption(wchar_t *buffer,int capacity){swprintf(buffer,capacity,L"Lv.%llu %ls",(unsigned long long)(growth_xp(&metrics)/100+1),growth.title?names[growth.title-1]:L"新朋友");}
static void refresh(void){
 if(!window)return;changing=true;uint64_t xp=growth_xp(&metrics);wchar_t text[300];
 swprintf(text,300,L"等级 Lv.%llu    经验 %llu（距离下一级 %llu）\r\n当前称号：%ls",(unsigned long long)(xp/100+1),(unsigned long long)xp,(unsigned long long)(100-xp%100),growth.title?names[growth.title-1]:L"新朋友");SetWindowTextW(summary,text);
 SendMessageW(scene_combo,CB_SETCURSEL,scene.scene,0);SendMessageW(title_combo,CB_SETCURSEL,growth.title,0);
 for(int i=0;i<ACHIEVEMENT_COUNT;i++){wchar_t progress[64];unsigned target=growth_target(i,&metrics),value=growth_value(i,&metrics);swprintf(progress,64,L"%u / %u",min(value,target),target);ListView_SetItemText(list,i,1,(growth.unlocked&(1u<<i))?L"已解锁":L"进行中");ListView_SetItemText(list,i,2,progress);}
 changing=false;
}
bool extras_update(bool notify){
 companion_growth_metrics(&metrics);unsigned added=growth_evaluate(&growth,&metrics);refresh();
 if(added&&notify){for(int i=0;i<ACHIEVEMENT_COUNT;i++)if(added&(1u<<i)){wchar_t text[128];swprintf(text,128,L"成就解锁：%ls。可以在成长面板选择这个称号。",names[i]);companion_notice(text);break;}}
 return added!=0;
}
void extras_load(const wchar_t *path){
 memset(&scene,0,sizeof(scene));memset(&growth,0,sizeof(growth));if(!path||!*path||GetPrivateProfileIntW(L"extras",L"version",0,path)!=1)return;
 scene.scene=(int)GetPrivateProfileIntW(L"extras",L"scene",0,path);if(scene.scene<0||scene.scene>3)scene.scene=0;
 scene.base_valid=scene.scene!=0&&GetPrivateProfileIntW(L"extras",L"base_valid",0,path)!=0;
 #define B(field,key) scene.base.field=GetPrivateProfileIntW(L"extras",L##key,0,path)!=0
 B(top,"base_top");B(pass,"base_pass");B(game,"base_game");B(quiet,"base_quiet");B(resources,"base_resources");
 #undef B
 unsigned unlocked=GetPrivateProfileIntW(L"extras",L"unlocked",0,path);growth.unlocked=unlocked<=255?unlocked:0;
 int title=(int)GetPrivateProfileIntW(L"extras",L"title",0,path);growth_title(&growth,title);
}
int extras_serialize(wchar_t *buffer,int capacity){return swprintf(buffer,capacity,L"\r\n[extras]\r\nversion=1\r\nscene=%d\r\nbase_valid=%d\r\nbase_top=%d\r\nbase_pass=%d\r\nbase_game=%d\r\nbase_quiet=%d\r\nbase_resources=%d\r\nunlocked=%u\r\ntitle=%d\r\n",scene.scene,scene.base_valid,scene.base.top,scene.base.pass,scene.base.game,scene.base.quiet,scene.base.resources,growth.unlocked,growth.title);}
static bool apply_scene(int chosen){
 ScenePrefs current={settings.topmost,settings.clickthrough,settings.game,companion_is_quiet(),companion_resources_enabled()};SceneState before=scene;ScenePrefs desired=scene_choose(&scene,chosen,current);
 if(!companion_apply_preferences(desired.quiet,desired.resources)){scene=before;MessageBoxW(window?window:owner,L"资源采样暂时无法启动，场景未切换。",L"场景模式",MB_ICONWARNING);refresh();return false;}
 pet_apply_scene_window(desired.top,desired.pass,desired.game);companion_save();refresh();return true;
}
void extras_manual_scene(void){if(changing)return;scene_custom(&scene);refresh();}
static bool pick_backup(bool importing,wchar_t *path){
 OPENFILENAMEW dialog={0};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;dialog.lpstrFilter=L"桌宠数据备份 (*.lpbackup)\0*.lpbackup\0\0";dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.lpstrDefExt=L"lpbackup";dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(importing?OFN_FILEMUSTEXIST:OFN_OVERWRITEPROMPT);
 pet_yield_to_panel(window);
 return importing?GetOpenFileNameW(&dialog)!=0:GetSaveFileNameW(&dialog)!=0;
}
static void export_backup(void){
 wchar_t *path=calloc(32768,sizeof(wchar_t));if(!path)return;SYSTEMTIME t;GetLocalTime(&t);swprintf(path,32768,L"桌宠数据-%04d%02d%02d.lpbackup",t.wYear,t.wMonth,t.wDay);
 if(pick_backup(false,path)){companion_save();if(companion_storage_ready()&&data_backup_export(companion_data_path(),path))MessageBoxW(window,L"任务、学习统计、养成、场景偏好和成就已导出。",L"备份完成",MB_OK);else MessageBoxW(window,L"备份失败，请检查存档与目标文件夹是否可写。",L"备份失败",MB_ICONWARNING);}free(path);
}
static void import_backup(void){
 wchar_t *path=calloc(32768,sizeof(wchar_t)),*folder=calloc(32768,sizeof(wchar_t)),*recovery=calloc(32768,sizeof(wchar_t));if(!path||!folder||!recovery){free(path);free(folder);free(recovery);return;}
 if(pick_backup(true,path)&&MessageBoxW(window,L"将用备份替换当前任务、学习统计、养成和成就数据。恢复前会自动备份当前存档。\n\n成功后桌宠将退出，请重新打开；计时会保持暂停。继续？",L"恢复桌宠数据",MB_YESNO|MB_ICONQUESTION)==IDYES){
  companion_suspend();if(!companion_storage_ready()){MessageBoxW(window,L"当前存档尚未成功保存，无法创建恢复前备份。请检查存档目录。",L"无法恢复",MB_ICONWARNING);free(path);free(folder);free(recovery);return;}lstrcpynW(folder,companion_data_path(),32768);wchar_t *slash=wcsrchr(folder,L'\\');if(slash)*slash=0;
  if(wcslen(folder)<32620){wcscat(folder,L"\\backups");bool dir_ok=CreateDirectoryW(folder,NULL)||GetLastError()==ERROR_ALREADY_EXISTS;SYSTEMTIME t;GetLocalTime(&t);swprintf(recovery,32768,L"%ls\\恢复前-%04d%02d%02d-%02d%02d%02d-%llu.lpbackup",folder,t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,(unsigned long long)GetTickCount64());
   if(dir_ok&&companion_storage_ready()&&data_backup_restore(path,companion_data_path(),recovery)){
    companion_discard_changes();MessageBoxW(window,L"数据已恢复，恢复前的副本保存在存档目录的 backups 文件夹。\n\n桌宠将退出，请双击启动入口重新打开。",L"恢复完成",MB_OK);free(path);free(folder);free(recovery);pet_command(CMD_QUIT);return;
   }
  }
  MessageBoxW(window,L"恢复失败：备份可能损坏、版本不受支持或无法写入。当前存档未被替换。",L"恢复失败",MB_ICONWARNING);
 }
 free(path);free(folder);free(recovery);
}
static HWND child(HWND w,const wchar_t *cls,const wchar_t *text,DWORD style,int x,int y,int width,int height,int id){HWND c=CreateWindowW(cls,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(width),px(height),w,(HMENU)(INT_PTR)id,pet_instance,NULL);SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;}
static LRESULT CALLBACK extras_proc(HWND w,UINT message,WPARAM wp,LPARAM lp){
 switch(message){
 case WM_CREATE:{
  child(w,L"STATIC",L"场景模式",0,24,20,120,26,0);scene_combo=child(w,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,154,16,468,180,ID_SCENE);
  const wchar_t *scenes[]={L"自定义（返回切换前的设置）",L"学习：置顶、可点击、开启提醒",L"游戏：置顶、穿透、安静、关闭资源提醒",L"安静：不置顶、穿透、关闭提示和资源提醒"};for(int i=0;i<4;i++)SendMessageW(scene_combo,CB_ADDSTRING,0,(LPARAM)scenes[i]);
  summary=child(w,L"STATIC",L"",0,24,66,600,48,0);child(w,L"STATIC",L"展示称号",0,24,126,120,24,0);title_combo=child(w,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,154,120,468,240,ID_TITLE);SendMessageW(title_combo,CB_ADDSTRING,0,(LPARAM)L"新朋友");for(int i=0;i<ACHIEVEMENT_COUNT;i++)SendMessageW(title_combo,CB_ADDSTRING,0,(LPARAM)names[i]);
  list=child(w,WC_LISTVIEWW,L"成就进度",WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,24,166,598,244,610);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
  const wchar_t *titles[]={L"成就",L"状态",L"进度",L"解锁条件"};int widths[]={104,74,84,308};for(int i=0;i<4;i++){LVCOLUMNW c={0};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=(wchar_t*)titles[i];c.cx=px(widths[i]);ListView_InsertColumn(list,i,&c);}for(int i=0;i<ACHIEVEMENT_COUNT;i++){LVITEMW item={0};item.mask=LVIF_TEXT;item.iItem=i;item.pszText=(wchar_t*)names[i];ListView_InsertItem(list,&item);ListView_SetItemText(list,i,3,(wchar_t*)conditions[i]);}
  child(w,L"STATIC",L"每完成一轮专注 +20 经验；每点亲密度提供 2 经验。每 100 经验升一级。",0,24,426,598,42,0);
  child(w,L"BUTTON",L"导出数据备份",WS_TABSTOP,24,482,286,34,ID_EXPORT);child(w,L"BUTTON",L"从备份恢复",WS_TABSTOP,334,482,288,34,ID_IMPORT);
  child(w,L"STATIC",L"备份包含任务、统计、养成与成就；不包含模型、窗口位置和开机启动。",0,24,534,598,42,0);return 0;
 }
 case WM_COMMAND:
  if(changing)return 0;
  if(LOWORD(wp)==ID_SCENE&&HIWORD(wp)==CBN_SELCHANGE){apply_scene((int)SendMessageW(scene_combo,CB_GETCURSEL,0,0));return 0;}
  if(LOWORD(wp)==ID_TITLE&&HIWORD(wp)==CBN_SELCHANGE){if(!growth_title(&growth,(int)SendMessageW(title_combo,CB_GETCURSEL,0,0)))MessageBoxW(w,L"该称号尚未解锁，请先完成对应成就。",L"选择称号",MB_ICONINFORMATION);companion_save();refresh();return 0;}
  if(HIWORD(wp)==BN_CLICKED){if(LOWORD(wp)==ID_EXPORT){export_backup();return 0;}if(LOWORD(wp)==ID_IMPORT){import_backup();return 0;}}break;
 case WM_CLOSE:ShowWindow(w,SW_HIDE);return 0;
 case WM_DESTROY:window=NULL;return 0;
 }return DefWindowProcW(w,message,wp,lp);
}
void extras_init(HWND parent,HFONT value,int scale){owner=parent;font=value;dpi=scale;if(!test_mode&&scene.scene)apply_scene(scene.scene);extras_update(false);}
void extras_show(void){
 if(!owner)return;
 if(!window){WNDCLASSW c={0};c.lpfnWndProc=extras_proc;c.hInstance=pet_instance;c.lpszClassName=L"LetterPetC.GrowthPanel";c.hCursor=LoadCursor(NULL,IDC_ARROW);c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&c);DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;RECT r={0,0,px(646),px(594)};AdjustWindowRect(&r,style,FALSE);MONITORINFO mi={sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&mi);window=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,c.lpszClassName,L"桌宠 · 场景 成长与备份",style,mi.rcWork.left+max(0,(mi.rcWork.right-mi.rcWork.left-r.right+r.left)/2),mi.rcWork.top+max(0,(mi.rcWork.bottom-mi.rcWork.top-r.bottom+r.top)/2),r.right-r.left,r.bottom-r.top,NULL,NULL,pet_instance,NULL);}
 if(window){ui_theme_apply(window);ui_theme_role(summary,UI_SURFACE_TEXT);extras_update(false);pet_show_panel(window,SW_SHOWNORMAL);}
}
void extras_close(void){if(window)DestroyWindow(window);window=NULL;owner=NULL;}
bool extras_dialog_message(MSG *m){return window&&IsWindowVisible(window)&&IsDialogMessageW(window,m);}
bool extras_ui_selftest(FILE *log){
 extras_show();bool ok=window&&scene_combo&&title_combo&&list&&ListView_GetItemCount(list)==ACHIEVEMENT_COUNT;
 if(ok){ok=apply_scene(1)&&!companion_is_quiet()&&!settings.game&&settings.topmost;ok=apply_scene(2)&&ok&&companion_is_quiet()&&settings.game&&settings.clickthrough&&!companion_resources_enabled();ok=apply_scene(3)&&ok&&!settings.game&&!settings.topmost;ok=apply_scene(0)&&ok&&scene.scene==0;SendMessageW(window,WM_CLOSE,0,0);ok=ok&&!IsWindowVisible(window);}
 fprintf(log,"%s: native scene controls, preferences, game/quiet behavior and custom restore\n",ok?"PASS":"FAIL");
 SceneState old_scene=scene;GrowthState old_growth=growth;scene=(SceneState){2,true,{false,true,false,true,true}};growth=(GrowthState){7,2};
 wchar_t path[MAX_PATH],text[4096];swprintf(path,MAX_PATH,L"%ls\\extras-storage-test.ini",app_dir);text[0]=0xfeff;int n=extras_serialize(text+1,4095);FILE *f=_wfopen(path,L"wb");bool stored=f&&n>0;if(f){stored=stored&&fwrite(text,sizeof(wchar_t),n+1,f)==(size_t)n+1;fclose(f);}
 extras_load(path);stored=stored&&scene.scene==2&&scene.base_valid&&!scene.base.top&&scene.base.pass&&scene.base.quiet&&scene.base.resources&&growth.unlocked==7&&growth.title==2;
 WritePrivateProfileStringW(L"extras",L"scene",L"999",path);WritePrivateProfileStringW(L"extras",L"title",L"8",path);extras_load(path);stored=stored&&scene.scene==0&&growth.title==0;
 scene=old_scene;growth=old_growth;fprintf(log,"%s: scene baseline, achievement bits and equipped title persist safely\n",stored?"PASS":"FAIL");return ok&&stored;
}
