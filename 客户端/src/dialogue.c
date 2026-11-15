#include "dialogue.h"
#include "ui_theme.h"
#include "dialogue_core.h"
#include "companion.h"
#include "study.h"
#include <commctrl.h>
#include <stdarg.h>
#define ID_PREFS 801
#define ID_ADD 802
#define ID_EDIT 803
#define ID_DELETE 804
#define ID_SAY 805
#define ID_PREVIEW 806
static TalkCatalog catalog;
static TalkRuntime runtime;
static HWND owner,window,enabled_check,builtin_check,interval_edit,category_combo,line_list,text_edit,preview_label;
static HFONT font;
static int dpi=96;
static uint32_t selected;
static bool refreshing;
static const wchar_t *categories[]={L"通用",L"清晨",L"白天",L"傍晚",L"深夜",L"专注中",L"已暂停",L"休息",L"低心情",L"低精力",L"抚摸回应",L"喂食回应"};
static int px(int n){return MulDiv(n,dpi,96);}
static unsigned read_number(const wchar_t *path,const wchar_t *section,const wchar_t *key,unsigned maximum,unsigned fallback){
 wchar_t text[24];GetPrivateProfileStringW(section,key,L"",text,24,path);if(!*text)return fallback;for(const wchar_t *p=text;*p;p++)if(*p<L'0'||*p>L'9')return fallback;
 wchar_t *end;unsigned long long n=wcstoull(text,&end,10);return *end||n>maximum?fallback:(unsigned)n;
}
static int hex(wchar_t c){return c>=L'0'&&c<=L'9'?c-L'0':c>=L'a'&&c<=L'f'?c-L'a'+10:-1;}
static bool decode(const wchar_t *text,wchar_t *output){size_t n=wcslen(text);if(!n||n%4||n/4>=TALK_TEXT)return false;for(size_t i=0;i<n/4;i++){unsigned value=0;for(int j=0;j<4;j++){int h=hex(text[i*4+j]);if(h<0)return false;value=value*16+h;}if(value<32)return false;output[i]=(wchar_t)value;}output[n/4]=0;return true;}
void dialogue_load(const wchar_t *path){
 talk_defaults(&catalog);memset(&runtime,0,sizeof(runtime));runtime.random=(uint32_t)GetTickCount()^GetCurrentProcessId();selected=0;
 if(!path||!*path||read_number(path,L"dialogue",L"version",100,0)!=1)return;
 catalog.enabled=read_number(path,L"dialogue",L"enabled",1,1)!=0;catalog.builtins=read_number(path,L"dialogue",L"builtins",1,1)!=0;
 catalog.interval=(int)read_number(path,L"dialogue",L"interval",120,10);if(catalog.interval<1)catalog.interval=10;
 int count=(int)read_number(path,L"dialogue",L"count",TALK_MAX_LINES,0);
 for(int i=0;i<count;i++){wchar_t section[24],encoded[TALK_TEXT*4],text[TALK_TEXT];swprintf(section,24,L"dialogue_line%d",i);unsigned id=read_number(path,section,L"id",0x7fffffff,0),category=read_number(path,section,L"category",TALK_CATEGORIES,99);GetPrivateProfileStringW(section,L"text_hex",L"",encoded,TALK_TEXT*4,path);
  if(!id||talk_line(&catalog,id)||!decode(encoded,text)||!talk_add(&catalog,(int)category,text))continue;catalog.lines[catalog.count-1].id=id;
 }
 catalog.next_id=read_number(path,L"dialogue",L"next_id",0x7fffffff,1);
}
static bool append(wchar_t *buffer,int capacity,int *used,const wchar_t *format,...){va_list args;va_start(args,format);int n=vswprintf(buffer+*used,capacity-*used,format,args);va_end(args);if(n<0||n>=capacity-*used)return false;*used+=n;return true;}
int dialogue_serialize(wchar_t *buffer,int capacity){
 int used=0;
 if(!append(buffer,capacity,&used,L"\r\n[dialogue]\r\nversion=1\r\nenabled=%d\r\nbuiltins=%d\r\ninterval=%d\r\ncount=%d\r\nnext_id=%u\r\n",catalog.enabled,catalog.builtins,catalog.interval,catalog.count,catalog.next_id))return -1;
 for(int i=0;i<catalog.count;i++){const TalkLine *line=&catalog.lines[i];wchar_t encoded[TALK_TEXT*4];int n=0;for(const wchar_t *c=line->text;*c;c++){swprintf(encoded+n,5,L"%04x",(unsigned)*c);n+=4;}encoded[n]=0;
  if(!append(buffer,capacity,&used,L"[dialogue_line%d]\r\nid=%u\r\ncategory=%d\r\ntext_hex=%ls\r\n",i,line->id,line->category,encoded))return -1;
 }return used;
}
static bool say_category(int category,bool manual){
 if(!catalog.enabled)return false;TalkRuntime trial=runtime;wchar_t text[TALK_TEXT],expanded[221],task[101];
 if(!talk_choose(&catalog,&trial,category,text,TALK_TEXT))return false;study_current_title(task,101);talk_expand(text,task,expanded,221);
 if(!companion_say(expanded,manual))return false;runtime=trial;talk_schedule(&runtime,now_ms(),catalog.interval,true);return true;
}
void dialogue_tick(double now){
 if(!talk_ready(&runtime,now,catalog.interval,catalog.enabled,companion_speech_allowed()))return;
 if(!say_category(companion_dialogue_context(),false))talk_schedule(&runtime,now,catalog.interval,false);
}
void dialogue_postpone(void){talk_schedule(&runtime,now_ms(),catalog.interval,false);}
void dialogue_event(bool feed){say_category(feed?TALK_FEED:TALK_PET,true);}
void dialogue_init(HWND parent,HFONT value,int scale){owner=parent;font=value;dpi=scale;runtime.next=now_ms()+60000;}
static void fill_controls(bool rebuild){
 if(!window)return;refreshing=true;SendMessageW(enabled_check,BM_SETCHECK,catalog.enabled?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(builtin_check,BM_SETCHECK,catalog.builtins?BST_CHECKED:BST_UNCHECKED,0);wchar_t number[16];swprintf(number,16,L"%d",catalog.interval);SetWindowTextW(interval_edit,number);
 if(rebuild){ListView_DeleteAllItems(line_list);for(int i=0;i<catalog.count;i++){TalkLine *line=&catalog.lines[i];LVITEMW item={0};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=i;item.pszText=(wchar_t*)categories[line->category];item.lParam=line->id;ListView_InsertItem(line_list,&item);ListView_SetItemText(line_list,i,1,line->text);if(line->id==selected)ListView_SetItemState(line_list,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}}
 refreshing=false;
}
static void saved(const wchar_t *message){companion_save();SetWindowTextW(preview_label,!test_mode&&!companion_storage_ready()?L"本次运行已生效，但写入存档失败。请检查存档目录。":message);fill_controls(true);}
static bool preview_selected(void){TalkLine *line=talk_line(&catalog,selected);wchar_t text[221],task[101];if(!line){SetWindowTextW(preview_label,L"请先选择一句自定义台词。查看内置台词可点击“按当前状态说一句”。");return false;}study_current_title(task,101);talk_expand(line->text,task,text,221);SetWindowTextW(preview_label,text);if(catalog.enabled&&companion_say(text,true))talk_schedule(&runtime,now_ms(),catalog.interval,true);return true;}
static HWND child(HWND w,const wchar_t *cls,const wchar_t *text,DWORD style,int x,int y,int width,int height,int id){HWND c=CreateWindowW(cls,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(width),px(height),w,(HMENU)(INT_PTR)id,pet_instance,NULL);SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;}
static LRESULT CALLBACK dialogue_proc(HWND w,UINT message,WPARAM wp,LPARAM lp){
 switch(message){
 case WM_CREATE:
  enabled_check=child(w,L"BUTTON",L"开启角色台词（不影响专注完成等功能提醒）",WS_TABSTOP|BS_AUTOCHECKBOX,24,18,578,28,810);
  builtin_check=child(w,L"BUTTON",L"同时使用内置台词；取消后仅使用自定义内容",WS_TABSTOP|BS_AUTOCHECKBOX,24,56,578,28,811);
  child(w,L"STATIC",L"自动间隔（1–120 分钟）",0,24,104,244,26,0);interval_edit=child(w,L"EDIT",L"10",WS_TABSTOP|WS_BORDER|ES_NUMBER,276,100,96,30,812);SendMessageW(interval_edit,EM_SETLIMITTEXT,3,0);child(w,L"BUTTON",L"应用设置",WS_TABSTOP,396,100,206,30,ID_PREFS);
  child(w,L"STATIC",L"适用情景",0,24,152,100,24,0);category_combo=child(w,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,132,146,470,280,813);for(int i=0;i<TALK_CATEGORIES;i++)SendMessageW(category_combo,CB_ADDSTRING,0,(LPARAM)categories[i]);SendMessageW(category_combo,CB_SETCURSEL,0,0);
  line_list=child(w,WC_LISTVIEWW,L"自定义台词",WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,24,182,578,126,814);ListView_SetExtendedListViewStyle(line_list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);{LVCOLUMNW c={0};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=L"情景";c.cx=px(102);ListView_InsertColumn(line_list,0,&c);c.pszText=L"自定义台词";c.cx=px(450);ListView_InsertColumn(line_list,1,&c);}
  child(w,L"STATIC",L"台词内容（最多 100 字，可用 {任务} 引用任务名）",0,24,322,578,26,0);text_edit=child(w,L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,24,352,578,52,815);SendMessageW(text_edit,EM_SETLIMITTEXT,TALK_TEXT-1,0);
  child(w,L"BUTTON",L"添加台词",WS_TABSTOP,24,418,182,32,ID_ADD);child(w,L"BUTTON",L"保存所选修改",WS_TABSTOP,222,418,182,32,ID_EDIT);child(w,L"BUTTON",L"删除所选台词",WS_TABSTOP,420,418,182,32,ID_DELETE);
  preview_label=child(w,L"EDIT",L"通用台词会参与各类日常闲聊；互动回应仅在对应操作时使用。",WS_BORDER|ES_MULTILINE|ES_READONLY|WS_VSCROLL,24,466,578,56,0);
  child(w,L"BUTTON",L"按当前状态说一句",WS_TABSTOP,24,536,280,34,ID_SAY);child(w,L"BUTTON",L"预览所选台词",WS_TABSTOP,322,536,280,34,ID_PREVIEW);
  child(w,L"STATIC",L"安静、游戏、隐藏时不弹出气泡；重要提醒优先，不会被闲聊覆盖。",0,24,584,578,42,0);return 0;
 case WM_NOTIFY:{NMHDR *n=(void*)lp;if(n->hwndFrom==line_list&&n->code==LVN_ITEMCHANGED&&!refreshing){NMLISTVIEW *item=(void*)lp;if(item->iItem>=0&&item->iItem<catalog.count&&(item->uNewState&LVIS_SELECTED)){TalkLine *line=&catalog.lines[item->iItem];selected=line->id;SendMessageW(category_combo,CB_SETCURSEL,line->category,0);SetWindowTextW(text_edit,line->text);}}return 0;}
 case WM_COMMAND:if(HIWORD(wp)!=BN_CLICKED)return 0;switch(LOWORD(wp)){
  case ID_PREFS:{wchar_t text[16],*end;GetWindowTextW(interval_edit,text,16);long interval=wcstol(text,&end,10);if(!*text||*end||interval<1||interval>120){MessageBoxW(w,L"请输入 1–120 的整数分钟。",L"检查间隔",MB_ICONINFORMATION);return 0;}catalog.enabled=SendMessageW(enabled_check,BM_GETCHECK,0,0)==BST_CHECKED;catalog.builtins=SendMessageW(builtin_check,BM_GETCHECK,0,0)==BST_CHECKED;catalog.interval=(int)interval;talk_schedule(&runtime,now_ms(),catalog.interval,true);if(!catalog.enabled)companion_cancel_speech();saved(L"台词设置已应用。关闭角色台词不影响专注完成等功能提醒。");return 0;}
  case ID_ADD:case ID_EDIT:{wchar_t text[TALK_TEXT];GetWindowTextW(text_edit,text,TALK_TEXT);int category=(int)SendMessageW(category_combo,CB_GETCURSEL,0,0);bool ok=LOWORD(wp)==ID_ADD?talk_add(&catalog,category,text):talk_edit(&catalog,selected,category,text);if(!ok){MessageBoxW(w,L"请填写非空台词；最多保存 32 句。修改前请先选择一行。",L"无法保存台词",MB_ICONINFORMATION);return 0;}if(LOWORD(wp)==ID_ADD)selected=catalog.lines[catalog.count-1].id;saved(L"自定义台词已保存。可以预览，或继续添加其他情景。");return 0;}
  case ID_DELETE:if(!talk_line(&catalog,selected))return 0;if(!test_mode&&MessageBoxW(w,L"删除这句自定义台词？",L"删除台词",MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;talk_delete(&catalog,selected);selected=0;SetWindowTextW(text_edit,L"");saved(L"所选台词已删除。");return 0;
  case ID_PREVIEW:preview_selected();return 0;
  case ID_SAY:{TalkRuntime trial=runtime;wchar_t line[TALK_TEXT],text[221],task[101];int category=companion_dialogue_context();if(!talk_choose(&catalog,&trial,category,line,TALK_TEXT)){SetWindowTextW(preview_label,L"当前情景没有可用台词。请添加通用或对应情景的内容，或开启内置台词。");return 0;}study_current_title(task,101);talk_expand(line,task,text,221);SetWindowTextW(preview_label,text);if(catalog.enabled&&companion_say(text,true)){runtime=trial;talk_schedule(&runtime,now_ms(),catalog.interval,true);}return 0;}
 }break;
 case WM_CLOSE:ShowWindow(w,SW_HIDE);return 0;
 case WM_DESTROY:window=NULL;return 0;
 }return DefWindowProcW(w,message,wp,lp);
}
void dialogue_show(void){
 if(!owner)return;if(!window){WNDCLASSW c={0};c.lpfnWndProc=dialogue_proc;c.hInstance=pet_instance;c.lpszClassName=L"LetterPetC.DialoguePanel";c.hCursor=LoadCursor(NULL,IDC_ARROW);c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&c);DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;RECT r={0,0,px(626),px(640)};AdjustWindowRect(&r,style,FALSE);MONITORINFO mi={sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&mi);window=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,c.lpszClassName,L"桌宠 · 角色台词",style,mi.rcWork.left+max(0,(mi.rcWork.right-mi.rcWork.left-r.right+r.left)/2),mi.rcWork.top+max(0,(mi.rcWork.bottom-mi.rcWork.top-r.bottom+r.top)/2),r.right-r.left,r.bottom-r.top,NULL,NULL,pet_instance,NULL);}
 if(window){ui_theme_apply(window);fill_controls(true);pet_show_panel(window,SW_SHOWNORMAL);}
}
void dialogue_close(void){if(window)DestroyWindow(window);window=NULL;owner=NULL;}
bool dialogue_dialog_message(MSG *message){return window&&IsWindowVisible(window)&&IsDialogMessageW(window,message);}
bool dialogue_ui_selftest(FILE *log){
 dialogue_show();bool ok=window&&enabled_check&&builtin_check&&interval_edit&&category_combo&&line_list&&text_edit&&preview_label;
 if(ok){SetWindowTextW(text_edit,L"我们一起完成「{任务}」！");SendMessageW(category_combo,CB_SETCURSEL,TALK_FOCUS,0);SendMessageW(window,WM_COMMAND,ID_ADD,0);ok=catalog.count==1&&catalog.lines[0].category==TALK_FOCUS;SetWindowTextW(text_edit,L"修改后的中文台词 😀");SendMessageW(window,WM_COMMAND,ID_EDIT,0);ok=ok&&!wcscmp(catalog.lines[0].text,L"修改后的中文台词 😀");SendMessageW(window,WM_COMMAND,ID_PREVIEW,0);wchar_t preview[221];GetWindowTextW(preview_label,preview,221);ok=ok&&!wcscmp(preview,L"修改后的中文台词 😀");SetWindowTextW(interval_edit,L"15");SendMessageW(enabled_check,BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(window,WM_COMMAND,ID_PREFS,0);ok=ok&&!catalog.enabled&&catalog.interval==15;SendMessageW(window,WM_COMMAND,ID_DELETE,0);ok=ok&&catalog.count==0;SendMessageW(window,WM_CLOSE,0,0);ok=ok&&!IsWindowVisible(window);}
 TalkCatalog saved_catalog=catalog;TalkRuntime saved_runtime=runtime;talk_defaults(&catalog);catalog.enabled=false;catalog.builtins=false;catalog.interval=23;talk_add(&catalog,TALK_GENERAL,L"你好 \"朋友\" = [陪伴] 😀 {任务}");
 wchar_t storage[MAX_PATH],text[16384];swprintf(storage,MAX_PATH,L"%ls\\dialogue-storage-test.ini",app_dir);text[0]=0xfeff;int n=dialogue_serialize(text+1,16383);FILE *f=_wfopen(storage,L"wb");bool stored=f&&n>0;if(f){stored=stored&&fwrite(text,sizeof(wchar_t),n+1,f)==(size_t)n+1;fclose(f);}dialogue_load(storage);stored=stored&&!catalog.enabled&&!catalog.builtins&&catalog.interval==23&&catalog.count==1&&!wcscmp(catalog.lines[0].text,L"你好 \"朋友\" = [陪伴] 😀 {任务}");
 WritePrivateProfileStringW(L"dialogue",L"interval",L"999",storage);WritePrivateProfileStringW(L"dialogue_line0",L"text_hex",L"broken",storage);dialogue_load(storage);stored=stored&&catalog.interval==10&&catalog.count==0;catalog=saved_catalog;runtime=saved_runtime;
 fprintf(log,"%s: dialogue native add/edit/delete, options and local preview\n",ok?"PASS":"FAIL");fprintf(log,"%s: Unicode dialogue persistence and corrupt-entry recovery\n",stored?"PASS":"FAIL");return ok&&stored;
}
