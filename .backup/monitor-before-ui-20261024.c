#include "pet.h"
#include <commctrl.h>
#include <tlhelp32.h>
#include <psapi.h>
typedef struct {DWORD pid;ULONGLONG identity,cpu_time;double cpu;SIZE_T memory;bool readable,mem_ok;wchar_t name[260],label[260];} Process;
typedef struct {Process rows[4096];int count;double cpu;DWORD memory;ULONGLONG total,idle;} Snapshot;
static HWND monitor_window,list,status;static HANDLE stop_event,worker;static Snapshot *shown;static int sort_column=3;static bool descending=true;
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
static int compare(const void *a,const void *b){const Process *x=a,*y=b;int r=0;if(sort_column==0)r=_wcsicmp(x->label,y->label);else if(sort_column==1)r=_wcsicmp(x->name,y->name);else if(sort_column==2)r=(x->pid>y->pid)-(x->pid<y->pid);else if(sort_column==3)r=(x->cpu>y->cpu)-(x->cpu<y->cpu);else r=(x->memory>y->memory)-(x->memory<y->memory);return descending?-r:r;}
static void refresh_list(void){if(!shown)return;qsort(shown->rows,shown->count,sizeof(Process),compare);ListView_SetItemCountEx(list,shown->count,LVSICF_NOSCROLL);InvalidateRect(list,NULL,FALSE);wchar_t text[256];if(shown->cpu<0)swprintf(text,256,L"CPU：采样中…    内存：%lu%%    %d 个进程",shown->memory,shown->count);else swprintf(text,256,L"CPU：%.1f%%    内存：%lu%%    %d 个进程    · 每 1.5 秒更新，点击列标题排序",shown->cpu,shown->memory,shown->count);SetWindowTextW(status,text);}
static void stop_worker(void){if(worker){SetEvent(stop_event);WaitForSingleObject(worker,INFINITE);CloseHandle(worker);worker=NULL;CloseHandle(stop_event);stop_event=NULL;}if(monitor_window){MSG m;while(PeekMessageW(&m,monitor_window,WM_SAMPLE,WM_SAMPLE,PM_REMOVE))free((void*)m.lParam);}}
static LRESULT CALLBACK monitor_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
 switch(msg){
 case WM_CREATE:{status=CreateWindowW(L"STATIC",L"正在采样…",WS_CHILD|WS_VISIBLE,14,10,800,30,w,NULL,pet_instance,NULL);list=CreateWindowW(WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_OWNERDATA|LVS_SHOWSELALWAYS,10,45,800,430,w,(HMENU)1,pet_instance,NULL);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);ListView_SetBkColor(list,RGB(39,20,25));ListView_SetTextBkColor(list,RGB(39,20,25));ListView_SetTextColor(list,RGB(239,218,174));HFONT font=GetStockObject(DEFAULT_GUI_FONT);SendMessageW(list,WM_SETFONT,(WPARAM)font,TRUE);SendMessageW(status,WM_SETFONT,(WPARAM)font,TRUE);const wchar_t *titles[]={L"中文名 / 描述",L"原进程名",L"PID",L"CPU %",L"内存 MB"};int widths[]={230,210,80,90,110};for(int i=0;i<5;i++){LVCOLUMNW col={0};col.mask=LVCF_TEXT|LVCF_WIDTH;col.pszText=(wchar_t*)titles[i];col.cx=widths[i];ListView_InsertColumn(list,i,&col);}return 0;}
 case WM_SIZE:MoveWindow(status,14,10,LOWORD(lp)-28,28,TRUE);MoveWindow(list,10,45,LOWORD(lp)-20,HIWORD(lp)-55,TRUE);return 0;
 case WM_SAMPLE:free(shown);shown=(Snapshot*)lp;refresh_list();return 0;
 case WM_NOTIFY:{NMHDR *h=(NMHDR*)lp;if(h->code==LVN_COLUMNCLICK){int c=((NMLISTVIEW*)lp)->iSubItem;descending=c==sort_column?!descending:c>=2;sort_column=c;refresh_list();}if(h->code==LVN_GETDISPINFOW&&shown){NMLVDISPINFOW *di=(void*)lp;int i=di->item.iItem;if(i>=0&&i<shown->count&&(di->item.mask&LVIF_TEXT)){Process *p=&shown->rows[i];wchar_t text[300];switch(di->item.iSubItem){case 0:lstrcpynW(text,p->label,300);break;case 1:lstrcpynW(text,p->name,300);break;case 2:swprintf(text,300,L"%lu",p->pid);break;case 3:if(p->cpu<0)lstrcpyW(text,L"—");else swprintf(text,300,L"%.2f",p->cpu);break;default:if(!p->mem_ok)lstrcpyW(text,L"受限");else swprintf(text,300,L"%.1f",p->memory/1048576.0);break;}lstrcpynW(di->item.pszText,text,di->item.cchTextMax);}}return 0;}
 case WM_CLOSE:stop_worker();ShowWindow(w,SW_HIDE);return 0;
 }return DefWindowProcW(w,msg,wp,lp);
}
void monitor_show(void){if(!monitor_window){WNDCLASSW c={0};c.lpfnWndProc=monitor_proc;c.hInstance=pet_instance;c.lpszClassName=L"LetterPetC.Monitor";c.hCursor=LoadCursor(NULL,IDC_ARROW);c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&c);monitor_window=CreateWindowExW(WS_EX_APPWINDOW,c.lpszClassName,L"桌宠 · 进程与 CPU 占用（只读）",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,800,520,NULL,NULL,pet_instance,NULL);}ShowWindow(monitor_window,SW_SHOW);SetForegroundWindow(monitor_window);if(!worker){stop_event=CreateEventW(NULL,TRUE,FALSE,NULL);worker=CreateThread(NULL,0,sampler,monitor_window,0,NULL);}}
void monitor_close(void){stop_worker();if(monitor_window)DestroyWindow(monitor_window);monitor_window=NULL;free(shown);shown=NULL;}
bool monitor_selftest(FILE *log){Snapshot *a=sample_processes(NULL);Sleep(100);Snapshot *b=sample_processes(a);bool ok=a&&b&&a->count>0&&b->count>0&&b->cpu>=0&&b->cpu<=100;bool own=false;if(b)for(int i=0;i<b->count;i++)if(b->rows[i].pid==GetCurrentProcessId())own=true;fprintf(log,"%s: process sampling, CPU range, current PID, %d rows.\n",ok&&own?"PASS":"FAIL",b?b->count:0);free(a);free(b);return ok&&own;}
