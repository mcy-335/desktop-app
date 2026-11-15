#include "timetable.h"
#include "ui_theme.h"
#include "timetable_core.h"
#include "companion.h"
#include <commctrl.h>
#include <stdarg.h>
#define ID_ADD 1001
#define ID_EDIT 1002
#define ID_DELETE 1003
#define ID_NEW 1004
#define ID_TEST 1005
#define ID_VIEW 1006
#define ID_FILTER 1007
#define ID_MASTER 1008
#define ID_SOUND 1009
#define ID_INTERRUPT 1010
#define ID_ACK 1011
#define ID_OPEN 1012
#define TT_CAP 131072
typedef struct {int start,end;} TimeRow;
typedef struct {Course course;int day;int64_t expires;bool demo;} Reminder;
static Timetable table;
static HWND owner,window,week_list,detail_list,view_combo,filter_combo,master_check,sound_check,interrupt_check,next_label,status_label,name_edit,room_edit,start_edit,end_edit,lead_edit,enabled_check,day_checks[7];
static HWND notice,notice_list,notice_label;
static HFONT font;
static int dpi=96,row_count,reminder_count;
static TimeRow rows[COURSE_MAX];
static Reminder reminders[COURSE_MAX];
static uint32_t selected;
static bool writable,refreshing,locked,suspended,delivery_dirty;
static double last_tick,last_retry;
static wchar_t path[32768],status[320];
static const wchar_t *days[]={L"周一",L"周二",L"周三",L"周四",L"周五",L"周六",L"周日"};
static int px(int n){return MulDiv(n,dpi,96);}
static CourseClock local_clock(void){SYSTEMTIME t;GetLocalTime(&t);return (CourseClock){course_day(t.wYear,t.wMonth,t.wDay),t.wHour*60+t.wMinute};}
static void message(const wchar_t *format,...){va_list args;va_start(args,format);vswprintf(status,320,format,args);va_end(args);if(status_label)SetWindowTextW(status_label,status);}
static void time_text(int value,wchar_t *text){swprintf(text,8,L"%02d:%02d",value/60,value%60);}
static void day_text(int mask,wchar_t *text,int capacity){text[0]=0;for(int i=0;i<7;i++)if(mask&(1<<i)){if(*text)wcsncat(text,L"、",capacity-wcslen(text)-1);wcsncat(text,days[i],capacity-wcslen(text)-1);}}
static bool add_text(wchar_t *text,int *length,const wchar_t *format,...){va_list args;va_start(args,format);int n=vswprintf(text+*length,TT_CAP-*length,format,args);va_end(args);if(n<0)return false;*length+=n;return true;}
static bool save_table(const Timetable *value){
 if(!writable||!path[0])return false;wchar_t *text=calloc(TT_CAP,sizeof(wchar_t));if(!text)return false;int n=0;
 bool ok=add_text(text,&n,L"\xfeff[timetable]\r\nversion=1\r\ncount=%d\r\nnext_id=%u\r\nenabled=%d\r\nsound=%d\r\ninterrupt=%d\r\n",value->count,value->next_id,value->enabled,value->sound,value->interrupt);
 for(int i=0;i<value->count&&ok;i++){
  const Course *c=&value->courses[i];ok=add_text(text,&n,L"[course%d]\r\nid=%u\r\ndays=%d\r\nstart=%d\r\nend=%d\r\nlead=%d\r\nenabled=%d\r\nlast_day=%d\r\nname_hex=",i,c->id,c->days,c->start,c->end,c->lead,c->enabled,c->last_day);
  for(const wchar_t *p=c->name;*p&&ok;p++)ok=add_text(text,&n,L"%04x",(unsigned)*p);
  if(ok)ok=add_text(text,&n,L"\r\nroom_hex=");for(const wchar_t *p=c->room;*p&&ok;p++)ok=add_text(text,&n,L"%04x",(unsigned)*p);if(ok)ok=add_text(text,&n,L"\r\n");
 }
 wchar_t temp[32768];swprintf(temp,32768,L"%ls.tmp",path);HANDLE file=ok?CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL):INVALID_HANDLE_VALUE;
 if(file!=INVALID_HANDLE_VALUE){DWORD written=0,bytes=n*sizeof(wchar_t);ok=WriteFile(file,text,bytes,&written,NULL)&&written==bytes&&FlushFileBuffers(file);CloseHandle(file);}else ok=false;
 if(ok)ok=MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;if(!ok)DeleteFileW(temp);free(text);return ok;
}
static unsigned number(const wchar_t *section,const wchar_t *key,unsigned fallback,unsigned maximum){wchar_t text[32],*end;GetPrivateProfileStringW(section,key,L"",text,32,path);if(!*text)return fallback;for(const wchar_t *p=text;*p;p++)if(*p<L'0'||*p>L'9')return fallback;unsigned long long n=wcstoull(text,&end,10);return *end||n>maximum?fallback:(unsigned)n;}
static bool decode(const wchar_t *hex,wchar_t *text,int capacity){size_t n=wcslen(hex);if(n%4||n/4>=(size_t)capacity)return false;for(size_t i=0;i<n/4;i++){unsigned value=0;for(int j=0;j<4;j++){wchar_t c=hex[i*4+j];int h=c>=L'0'&&c<=L'9'?c-L'0':c>=L'a'&&c<=L'f'?c-L'a'+10:-1;if(h<0)return false;value=value*16+h;}if(value<32)return false;text[i]=(wchar_t)value;}text[n/4]=0;return true;}
static void load_table(void){
 course_defaults(&table);writable=true;delivery_dirty=false;
 if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES){DWORD error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND){writable=false;message(L"课程表文件暂时无法读取，已停止修改。请检查存档目录。");}return;}
 unsigned count=number(L"timetable",L"count",COURSE_MAX+1,COURSE_MAX);
 if(number(L"timetable",L"version",0,0x7fffffff)!=1||count>COURSE_MAX){writable=false;message(L"课程表版本或数量无法识别，已停止修改原文件。");return;}
 table.enabled=number(L"timetable",L"enabled",1,1)!=0;table.sound=number(L"timetable",L"sound",0,1)!=0;table.interrupt=number(L"timetable",L"interrupt",0,1)!=0;
 int invalid=0;
 for(unsigned i=0;i<count;i++){
  wchar_t section[24],hex[COURSE_ROOM*4];Course c={0};swprintf(section,24,L"course%u",i);
  c.id=number(section,L"id",0,0x7ffffffe);c.days=(int)number(section,L"days",0,127);c.start=(int)number(section,L"start",1440,1439);c.end=(int)number(section,L"end",0,1440);c.lead=(int)number(section,L"lead",121,120);c.last_day=(int)number(section,L"last_day",0,3652059);c.enabled=number(section,L"enabled",1,1)!=0;
  GetPrivateProfileStringW(section,L"name_hex",L"",hex,COURSE_ROOM*4,path);bool ok=decode(hex,c.name,COURSE_NAME);GetPrivateProfileStringW(section,L"room_hex",L"",hex,COURSE_ROOM*4,path);ok=decode(hex,c.room,COURSE_ROOM)&&ok;
  if(!ok||!c.id||!course_valid(&c)||course_find(&table,c.id)>=0){invalid++;continue;}
  table.courses[table.count++]=c;if(table.next_id<=c.id)table.next_id=c.id+1;
 }
 unsigned next=number(L"timetable",L"next_id",1,0x7fffffff);if(next>table.next_id)table.next_id=next;
 if(invalid)message(L"已跳过 %d 条损坏的课程记录，其余课程仍可使用。",invalid);
}
static void update_next(CourseClock now){
 if(!next_label)return;CourseOccurrence event;wchar_t text[320],start[8],end[8];
 if(!course_next(&table,now,&event))swprintf(text,320,L"暂无启用的课程。添加后可查看整周安排；提醒仅在桌宠运行时生效。");
 else{Course *c=&table.courses[event.index];time_text(c->start,start);time_text(c->end,end);int64_t minutes=event.start-((int64_t)now.day*1440+now.minute);swprintf(text,320,L"%ls：%ls %ls %ls–%ls　%ls",minutes<=0?L"正在上课":L"下一门课",c->name,days[course_weekday(event.day)],start,end,*c->room?c->room:L"地点未填写");}
 SetWindowTextW(next_label,text);
}
static void fill_editor(void){
 int index=course_find(&table,selected);Course fresh={0};fresh.days=1<<course_weekday(local_clock().day);fresh.start=480;fresh.end=570;fresh.lead=10;fresh.enabled=true;const Course *c=index>=0?&table.courses[index]:&fresh;
 SetWindowTextW(name_edit,c->name);SetWindowTextW(room_edit,c->room);wchar_t text[16];time_text(c->start,text);SetWindowTextW(start_edit,text);time_text(c->end,text);SetWindowTextW(end_edit,text);swprintf(text,16,L"%d",c->lead);SetWindowTextW(lead_edit,text);SendMessageW(enabled_check,BM_SETCHECK,c->enabled?BST_CHECKED:BST_UNCHECKED,0);for(int i=0;i<7;i++)SendMessageW(day_checks[i],BM_SETCHECK,c->days&(1<<i)?BST_CHECKED:BST_UNCHECKED,0);
}
static int first_day(int mask){for(int i=0;i<7;i++)if(mask&(1<<i))return i;return 7;}
static void refresh(bool rebuild){
 if(!window)return;refreshing=true;SendMessageW(master_check,BM_SETCHECK,table.enabled?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(sound_check,BM_SETCHECK,table.sound?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(interrupt_check,BM_SETCHECK,table.interrupt?BST_CHECKED:BST_UNCHECKED,0);
 int view=(int)SendMessageW(view_combo,CB_GETCURSEL,0,0),filter=(int)SendMessageW(filter_combo,CB_GETCURSEL,0,0);ShowWindow(week_list,view==0?SW_SHOW:SW_HIDE);ShowWindow(detail_list,view==1?SW_SHOW:SW_HIDE);EnableWindow(filter_combo,view==1);
 if(rebuild){
  row_count=0;ListView_DeleteAllItems(week_list);ListView_DeleteAllItems(detail_list);
  int order[COURSE_MAX];for(int i=0;i<table.count;i++)order[i]=i;
  for(int i=1;i<table.count;i++){int v=order[i],j=i;while(j>0){Course *a=&table.courses[order[j-1]],*b=&table.courses[v];int da=first_day(a->days),db=first_day(b->days);if(da<db||(da==db&&(a->start<b->start||(a->start==b->start&&a->id<b->id))))break;order[j]=order[j-1];j--;}order[j]=v;}
  for(int k=0;k<table.count;k++){
   Course *c=&table.courses[order[k]];int row=-1;for(int r=0;r<row_count;r++)if(rows[r].start==c->start&&rows[r].end==c->end)row=r;
   if(row<0)rows[row_count++]=(TimeRow){c->start,c->end};
   if(filter>0&&!(c->days&(1<<(filter-1))))continue;
   wchar_t day[64],range[24],start[8],end[8],lead[24];day_text(c->days,day,64);time_text(c->start,start);time_text(c->end,end);swprintf(range,24,L"%ls–%ls",start,end);swprintf(lead,24,L"提前 %d 分钟",c->lead);
   int at=ListView_GetItemCount(detail_list);LVITEMW item={0};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=at;item.pszText=day;item.lParam=c->id;ListView_InsertItem(detail_list,&item);ListView_SetItemText(detail_list,at,1,range);ListView_SetItemText(detail_list,at,2,c->name);ListView_SetItemText(detail_list,at,3,c->room);ListView_SetItemText(detail_list,at,4,lead);ListView_SetItemText(detail_list,at,5,c->enabled?L"启用":L"停用");if(c->id==selected)ListView_SetItemState(detail_list,at,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
  }
  for(int i=1;i<row_count;i++){TimeRow value=rows[i];int j=i;while(j>0&&(rows[j-1].start>value.start||(rows[j-1].start==value.start&&rows[j-1].end>value.end))){rows[j]=rows[j-1];j--;}rows[j]=value;}
  for(int r=0;r<row_count;r++){
   wchar_t range[24],start[8],end[8];time_text(rows[r].start,start);time_text(rows[r].end,end);swprintf(range,24,L"%ls–%ls",start,end);LVITEMW item={0};item.mask=LVIF_TEXT;item.iItem=r;item.pszText=range;ListView_InsertItem(week_list,&item);
   for(int d=0;d<7;d++){wchar_t cell[1024]=L"";for(int i=0;i<table.count;i++){Course *c=&table.courses[i];if(c->start!=rows[r].start||c->end!=rows[r].end||!(c->days&(1<<d)))continue;wchar_t part[180];swprintf(part,180,L"%ls%ls%ls%ls",c->enabled?L"":L"(停) ",c->name,*c->room?L" · ":L"",c->room);if(*cell)wcsncat(cell,L" / ",1023-wcslen(cell));wcsncat(cell,part,1023-wcslen(cell));}ListView_SetItemText(week_list,r,d+1,cell);}
  }
 }
 const int ids[]={ID_ADD,ID_EDIT,ID_DELETE};for(int i=0;i<3;i++)EnableWindow(GetDlgItem(window,ids[i]),writable);EnableWindow(master_check,writable);EnableWindow(sound_check,writable);EnableWindow(interrupt_check,writable);SetWindowTextW(status_label,status);update_next(local_clock());refreshing=false;
}
static HWND child(HWND w,const wchar_t *cls,const wchar_t *text,DWORD style,int x,int y,int width,int height,int id){HWND c=CreateWindowW(cls,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(width),px(height),w,(HMENU)(INT_PTR)id,pet_instance,NULL);SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;}
static void render_notice(void);
static void clear_reminders(void){reminder_count=0;if(notice)ShowWindow(notice,SW_HIDE);}
static void remove_reminders(uint32_t id){for(int i=reminder_count-1;i>=0;i--)if(reminders[i].course.id==id){memmove(&reminders[i],&reminders[i+1],(reminder_count-i-1)*sizeof(Reminder));reminder_count--;}if(!reminder_count&&notice)ShowWindow(notice,SW_HIDE);else render_notice();}
static LRESULT CALLBACK notice_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
 switch(msg){
 case WM_CREATE:
  notice_label=child(w,L"STATIC",L"课程提醒",0,16,14,468,26,0);notice_list=child(w,WC_LISTVIEWW,L"即将开始的课程",WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL,16,50,468,124,0);ListView_SetExtendedListViewStyle(notice_list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
  {const wchar_t *names[]={L"上课时间",L"课程",L"地点"};int widths[]={92,178,174};for(int i=0;i<3;i++){LVCOLUMNW c={0};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=(LPWSTR)names[i];c.cx=px(widths[i]);ListView_InsertColumn(notice_list,i,&c);}}
  child(w,L"BUTTON",L"知道了",WS_TABSTOP,16,192,220,34,ID_ACK);child(w,L"BUTTON",L"打开课程表",WS_TABSTOP,252,192,232,34,ID_OPEN);return 0;
 case WM_COMMAND:if(LOWORD(wp)==ID_ACK){clear_reminders();return 0;}if(LOWORD(wp)==ID_OPEN){clear_reminders();timetable_show();return 0;}break;
 case WM_CLOSE:clear_reminders();return 0;case WM_DESTROY:notice=NULL;notice_list=notice_label=NULL;return 0;
 }return DefWindowProcW(w,msg,wp,lp);
}
static bool ensure_notice(void){
 if(notice)return true;WNDCLASSW c={0};c.lpfnWndProc=notice_proc;c.hInstance=pet_instance;c.lpszClassName=L"LetterPetC.CourseNotice";c.hCursor=LoadCursor(NULL,IDC_ARROW);c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&c);
 DWORD style=WS_POPUP|WS_CAPTION|WS_SYSMENU;RECT rect={0,0,px(500),px(242)};AdjustWindowRect(&rect,style,FALSE);MONITORINFO info={sizeof(info)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&info);int width=rect.right-rect.left,height=rect.bottom-rect.top;
 notice=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,c.lpszClassName,L"桌宠 · 课程提醒",style,max(info.rcWork.left,info.rcWork.right-width-px(16)),max(info.rcWork.top,info.rcWork.bottom-height-px(16)),width,height,owner,NULL,pet_instance,NULL);ui_theme_apply(notice);return notice!=NULL;
}
static void render_notice(void){
 if(!reminder_count||!ensure_notice())return;ListView_DeleteAllItems(notice_list);wchar_t title[100];bool demo_only=true;for(int i=0;i<reminder_count;i++)if(!reminders[i].demo)demo_only=false;swprintf(title,100,L"%ls（%d 门）",demo_only?L"测试提醒，不改变正式提醒记录":L"请留意即将开始的课程",reminder_count);SetWindowTextW(notice_label,title);
 for(int i=0;i<reminder_count;i++){wchar_t when[40],start[8],name[COURSE_NAME+12];Reminder *r=&reminders[i];time_text(r->course.start,start);swprintf(when,40,L"%ls %ls",days[course_weekday(r->day)],start);swprintf(name,COURSE_NAME+12,L"%ls%ls",r->demo?L"（测试）":L"",r->course.name);LVITEMW item={0};item.mask=LVIF_TEXT;item.iItem=i;item.pszText=when;ListView_InsertItem(notice_list,&item);ListView_SetItemText(notice_list,i,1,name);ListView_SetItemText(notice_list,i,2,*r->course.room?r->course.room:L"地点未填写");}
}
static bool alerts_allowed(void){return !locked&&!suspended&&(table.interrupt||(!companion_is_quiet()&&!settings.game));}
static void process_reminders(CourseClock now,bool allowed,bool deliver){
 int64_t absolute=(int64_t)now.day*1440+now.minute;bool changed=false;
 for(int i=reminder_count-1;i>=0;i--)if(reminders[i].expires<=absolute){memmove(&reminders[i],&reminders[i+1],(reminder_count-i-1)*sizeof(Reminder));reminder_count--;changed=true;}
 if(!table.enabled||!allowed){if(changed&&reminder_count)render_notice();if(notice)ShowWindow(notice,SW_HIDE);return;}
 if(deliver){CourseOccurrence due[COURSE_MAX];int count=course_due(&table,now,due,COURSE_MAX);
  if(count&&ensure_notice()){
   int delivered=0;for(int i=0;i<count;i++){CourseOccurrence event=due[i];Course *c=&table.courses[event.index];if(reminder_count==COURSE_MAX){for(int j=0;j<reminder_count;j++)if(reminders[j].demo){memmove(&reminders[j],&reminders[j+1],(reminder_count-j-1)*sizeof(Reminder));reminder_count--;break;}}if(reminder_count>=COURSE_MAX)continue;c->last_day=event.day;reminders[reminder_count++]=(Reminder){*c,event.day,event.expires,false};delivered++;}
   if(delivered){delivery_dirty=!save_table(&table);if(delivery_dirty)message(L"课程提醒已显示，但提醒记录保存失败。重启可能重复提醒，请检查存档目录。");changed=true;if(table.sound&&!test_mode)MessageBeep(MB_ICONINFORMATION);}
  }
 }
 if(!reminder_count){if(notice)ShowWindow(notice,SW_HIDE);return;}if(changed)render_notice();if(notice)ShowWindow(notice,SW_SHOWNOACTIVATE);
}
static void test_notice(void){
 if(locked||suspended){message(L"锁屏或休眠期间暂不显示测试提醒。");return;}
 if(!alerts_allowed()){message(L"当前为安静或游戏模式。可关闭该模式，或勾选“安静 / 游戏模式仍提醒”。");return;}
 CourseClock now=local_clock();int index=course_find(&table,selected);Course demo={0};if(index>=0)demo=table.courses[index];else{wcscpy(demo.name,L"示例课程");wcscpy(demo.room,L"教学楼 A-201");demo.start=(now.minute+10)%1440;}
 if(!ensure_notice()){message(L"无法创建课程提醒窗口，请重试。");return;}
 // A preview is added alongside real reminders so testing never dismisses one.
 if(reminder_count<COURSE_MAX)reminders[reminder_count++]=(Reminder){demo,now.day,(int64_t)now.day*1440+now.minute+2,true};render_notice();ShowWindow(notice,SW_SHOWNOACTIVATE);if(table.sound&&!test_mode)MessageBeep(MB_ICONINFORMATION);message(L"已显示测试提醒。测试不记录为正式课程提醒。");
}
static bool read_editor(Course *c){
 memset(c,0,sizeof(*c));GetWindowTextW(name_edit,c->name,COURSE_NAME);GetWindowTextW(room_edit,c->room,COURSE_ROOM);wchar_t start[16],end[16],lead[16],*tail;GetWindowTextW(start_edit,start,16);GetWindowTextW(end_edit,end,16);GetWindowTextW(lead_edit,lead,16);
 c->start=course_parse_time(start,false);c->end=course_parse_time(end,true);long minutes=wcstol(lead,&tail,10);c->lead=*lead&&!*tail?(int)minutes:-1;c->enabled=SendMessageW(enabled_check,BM_GETCHECK,0,0)==BST_CHECKED;for(int i=0;i<7;i++)if(SendMessageW(day_checks[i],BM_GETCHECK,0,0)==BST_CHECKED)c->days|=1<<i;
 return course_valid(c);
}
static bool edit_course(bool editing){
 if(!writable)return false;Course c;if(!read_editor(&c)){message(L"请填写课程名、至少一个星期、有效上下课时间（如 08:00–09:30）和 0–120 分钟提醒；下课须晚于上课。");return false;}
 if(editing&&course_find(&table,selected)<0){message(L"请先选择需要修改的课程。");return false;}
 int conflict=course_conflict(&table,&c,editing?selected:0);
 if(conflict>=0&&!test_mode){wchar_t prompt[240];swprintf(prompt,240,L"时间与「%ls」有重叠，仍然保存这门课吗？",table.courses[conflict].name);if(MessageBoxW(window,prompt,L"课程时间重叠",MB_YESNO|MB_ICONQUESTION)!=IDYES)return false;}
 Timetable next=table;if(!course_put(&next,&c,editing?selected:0)){message(L"无法保存课程。最多添加 128 门，请先移除不需要的课程。");return false;}
 if(!save_table(&next)){message(L"保存失败，课程表未更改。请检查存档目录。");return false;}
 if(editing)remove_reminders(selected);table=next;delivery_dirty=false;if(!editing)selected=table.courses[table.count-1].id;message(L"课程已保存，按所选星期每周重复；提醒需要桌宠保持运行。");refresh(true);fill_editor();return true;
}
static bool delete_course(bool confirm){
 if(!writable||course_find(&table,selected)<0){message(L"请先选择要删除的课程。");return false;}
 if(confirm&&MessageBoxW(window,L"删除所选课程及其每周提醒？",L"删除课程",MB_YESNO|MB_ICONQUESTION)!=IDYES)return false;
 Timetable next=table;course_delete(&next,selected);if(!save_table(&next)){message(L"保存失败，课程仍然保留。");return false;}
 remove_reminders(selected);table=next;selected=0;delivery_dirty=false;message(L"课程已删除。");refresh(true);fill_editor();return true;
}
static void set_preferences(void){
 if(!writable)return;Timetable next=table;next.enabled=SendMessageW(master_check,BM_GETCHECK,0,0)==BST_CHECKED;next.sound=SendMessageW(sound_check,BM_GETCHECK,0,0)==BST_CHECKED;next.interrupt=SendMessageW(interrupt_check,BM_GETCHECK,0,0)==BST_CHECKED;
 if(!save_table(&next)){message(L"提醒设置保存失败，仍使用原设置。");refresh(false);return;}table=next;delivery_dirty=false;if(!table.enabled)clear_reminders();process_reminders(local_clock(),alerts_allowed(),false);message(table.enabled?L"课程提醒已开启。默认无提示音，可按需勾选。":L"已关闭课程提醒，课程表内容保留。");refresh(false);
}
static LRESULT CALLBACK timetable_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
 switch(msg){
 case WM_CREATE:
  master_check=child(w,L"BUTTON",L"开启课程提醒（桌宠运行时）",WS_TABSTOP|BS_AUTOCHECKBOX,20,14,278,28,ID_MASTER);sound_check=child(w,L"BUTTON",L"提醒时播放提示音",WS_TABSTOP|BS_AUTOCHECKBOX,310,14,220,28,ID_SOUND);interrupt_check=child(w,L"BUTTON",L"安静 / 游戏模式仍提醒",WS_TABSTOP|BS_AUTOCHECKBOX,540,14,252,28,ID_INTERRUPT);child(w,L"BUTTON",L"测试提醒",WS_TABSTOP,808,12,132,30,ID_TEST);
  child(w,L"STATIC",L"查看",0,20,58,48,24,0);view_combo=child(w,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,68,52,196,160,ID_VIEW);SendMessageW(view_combo,CB_ADDSTRING,0,(LPARAM)L"整周课程表");SendMessageW(view_combo,CB_ADDSTRING,0,(LPARAM)L"课程明细");SendMessageW(view_combo,CB_SETCURSEL,0,0);
  child(w,L"STATIC",L"筛选",0,288,58,48,24,0);filter_combo=child(w,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,336,52,164,240,ID_FILTER);SendMessageW(filter_combo,CB_ADDSTRING,0,(LPARAM)L"全部星期");for(int i=0;i<7;i++)SendMessageW(filter_combo,CB_ADDSTRING,0,(LPARAM)days[i]);SendMessageW(filter_combo,CB_SETCURSEL,0,0);child(w,L"STATIC",L"点击课程可编辑；双击周视图单元格可查看明细。",0,520,58,420,26,0);
  next_label=child(w,L"STATIC",L"",SS_PATHELLIPSIS,20,94,920,28,0);
  week_list=child(w,WC_LISTVIEWW,L"整周课程表",WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,20,132,920,206,1020);detail_list=child(w,WC_LISTVIEWW,L"课程明细",WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,20,132,920,206,1021);
  ListView_SetExtendedListViewStyle(week_list,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER|LVS_EX_INFOTIP);ListView_SetExtendedListViewStyle(detail_list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
  {LVCOLUMNW col={0};col.mask=LVCF_TEXT|LVCF_WIDTH;col.pszText=L"时间";col.cx=px(120);ListView_InsertColumn(week_list,0,&col);for(int i=0;i<7;i++){col.pszText=(LPWSTR)days[i];col.cx=px(110);ListView_InsertColumn(week_list,i+1,&col);}const wchar_t *names[]={L"每周",L"时间",L"课程",L"地点",L"提醒",L"状态"};int widths[]={150,120,230,214,110,62};for(int i=0;i<6;i++){col.pszText=(LPWSTR)names[i];col.cx=px(widths[i]);ListView_InsertColumn(detail_list,i,&col);}}
  child(w,L"STATIC",L"课程名",0,20,358,72,26,0);name_edit=child(w,L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,92,352,350,32,1030);SendMessageW(name_edit,EM_SETLIMITTEXT,COURSE_NAME-1,0);child(w,L"STATIC",L"上课地点",0,464,358,88,26,0);room_edit=child(w,L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,552,352,388,32,1031);SendMessageW(room_edit,EM_SETLIMITTEXT,COURSE_ROOM-1,0);
  child(w,L"STATIC",L"每周重复",0,20,406,88,26,0);for(int i=0;i<7;i++)day_checks[i]=child(w,L"BUTTON",days[i],WS_TABSTOP|BS_AUTOCHECKBOX,116+i*114,400,104,30,1040+i);
  child(w,L"STATIC",L"上课",0,20,454,50,26,0);start_edit=child(w,L"EDIT",L"08:00",WS_TABSTOP|WS_BORDER,72,448,90,32,1032);child(w,L"STATIC",L"下课",0,186,454,50,26,0);end_edit=child(w,L"EDIT",L"09:30",WS_TABSTOP|WS_BORDER,238,448,90,32,1033);SendMessageW(start_edit,EM_SETLIMITTEXT,5,0);SendMessageW(end_edit,EM_SETLIMITTEXT,5,0);
  child(w,L"STATIC",L"提前提醒",0,352,454,92,26,0);lead_edit=child(w,L"EDIT",L"10",WS_TABSTOP|WS_BORDER|ES_NUMBER,446,448,64,32,1034);SendMessageW(lead_edit,EM_SETLIMITTEXT,3,0);child(w,L"STATIC",L"分钟（0–120）",0,520,454,150,26,0);enabled_check=child(w,L"BUTTON",L"启用此课程",WS_TABSTOP|BS_AUTOCHECKBOX,716,448,224,30,1035);
  child(w,L"BUTTON",L"添加新课程",WS_TABSTOP,20,500,218,36,ID_ADD);child(w,L"BUTTON",L"保存所选修改",WS_TABSTOP,254,500,218,36,ID_EDIT);child(w,L"BUTTON",L"删除所选课程",WS_TABSTOP,488,500,218,36,ID_DELETE);child(w,L"BUTTON",L"清空编辑 / 新建",WS_TABSTOP,722,500,218,36,ID_NEW);
  status_label=child(w,L"STATIC",L"",0,20,554,920,52,0);child(w,L"STATIC",L"每门课可选多个星期；跨午夜课程请拆为两条。关闭窗口后仍会提醒，退出桌宠后停止提醒。",0,20,618,920,42,0);return 0;
 case WM_NOTIFY:{NMHDR *n=(void*)lp;
  if(n->hwndFrom==detail_list&&n->code==LVN_ITEMCHANGED&&!refreshing){NMLISTVIEW *change=(void*)lp;if(change->iItem>=0&&(change->uNewState&LVIS_SELECTED)){LVITEMW item={0};item.mask=LVIF_PARAM;item.iItem=change->iItem;ListView_GetItem(detail_list,&item);selected=(uint32_t)item.lParam;fill_editor();}}
  if(n->hwndFrom==week_list&&(n->code==NM_CLICK||n->code==NM_DBLCLK)){NMITEMACTIVATE *hit=(void*)lp;if(hit->iItem>=0&&hit->iItem<row_count&&hit->iSubItem>=1&&hit->iSubItem<=7){TimeRow row=rows[hit->iItem];int day=hit->iSubItem-1;for(int i=0;i<table.count;i++){Course *c=&table.courses[i];if(c->start==row.start&&c->end==row.end&&(c->days&(1<<day))){selected=c->id;fill_editor();break;}}if(n->code==NM_DBLCLK){SendMessageW(view_combo,CB_SETCURSEL,1,0);SendMessageW(filter_combo,CB_SETCURSEL,day+1,0);refresh(true);}}}return 0;}
 case WM_COMMAND:
  if((LOWORD(wp)==ID_VIEW||LOWORD(wp)==ID_FILTER)&&HIWORD(wp)==CBN_SELCHANGE){refresh(true);return 0;}
  if(HIWORD(wp)!=BN_CLICKED)return 0;switch(LOWORD(wp)){
   case ID_ADD:edit_course(false);break;case ID_EDIT:edit_course(true);break;case ID_DELETE:delete_course(!test_mode);break;
   case ID_NEW:selected=0;fill_editor();message(L"正在编辑新课程，填写后点击“添加新课程”。");break;
   case ID_MASTER:case ID_SOUND:case ID_INTERRUPT:set_preferences();break;case ID_TEST:test_notice();break;
  }return 0;
 case WM_CLOSE:ShowWindow(w,SW_HIDE);return 0;case WM_DESTROY:window=NULL;return 0;
 }return DefWindowProcW(w,msg,wp,lp);
}
void timetable_init(HWND parent,const wchar_t *settings_path){
 owner=parent;path[0]=status[0]=0;writable=false;selected=0;locked=suspended=delivery_dirty=false;last_tick=last_retry=0;reminder_count=0;course_defaults(&table);
 status_label=next_label=NULL;
 HDC dc=GetDC(owner);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(owner,dc);font=ui_theme_font(px(15),FW_NORMAL,false);
 if(settings_path&&wcslen(settings_path)<32600){wcscpy(path,settings_path);wchar_t *slash=wcsrchr(path,L'\\');if(slash){wcscpy(slash+1,L"timetable.ini");load_table();}else path[0]=0;}
 if(!path[0])message(L"课程表存档路径不可用，暂时无法保存。");else if(!status[0])message(L"添加课程后每周重复提醒。默认提前 10 分钟，无提示音；可分别调整。");
}
bool timetable_flush(void){
 if(!path[0]||!writable)return false;
 if(delivery_dirty&&!save_table(&table))return false;
 delivery_dirty=false;return true;
}
void timetable_show(void){
 if(!owner)return;if(!window){WNDCLASSW cls={0};cls.lpfnWndProc=timetable_proc;cls.hInstance=pet_instance;cls.lpszClassName=L"LetterPetC.TimetablePanel";cls.hCursor=LoadCursor(NULL,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&cls);DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;RECT rect={0,0,px(960),px(672)};AdjustWindowRect(&rect,style,FALSE);MONITORINFO info={sizeof(info)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&info);int width=rect.right-rect.left,height=rect.bottom-rect.top;window=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,cls.lpszClassName,L"桌宠 · 每周课程表",style,info.rcWork.left+max(0,(info.rcWork.right-info.rcWork.left-width)/2),info.rcWork.top+max(0,(info.rcWork.bottom-info.rcWork.top-height)/2),width,height,NULL,NULL,pet_instance,NULL);}
 if(window){ui_theme_apply(window);ui_theme_role(next_label,UI_READOUT);ui_theme_role(status_label,UI_SURFACE_TEXT);refresh(true);fill_editor();pet_show_panel(window,SW_SHOWNORMAL);}
}
void timetable_tick(double now){
 if(!owner||test_mode)return;if(now>=last_tick&&now-last_tick<1000)return;last_tick=now;CourseClock clock=local_clock();process_reminders(clock,alerts_allowed(),true);update_next(clock);
 if(delivery_dirty&&now-last_retry>=60000){last_retry=now;if(save_table(&table)){delivery_dirty=false;message(L"课程提醒记录已补存。");}}
}
void timetable_session(bool value){locked=value;if(locked&&notice)ShowWindow(notice,SW_HIDE);last_tick=0;}
void timetable_power(bool value){suspended=value;if(suspended&&notice)ShowWindow(notice,SW_HIDE);last_tick=0;}
void timetable_shutdown(void){if(delivery_dirty)save_table(&table);clear_reminders();if(notice)DestroyWindow(notice);if(window)DestroyWindow(window);window=NULL;notice=NULL;owner=NULL;DeleteObject(font);font=NULL;}
bool timetable_dialog_message(MSG *msg){return (window&&IsWindowVisible(window)&&IsDialogMessageW(window,msg))||(notice&&IsWindowVisible(notice)&&IsDialogMessageW(notice,msg));}
int timetable_preview_test(void){
 wchar_t directory[32768],settings_path[32768];swprintf(directory,32768,L"%ls\\timetable-preview",app_dir);CreateDirectoryW(directory,NULL);swprintf(settings_path,32768,L"%ls\\settings.ini",directory);HWND host=CreateWindowW(L"STATIC",L"",WS_POPUP,0,0,1,1,NULL,NULL,pet_instance,NULL);if(!host)return 1;timetable_init(host,settings_path);
 if(!table.count){Course c={0};wcscpy(c.name,L"高等数学");wcscpy(c.room,L"教学楼 A-201");c.start=480;c.end=570;c.days=1|4;c.lead=10;c.enabled=true;course_put(&table,&c,0);wcscpy(c.name,L"程序设计");wcscpy(c.room,L"实验楼 302");c.start=600;c.end=690;c.days=2|8;course_put(&table,&c,0);}
 timetable_show();ULONGLONG deadline=GetTickCount64()+90000;MSG msg;while(window&&IsWindowVisible(window)&&GetTickCount64()<deadline){while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){if(!timetable_dialog_message(&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}MsgWaitForMultipleObjects(0,NULL,FALSE,100,QS_ALLINPUT);}timetable_shutdown();DestroyWindow(host);return 0;
}
bool timetable_ui_selftest(FILE *log){
 wchar_t directory[32768],settings_path[32768];swprintf(directory,32768,L"%ls\\timetable-test-%lu",app_dir,GetCurrentProcessId());if(!CreateDirectoryW(directory,NULL))return false;swprintf(settings_path,32768,L"%ls\\settings.ini",directory);bool old_game=settings.game;settings.game=false;
 timetable_init(pet_window,settings_path);timetable_show();bool ui=window&&week_list&&detail_list&&name_edit&&day_checks[6]&&lead_edit;
 uint32_t first=0,second=0;int monday=course_day(2026,11,2);
 if(ui){
  SetWindowTextW(name_edit,L"计算机 [实验] = \"A\" 😀");SetWindowTextW(room_edit,L"实验楼 · 302");SetWindowTextW(start_edit,L"08:15");SetWindowTextW(end_edit,L"09:45");SetWindowTextW(lead_edit,L"15");for(int i=0;i<7;i++)SendMessageW(day_checks[i],BM_SETCHECK,i==0||i==2?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(window,WM_COMMAND,ID_ADD,0);
  ui=table.count==1&&table.courses[0].days==5&&table.courses[0].start==495&&table.courses[0].lead==15;first=selected;
  SetWindowTextW(room_edit,L"新教室 B-201 😀");SendMessageW(window,WM_COMMAND,ID_EDIT,0);ui=ui&&table.count==1&&!wcscmp(table.courses[0].room,L"新教室 B-201 😀");
  SetWindowTextW(name_edit,L"大学物理");SetWindowTextW(room_edit,L"主楼 105");SetWindowTextW(start_edit,L"08:20");SetWindowTextW(end_edit,L"09:00");SetWindowTextW(lead_edit,L"20");SendMessageW(day_checks[2],BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(window,WM_COMMAND,ID_ADD,0);second=selected;ui=ui&&table.count==2&&second!=first;
 }
 fprintf(log,"%s: native weekly course add/edit, multiple weekdays and Unicode fields\n",ui?"PASS":"FAIL");
 bool views=false,validation=false,stored=false,policy=false,delivery=false,dedup=false,wake=false,preview=false,atomic=false,corrupt=false,removed=false;
 if(ui){
  wchar_t cell[256];ListView_GetItemText(week_list,0,1,cell,256);views=ListView_GetItemCount(week_list)==2&&wcsstr(cell,L"计算机")!=NULL;
  NMITEMACTIVATE click={0};click.hdr.hwndFrom=week_list;click.hdr.code=NM_DBLCLK;click.iItem=0;click.iSubItem=1;SendMessageW(window,WM_NOTIFY,0,(LPARAM)&click);views=views&&selected==first&&SendMessageW(view_combo,CB_GETCURSEL,0,0)==1&&ListView_GetItemCount(detail_list)==2;
  SendMessageW(filter_combo,CB_SETCURSEL,7,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(ID_FILTER,CBN_SELCHANGE),0);views=views&&ListView_GetItemCount(detail_list)==0;SendMessageW(filter_combo,CB_SETCURSEL,0,0);refresh(true);
  SetWindowTextW(start_edit,L"25:00");SendMessageW(window,WM_COMMAND,ID_ADD,0);validation=table.count==2;fill_editor();
  table.interrupt=true;save_table(&table);load_table();stored=table.count==2&&table.courses[0].days==5&&!wcscmp(table.courses[0].name,L"计算机 [实验] = \"A\" 😀")&&!wcscmp(table.courses[0].room,L"新教室 B-201 😀")&&table.interrupt&&!table.sound;
  table.interrupt=false;settings.game=true;policy=!alerts_allowed();table.interrupt=true;policy=policy&&alerts_allowed();timetable_session(true);policy=policy&&!alerts_allowed();timetable_session(false);timetable_power(true);policy=policy&&!alerts_allowed();timetable_power(false);settings.game=false;policy=policy&&alerts_allowed();
  process_reminders((CourseClock){monday,480},false,true);policy=policy&&reminder_count==0&&table.courses[0].last_day==0;
  HWND foreground=GetForegroundWindow();process_reminders((CourseClock){monday,480},true,true);delivery=reminder_count==2&&notice&&IsWindowVisible(notice)&&ListView_GetItemCount(notice_list)==2&&GetForegroundWindow()==foreground&&table.courses[0].last_day==monday&&table.courses[1].last_day==monday;
  process_reminders((CourseClock){monday,480},true,true);delivery=delivery&&reminder_count==2;process_reminders((CourseClock){monday,501},false,true);process_reminders((CourseClock){monday,502},true,true);delivery=delivery&&reminder_count==1&&ListView_GetItemCount(notice_list)==1;process_reminders((CourseClock){monday,505},true,true);delivery=delivery&&reminder_count==0&&!IsWindowVisible(notice);clear_reminders();load_table();process_reminders((CourseClock){monday,480},true,true);dedup=reminder_count==0&&table.courses[0].last_day==monday;
  SendMessageW(window,WM_CLOSE,0,0);process_reminders((CourseClock){monday+7,480},false,true);wake=reminder_count==0;process_reminders((CourseClock){monday+7,481},true,true);wake=wake&&reminder_count==2&&IsWindowVisible(notice)&&!IsWindowVisible(window);SendMessageW(notice,WM_COMMAND,ID_ACK,0);process_reminders((CourseClock){monday+7,481},true,true);wake=wake&&reminder_count==0;process_reminders((CourseClock){monday+14,510},true,true);wake=wake&&reminder_count==0;
  selected=first;fill_editor();int stamp=table.courses[0].last_day;test_notice();preview=reminder_count==1&&reminders[0].demo&&table.courses[0].last_day==stamp;clear_reminders();
  Timetable saved=table;wchar_t saved_path[32768];wcscpy(saved_path,path);swprintf(path,32768,L"%ls\\missing-directory\\timetable.ini",directory);SetWindowTextW(name_edit,L"保存失败时不应更改");atomic=!edit_course(true)&&!wcscmp(table.courses[0].name,saved.courses[0].name);process_reminders((CourseClock){monday+14,480},true,true);atomic=atomic&&delivery_dirty&&reminder_count==2;process_reminders((CourseClock){monday+14,480},true,true);atomic=atomic&&reminder_count==2;wcscpy(path,saved_path);atomic=save_table(&table)&&atomic;clear_reminders();saved=table;fill_editor();
  WritePrivateProfileStringW(L"course0",L"name_hex",L"bad-hex",path);load_table();corrupt=table.count==1&&table.courses[0].id==second;table=saved;save_table(&table);
  WritePrivateProfileStringW(L"timetable",L"version",L"99",path);load_table();corrupt=corrupt&&!writable&&!save_table(&table);WritePrivateProfileStringW(L"timetable",L"version",L"1",path);WritePrivateProfileStringW(L"timetable",L"count",L"999",path);load_table();corrupt=corrupt&&!writable;writable=true;table=saved;save_table(&table);load_table();corrupt=corrupt&&writable&&table.count==2;
  refresh(true);SendMessageW(master_check,BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(window,WM_COMMAND,ID_MASTER,0);process_reminders((CourseClock){monday+21,480},true,true);policy=policy&&!table.enabled&&reminder_count==0;SendMessageW(master_check,BM_SETCHECK,BST_CHECKED,0);SendMessageW(window,WM_COMMAND,ID_MASTER,0);policy=policy&&table.enabled;
  selected=first;SendMessageW(window,WM_COMMAND,ID_DELETE,0);load_table();removed=table.count==1&&table.courses[0].id==second&&selected==0;
 }
 fprintf(log,"%s: whole-week columns, course-cell selection, detail view and weekday filtering\n",views?"PASS":"FAIL");fprintf(log,"%s: invalid editor times do not modify the weekly schedule\n",validation?"PASS":"FAIL");fprintf(log,"%s: Unicode schedule and reminder preferences persist atomically\n",stored?"PASS":"FAIL");fprintf(log,"%s: global off, game-mode override, lock and sleep defer reminders without consuming them\n",policy?"PASS":"FAIL");fprintf(log,"%s: simultaneous course reminders aggregate without taking focus\n",delivery?"PASS":"FAIL");fprintf(log,"%s: persisted delivery stamps prevent reminders repeating after restart\n",dedup?"PASS":"FAIL");fprintf(log,"%s: closed panel keeps reminding, deferred reminders catch up and expired ones stay silent\n",wake?"PASS":"FAIL");fprintf(log,"%s: manual reminder preview never consumes a real occurrence\n",preview?"PASS":"FAIL");fprintf(log,"%s: failed edits roll back; failed delivery saves remain deduplicated in memory\n",atomic?"PASS":"FAIL");fprintf(log,"%s: corrupt records recover and future versions or invalid counts refuse overwrites\n",corrupt?"PASS":"FAIL");fprintf(log,"%s: native course deletion persists and preserves other course IDs\n",removed?"PASS":"FAIL");
 timetable_shutdown();settings.game=old_game;return ui&&views&&validation&&stored&&policy&&delivery&&dedup&&wake&&preview&&atomic&&corrupt&&removed;
}
