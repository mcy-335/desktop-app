#include "pet.h"
#include "gpu_monitor.h"
#include <commctrl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <wctype.h>
#define PRIMARY_LIMIT 10
#define ID_PRIMARY 101
#define ID_ALL 102
typedef struct {
 DWORD pid,parent_pid;ULONGLONG identity,cpu_time,path_hash,app_hash;double cpu;SIZE_T memory;
 bool readable,mem_ok,has_window;int instances,cpu_samples,mem_samples;
 bool app_verified,app_instance,codex_owner;
 wchar_t name[260],label[260];
 wchar_t path[1024],app_name[260],app_label[260];
} Process;
typedef struct {Process rows[4096];int count;double cpu;DWORD memory;ULONGLONG total,idle;GpuReading gpu;} Snapshot;
static HWND monitor_window,list;
static HWND primary_button,all_button;
static HANDLE stop_event,worker;
static Snapshot *shown;
static Process view_rows[4096];
static int view_count;
static bool primary_view=true;
static int sort_column=3;
static bool descending=true;
static int dpi=96;
static HFONT body_font,small_font,title_font,metric_font,heading_font;
static bool updating_list,has_selection;
static DWORD selected_pid;
static ULONGLONG selected_identity;
static ULONGLONG selected_path_hash;
static bool selected_app_instance;
static wchar_t selected_name[260];
static SYSTEMTIME sampled_at;
static const COLORREF background=RGB(27,21,25),surface=RGB(39,28,33);
static const COLORREF border=RGB(78,56,57),gold=RGB(220,183,119);
static const COLORREF ink=RGB(242,232,219),muted=RGB(178,159,156);
static const wchar_t *column_titles[]={L"进程名称",L"原进程名",L"PID",L"CPU",L"内存"};

#define WM_SAMPLE (WM_APP+22)
static ULONGLONG time64(FILETIME f){return ((ULONGLONG)f.dwHighDateTime<<32)|f.dwLowDateTime;}
static ULONGLONG executable_hash(const wchar_t *path){
 if(!path||!*path)return 0;
 ULONGLONG hash=14695981039346656037ULL;
 for(;*path;path++){hash^=(unsigned)towlower(*path);hash*=1099511628211ULL;}
 return hash?hash:1;
}
static bool named(const Process *p,const wchar_t *name){return !_wcsicmp(p->name,name);}
static bool path_contains(const wchar_t *path,const wchar_t *fragment){
 if(!path)return false;
 size_t n=wcslen(fragment);for(;*path;path++)if(!_wcsnicmp(path,fragment,n))return true;return false;
}
static bool codex_desktop(const Process *p){
 return named(p,L"ChatGPT.exe")&&path_contains(p->path,L"\\WindowsApps\\OpenAI.Codex_");
}
static bool codex_component_root(wchar_t *root,DWORD capacity){
 DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",root,capacity);
 const wchar_t *suffix=L"\\OpenAI\\Codex\\";
 if(!n||n>=capacity||n+wcslen(suffix)>=capacity){root[0]=0;return false;}
 wcscat(root,suffix);return true;
}
static bool codex_component(const Process *p){
 wchar_t root[1024];if(!codex_component_root(root,1024))return false;
 size_t n=wcslen(root);if(_wcsnicmp(p->path,root,n))return false;
 const wchar_t *relative=p->path+n;
 return (named(p,L"codex.exe")&&!_wcsnicmp(relative,L"bin\\",4))||
  (named(p,L"node_repl.exe")&&!_wcsnicmp(relative,L"runtimes\\cua_node\\",18));
}
static size_t directory_length(const wchar_t *path){const wchar_t *slash=wcsrchr(path,L'\\');return slash?(size_t)(slash-path)+1:0;}
static bool same_directory(const Process *a,const Process *b){
 size_t n=directory_length(a->path);return n&&n==directory_length(b->path)&&!_wcsnicmp(a->path,b->path,n);
}
static bool shared_runtime(const Process *p){return named(p,L"msedgewebview2.exe");}
static bool system_launcher(const Process *p){
 const wchar_t *names[]={L"explorer.exe",L"cmd.exe",L"powershell.exe",L"pwsh.exe",L"svchost.exe",L"services.exe",L"rundll32.exe",L"dllhost.exe",L"conhost.exe",L"winlogon.exe",L"taskmgr.exe",L"WindowsTerminal.exe",L"OpenConsole.exe",L"ApplicationFrameHost.exe",L"ShellExperienceHost.exe",L"sihost.exe"};
 for(size_t i=0;i<sizeof(names)/sizeof(names[0]);i++)if(named(p,names[i]))return true;return false;
}
static bool inside_installation(const Process *child,const Process *parent){
 size_t n=directory_length(parent->path);
 /* A drive root, Windows directory or shared container is not an application install root. */
 if(n<=3||!child->path[0]||_wcsnicmp(child->path,parent->path,n))return false;
 wchar_t directory[1024];wmemcpy(directory,parent->path,n);directory[n]=0;
 const wchar_t *containers[]={L"\\Windows\\",L"\\System32\\",L"\\SysWOW64\\",L"\\Program Files\\",L"\\Program Files (x86)\\",L"\\Users\\",L"\\Desktop\\",L"\\Downloads\\",L"\\Documents\\",L"\\Temp\\"};
 for(size_t i=0;i<sizeof(containers)/sizeof(containers[0]);i++){
  size_t length=wcslen(containers[i]);if(n>=length&&!_wcsicmp(directory+n-length,containers[i]))return false;
 }
 return true;
}
static bool allowed_owner_link(const Process *child,const Process *parent){
 if(!child->path_hash||!parent->path_hash||system_launcher(child)||system_launcher(parent))return false;
 if(child->path_hash==parent->path_hash&&!_wcsicmp(child->name,parent->name))return true;
 bool wechat=!_wcsicmp(parent->app_name,L"Weixin.exe")||!_wcsicmp(parent->app_name,L"WeChat.exe");
 bool wechat_helper=named(child,L"WeChatAppEx.exe")||named(child,L"WeChatPlayer.exe")||named(child,L"WeChatUtility.exe");
 if(wechat&&wechat_helper&&(inside_installation(child,parent)||
    path_contains(child->path,L"\\Tencent\\xwechat\\")||path_contains(child->path,L"\\Tencent\\WeChat\\")||path_contains(child->path,L"\\Tencent\\Weixin\\")))return true;
 if(!_wcsicmp(parent->app_name,L"QQ.exe")&&named(child,L"QQEX.exe")&&
    (same_directory(child,parent)||inside_installation(child,parent)))return true;
 /* Preserve the verified desktop owner across its cross-directory runtime chain. */
 if(parent->codex_owner&&codex_component(child))return true;
 /* WebView is shared across products: only its validated parent chain establishes the host. */
 if(shared_runtime(child)&&(path_contains(child->path,L"\\Microsoft\\EdgeWebView\\Application\\")||inside_installation(child,parent)))return true;
 /* Separate visible applications opened by another app must remain separate. */
 return !child->has_window&&inside_installation(child,parent);
}
static void inherit_owner(Process *child,const Process *parent){
 child->app_hash=parent->app_hash;child->app_instance=parent->app_instance;child->app_verified=true;
 child->codex_owner=parent->codex_owner;
 lstrcpynW(child->app_name,parent->app_name,260);lstrcpynW(child->app_label,parent->app_label,260);
}
static int parent_index(const Snapshot *s,int index){
 const Process *child=&s->rows[index];if(!child->identity||!child->parent_pid||child->parent_pid==child->pid)return -1;
 for(int i=0;i<s->count;i++){
  const Process *parent=&s->rows[i];
  /* Reject reused parent PIDs: the real parent cannot be newer than its child. */
  if(parent->pid==child->parent_pid&&parent->identity&&parent->identity<child->identity)return i;
 }
 return -1;
}
static void resolve_owner(Snapshot *s,int index,unsigned char *states,int depth){
 if(states[index]==2)return;if(states[index]==1||depth>=64)return;states[index]=1;
 int parent=parent_index(s,index);
 if(parent>=0){resolve_owner(s,parent,states,depth+1);
  if(states[parent]==2&&allowed_owner_link(&s->rows[index],&s->rows[parent]))inherit_owner(&s->rows[index],&s->rows[parent]);
 }
 states[index]=2;
}
static void resolve_software_owners(Snapshot *s,const Snapshot *previous){
 unsigned char states[4096]={0};
 for(int i=0;i<s->count;i++){
  Process *p=&s->rows[i];p->app_hash=p->path_hash;p->app_verified=false;p->app_instance=false;
  p->codex_owner=codex_desktop(p);
  lstrcpynW(p->app_name,p->name,260);lstrcpynW(p->app_label,p->label,260);
  if(shared_runtime(p)){
   /* An unowned runtime is not merged with other applications' runtimes by file name. */
   p->app_hash=p->identity^((ULONGLONG)p->pid<<32);if(!p->app_hash)p->app_hash=p->pid+1;p->app_instance=true;
  }
  if(previous&&p->identity&&p->path_hash)for(int j=0;j<previous->count;j++){
   const Process *old=&previous->rows[j];
   if(old->pid==p->pid&&old->identity==p->identity&&old->path_hash==p->path_hash&&old->parent_pid==p->parent_pid&&old->app_verified){
    /* Keep a verified owner if the launcher has since exited; never cache by PID alone. */
    inherit_owner(p,old);break;
   }
  }
 }
 for(int i=0;i<s->count;i++)resolve_owner(s,i,states,0);
}
typedef HRESULT (WINAPI *GetWindowAttribute)(HWND,DWORD,PVOID,DWORD);
typedef struct {Snapshot *snapshot;GetWindowAttribute get_attribute;} WindowScan;
static BOOL CALLBACK mark_application_window(HWND w,LPARAM param){
 WindowScan *scan=(WindowScan*)param;
 if(!IsWindowVisible(w)||w==GetShellWindow()||GetWindow(w,GW_OWNER)||
    (GetWindowLongPtrW(w,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)||GetWindowTextLengthW(w)==0)return TRUE;
 DWORD cloaked=0;
 if(scan->get_attribute&&SUCCEEDED(scan->get_attribute(w,14,&cloaked,sizeof(cloaked)))&&cloaked)return TRUE;
 DWORD pid=0;GetWindowThreadProcessId(w,&pid);
 for(int i=0;i<scan->snapshot->count;i++)if(scan->snapshot->rows[i].pid==pid){scan->snapshot->rows[i].has_window=true;break;}
 return TRUE;
}
static void display_name(Process *p,const wchar_t *path){
 if(named(p,L"ChatGPT.exe")&&path_contains(path,L"\\WindowsApps\\OpenAI.Codex_")){lstrcpyW(p->label,L"Codex");return;}
 if(named(p,L"cloudmusic.exe")){lstrcpyW(p->label,L"网易云音乐");return;}
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
  if(s->count>=4096)break;Process *p=&s->rows[s->count++];p->pid=pe.th32ProcessID;p->parent_pid=pe.th32ParentProcessID;p->cpu=-1;p->instances=1;lstrcpynW(p->name,pe.szExeFile,260);
  const Process *previous=NULL;if(old)for(int i=0;i<old->count;i++)if(old->rows[i].pid==p->pid){previous=&old->rows[i];break;}
  wchar_t path[32768]={0};HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,p->pid);
  if(h){FILETIME create,exit,k,u;if(GetProcessTimes(h,&create,&exit,&k,&u)){p->readable=true;p->identity=time64(create);p->cpu_time=time64(k)+time64(u);if(previous&&previous->readable&&p->identity==previous->identity&&s->total>old->total&&p->cpu_time>=previous->cpu_time)p->cpu=clampd((double)(p->cpu_time-previous->cpu_time)/(s->total-old->total)*100,0,100);}DWORD len=32768;QueryFullProcessImageNameW(h,0,path,&len);CloseHandle(h);}
  h=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,p->pid);if(h){PROCESS_MEMORY_COUNTERS pm={sizeof(pm)};if(GetProcessMemoryInfo(h,&pm,sizeof(pm))){p->mem_ok=true;p->memory=pm.WorkingSetSize;}CloseHandle(h);}
  if(previous&&previous->identity&&previous->identity==p->identity)lstrcpynW(p->label,previous->label,260);else display_name(p,path);
  p->path_hash=executable_hash(path);p->cpu_samples=p->cpu>=0?1:0;p->mem_samples=p->mem_ok?1:0;
  if(wcslen(path)<sizeof(p->path)/sizeof(p->path[0]))lstrcpyW(p->path,path);
  p->has_window=p->pid==GetCurrentProcessId();
 }while(Process32NextW(snap,&pe));CloseHandle(snap);
 HMODULE dwm=LoadLibraryW(L"dwmapi.dll");WindowScan scan={s,NULL};
 if(dwm)scan.get_attribute=(GetWindowAttribute)(void*)GetProcAddress(dwm,"DwmGetWindowAttribute");
 EnumWindows(mark_application_window,(LPARAM)&scan);if(dwm)FreeLibrary(dwm);
 resolve_software_owners(s,old);
 return s;
}
static DWORD WINAPI sampler(LPVOID param){
 HWND w=(HWND)param;Snapshot *old=NULL;GpuSampler gpu={0};
 while(WaitForSingleObject(stop_event,0)!=WAIT_OBJECT_0){
  Snapshot *s=sample_processes(old);
  if(s){
   gpu_sample(&gpu,&s->gpu);
   Snapshot *copy=malloc(sizeof(*copy));if(copy){memcpy(copy,s,sizeof(*copy));if(!PostMessageW(w,WM_SAMPLE,0,(LPARAM)copy))free(copy);}
   free(old);old=s;
  }
  if(WaitForSingleObject(stop_event,1500)==WAIT_OBJECT_0)break;
 }
 gpu_close(&gpu);free(old);return 0;
}

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
static bool same_application(const Process *a,const Process *b){
 if(a->app_hash||b->app_hash)return a->app_hash&&a->app_hash==b->app_hash&&a->app_instance==b->app_instance&&!_wcsicmp(a->app_name,b->app_name);
 /* Fallback for unavailable ownership metadata; unknown paths remain separate. */
 return a->path_hash&&a->path_hash==b->path_hash&&!_wcsicmp(a->name,b->name);
}
static int primary_priority(const void *a,const void *b){
 const Process *x=a,*y=b;
 if(x->has_window!=y->has_window)return x->has_window?-1:1;
 if(x->cpu!=y->cpu)return x->cpu>y->cpu?-1:1;
 if(x->memory!=y->memory)return x->memory>y->memory?-1:1;
 return (x->pid>y->pid)-(x->pid<y->pid);
}
static int collect_software_groups(const Snapshot *source,Process *output){
 if(!source)return 0;
 int count=0;
 for(int i=0;i<source->count;i++){
  const Process *p=&source->rows[i];int match=-1;
  for(int j=0;j<count;j++)if(same_application(p,&output[j])){match=j;break;}
  if(match<0){
   Process *group=&output[count++];*group=*p;
   if(p->app_hash){lstrcpynW(group->name,p->app_name,260);lstrcpynW(group->label,p->app_label,260);}
   continue;
  }
  Process *group=&output[match];
  if(p->cpu>=0){if(group->cpu<0)group->cpu=0;group->cpu+=p->cpu;}
  if(p->mem_ok){group->memory+=p->memory;group->mem_ok=true;}
  group->instances+=p->instances;group->cpu_samples+=p->cpu_samples;group->mem_samples+=p->mem_samples;
  group->has_window|=p->has_window;
  if(!group->app_hash&&!_wcsicmp(group->label,group->name)&&_wcsicmp(p->label,p->name))lstrcpynW(group->label,p->label,260);
  if(p->pid<group->pid){group->pid=p->pid;group->identity=p->identity;}
 }
 return count;
}
static int build_view(const Snapshot *source,Process *output,bool primary){
 if(!source)return 0;
 if(!primary){memcpy(output,source->rows,source->count*sizeof(Process));return source->count;}
 int count=collect_software_groups(source,output);
 int eligible=0;
 for(int i=0;i<count;i++)if(output[i].has_window||output[i].cpu>=1.0||
    (output[i].mem_ok&&output[i].memory>=(SIZE_T)200*1024*1024))output[eligible++]=output[i];
 qsort(output,eligible,sizeof(Process),primary_priority);
 return eligible>PRIMARY_LIMIT?PRIMARY_LIMIT:eligible;
}
static void cell_text(const Process *p,int column,wchar_t *text,int capacity){
 switch(column){
 case 0:if(p->instances>1)swprintf(text,capacity,L"%ls（%d 个进程）",p->label,p->instances);else lstrcpynW(text,p->label,capacity);break;
 case 1:lstrcpynW(text,p->name,capacity);break;
 case 2:swprintf(text,capacity,L"%lu",p->pid);break;
 case 3:if(p->cpu<0)lstrcpynW(text,L"—",capacity);else swprintf(text,capacity,L"%ls%.2f%%",p->cpu_samples<p->instances?L"≥":L"",p->cpu);break;
 default:if(!p->mem_ok)lstrcpynW(text,L"—",capacity);else swprintf(text,capacity,L"%ls%.1f MB",p->mem_samples<p->instances?L"≥":L"",p->memory/1048576.0);break;
 }
}
static void refresh_list(void){
 if(!shown||!list)return;
 updating_list=true;SendMessageW(list,WM_SETREDRAW,FALSE,0);
 view_count=build_view(shown,view_rows,primary_view);
 qsort(view_rows,view_count,sizeof(Process),compare);
 ListView_SetItemCountEx(list,view_count,LVSICF_NOSCROLL|LVSICF_NOINVALIDATEALL);
 ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
 bool found=false;
 if(has_selection)for(int i=0;i<view_count;i++){
  Process *p=&view_rows[i];
  ULONGLONG key=p->app_hash?p->app_hash:p->path_hash;
  bool matches=primary_view&&selected_path_hash?
   key==selected_path_hash&&p->app_instance==selected_app_instance&&!_wcsicmp(p->name,selected_name):p->pid==selected_pid&&p->identity==selected_identity;
  if(matches){
   ListView_SetItemState(list,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);found=true;break;
  }
 }
 has_selection=found;updating_list=false;SendMessageW(list,WM_SETREDRAW,TRUE,0);
 InvalidateRect(list,NULL,FALSE);InvalidateRect(ListView_GetHeader(list),NULL,FALSE);
 InvalidateRect(monitor_window,NULL,FALSE);
}
static void draw_row(const DRAWITEMSTRUCT *di){
 if(!shown||di->itemID>=(UINT)view_count)return;
 const Process *p=&view_rows[di->itemID];RECT r=di->rcItem;
 bool selected=(di->itemState&ODS_SELECTED)!=0;
 COLORREF row=selected?RGB(76,42,47):(di->itemID%2?RGB(35,27,31):RGB(39,30,34));
 fill(di->hDC,r,row);
 if(selected)fill(di->hDC,(RECT){r.left,r.top+px(6),r.left+px(3),r.bottom-px(6)},gold);
 HWND header=ListView_GetHeader(list);
 for(int c=0;c<5;c++){
  if(primary_view&&c==2)continue;
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
   if(primary_view&&i==2)continue;
   RECT r;Header_GetItemRect(w,i,&r);int saved=SaveDC(dc);IntersectClipRect(dc,r.left,r.top,r.right,r.bottom);
   if(i==sort_column)fill(dc,r,RGB(61,41,45));
   r.left+=px(14);r.right-=px(14);
   if(i==sort_column){
    int x=r.right-px(3),y=(r.top+r.bottom)/2;
    POINT pts[3]={{x-px(4),y+(descending?-px(2):px(2))},{x+px(4),y+(descending?-px(2):px(2))},{x,y+(descending?px(3):-px(3))}};
    HBRUSH b=CreateSolidBrush(gold);HGDIOBJ ob=SelectObject(dc,b),op=SelectObject(dc,GetStockObject(NULL_PEN));
    Polygon(dc,pts,3);SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(b);r.right-=px(16);
   }
   const wchar_t *caption=primary_view&&i==0?L"软件名称":primary_view&&i==1?L"主程序":column_titles[i];
   text_at(dc,caption,r,small_font,i==sort_column?gold:muted,i>=2?DT_RIGHT:DT_LEFT);
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
 const wchar_t *labels[]={L"整机 CPU 使用率",L"整机内存使用率",L"GPU 使用率",primary_view?L"主要应用 / 进程":L"全部进程"};
 text_at(dc,labels[index],label,small_font,muted,DT_LEFT);
 wchar_t value[40];double amount=-1;
 if(!shown)lstrcpyW(value,L"—");
 else if(index==0){amount=shown->cpu;if(amount<0)lstrcpyW(value,L"—");else swprintf(value,40,L"%.1f%%",amount);}
 else if(index==1){amount=shown->memory;swprintf(value,40,L"%lu%%",shown->memory);}
 else if(index==2){
  if(shown->gpu.status==GPU_READY){amount=shown->gpu.usage;swprintf(value,40,L"%.1f%%",amount);}
  else lstrcpyW(value,shown->gpu.status==GPU_WARMUP?L"采样中…":L"暂不可用");
 }
 else swprintf(value,40,L"%d",view_count);
 bool gpu_text=index==2&&shown&&shown->gpu.status!=GPU_READY;
 text_at(dc,value,(RECT){label.left,r.top+px(33),label.right,r.top+px(76)},gpu_text?heading_font:metric_font,gpu_text?muted:ink,DT_LEFT);
 if(index<3){
  if(index==2){
   wchar_t detail[80];
   if(shown&&shown->gpu.status==GPU_READY)swprintf(detail,80,L"%ls · 最忙引擎",shown->gpu.engine);
   else lstrcpyW(detail,shown&&shown->gpu.status==GPU_UNAVAILABLE?L"系统未提供有效读数":L"正在读取系统计数器");
   text_at(dc,detail,(RECT){label.left,r.bottom-px(36),label.right,r.bottom-px(17)},small_font,muted,DT_LEFT);
  }
  RECT track={label.left,r.bottom-px(11),label.right,r.bottom-px(7)};fill(dc,track,RGB(69,49,53));
  const COLORREF accents[]={RGB(220,183,119),RGB(172,113,121),RGB(194,150,102)};
  if(amount>=0){track.right=track.left+(int)((track.right-track.left)*clampd(amount,0,100)/100.0);fill(dc,track,accents[index]);}
 }else{
  wchar_t detail[64];if(shown)swprintf(detail,64,primary_view?L"系统共 %d 个进程":L"实时读取 · 仅查看",shown->count);else lstrcpyW(detail,L"正在读取…");
  text_at(dc,detail,(RECT){label.left,r.bottom-px(29),label.right,r.bottom-px(9)},small_font,muted,DT_LEFT);
 }
}
static void paint_panel(HWND w,HDC target){
 RECT r;GetClientRect(w,&r);if(r.right<=0||r.bottom<=0)return;
 HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,r.right,r.bottom);HGDIOBJ old=SelectObject(dc,bitmap);
 fill(dc,r,background);int margin=px(24),right=r.right-margin;
 text_at(dc,L"系统运行概览",(RECT){margin,px(18),right,px(54)},title_font,ink,DT_LEFT);
 text_at(dc,primary_view?L"按软件归属汇总主程序与辅助组件；归属不明的进程独立保留。":L"查看完整进程明细，保留每个进程的原名、PID 与资源占用。",(RECT){margin,px(56),right,px(79)},body_font,muted,DT_LEFT);
 RECT badge={right-px(100),px(26),right,px(54)};
 rounded(dc,badge,surface,border,24);text_at(dc,L"只读监测",badge,small_font,gold,DT_CENTER);
 int gap=px(14),width=(right-margin-gap*3)/4;
 for(int i=0;i<4;i++)paint_card(dc,(RECT){margin+i*(width+gap),px(100),i==3?right:margin+i*(width+gap)+width,px(211)},i);
 text_at(dc,primary_view?L"最多 10 项 · 每 1.5 秒刷新 · 点击表头排序":L"每 1.5 秒刷新 · 点击表头排序",(RECT){margin+px(246),px(232),right,px(260)},small_font,muted,DT_RIGHT);
 RECT frame={margin-1,px(272)-1,right+1,r.bottom-px(48)+1};
 HBRUSH edge=CreateSolidBrush(border);FrameRect(dc,&frame,edge);DeleteObject(edge);
 text_at(dc,primary_view?L"≥ 仅合计可读值 · 内存为工作集之和（共享页可能重复）":L"— 表示采样中或权限受限 · 不修改或结束进程",(RECT){margin,r.bottom-px(39),right-px(145),r.bottom-px(10)},small_font,muted,DT_LEFT);
 wchar_t updated[64];if(shown)swprintf(updated,64,L"更新于 %02d:%02d:%02d",sampled_at.wHour,sampled_at.wMinute,sampled_at.wSecond);else lstrcpyW(updated,L"正在读取…");
 text_at(dc,updated,(RECT){right-px(150),r.bottom-px(39),right,r.bottom-px(10)},small_font,gold,DT_RIGHT);
 BitBlt(target,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
static void layout_panel(HWND w){
 RECT r;GetClientRect(w,&r);if(!list||r.right<px(200))return;
 MoveWindow(primary_button,px(24),px(228),px(110),px(34),TRUE);
 MoveWindow(all_button,px(142),px(228),px(110),px(34),TRUE);
 MoveWindow(list,px(24),px(272),r.right-px(48),r.bottom-px(320),TRUE);
 RECT client;GetClientRect(list,&client);int width=client.right;
 int pid=primary_view?0:px(82),cpu=px(112),memory=px(126),remaining=width-pid-cpu-memory;
 int name=(int)(remaining*0.53);
 int widths[]={name,remaining-name,pid,cpu,memory};
 for(int i=0;i<5;i++)ListView_SetColumnWidth(list,i,widths[i]);
 for(int i=0;i<2;i++){LVCOLUMNW col={0};col.mask=LVCF_TEXT;col.pszText=(wchar_t*)(primary_view?(i==0?L"软件名称":L"主程序"):column_titles[i]);ListView_SetColumn(list,i,&col);}
 InvalidateRect(w,NULL,FALSE);
}
static void switch_view(HWND w,bool primary){
 if(primary_view==primary)return;
 primary_view=primary;has_selection=false;
 if(primary_view&&sort_column==2){sort_column=3;descending=true;}
 refresh_list();layout_panel(w);if(view_count)ListView_EnsureVisible(list,0,FALSE);
 InvalidateRect(primary_button,NULL,FALSE);InvalidateRect(all_button,NULL,FALSE);
}
static void draw_mode_button(const DRAWITEMSTRUCT *di){
 bool active=(di->CtlID==ID_PRIMARY)==primary_view;
 RECT r=di->rcItem;fill(di->hDC,r,background);InflateRect(&r,-1,-1);
 rounded(di->hDC,r,active?RGB(81,44,49):surface,active?RGB(155,113,72):border,10);
 text_at(di->hDC,di->CtlID==ID_PRIMARY?L"主要进程":L"全部进程",r,body_font,active?gold:muted,DT_CENTER);
 if(di->itemState&ODS_FOCUS){InflateRect(&r,-px(4),-px(4));DrawFocusRect(di->hDC,&r);}
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
  primary_button=CreateWindowW(L"BUTTON",L"主要进程",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,w,(HMENU)ID_PRIMARY,pet_instance,NULL);
  all_button=CreateWindowW(L"BUTTON",L"全部进程",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,w,(HMENU)ID_ALL,pet_instance,NULL);
  list=CreateWindowW(WC_LISTVIEWW,L"进程列表",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_OWNERDATA|LVS_OWNERDRAWFIXED|LVS_SHOWSELALWAYS|LVS_SINGLESEL,
   0,0,100,100,w,(HMENU)1,pet_instance,NULL);
  ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
  ListView_SetBkColor(list,surface);ListView_SetTextBkColor(list,surface);ListView_SetTextColor(list,ink);
  SendMessageW(list,WM_SETFONT,(WPARAM)body_font,FALSE);
  for(int i=0;i<5;i++){LVCOLUMNW col={0};col.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT;col.pszText=(wchar_t*)column_titles[i];col.cx=px(120);col.fmt=i>=2?LVCFMT_RIGHT:LVCFMT_LEFT;ListView_InsertColumn(list,i,&col);}
  SetWindowSubclass(ListView_GetHeader(list),header_proc,1,0);theme_window(w);return 0;
 }
 case WM_SIZE:if(wp!=SIZE_MINIMIZED)layout_panel(w);return 0;
 case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED&&(LOWORD(wp)==ID_PRIMARY||LOWORD(wp)==ID_ALL)){switch_view(w,LOWORD(wp)==ID_PRIMARY);return 0;}break;
 case WM_GETMINMAXINFO:{MINMAXINFO *m=(MINMAXINFO*)lp;m->ptMinTrackSize=(POINT){px(760),px(560)};return 0;}
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);paint_panel(w,dc);EndPaint(w,&ps);return 0;}
 case WM_MEASUREITEM:{MEASUREITEMSTRUCT *mi=(void*)lp;if(mi->CtlType==ODT_LISTVIEW){mi->itemHeight=px(36);return TRUE;}break;}
 case WM_DRAWITEM:{DRAWITEMSTRUCT *di=(void*)lp;if(di->CtlType==ODT_LISTVIEW){draw_row(di);return TRUE;}if(di->CtlType==ODT_BUTTON){draw_mode_button(di);return TRUE;}break;}
 case WM_SAMPLE:{int previous_count=view_count;free(shown);shown=(Snapshot*)lp;GetLocalTime(&sampled_at);refresh_list();if(previous_count!=view_count)layout_panel(w);return 0;}
 case WM_SETFOCUS:if(list)SetFocus(list);return 0;
 case WM_NOTIFY:{
  NMHDR *h=(NMHDR*)lp;
  if(h->hwndFrom==list&&h->code==LVN_COLUMNCLICK){int c=((NMLISTVIEW*)lp)->iSubItem;descending=c==sort_column?!descending:c>=2;sort_column=c;refresh_list();}
  if(h->hwndFrom==list&&h->code==LVN_ITEMCHANGED&&!updating_list&&shown){
   NMLISTVIEW *change=(void*)lp;int i=change->iItem;
   if(i>=0&&i<view_count&&(change->uNewState&LVIS_SELECTED)){
    selected_pid=view_rows[i].pid;selected_identity=view_rows[i].identity;selected_path_hash=view_rows[i].app_hash?view_rows[i].app_hash:view_rows[i].path_hash;
    selected_app_instance=view_rows[i].app_instance;
    lstrcpynW(selected_name,view_rows[i].name,260);has_selection=true;
   }
  }
  if(h->code==LVN_GETDISPINFOW&&shown){
   NMLVDISPINFOW *di=(void*)lp;int i=di->item.iItem;
   if(i>=0&&i<view_count&&(di->item.mask&LVIF_TEXT))cell_text(&view_rows[i],di->item.iSubItem,di->item.pszText,di->item.cchTextMax);
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
static void fixture_process(Snapshot *s,int index,DWORD pid,DWORD parent,ULONGLONG created,const wchar_t *path,bool window){
 Process *p=&s->rows[index];memset(p,0,sizeof(*p));p->pid=pid;p->parent_pid=parent;p->identity=created;
 lstrcpynW(p->path,path,1024);const wchar_t *slash=wcsrchr(path,L'\\');lstrcpynW(p->name,slash?slash+1:path,260);
 display_name(p,path);p->path_hash=executable_hash(path);p->has_window=window;
 p->instances=p->cpu_samples=p->mem_samples=1;p->cpu=1;p->mem_ok=true;p->memory=1048576;
 if(index>=s->count)s->count=index+1;
}
static bool software_ownership_selftest(FILE *log){
 Snapshot *s=calloc(1,sizeof(*s)),*previous=calloc(1,sizeof(*previous));Process *groups=calloc(4096,sizeof(Process));
 if(!s||!previous||!groups){free(s);free(previous);free(groups);return false;}
 fixture_process(s,0,100,0,1,L"C:\\Windows\\explorer.exe",true);
 fixture_process(s,1,200,100,10,L"C:\\Program Files\\Tencent\\Weixin\\Weixin.exe",true);
 const wchar_t *wechat_helper=L"C:\\Users\\Test\\AppData\\Roaming\\Tencent\\xwechat\\xplugin\\plugins\\RadiumWMPF\\WeChatAppEx.exe";
 fixture_process(s,2,201,200,11,wechat_helper,true);
 fixture_process(s,3,202,201,12,wechat_helper,false);
 fixture_process(s,4,300,100,20,L"D:\\QQ.exe",true);
 fixture_process(s,5,301,300,21,L"D:\\QQEX.exe",true);
 fixture_process(s,6,302,301,22,L"D:\\QQEX.exe",false);
 fixture_process(s,7,400,100,30,L"C:\\Apps\\Browser\\browser.exe",true);
 fixture_process(s,8,401,400,31,L"C:\\Apps\\Browser\\browser.exe",false);
 fixture_process(s,9,500,100,40,L"C:\\Apps\\Notes\\notes.exe",true);
 const wchar_t *webview=L"C:\\Program Files (x86)\\Microsoft\\EdgeWebView\\Application\\155.0\\msedgewebview2.exe";
 fixture_process(s,10,501,500,41,webview,true);
 fixture_process(s,11,502,501,42,webview,false);
 fixture_process(s,12,600,100,50,L"C:\\Apps\\Mail\\mail.exe",true);
 fixture_process(s,13,601,600,51,webview,false);
 fixture_process(s,14,700,200,60,L"C:\\Apps\\OtherBrowser\\browser.exe",true);
 fixture_process(s,15,800,900,70,L"C:\\Apps\\Reuse\\helper.exe",false);
 fixture_process(s,16,900,100,80,L"C:\\Apps\\Reuse\\host.exe",true);
 fixture_process(s,17,910,0,90,L"C:\\Temp\\WeChatAppEx.exe",true);
 fixture_process(s,18,920,0,91,webview,false);
 fixture_process(s,19,921,0,92,webview,false);
 fixture_process(s,20,930,300,93,L"D:\\standalone.exe",true);
 fixture_process(s,21,940,500,94,L"C:\\Apps\\Notes\\bin\\worker.exe",false);
 fixture_process(s,22,950,100,95,L"C:\\Windows\\System32\\notepad.exe",true);
 resolve_software_owners(s,NULL);
 bool links=s->rows[2].app_hash==s->rows[1].path_hash&&s->rows[3].app_hash==s->rows[1].path_hash&&
  s->rows[5].app_hash==s->rows[4].path_hash&&s->rows[6].app_hash==s->rows[4].path_hash&&
  s->rows[10].app_hash==s->rows[9].path_hash&&s->rows[11].app_hash==s->rows[9].path_hash&&
  s->rows[13].app_hash==s->rows[12].path_hash&&s->rows[21].app_hash==s->rows[9].path_hash;
 bool isolation=s->rows[14].app_hash==s->rows[14].path_hash&&s->rows[15].app_hash==s->rows[15].path_hash&&
  s->rows[17].app_hash==s->rows[17].path_hash&&s->rows[20].app_hash==s->rows[20].path_hash&&
  s->rows[22].app_hash==s->rows[22].path_hash&&!same_application(&s->rows[18],&s->rows[19])&&
  !same_application(&s->rows[10],&s->rows[13]);
 int count=collect_software_groups(s,groups),instances=0;SIZE_T memory=0;double cpu=0;bool wechat=false,qq=false;
 for(int i=0;i<count;i++){
  instances+=groups[i].instances;memory+=groups[i].memory;cpu+=groups[i].cpu;
  if(groups[i].app_hash==s->rows[1].path_hash)wechat=groups[i].instances==3&&!wcscmp(groups[i].label,L"微信")&&!wcscmp(groups[i].name,L"Weixin.exe");
  if(groups[i].app_hash==s->rows[4].path_hash)qq=groups[i].instances==3&&!wcscmp(groups[i].label,L"腾讯 QQ");
 }
 bool totals=instances==s->count&&memory==(SIZE_T)s->count*1048576&&fabs(cpu-s->count)<0.00001&&wechat&&qq;
 int raw=build_view(s,groups,false);bool raw_ok=raw==s->count&&named(&groups[2],L"WeChatAppEx.exe")&&groups[2].instances==1;
 memcpy(previous,s,sizeof(*s));s->rows[1].pid=9999;s->rows[1].identity=2000;
 resolve_software_owners(s,previous);
 bool cache=s->rows[2].app_hash==previous->rows[1].app_hash&&s->rows[2].app_verified;
 s->rows[2].identity=3000;resolve_software_owners(s,previous);
 cache=cache&&s->rows[2].app_hash==s->rows[2].path_hash&&!s->rows[2].app_verified;
 fprintf(log,"%s: software ownership (WeChat/QQ/helpers, separate WebView hosts, install boundaries, PID reuse, orphan cache and no double counting).\n",links&&isolation&&totals&&raw_ok&&cache?"PASS":"FAIL");
 free(s);free(previous);free(groups);return links&&isolation&&totals&&raw_ok&&cache;
}
static bool codex_chain_selftest(FILE *log){
 Snapshot *s=calloc(1,sizeof(*s)),*previous=calloc(1,sizeof(*previous));Process *groups=calloc(4096,sizeof(Process));
 if(!s||!previous||!groups){free(s);free(previous);free(groups);return false;}
 wchar_t root[1024],backend[1024],runtime[1024],unrelated[1024];
 if(!codex_component_root(root,1024)||wcslen(root)>900){free(s);free(previous);free(groups);return false;}
 swprintf(backend,1024,L"%lsbin\\test-build\\codex.exe",root);
 swprintf(runtime,1024,L"%lsruntimes\\cua_node\\test-runtime\\bin\\node_repl.exe",root);
 swprintf(unrelated,1024,L"%lsruntimes\\unrelated\\node_repl.exe",root);
 fixture_process(s,0,10,0,10,L"C:\\Program Files\\WindowsApps\\OpenAI.Codex_Test\\app\\ChatGPT.exe",true);
 fixture_process(s,1,20,10,20,backend,false);
 fixture_process(s,2,30,20,30,runtime,false);
 fixture_process(s,3,40,30,40,backend,false);
 fixture_process(s,4,50,10,50,L"C:\\Windows\\System32\\cmd.exe",true);
 fixture_process(s,5,60,50,60,backend,false);
 fixture_process(s,6,70,30,70,L"D:\\Downloads\\codex.exe",false);
 fixture_process(s,7,80,20,80,unrelated,false);
 fixture_process(s,8,90,10,5,backend,false);
 fixture_process(s,9,100,60,100,runtime,false);
 fixture_process(s,10,110,100,110,backend,false);
 resolve_software_owners(s,NULL);
 bool links=true;for(int i=0;i<4;i++)links=links&&s->rows[i].codex_owner&&s->rows[i].app_hash==s->rows[0].path_hash;
 bool independent=true;for(int i=4;i<s->count;i++)independent=independent&&!s->rows[i].codex_owner&&s->rows[i].app_hash!=s->rows[0].path_hash;
 int count=collect_software_groups(s,groups),owned=0,total=0;bool title=false;
 for(int i=0;i<count;i++){
  total+=groups[i].instances;
  if(groups[i].codex_owner){owned+=groups[i].instances;title=!wcscmp(groups[i].label,L"Codex");}
 }
 bool totals=owned==4&&total==s->count&&title;
 memcpy(previous,s,sizeof(*s));s->rows[0].pid=9999;s->rows[0].identity=1000;
 resolve_software_owners(s,previous);
 bool cache=s->rows[3].codex_owner&&s->rows[3].app_hash==previous->rows[0].path_hash;
 s->rows[3].identity=3000;s->rows[2].identity=4000;resolve_software_owners(s,previous);
 bool reused=!s->rows[3].codex_owner&&s->rows[3].app_hash==s->rows[3].path_hash;
 int raw=build_view(s,groups,false);bool original=raw==s->count&&named(&groups[2],L"node_repl.exe")&&named(&groups[3],L"codex.exe")&&groups[3].instances==1;
 bool ok=links&&independent&&totals&&cache&&reused&&original;
 fprintf(log,"%s: Codex desktop -> backend -> runtime -> backend chain, standalone CLI isolation, runtime path checks, cache and PID reuse.\n",ok?"PASS":"FAIL");
 free(s);free(previous);free(groups);return ok;
}
static bool primary_view_selftest(FILE *log){
 Snapshot *fixture=calloc(1,sizeof(*fixture));Process *output=calloc(4096,sizeof(Process));
 if(!fixture||!output){free(fixture);free(output);return false;}
 fixture->count=9;
 for(int i=0;i<fixture->count;i++){
  Process *p=&fixture->rows[i];p->pid=10+i;p->identity=100+i;p->instances=1;
  p->cpu_samples=p->mem_samples=1;p->mem_ok=true;p->cpu=0;p->memory=5*1048576;
  swprintf(p->name,260,L"app%d.exe",i);lstrcpynW(p->label,p->name,260);p->path_hash=100+i;
 }
 for(int i=0;i<4;i++){lstrcpyW(fixture->rows[i].name,L"browser.exe");lstrcpyW(fixture->rows[i].label,L"浏览器");fixture->rows[i].path_hash=100;}
 fixture->rows[0].has_window=true;fixture->rows[0].cpu=1.25;fixture->rows[0].memory=100*1048576;
 fixture->rows[1].cpu=2.75;fixture->rows[1].memory=200*1048576;
 fixture->rows[2].cpu=-1;fixture->rows[2].cpu_samples=fixture->rows[2].mem_samples=0;fixture->rows[2].mem_ok=false;fixture->rows[2].memory=0;
 fixture->rows[3].path_hash=200;fixture->rows[3].has_window=true;
 fixture->rows[5].cpu=2.0;
 for(int i=6;i<=7;i++){fixture->rows[i].path_hash=0;fixture->rows[i].has_window=true;lstrcpyW(fixture->rows[i].name,L"unknown.exe");}
 fixture->rows[8].memory=250*1048576;
 int count=build_view(fixture,output,true);bool ok=count==6,grouped=false,other_path=false,low_hidden=true,unknowns=false;
 int unknown_count=0;wchar_t text[300];
 for(int i=0;i<count;i++){
  Process *p=&output[i];
  if(p->path_hash==100){
   grouped=p->instances==3&&fabs(p->cpu-4.0)<0.00001&&p->memory==300*1048576&&p->has_window;
   cell_text(p,3,text,300);grouped=grouped&&!wcscmp(text,L"≥4.00%");
   cell_text(p,4,text,300);grouped=grouped&&!wcscmp(text,L"≥300.0 MB");
   cell_text(p,0,text,300);grouped=grouped&&wcsstr(text,L"3 个进程")!=NULL;
  }
  if(p->path_hash==200)other_path=p->instances==1;
  if(p->pid==14)low_hidden=false;
  if(!p->path_hash)unknown_count++;
 }
 unknowns=unknown_count==2;ok=ok&&grouped&&other_path&&low_hidden&&unknowns;
 int all=build_view(fixture,output,false);ok=ok&&all==9&&output[0].cpu==1.25&&fixture->rows[0].instances==1;
 fixture->count=30;
 for(int i=0;i<fixture->count;i++){
  Process *p=&fixture->rows[i];memset(p,0,sizeof(*p));p->pid=100+i;p->path_hash=1000+i;p->instances=1;
  p->cpu=i==29?90:0;p->has_window=i<20;swprintf(p->name,260,L"cap%d.exe",i);
 }
 count=build_view(fixture,output,true);ok=ok&&count==PRIMARY_LIMIT;
 for(int i=0;i<count;i++)ok=ok&&output[i].has_window;
 fixture->count=0;ok=ok&&build_view(fixture,output,true)==0;
 ok=ok&&executable_hash(L"C:\\Apps\\Browser.exe")==executable_hash(L"c:\\apps\\browser.EXE");
 fprintf(log,"%s: primary grouping, visibility priority, CPU/memory filters, 10-row limit, full-view restoration and partial readings.\n",ok?"PASS":"FAIL");
 free(fixture);free(output);return ok;
}
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
 bool primary_ok=primary_view_selftest(log);
 bool ownership_ok=software_ownership_selftest(log);
 bool codex_ok=codex_chain_selftest(log);
 bool gpu_ok=gpu_selftest(log);
 if(b){
  int codex_total=0,codex_owned=0;
  for(int i=0;i<b->count;i++)if(named(&b->rows[i],L"codex.exe")){codex_total++;if(b->rows[i].codex_owner)codex_owned++;}
  fprintf(log,"INFO: live codex.exe backends: %d total, %d desktop-owned, %d separate.\n",codex_total,codex_owned,codex_total-codex_owned);
  int groups=collect_software_groups(b,view_rows);
  for(int i=0;i<groups;i++)if(named(&view_rows[i],L"Weixin.exe")||named(&view_rows[i],L"WeChat.exe")||named(&view_rows[i],L"QQ.exe"))
   fprintf(log,"INFO: software group %ls includes %d processes, CPU %.2f, working set %.1f MB.\n",view_rows[i].name,view_rows[i].instances,view_rows[i].cpu,view_rows[i].memory/1048576.0);
  int n=build_view(b,view_rows,true);fprintf(log,"INFO: primary view %d rows from %d live processes.\n",n,b->count);
 }
 free(a);free(b);return ok&&own&&sorting&&primary_ok&&ownership_ok&&codex_ok&&gpu_ok;
}
