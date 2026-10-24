#include "pet.h"
#include <commctrl.h>
#include <tlhelp32.h>
#include <psapi.h>
typedef struct {DWORD pid;ULONGLONG identity,cpu_time;double cpu;SIZE_T memory;bool readable,mem_ok;wchar_t name[260],label[260];} Process;
typedef struct {Process rows[4096];int count;double cpu;DWORD memory;ULONGLONG total,idle;} Snapshot;
static HWND monitor_window,list;
static HANDLE stop_event,worker;
static Snapshot *shown;
static int sort_column=3;
static bool descending=true;
static int dpi=96;
static HFONT body_font,small_font,title_font,metric_font,heading_font;
static bool updating_list,has_selection;
static DWORD selected_pid;
static ULONGLONG selected_identity;
static SYSTEMTIME sampled_at;
static const COLORREF background=RGB(27,21,25),surface=RGB(39,28,33);
static const COLORREF border=RGB(78,56,57),gold=RGB(220,183,119);
static const COLORREF ink=RGB(242,232,219),muted=RGB(178,159,156);
static const wchar_t *column_titles[]={L"进程名称",L"原进程名",L"PID",L"CPU",L"内存"};

#define WM_SAMPLE (WM_APP+22)
static ULONGLONG time64(FILETIME f){return ((ULONGLONG)f.dwHighDateTime<<32)|f.dwLowDateTime;}
static void display_name(Process *p,const wchar_t *path){
 const struct {const wchar_t *exe,*label;} aliases[]={{L"explorer.exe",L"Windows 资源管理器"},{L"dwm.exe",L"桌面窗口管理器"},{L"svchost.exe",L"服务主机"},{L"msedge.exe",L"微软 Edge"},{L"chrome.exe",L"谷歌 Chrome"},{L"WeChat.exe",L"微信"},{L"Weixin.exe",L"微信"},{L"QQ.exe",L"腾讯 QQ"},{L"YuanShen.exe",L"原神"},{L"GenshinImpact.exe",L"原神"},{L"notepad.exe",L"记事本"},{L"Taskmgr.exe",L"任务管理器"},{L"LetterPet.exe",L"桌宠"},{L"SearchHost.exe",L"Windows 搜索"},{L"RuntimeBroker.exe",L"运行时代理"}};
 lstrcpynW(p->label,p->name,260);for(size_t i=0;i<sizeof(aliases)/sizeof(aliases[0]);i++)if(!_wcsicmp(p->name,aliases[i].exe)){lstrcpynW(p->label,aliases[i].label,260);return;}
 if(!path||!*path)return;DWORD unused,n=GetFileVersionInfoSizeW(path,&unused);if(!n||n>4*1024*1024)return;void *buf=malloc(n);if(!buf)return;
 if(GetFileVersionInfoW(path,0,n,buf)){struct Translation {WORD lang,cp;} *tr;UINT translations;if(VerQueryValueW(buf,L"\\VarFileInfo\\Translation",(void**)&tr,&translations))for(UINT i=0;i<translations/sizeof(*tr);i++){UINT len;wchar_t query[80],*desc;swprintf(query,80,L"\\StringFileInfo\\%04x%04x\\FileDescription",tr[i].lang,tr[i].cp);if(VerQueryValueW(buf,query,(void**)&desc,&len)&&*desc){bool chinese=false;for(const wchar_t *s=desc;*s;s++)if(*s>=0x3400&&*s<=0x9fff)chinese=true;if(chinese){lstrcpynW(p->label,desc,260);break;}}}}
 free(buf);
}
static Snapshot *sample_processes(const Snapshot *old){
 Snapshot *s=calloc(1,sizeof(*s));if(!s)return NULL;s->cpu=-1;FILETIME idle,kernel,user;if(GetSystemTimes(&idle,&kernel,&user)){s->total=time64(kernel)+time64(user);s->idle=time64(idle);if(old&&s->total>old->total)s->cpu=clampd(100*(1-(double)(s->idle-old->idle)/(s->total-old->total)),0,100);}
 MEMORYSTATUSEX mem={sizeof(mem)};if(GlobalMemoryStatusEx(&mem))s->memory=mem.dwMemoryLoad;
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snap==INVALID_HANDLE_VALUE){free(s);return NULL;}PROCESSENTRY32W pe={sizeof(pe)};
 if(Process32FirstW(snap,&pe))do{
  if(stop_event&&WaitForSingleObject(stop_event,0)==WAIT_OBJECT_0)break;
  if(s->count>=4096)break;Process *p=&s->rows[s->count++];p->pid=pe.th32ProcessID;p->cpu=-1;lstrcpynW(p->name,pe.szExeFile,260);
  const Process *previous=NULL;if(old)for(int i=0;i<old->count;i++)if(old->rows[i].pid==p->pid){previous=&old->rows[i];break;}
  wchar_t path[32768]={0};HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,p->pid);
  if(h){FILETIME create,exit,k,u;if(GetProcessTimes(h,&create,&exit,&k,&u)){p->readable=true;p->identity=time64(create);p->cpu_time=time64(k)+time64(u);if(previous&&previous->readable&&p->identity==previous->identity&&s->total>old->total&&p->cpu_time>=previous->cpu_time)p->cpu=clampd((double)(p->cpu_time-previous->cpu_time)/(s->total-old->total)*100,0,100);}DWORD len=32768;QueryFullProcessImageNameW(h,0,path,&len);CloseHandle(h);}
  h=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,p->pid);if(h){PROCESS_MEMORY_COUNTERS pm={sizeof(pm)};if(GetProcessMemoryInfo(h,&pm,sizeof(pm))){p->mem_ok=true;p->memory=pm.WorkingSetSize;}CloseHandle(h);}
  if(previous&&previous->identity&&previous->identity==p->identity)lstrcpynW(p->label,previous->label,260);else display_name(p,path);
 }while(Process32NextW(snap,&pe));CloseHandle(snap);return s;
}
static DWORD WINAPI sampler(LPVOID param){HWND w=(HWND)param;Snapshot *old=NULL;while(WaitForSingleObject(stop_event,0)!=WAIT_OBJECT_0){Snapshot *s=sample_processes(old);if(s){Snapshot *copy=malloc(sizeof(*copy));if(copy){memcpy(copy,s,sizeof(*copy));if(!PostMessageW(w,WM_SAMPLE,0,(LPARAM)copy))free(copy);}free(old);old=s;}if(WaitForSingleObject(stop_event,1500)==WAIT_OBJECT_0)break;}free(old);return 0;}

/* The sampler remains independent of painting: no process control or writes. */
static int px(int value){return MulDiv(value,dpi,96);}
static HFONT make_font(int size,int weight,const wchar_t *face){
 return CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
  OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,face);
}
static void fill(HDC dc,RECT r,COLORREF color){HBRUSH b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
static void rule(HDC dc,int x,int y,int right,COLORREF color){fill(dc,(RECT){x,y,right,y+1},color);}
static void rounded(HDC dc,RECT r,COLORREF color,COLORREF edge,int radius){
 HBRUSH b=CreateSolidBrush(color);HPEN p=CreatePen(PS_SOLID,1,edge);
 HGDIOBJ old_b=SelectObject(dc,b),old_p=SelectObject(dc,p);
 RoundRect(dc,r.left,r.top,r.right,r.bottom,px(radius),px(radius));
 SelectObject(dc,old_b);SelectObject(dc,old_p);DeleteObject(b);DeleteObject(p);
}
static void text_at(HDC dc,const wchar_t *text,RECT r,HFONT font,COLORREF color,UINT align){
 HGDIOBJ previous=SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);
 DrawTextW(dc,text,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX|align);
 SelectObject(dc,previous);
}
static int compare(const void *a,const void *b){
 const Process *x=a,*y=b;int r=0;
 if(sort_column==0)r=_wcsicmp(x->label,y->label);
 else if(sort_column==1)r=_wcsicmp(x->name,y->name);
 else if(sort_column==2)r=(x->pid>y->pid)-(x->pid<y->pid);
 else if(sort_column==3){
  if((x->cpu<0)!=(y->cpu<0))return x->cpu<0?1:-1;
  r=(x->cpu>y->cpu)-(x->cpu<y->cpu);
 }else{
  if(x->mem_ok!=y->mem_ok)return x->mem_ok?-1:1;
  r=(x->memory>y->memory)-(x->memory<y->memory);
 }
 if(!r)return (x->pid>y->pid)-(x->pid<y->pid);
 return descending?-r:r;
}
static void cell_text(const Process *p,int column,wchar_t *text,int capacity){
 switch(column){
 case 0:lstrcpynW(text,p->label,capacity);break;
 case 1:lstrcpynW(text,p->name,capacity);break;
 case 2:swprintf(text,capacity,L"%lu",p->pid);break;
 case 3:if(p->cpu<0)lstrcpynW(text,L"—",capacity);else swprintf(text,capacity,L"%.2f%%",p->cpu);break;
 default:if(!p->mem_ok)lstrcpynW(text,L"—",capacity);else swprintf(text,capacity,L"%.1f MB",p->memory/1048576.0);break;
 }
}
static void refresh_list(void){
 if(!shown||!list)return;
 updating_list=true;SendMessageW(list,WM_SETREDRAW,FALSE,0);
 qsort(shown->rows,shown->count,sizeof(Process),compare);
 ListView_SetItemCountEx(list,shown->count,LVSICF_NOSCROLL|LVSICF_NOINVALIDATEALL);
 ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
 bool found=false;
 if(has_selection)for(int i=0;i<shown->count;i++){
  if(shown->rows[i].pid==selected_pid&&shown->rows[i].identity==selected_identity){
   ListView_SetItemState(list,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);found=true;break;
  }
 }
 has_selection=found;updating_list=false;SendMessageW(list,WM_SETREDRAW,TRUE,0);
 InvalidateRect(list,NULL,FALSE);InvalidateRect(ListView_GetHeader(list),NULL,FALSE);
 InvalidateRect(monitor_window,NULL,FALSE);
}
static void draw_row(const DRAWITEMSTRUCT *di){
 if(!shown||di->itemID>=(UINT)shown->count)return;
 const Process *p=&shown->rows[di->itemID];RECT r=di->rcItem;
 bool selected=(di->itemState&ODS_SELECTED)!=0;
 COLORREF row=selected?RGB(76,42,47):(di->itemID%2?RGB(35,27,31):RGB(39,30,34));
 fill(di->hDC,r,row);
 if(selected)fill(di->hDC,(RECT){r.left,r.top+px(6),r.left+px(3),r.bottom-px(6)},gold);
 HWND header=ListView_GetHeader(list);
 for(int c=0;c<5;c++){
  RECT cell;Header_GetItemRect(header,c,&cell);MapWindowPoints(header,list,(POINT*)&cell,2);
  cell.top=r.top;cell.bottom=r.bottom;
  int saved=SaveDC(di->hDC);IntersectClipRect(di->hDC,cell.left,cell.top,cell.right,cell.bottom);
  cell.left+=px(14);cell.right-=px(14);
  if(c==3&&p->cpu>0){
   RECT bar={cell.left,r.top+px(7),cell.right,r.bottom-px(7)};
   int width=bar.right-bar.left;bar.right=bar.left+(int)(width*clampd(p->cpu/100.0,0,1));
   if(bar.right<bar.left+px(2))bar.right=bar.left+px(2);
   fill(di->hDC,bar,selected?RGB(112,73,54):RGB(73,56,42));
  }
  wchar_t value[300];cell_text(p,c,value,300);
  COLORREF color=c==0?ink:muted;
  if(c==3&&p->cpu>=0)color=gold;
  if(c==4&&p->mem_ok)color=RGB(219,207,196);
  text_at(di->hDC,value,cell,body_font,color,c>=2?DT_RIGHT:DT_LEFT);
  RestoreDC(di->hDC,saved);
 }
 rule(di->hDC,r.left,r.bottom-1,r.right,RGB(49,36,41));
}
/* Keep the native header's hit testing, resizing and keyboard accessibility. */
static LRESULT CALLBACK header_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
 (void)id;(void)data;
 if(msg==HDM_LAYOUT){
  LRESULT result=DefSubclassProc(w,msg,wp,lp);HDLAYOUT *layout=(HDLAYOUT*)lp;
  int delta=px(38)-layout->pwpos->cy;layout->pwpos->cy=px(38);layout->prc->top+=delta;return result;
 }
 if(msg==WM_ERASEBKGND)return 1;
 if(msg==WM_PAINT){
  PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT all;GetClientRect(w,&all);fill(dc,all,RGB(48,33,39));
  for(int i=0;i<5;i++){
   RECT r;Header_GetItemRect(w,i,&r);int saved=SaveDC(dc);IntersectClipRect(dc,r.left,r.top,r.right,r.bottom);
   if(i==sort_column)fill(dc,r,RGB(61,41,45));
   r.left+=px(14);r.right-=px(14);
   if(i==sort_column){
    int x=r.right-px(3),y=(r.top+r.bottom)/2;
    POINT pts[3]={{x-px(4),y+(descending?-px(2):px(2))},{x+px(4),y+(descending?-px(2):px(2))},{x,y+(descending?px(3):-px(3))}};
    HBRUSH b=CreateSolidBrush(gold);HGDIOBJ ob=SelectObject(dc,b),op=SelectObject(dc,GetStockObject(NULL_PEN));
    Polygon(dc,pts,3);SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(b);r.right-=px(16);
   }
   text_at(dc,column_titles[i],r,small_font,i==sort_column?gold:muted,i>=2?DT_RIGHT:DT_LEFT);
   RestoreDC(dc,saved);
  }
  rule(dc,all.left,all.bottom-1,all.right,border);EndPaint(w,&ps);return 0;
 }
 if(msg==WM_NCDESTROY)RemoveWindowSubclass(w,header_proc,1);
 return DefSubclassProc(w,msg,wp,lp);
}
static void paint_card(HDC dc,RECT r,int index){
 rounded(dc,r,index==0?RGB(52,31,38):surface,index==0?RGB(107,70,61):border,14);
 RECT label={r.left+px(18),r.top+px(12),r.right-px(18),r.top+px(32)};
 const wchar_t *labels[]={L"CPU 使用率",L"内存使用率",L"运行进程"};
 text_at(dc,labels[index],label,small_font,muted,DT_LEFT);
 wchar_t value[40];double amount=-1;
 if(!shown)lstrcpyW(value,L"—");
 else if(index==0){amount=shown->cpu;if(amount<0)lstrcpyW(value,L"—");else swprintf(value,40,L"%.1f%%",amount);}
 else if(index==1){amount=shown->memory;swprintf(value,40,L"%lu%%",shown->memory);}
 else swprintf(value,40,L"%d",shown->count);
 text_at(dc,value,(RECT){label.left,r.top+px(33),label.right,r.top+px(76)},metric_font,ink,DT_LEFT);
 if(index<2){
  RECT track={label.left,r.bottom-px(19),label.right,r.bottom-px(15)};fill(dc,track,RGB(69,49,53));
  if(amount>=0){track.right=track.left+(int)((track.right-track.left)*amount/100.0);fill(dc,track,index==0?gold:RGB(172,113,121));}
 }else text_at(dc,L"实时读取 · 仅查看",(RECT){label.left,r.bottom-px(29),label.right,r.bottom-px(9)},small_font,muted,DT_LEFT);
}
static void paint_panel(HWND w,HDC target){
 RECT r;GetClientRect(w,&r);if(r.right<=0||r.bottom<=0)return;
 HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,r.right,r.bottom);HGDIOBJ old=SelectObject(dc,bitmap);
 fill(dc,r,background);int margin=px(24),right=r.right-margin;
 text_at(dc,L"系统运行概览",(RECT){margin,px(18),right,px(54)},title_font,ink,DT_LEFT);
 text_at(dc,L"看清资源使用，让陪伴更轻盈。",(RECT){margin,px(56),right,px(79)},body_font,muted,DT_LEFT);
 RECT badge={right-px(100),px(26),right,px(54)};
 rounded(dc,badge,surface,border,24);text_at(dc,L"只读监测",badge,small_font,gold,DT_CENTER);
 int gap=px(14),width=(right-margin-gap*2)/3;
 for(int i=0;i<3;i++)paint_card(dc,(RECT){margin+i*(width+gap),px(100),i==2?right:margin+i*(width+gap)+width,px(211)},i);
 text_at(dc,L"进程列表",(RECT){margin,px(232),margin+px(130),px(260)},heading_font,ink,DT_LEFT);
 text_at(dc,L"每 1.5 秒刷新  /  点击表头排序",(RECT){margin+px(145),px(232),right,px(260)},small_font,muted,DT_RIGHT);
 RECT frame={margin-1,px(272)-1,right+1,r.bottom-px(48)+1};
 HBRUSH edge=CreateSolidBrush(border);FrameRect(dc,&frame,edge);DeleteObject(edge);
 text_at(dc,L"— 表示采样中或权限受限 · 不修改或结束进程",(RECT){margin,r.bottom-px(39),right-px(145),r.bottom-px(10)},small_font,muted,DT_LEFT);
 wchar_t updated[64];if(shown)swprintf(updated,64,L"更新于 %02d:%02d:%02d",sampled_at.wHour,sampled_at.wMinute,sampled_at.wSecond);else lstrcpyW(updated,L"正在读取…");
 text_at(dc,updated,(RECT){right-px(150),r.bottom-px(39),right,r.bottom-px(10)},small_font,gold,DT_RIGHT);
 BitBlt(target,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
static void layout_panel(HWND w){
 RECT r;GetClientRect(w,&r);if(!list||r.right<px(200))return;
 MoveWindow(list,px(24),px(272),r.right-px(48),r.bottom-px(320),TRUE);
 RECT client;GetClientRect(list,&client);int width=client.right;
 int pid=px(82),cpu=px(112),memory=px(126),remaining=width-pid-cpu-memory;
 int name=(int)(remaining*0.53);
 int widths[]={name,remaining-name,pid,cpu,memory};
 for(int i=0;i<5;i++)ListView_SetColumnWidth(list,i,widths[i]);
 InvalidateRect(w,NULL,FALSE);
}
static void stop_worker(void){
 if(worker){SetEvent(stop_event);WaitForSingleObject(worker,INFINITE);CloseHandle(worker);worker=NULL;}
 if(stop_event){CloseHandle(stop_event);stop_event=NULL;}
 if(monitor_window){MSG m;while(PeekMessageW(&m,monitor_window,WM_SAMPLE,WM_SAMPLE,PM_REMOVE))free((void*)m.lParam);}
}
static void theme_window(HWND w){
 /* Optional Windows 10/11 caption colors; older Windows uses its native caption. */
 HMODULE dwm=LoadLibraryW(L"dwmapi.dll");if(dwm){
  typedef HRESULT (WINAPI *SetAttribute)(HWND,DWORD,LPCVOID,DWORD);
  SetAttribute set=(SetAttribute)(void*)GetProcAddress(dwm,"DwmSetWindowAttribute");
  if(set){BOOL dark=TRUE;set(w,20,&dark,sizeof(dark));set(w,35,&background,sizeof(background));set(w,36,&ink,sizeof(ink));}
  FreeLibrary(dwm);
 }
 HMODULE theme=LoadLibraryW(L"uxtheme.dll");if(theme){
  typedef HRESULT (WINAPI *SetTheme)(HWND,LPCWSTR,LPCWSTR);
  SetTheme set=(SetTheme)(void*)GetProcAddress(theme,"SetWindowTheme");
  if(set){set(list,L"DarkMode_Explorer",NULL);set(ListView_GetHeader(list),L"",L"");}FreeLibrary(theme);
 }
}
static LRESULT CALLBACK monitor_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
 switch(msg){
 case WM_CREATE:{
  HDC dc=GetDC(w);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(w,dc);
  body_font=make_font(14,FW_NORMAL,L"Microsoft YaHei UI");small_font=make_font(12,FW_NORMAL,L"Microsoft YaHei UI");
  title_font=make_font(24,FW_SEMIBOLD,L"Microsoft YaHei UI");metric_font=make_font(32,FW_SEMIBOLD,L"Segoe UI");heading_font=make_font(15,FW_SEMIBOLD,L"Microsoft YaHei UI");
  list=CreateWindowW(WC_LISTVIEWW,L"进程列表",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_OWNERDATA|LVS_OWNERDRAWFIXED|LVS_SHOWSELALWAYS|LVS_SINGLESEL,
   0,0,100,100,w,(HMENU)1,pet_instance,NULL);
  ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
  ListView_SetBkColor(list,surface);ListView_SetTextBkColor(list,surface);ListView_SetTextColor(list,ink);
  SendMessageW(list,WM_SETFONT,(WPARAM)body_font,FALSE);
  for(int i=0;i<5;i++){LVCOLUMNW col={0};col.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT;col.pszText=(wchar_t*)column_titles[i];col.cx=px(120);col.fmt=i>=2?LVCFMT_RIGHT:LVCFMT_LEFT;ListView_InsertColumn(list,i,&col);}
  SetWindowSubclass(ListView_GetHeader(list),header_proc,1,0);theme_window(w);return 0;
 }
 case WM_SIZE:if(wp!=SIZE_MINIMIZED)layout_panel(w);return 0;
 case WM_GETMINMAXINFO:{MINMAXINFO *m=(MINMAXINFO*)lp;m->ptMinTrackSize=(POINT){px(760),px(560)};return 0;}
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);paint_panel(w,dc);EndPaint(w,&ps);return 0;}
 case WM_MEASUREITEM:{MEASUREITEMSTRUCT *mi=(void*)lp;if(mi->CtlType==ODT_LISTVIEW){mi->itemHeight=px(36);return TRUE;}break;}
 case WM_DRAWITEM:{DRAWITEMSTRUCT *di=(void*)lp;if(di->CtlType==ODT_LISTVIEW){draw_row(di);return TRUE;}break;}
 case WM_SAMPLE:{bool first=shown==NULL;free(shown);shown=(Snapshot*)lp;GetLocalTime(&sampled_at);refresh_list();if(first)layout_panel(w);return 0;}
 case WM_SETFOCUS:if(list)SetFocus(list);return 0;
 case WM_NOTIFY:{
  NMHDR *h=(NMHDR*)lp;
  if(h->hwndFrom==list&&h->code==LVN_COLUMNCLICK){int c=((NMLISTVIEW*)lp)->iSubItem;descending=c==sort_column?!descending:c>=2;sort_column=c;refresh_list();}
  if(h->hwndFrom==list&&h->code==LVN_ITEMCHANGED&&!updating_list&&shown){
   NMLISTVIEW *change=(void*)lp;int i=change->iItem;
   if(i>=0&&i<shown->count&&(change->uNewState&LVIS_SELECTED)){selected_pid=shown->rows[i].pid;selected_identity=shown->rows[i].identity;has_selection=true;}
  }
  if(h->code==LVN_GETDISPINFOW&&shown){
   NMLVDISPINFOW *di=(void*)lp;int i=di->item.iItem;
   if(i>=0&&i<shown->count&&(di->item.mask&LVIF_TEXT))cell_text(&shown->rows[i],di->item.iSubItem,di->item.pszText,di->item.cchTextMax);
  }
  return 0;
 }
 case WM_CLOSE:stop_worker();ShowWindow(w,SW_HIDE);return 0;
 case WM_DESTROY:
  DeleteObject(body_font);DeleteObject(small_font);DeleteObject(title_font);DeleteObject(metric_font);DeleteObject(heading_font);
  body_font=small_font=title_font=metric_font=heading_font=NULL;list=NULL;return 0;
 }
 return DefWindowProcW(w,msg,wp,lp);
}
void monitor_show(void){
 if(!monitor_window){
  HDC dc=GetDC(NULL);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(NULL,dc);
  WNDCLASSW c={0};c.lpfnWndProc=monitor_proc;c.hInstance=pet_instance;c.lpszClassName=L"LetterPetC.Monitor";c.hCursor=LoadCursor(NULL,IDC_ARROW);RegisterClassW(&c);
  RECT area;SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);int width=px(960),height=px(740);
  if(width>area.right-area.left)width=area.right-area.left;if(height>area.bottom-area.top)height=area.bottom-area.top;
  monitor_window=CreateWindowExW(WS_EX_APPWINDOW,c.lpszClassName,L"桌宠 · 系统运行概览",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
   area.left+(area.right-area.left-width)/2,area.top+(area.bottom-area.top-height)/2,width,height,NULL,NULL,pet_instance,NULL);
 }
 if(!monitor_window)return;
 ShowWindow(monitor_window,SW_SHOWNORMAL);SetForegroundWindow(monitor_window);
 if(!worker){
  stop_event=CreateEventW(NULL,TRUE,FALSE,NULL);if(stop_event)worker=CreateThread(NULL,0,sampler,monitor_window,0,NULL);
  if(!worker){if(stop_event){CloseHandle(stop_event);stop_event=NULL;}MessageBoxW(monitor_window,L"暂时无法启动采样，请关闭面板后重试。",L"桌宠",MB_ICONWARNING);}
 }
}
void monitor_close(void){stop_worker();if(monitor_window)DestroyWindow(monitor_window);monitor_window=NULL;free(shown);shown=NULL;}
bool monitor_selftest(FILE *log){
 Snapshot *a=sample_processes(NULL);Sleep(100);Snapshot *b=sample_processes(a);
 bool ok=a&&b&&a->count>0&&b->count>0&&b->cpu>=0&&b->cpu<=100,own=false;
 if(b)for(int i=0;i<b->count;i++)if(b->rows[i].pid==GetCurrentProcessId())own=true;
 fprintf(log,"%s: process sampling, CPU range, current PID, %d rows.\n",ok&&own?"PASS":"FAIL",b?b->count:0);
 Process rows[3]={{.pid=3,.cpu=-1},{.pid=2,.cpu=6},{.pid=1,.cpu=1}};
 int saved_column=sort_column;bool saved_direction=descending;sort_column=3;
 descending=true;qsort(rows,3,sizeof(Process),compare);bool sorting=rows[0].pid==2&&rows[2].pid==3;
 descending=false;qsort(rows,3,sizeof(Process),compare);sorting=sorting&&rows[0].pid==1&&rows[2].pid==3;
 sort_column=saved_column;descending=saved_direction;
 wchar_t value[32];cell_text(&rows[2],3,value,32);sorting=sorting&&!wcscmp(value,L"—");
 fprintf(log,"%s: monitor sorting and unavailable-value formatting.\n",sorting?"PASS":"FAIL");
 free(a);free(b);return ok&&own&&sorting;
}

