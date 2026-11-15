#include "resources.h"
#include "companion.h"
#include "gpu_monitor.h"
static HANDLE stop_event,thread;
static ULONGLONG ft(FILETIME t){return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static DWORD WINAPI resource_worker(void *arg){
 HWND owner=arg;ULONGLONG old_total=0,old_idle=0;GpuSampler gpu={0};
 while(WaitForSingleObject(stop_event,0)!=WAIT_OBJECT_0){
  ResourceSample value={-1,-1,-1,now_ms()};FILETIME idle,kernel,user;
  if(GetSystemTimes(&idle,&kernel,&user)){
   ULONGLONG total=ft(kernel)+ft(user),id=ft(idle);
   if(old_total&&total>old_total&&id>=old_idle)value.cpu=clampd(100.0*(1-(double)(id-old_idle)/(total-old_total)),0,100);
   old_total=total;old_idle=id;
  }
  MEMORYSTATUSEX mem={sizeof(mem)};if(GlobalMemoryStatusEx(&mem))value.memory=mem.dwMemoryLoad;
  GpuReading reading={0};gpu_sample(&gpu,&reading);if(reading.status==GPU_READY)value.gpu=reading.usage;
  ResourceSample *copy=malloc(sizeof(*copy));if(copy){*copy=value;if(!PostMessageW(owner,WM_RESOURCE_SAMPLE,0,(LPARAM)copy))free(copy);}
  if(WaitForSingleObject(stop_event,5000)==WAIT_OBJECT_0)break;
 }
 gpu_close(&gpu);return 0;
}
bool resources_start(HWND owner){
 if(thread)return true;
 stop_event=CreateEventW(NULL,TRUE,FALSE,NULL);if(!stop_event)return false;
 thread=CreateThread(NULL,0,resource_worker,owner,0,NULL);
 if(!thread){CloseHandle(stop_event);stop_event=NULL;return false;}return true;
}
void resources_stop(HWND owner){
 if(thread){SetEvent(stop_event);WaitForSingleObject(thread,INFINITE);CloseHandle(thread);thread=NULL;}
 if(stop_event){CloseHandle(stop_event);stop_event=NULL;}
 MSG msg;while(PeekMessageW(&msg,owner,WM_RESOURCE_SAMPLE,WM_RESOURCE_SAMPLE,PM_REMOVE))free((void*)msg.lParam);
}
