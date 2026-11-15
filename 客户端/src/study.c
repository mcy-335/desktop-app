#include "study.h"
#include "ui_theme.h"
#include "study_core.h"
#include "companion.h"
#include <commctrl.h>
#include <stdarg.h>
#include <errno.h>
#define ID_ADD 501
#define ID_MARK 502
#define ID_DELETE 503
#define ID_FREE 504
#define ID_START 505
#define ID_GOAL 506
static StudyState data;
static HWND window,task_list,title_edit,summary,selected_label,goal_edit,start_button,mark_button,delete_button,free_button,chart;
static HFONT ui_font;
static int scale_dpi=96,previous_date;
static bool locked,updating;
static uint32_t row_selection;
static int chart_date,chart_goal;
static uint64_t chart_minutes[7];
static void text_if_changed(HWND control,const wchar_t *text){
 wchar_t previous[512];GetWindowTextW(control,previous,ARRAYSIZE(previous));
 if(wcscmp(previous,text))SetWindowTextW(control,text);
}
static void enable_if_changed(HWND control,bool enabled){
 if((IsWindowEnabled(control)!=0)!=enabled)EnableWindow(control,enabled);
}
static void cell_if_changed(int row,int column,const wchar_t *text){
 wchar_t previous[128];ListView_GetItemText(task_list,row,column,previous,ARRAYSIZE(previous));
 if(wcscmp(previous,text))ListView_SetItemText(task_list,row,column,(wchar_t*)text);
}
static int px(int n){return MulDiv(n,scale_dpi,96);}
static int today(SYSTEMTIME *time){GetLocalTime(time);return time->wYear*10000+time->wMonth*100+time->wDay;}
void study_growth_metrics(GrowthMetrics *m){
 SYSTEMTIME t;int date=today(&t);const StudyDay *d=study_day(&data,date);m->today_ms=d?d->milliseconds:0;m->goal_minutes=(unsigned)data.goal;m->done_tasks=0;m->active_days=0;
 for(int i=0;i<data.count;i++)if(data.tasks[i].done)m->done_tasks++;
 for(int i=0;i<7;i++){d=study_day(&data,date);if(d&&d->milliseconds>=60000)m->active_days++;date=study_previous_date(date);}
}
void study_clock_reset(void){SYSTEMTIME t;previous_date=today(&t);}
void study_set_locked(bool value){locked=value;study_refresh(false);}
void study_begin_session(void){study_begin(&data);study_clock_reset();}
void study_end_session(void){data.active=0;}
void study_current_title(wchar_t *buffer,int capacity){StudyTask *t=study_task(&data,locked?data.active:data.selected);lstrcpynW(buffer,t?t->title:L"自由专注",capacity);}
void study_credit_time(double elapsed,double focused,bool completed){
 SYSTEMTIME t;int date=today(&t);uint64_t ms=((uint64_t)t.wHour*3600+t.wMinute*60+t.wSecond)*1000+t.wMilliseconds;
 if(elapsed>0&&focused>0)study_credit(&data,date,previous_date,ms,(uint64_t)elapsed,(uint64_t)focused,completed);
 previous_date=date;
}
static uint64_t number(const wchar_t *path,const wchar_t *section,const wchar_t *key,uint64_t maximum,uint64_t fallback){
 wchar_t text[40];GetPrivateProfileStringW(section,key,L"",text,40,path);if(!text[0])return fallback;
 for(const wchar_t *p=text;*p;p++)if(*p<L'0'||*p>L'9')return fallback;
 errno=0;unsigned long long n=wcstoull(text,NULL,10);return errno==ERANGE||n>maximum?fallback:n;
}
static int hex(wchar_t c){return c>=L'0'&&c<=L'9'?c-L'0':c>=L'a'&&c<=L'f'?c-L'a'+10:c>=L'A'&&c<=L'F'?c-L'A'+10:-1;}
static bool decode(const wchar_t *text,wchar_t *title){size_t n=wcslen(text);if(!n||n%4||n/4>=STUDY_TITLE)return false;for(size_t i=0;i<n/4;i++){unsigned v=0;for(int j=0;j<4;j++){int h=hex(text[i*4+j]);if(h<0)return false;v=v*16+h;}if(v<32)return false;title[i]=(wchar_t)v;}title[n/4]=0;return true;}
void study_load(const wchar_t *path,bool has_work){
 study_defaults(&data);row_selection=0;locked=false;study_clock_reset();
 if(!path||!*path||number(path,L"study",L"version",100,0)!=1)return;
 data.goal=(int)number(path,L"study",L"goal",1440,120);if(data.goal<1)data.goal=120;
 int count=(int)number(path,L"study",L"tasks",STUDY_TASKS,0);
 for(int i=0;i<count;i++){
  wchar_t section[32],encoded[STUDY_TITLE*4],title[STUDY_TITLE];swprintf(section,32,L"task%d",i);
  uint32_t id=(uint32_t)number(path,section,L"id",UINT32_MAX,0);GetPrivateProfileStringW(section,L"title_hex",L"",encoded,STUDY_TITLE*4,path);
  if(!id||study_task(&data,id)||!decode(encoded,title)||!study_add(&data,title))continue;
  StudyTask *t=&data.tasks[data.count-1];t->id=id;t->done=number(path,section,L"done",1,0)!=0;
  t->milliseconds=number(path,section,L"ms",1000000000000ULL,0);t->rounds=(unsigned)number(path,section,L"rounds",1000000,0);
 }
 data.next_id=(uint32_t)number(path,L"study",L"next_id",UINT32_MAX,1);
 data.selected=(uint32_t)number(path,L"study",L"selected",UINT32_MAX,0);
 StudyTask *selected=study_task(&data,data.selected);if(!selected||selected->done)data.selected=0;
 row_selection=data.selected;
 data.active=(uint32_t)number(path,L"study",L"active",UINT32_MAX,0);
 StudyTask *active=study_task(&data,data.active);if(!has_work||!active||active->done)data.active=0;
 int days=(int)number(path,L"study",L"days",STUDY_DAYS,0),used=0;
 for(int i=0;i<days;i++){wchar_t section[32];swprintf(section,32,L"day%d",i);int date=(int)number(path,section,L"date",99991231,0);if(!study_date_valid(date)||study_day(&data,date))continue;data.days[used++]=(StudyDay){date,number(path,section,L"ms",86400000,0),(unsigned)number(path,section,L"rounds",1000000,0)};}
}
static bool append(wchar_t *buffer,int capacity,int *used,const wchar_t *format,...){va_list args;va_start(args,format);int n=vswprintf(buffer+*used,capacity-*used,format,args);va_end(args);if(n<0||n>=capacity-*used)return false;*used+=n;return true;}
int study_serialize(wchar_t *buffer,int capacity){
 int used=0,days=0;for(int i=0;i<STUDY_DAYS;i++)if(data.days[i].date)days++;
 #define A(...) do{if(!append(buffer,capacity,&used,__VA_ARGS__))return -1;}while(0)
 A(L"\r\n[study]\r\nversion=1\r\ngoal=%d\r\nnext_id=%u\r\nselected=%u\r\nactive=%u\r\ntasks=%d\r\ndays=%d\r\n",data.goal,data.next_id,data.selected,data.active,data.count,days);
 for(int i=0;i<data.count;i++){StudyTask *t=&data.tasks[i];wchar_t encoded[STUDY_TITLE*4];int n=0;for(const wchar_t *c=t->title;*c;c++){swprintf(encoded+n,5,L"%04x",(unsigned)*c);n+=4;}encoded[n]=0;
  A(L"[task%d]\r\nid=%u\r\ntitle_hex=%ls\r\ndone=%d\r\nms=%llu\r\nrounds=%u\r\n",i,t->id,encoded,t->done,(unsigned long long)t->milliseconds,t->rounds);
 }
 int index=0;for(int i=0;i<STUDY_DAYS;i++){StudyDay *d=&data.days[i];if(d->date)A(L"[day%d]\r\ndate=%d\r\nms=%llu\r\nrounds=%u\r\n",index++,d->date,(unsigned long long)d->milliseconds,d->rounds);}
 #undef A
 return used;
}
static HWND child(HWND w,const wchar_t *cls,const wchar_t *text,DWORD style,int x,int y,int width,int height,int id){HWND c=CreateWindowW(cls,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(width),px(height),w,(HMENU)(INT_PTR)id,pet_instance,NULL);SendMessageW(c,WM_SETFONT,(WPARAM)ui_font,TRUE);return c;}
static void refresh_rows(bool rebuild){
 updating=true;
 if(rebuild){ListView_DeleteAllItems(task_list);for(int i=0;i<data.count;i++){LVITEMW item={0};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=i;item.pszText=data.tasks[i].title;item.lParam=data.tasks[i].id;ListView_InsertItem(task_list,&item);}}
 for(int i=0;i<data.count;i++){StudyTask *t=&data.tasks[i];wchar_t value[64];cell_if_changed(i,1,t->done?L"已完成":L"待办");swprintf(value,64,L"%.1f 分钟",t->milliseconds/60000.0);cell_if_changed(i,2,value);swprintf(value,64,L"%u",t->rounds);cell_if_changed(i,3,value);if(rebuild&&t->id==row_selection)ListView_SetItemState(task_list,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}
 updating=false;
}
void study_refresh(bool rebuild){
 if(!window)return;wchar_t text[512],title[STUDY_TITLE];study_current_title(title,STUDY_TITLE);
 swprintf(text,512,L"%ls：%ls",locked?L"本轮任务（暂停时仍保持绑定）":L"下一轮任务",title);text_if_changed(selected_label,text);
 SYSTEMTIME t;int date=today(&t);const StudyDay *d=study_day(&data,date);double minutes=d?d->milliseconds/60000.0:0;unsigned rounds=d?d->rounds:0;
 swprintf(text,512,L"今日专注 %.1f 分钟    完成 %u 轮    每日目标 %d 分钟（%.0f%%）",minutes,rounds,data.goal,fmin(100,minutes/data.goal*100));text_if_changed(summary,text);
 StudyTask *row=study_task(&data,row_selection);
 enable_if_changed(start_button,!locked&&row&&!row->done);enable_if_changed(mark_button,row&&row->id!=data.active);enable_if_changed(delete_button,row&&row->id!=data.active);enable_if_changed(free_button,!locked);
 text_if_changed(mark_button,row&&row->done?L"重新打开":L"标记完成");refresh_rows(rebuild);
 bool chart_changed=rebuild||chart_date!=date||chart_goal!=data.goal;
 chart_date=date;chart_goal=data.goal;
 for(int i=6;i>=0;i--){
  const StudyDay *day=study_day(&data,date);uint64_t value=day?day->milliseconds/6000:0;
  chart_changed=chart_changed||chart_minutes[i]!=value;chart_minutes[i]=value;date=study_previous_date(date);
 }
 if(chart_changed)InvalidateRect(chart,NULL,FALSE);
}
static LRESULT CALLBACK chart_proc(HWND w,UINT message,WPARAM wp,LPARAM lp){
 if(message==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT bounds;GetClientRect(w,&bounds);ui_theme_fill(dc,&bounds,UI_SURFACE);HFONT old=SelectObject(dc,ui_font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,UI_MUTED);
  SYSTEMTIME t;int date=today(&t),dates[7];double values[7],maximum=data.goal;for(int i=6;i>=0;i--){dates[i]=date;const StudyDay *d=study_day(&data,date);values[i]=d?d->milliseconds/60000.0:0;if(values[i]>maximum)maximum=values[i];date=study_previous_date(date);}
  int column=bounds.right/7,top=px(28),bottom=bounds.bottom-px(31),height=bottom-top;
  HBRUSH edge=CreateSolidBrush(UI_BORDER);FrameRect(dc,&bounds,edge);DeleteObject(edge);
  for(int i=0;i<=3;i++){
   int y=bottom-MulDiv(height,i,3);RECT line={px(12),y,bounds.right-px(12),y+1};
   ui_theme_fill(dc,&line,ui_theme_mix(UI_SURFACE,UI_BORDER,55));
  }
  for(int i=0;i<7;i++){
   int x=i*column;RECT bar={x+px(28),bottom-(int)(height*values[i]/maximum),x+column-px(28),bottom};
   ui_theme_fill(dc,&bar,i==6?UI_GOLD:ui_theme_mix(UI_WINE,UI_GOLD,45));
   wchar_t label[32];swprintf(label,32,L"%.0f 分",values[i]);RECT r={x,bar.top-px(24),x+column,bar.top};
   SetTextColor(dc,i==6?UI_GOLD:UI_INK);DrawTextW(dc,label,-1,&r,DT_CENTER|DT_SINGLELINE);
   swprintf(label,32,L"%02d/%02d",dates[i]/100%100,dates[i]%100);r=(RECT){x,bottom+px(6),x+column,bounds.bottom};
   SetTextColor(dc,i==6?UI_GOLD:UI_MUTED);DrawTextW(dc,label,-1,&r,DT_CENTER|DT_SINGLELINE);
  }
  SelectObject(dc,old);EndPaint(w,&ps);return 0;
 }
 return DefWindowProcW(w,message,wp,lp);
}
static LRESULT CALLBACK study_proc(HWND w,UINT message,WPARAM wp,LPARAM lp){
 switch(message){
 case WM_CREATE:{
  child(w,L"STATIC",L"任务与学习统计",0,24,18,650,26,0);title_edit=child(w,L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,24,56,502,30,520);SendMessageW(title_edit,EM_SETLIMITTEXT,STUDY_TITLE-1,0);
  child(w,L"BUTTON",L"添加任务",WS_TABSTOP,542,56,134,30,ID_ADD);
  task_list=child(w,WC_LISTVIEWW,L"任务列表",WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,24,100,652,156,521);ListView_SetExtendedListViewStyle(task_list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
  const wchar_t *names[]={L"任务",L"状态",L"累计专注",L"完成轮数"};int widths[]={348,76,124,80};for(int i=0;i<4;i++){LVCOLUMNW c={0};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=(wchar_t*)names[i];c.cx=px(widths[i]);ListView_InsertColumn(task_list,i,&c);}
  start_button=child(w,L"BUTTON",L"开始所选任务",WS_TABSTOP,24,270,150,32,ID_START);mark_button=child(w,L"BUTTON",L"标记完成",WS_TABSTOP,190,270,150,32,ID_MARK);delete_button=child(w,L"BUTTON",L"删除任务",WS_TABSTOP,356,270,150,32,ID_DELETE);free_button=child(w,L"BUTTON",L"选择自由专注",WS_TABSTOP,522,270,154,32,ID_FREE);
  selected_label=child(w,L"STATIC",L"",SS_ENDELLIPSIS,24,316,652,24,0);summary=child(w,L"STATIC",L"",0,24,354,652,24,0);
  child(w,L"STATIC",L"每日目标（1–1440 分钟）",0,24,390,204,24,0);goal_edit=child(w,L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_NUMBER,236,386,80,28,522);SendMessageW(goal_edit,EM_SETLIMITTEXT,4,0);wchar_t number[16];swprintf(number,16,L"%d",data.goal);SetWindowTextW(goal_edit,number);child(w,L"BUTTON",L"保存目标",WS_TABSTOP,332,386,120,28,ID_GOAL);
  child(w,L"STATIC",L"最近七天专注时长",0,24,432,652,24,0);chart=child(w,L"LetterPetC.StudyChart",L"最近七天趋势",0,24,462,652,142,0);
  child(w,L"STATIC",L"暂停和休息不计时；取消保留已投入时间，完成一轮才计入轮数。",0,24,614,652,26,0);return 0;
 }
 case WM_NOTIFY:{NMHDR *n=(void*)lp;if(n->hwndFrom==task_list&&n->code==LVN_ITEMCHANGED&&!updating){NMLISTVIEW *item=(void*)lp;if(item->iItem>=0&&item->iItem<data.count&&(item->uNewState&LVIS_SELECTED)){row_selection=data.tasks[item->iItem].id;study_select(&data,row_selection,locked);companion_save();study_refresh(false);}}return 0;}
 case WM_COMMAND:{if(HIWORD(wp)!=BN_CLICKED)return 0;switch(LOWORD(wp)){
  case ID_ADD:{wchar_t title[STUDY_TITLE];GetWindowTextW(title_edit,title,STUDY_TITLE);uint32_t chosen=data.selected;if(!study_add(&data,title)){MessageBoxW(w,L"请输入任务名称，最多保留 64 项任务。",L"添加任务",MB_ICONINFORMATION);return 0;}row_selection=data.selected;if(locked)data.selected=chosen;SetWindowTextW(title_edit,L"");break;}
  case ID_START:if(!study_select(&data,row_selection,locked)||!row_selection)return 0;if(!companion_start_task())return 0;break;
  case ID_MARK:if(!study_mark(&data,row_selection))return 0;break;
  case ID_DELETE:if(!study_task(&data,row_selection)||data.active==row_selection)return 0;if(MessageBoxW(w,L"删除此任务及其任务内累计记录？每日学习统计仍会保留。",L"删除任务",MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;study_delete(&data,row_selection);row_selection=0;break;
  case ID_FREE:if(!study_select(&data,0,locked))return 0;row_selection=0;ListView_SetItemState(task_list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);break;
  case ID_GOAL:{wchar_t number[16],*end;GetWindowTextW(goal_edit,number,16);long goal=wcstol(number,&end,10);if(!*number||*end||goal<1||goal>1440){MessageBoxW(w,L"每日目标请输入 1–1440 的整数分钟。",L"检查目标",MB_ICONINFORMATION);return 0;}data.goal=(int)goal;break;}
  default:return 0;
 }companion_save();study_refresh(true);return 0;}
 case WM_CLOSE:ShowWindow(w,SW_HIDE);return 0;
 case WM_DESTROY:window=NULL;return 0;
 }
 return DefWindowProcW(w,message,wp,lp);
}
void study_show(HWND owner,HFONT font,int dpi){
 ui_font=font;scale_dpi=dpi;
 if(!window){WNDCLASSW c={0};c.lpfnWndProc=chart_proc;c.hInstance=pet_instance;c.lpszClassName=L"LetterPetC.StudyChart";RegisterClassW(&c);c.lpfnWndProc=study_proc;c.lpszClassName=L"LetterPetC.StudyPanel";c.hCursor=LoadCursor(NULL,IDC_ARROW);c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&c);
  DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;RECT r={0,0,px(700),px(656)};AdjustWindowRect(&r,style,FALSE);MONITORINFO mi={sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&mi);
  window=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,c.lpszClassName,L"桌宠 · 任务与学习统计",style,mi.rcWork.left+max(0,(mi.rcWork.right-mi.rcWork.left-r.right+r.left)/2),mi.rcWork.top+max(0,(mi.rcWork.bottom-mi.rcWork.top-r.bottom+r.top)/2),r.right-r.left,r.bottom-r.top,NULL,NULL,pet_instance,NULL);
 }
 if(window){ui_theme_apply(window);study_refresh(true);pet_show_panel(window,SW_SHOWNORMAL);}
}
void study_close(void){if(window)DestroyWindow(window);window=NULL;}
bool study_dialog_message(MSG *m){return window&&IsWindowVisible(window)&&IsDialogMessageW(window,m);}
static LRESULT CALLBACK repaint_probe(HWND w,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR context){
 if(message==WM_SETTEXT||message==WM_ENABLE||message==WM_PAINT)(*(unsigned*)context)++;
 return DefSubclassProc(w,message,wp,lp);
}
bool study_ui_selftest(HWND owner,HFONT font,int dpi,FILE *log){
 study_defaults(&data);locked=false;study_show(owner,font,dpi);bool ok=window&&task_list&&title_edit&&chart;
 if(ok){SetWindowTextW(title_edit,L"课程报告 \"第一章\"");SendMessageW(window,WM_COMMAND,ID_ADD,0);ok=data.count==1;row_selection=data.tasks[0].id;SendMessageW(window,WM_COMMAND,ID_GOAL,0);SendMessageW(window,WM_COMMAND,ID_MARK,0);ok=ok&&data.tasks[0].done;SendMessageW(window,WM_COMMAND,ID_MARK,0);ok=ok&&!data.tasks[0].done;study_set_locked(true);ok=ok&&!IsWindowEnabled(start_button);study_set_locked(false);SendMessageW(window,WM_COMMAND,ID_FREE,0);ok=ok&&!data.selected;SendMessageW(window,WM_CLOSE,0,0);ok=ok&&!IsWindowVisible(window);}
 if(ok){row_selection=data.tasks[0].id;SendMessageW(window,WM_COMMAND,ID_START,0);ok=data.active==data.tasks[0].id&&locked&&!IsWindowEnabled(start_button);uint32_t bound=data.active;SetWindowTextW(title_edit,L"专注期间新增任务");SendMessageW(window,WM_COMMAND,ID_ADD,0);ok=ok&&data.active==bound&&data.selected==bound;}
 if(ok){
  unsigned paints=0;study_refresh(false);ValidateRect(mark_button,NULL);ValidateRect(chart,NULL);
  bool installed=SetWindowSubclass(mark_button,repaint_probe,777,(DWORD_PTR)&paints)!=0;
  for(int i=0;i<30;i++){study_refresh(false);UpdateWindow(mark_button);}
  bool stable=installed&&paints==0&&!GetUpdateRect(chart,NULL,FALSE);
  if(installed)RemoveWindowSubclass(mark_button,repaint_probe,777);
  fprintf(log,"%s: unchanged task refresh produces no mark-button redraw or chart invalidation.\n",stable?"PASS":"FAIL");ok=ok&&stable;
 }
 fprintf(log,"%s: task panel add, complete, reopen, free focus, start binding and locked retarget protection\n",ok?"PASS":"FAIL");study_close();return ok;
}
bool study_storage_selftest(const wchar_t *path,FILE *log){
 StudyState saved=data;study_defaults(&data);
 study_add(&data,L"软件报告 \"第一章\" = [计划] 😀");uint32_t first=data.selected;
 study_add(&data,L"已完成的任务");study_mark(&data,data.selected);study_select(&data,first,false);study_begin(&data);
 data.goal=90;study_record(&data,20261102,123456,true);study_record(&data,20261103,654321,false);
 wchar_t text[65536];text[0]=0xfeff;int n=study_serialize(text+1,65535);DWORD wrote=0;bool ok=false;
 HANDLE f=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
 if(f!=INVALID_HANDLE_VALUE){DWORD bytes=(DWORD)((n+1)*sizeof(wchar_t));ok=n>0&&WriteFile(f,text,bytes,&wrote,NULL)&&wrote==bytes;CloseHandle(f);}
 study_load(path,true);
 ok=ok&&data.count==2&&data.selected==first&&data.active==first&&data.goal==90&&data.tasks[1].done&&data.tasks[0].milliseconds==777777&&data.tasks[0].rounds==1&&!wcscmp(data.tasks[0].title,L"软件报告 \"第一章\" = [计划] 😀");
 const StudyDay *d=study_day(&data,20261103);ok=ok&&d&&d->milliseconds==654321;
 study_load(path,false);ok=ok&&data.active==0&&data.selected==first;
 WritePrivateProfileStringW(L"task0",L"title_hex",L"not hex",path);WritePrivateProfileStringW(L"study",L"goal",L"0",path);WritePrivateProfileStringW(L"day0",L"date",L"20260230",path);
 study_load(path,true);ok=ok&&data.count==1&&data.selected==0&&data.active==0&&data.goal==120&&!study_day(&data,20260230);
 fprintf(log,"%s: Unicode/punctuation titles, task identity, daily totals and safe corrupt-study recovery\n",ok?"PASS":"FAIL");
 data=saved;return ok;
}
