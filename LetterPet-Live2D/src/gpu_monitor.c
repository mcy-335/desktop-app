#include "gpu_monitor.h"
#include <pdhmsg.h>

/* GPU Engine counters are per process. First sum each physical engine across
   processes, then take the busiest engine, never a sum of parallel engines.
   https://devblogs.microsoft.com/directx/gpus-in-the-task-manager/ */
typedef struct {
 DWORD luid_hi,luid_lo,physical,engine;
 double usage;
 wchar_t type[48];
} EngineSample;

static GpuReading unavailable(void){GpuReading r={0};r.usage=-1;return r;}
void gpu_close(GpuSampler *sampler){
 if(sampler->query)PdhCloseQuery(sampler->query);
 sampler->query=NULL;sampler->counter=NULL;sampler->primed=false;
}
static bool valid_status(DWORD status){return status==PDH_CSTATUS_VALID_DATA||status==PDH_CSTATUS_NEW_DATA;}
static bool parse_engine(const wchar_t *name,EngineSample *engine){
 if(!name)return false;DWORD pid=0;int end=0;
 int fields=swscanf(name,L"pid_%lu_luid_0x%lx_0x%lx_phys_%lu_eng_%lu_engtype_%n",
  &pid,&engine->luid_hi,&engine->luid_lo,&engine->physical,&engine->engine,&end);
 if(fields!=5||end<=0||!name[end])return false;
 lstrcpynW(engine->type,name+end,48);return true;
}
static int compare_engine(const void *a,const void *b){
 const EngineSample *x=a,*y=b;
 if(x->luid_hi!=y->luid_hi)return x->luid_hi>y->luid_hi?1:-1;
 if(x->luid_lo!=y->luid_lo)return x->luid_lo>y->luid_lo?1:-1;
 if(x->physical!=y->physical)return x->physical>y->physical?1:-1;
 return (x->engine>y->engine)-(x->engine<y->engine);
}
static bool same_adapter(const EngineSample *a,const EngineSample *b){
 return a->luid_hi==b->luid_hi&&a->luid_lo==b->luid_lo&&a->physical==b->physical;
}
static GpuReading aggregate_engines(const PDH_FMT_COUNTERVALUE_ITEM_W *items,DWORD count){
 GpuReading result=unavailable();if(!items||!count||count>65536)return result;
 EngineSample *samples=calloc(count,sizeof(*samples));if(!samples)return result;
 unsigned valid=0;
 for(DWORD i=0;i<count;i++){
  double value=items[i].FmtValue.doubleValue;
  if(!valid_status(items[i].FmtValue.CStatus)||!isfinite(value)||value<0)continue;
  if(parse_engine(items[i].szName,&samples[valid])){samples[valid].usage=value;valid++;}
 }
 if(!valid){free(samples);return result;}
 qsort(samples,valid,sizeof(*samples),compare_engine);
 result.status=GPU_READY;result.usage=0;
 for(unsigned i=0;i<valid;){
  unsigned next=i+1;double total=samples[i].usage;
  while(next<valid&&!compare_engine(&samples[i],&samples[next]))total+=samples[next++].usage;
  if(!i||!same_adapter(&samples[i],&samples[i-1]))result.adapters++;
  result.engines++;
  total=clampd(total,0,100);
  if(!result.engine[0]||total>result.usage){result.usage=total;lstrcpynW(result.engine,samples[i].type,48);}
  i=next;
 }
 free(samples);return result;
}
static bool open_query(GpuSampler *sampler){
 sampler->last_error=PdhOpenQueryW(NULL,0,&sampler->query);
 if(sampler->last_error==ERROR_SUCCESS){
  /* AddEnglishCounter localizes the object/counter names; the wildcard handle
     intentionally stays unexpanded for GetFormattedCounterArrayW. */
  sampler->last_error=PdhAddEnglishCounterW(sampler->query,L"\\GPU Engine(*)\\Utilization Percentage",0,&sampler->counter);
 }
 if(sampler->last_error!=ERROR_SUCCESS){gpu_close(sampler);sampler->retry_at=GetTickCount64()+15000;return false;}
 return true;
}
void gpu_sample(GpuSampler *sampler,GpuReading *reading){
 *reading=unavailable();
 if(!sampler->query&&(GetTickCount64()<sampler->retry_at||!open_query(sampler)))return;
 sampler->last_error=PdhCollectQueryData(sampler->query);
 if(sampler->last_error!=ERROR_SUCCESS){gpu_close(sampler);sampler->retry_at=GetTickCount64()+15000;return;}
 /* Rate counters require two samples. Keep the query across refreshes. */
 if(!sampler->primed){sampler->primed=true;reading->status=GPU_WARMUP;return;}
 for(int attempt=0;attempt<3;attempt++){
  DWORD bytes=0,count=0;
  sampler->last_error=PdhGetFormattedCounterArrayW(sampler->counter,PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,&bytes,&count,NULL);
  if((DWORD)sampler->last_error!=PDH_MORE_DATA||!bytes||bytes>16*1024*1024)break;
  PDH_FMT_COUNTERVALUE_ITEM_W *items=malloc(bytes);if(!items){sampler->last_error=PDH_MEMORY_ALLOCATION_FAILURE;break;}
  sampler->last_error=PdhGetFormattedCounterArrayW(sampler->counter,PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,&bytes,&count,items);
  if(sampler->last_error==ERROR_SUCCESS)*reading=aggregate_engines(items,count);
  free(items);
  if((DWORD)sampler->last_error!=PDH_MORE_DATA)break;
  /* Re-query the size from zero when instances arrive/disappear. */
 }
 if(reading->status!=GPU_READY){
  gpu_close(sampler);sampler->retry_at=GetTickCount64()+15000;
 }
}

static void test_counter(PDH_FMT_COUNTERVALUE_ITEM_W *item,const wchar_t *name,double value,DWORD status){
 item->szName=(wchar_t*)name;item->FmtValue.CStatus=status;item->FmtValue.doubleValue=value;
}
bool gpu_selftest(FILE *log){
 PDH_FMT_COUNTERVALUE_ITEM_W items[7]={0};
 const wchar_t *a=L"pid_10_luid_0x00000000_0x00000001_phys_0_eng_0_engtype_3D";
 const wchar_t *b=L"pid_20_luid_0x00000000_0x00000001_phys_0_eng_0_engtype_3D";
 const wchar_t *c=L"pid_30_luid_0x00000000_0x00000001_phys_0_eng_1_engtype_VideoDecode";
 const wchar_t *d=L"pid_40_luid_0x00000000_0x00000002_phys_0_eng_0_engtype_3D";
 test_counter(&items[0],a,35,PDH_CSTATUS_VALID_DATA);test_counter(&items[1],b,25,PDH_CSTATUS_NEW_DATA);
 test_counter(&items[2],c,50,PDH_CSTATUS_VALID_DATA);test_counter(&items[3],d,70,PDH_CSTATUS_VALID_DATA);
 test_counter(&items[4],a,999,PDH_CSTATUS_INVALID_DATA);test_counter(&items[5],L"_Total",999,PDH_CSTATUS_VALID_DATA);
 test_counter(&items[6],a,NAN,PDH_CSTATUS_VALID_DATA);
 GpuReading reading=aggregate_engines(items,7);bool ok=reading.status==GPU_READY&&reading.usage==70&&reading.adapters==2&&reading.engines==3;
 reading=aggregate_engines(items,3);ok=ok&&reading.usage==60&&reading.adapters==1;
 test_counter(&items[0],a,0,PDH_CSTATUS_VALID_DATA);reading=aggregate_engines(items,1);ok=ok&&reading.status==GPU_READY&&reading.usage==0;
 test_counter(&items[0],a,130,PDH_CSTATUS_VALID_DATA);reading=aggregate_engines(items,1);ok=ok&&reading.usage==100;
 test_counter(&items[0],a,-1,PDH_CSTATUS_VALID_DATA);reading=aggregate_engines(items,1);ok=ok&&reading.status==GPU_UNAVAILABLE&&reading.usage<0;
 reading=aggregate_engines(NULL,0);ok=ok&&reading.status==GPU_UNAVAILABLE;
 GpuSampler blocked={0};blocked.retry_at=GetTickCount64()+10000;gpu_sample(&blocked,&reading);
 ok=ok&&reading.status==GPU_UNAVAILABLE&&reading.usage<0&&!blocked.query;gpu_close(&blocked);
 fprintf(log,"%s: GPU aggregation (per-engine sums, multiple adapters, idle zero, invalid samples, unavailable state, 0-100 bounds).\n",ok?"PASS":"FAIL");
 GpuSampler live={0};gpu_sample(&live,&reading);
 if(reading.status==GPU_WARMUP){Sleep(1100);gpu_sample(&live,&reading);}
 if(reading.status==GPU_READY){
  ok=ok&&reading.usage>=0&&reading.usage<=100;
  fprintf(log,"INFO: live GPU %.1f%%, %u adapters, %u engines, busiest %ls.\n",reading.usage,reading.adapters,reading.engines,reading.engine);
 }else fprintf(log,"INFO: GPU counters unavailable on this system (PDH 0x%08lx); UI shows unavailable, not 0%%.\n",(unsigned long)live.last_error);
 gpu_close(&live);return ok&&!live.query&&!live.counter&&!live.primed;
}
