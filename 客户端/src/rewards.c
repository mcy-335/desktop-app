#include "rewards.h"
#include "account.h"
#include <objbase.h>

typedef struct {
 char run[33];
 double elapsed;
 unsigned serial;
 bool active;
} FocusReport;
typedef struct {
 AccountServer server;
 char token[65],form[120],reply[160];
 FocusReport report;
 ULONGLONG started;
 bool checkpoint,final;
 unsigned version;
 DWORD status,used;
 LONG references;
} RewardRequest;
typedef enum {WAITING,ONLINE,OFFLINE,OTHER_DEVICE,UPGRADE,EXPIRED,UNAVAILABLE} Connection;
typedef enum {ELIGIBLE,OUTSIDE_ALLOWLIST} Eligibility;

static HANDLE worker;
static RewardRequest *request;
static FocusReport current,closing;
static bool round_open,phase_changed,retry_checkpoint;
static ULONGLONG next_request,preview_until;
static RewardEquipment equipped;
static RewardAccessory preview;
static const struct {const char *sku;const wchar_t *sprite;RewardSlot slot;} items[REWARD_ITEM_COUNT]={
 [REWARD_STAR]={"star",L"star",REWARD_SLOT_PLAQUE},
 [REWARD_HEART]={"heart",L"heart",REWARD_SLOT_PLAQUE},
 [REWARD_MOON]={"moon",L"moon",REWARD_SLOT_PLAQUE},
 [REWARD_LAMP]={"lamp",L"hanging-lantern",REWARD_SLOT_LAMP},
 [REWARD_BONSAI]={"bonsai",L"bonsai",REWARD_SLOT_BONSAI},
 [REWARD_HOURGLASS]={"hourglass",L"hourglass",REWARD_SLOT_HOURGLASS},
 [REWARD_TEA]={"tea",L"tea",REWARD_SLOT_TEA},
 [REWARD_BOOKMARK]={"bookmark",L"bookmark",REWARD_SLOT_BOOKMARK},
 [REWARD_STARLAMP]={"starlamp",L"starlamp",REWARD_SLOT_LAMP},
 [REWARD_IVORYTEA]={"ivorytea",L"hanging-letter",REWARD_SLOT_TEA},
 [REWARD_FERN]={"fern",L"hanging-leaf",REWARD_SLOT_BONSAI},
 [REWARD_PRISM]={"prism",L"prism",REWARD_SLOT_HOURGLASS}
};
static long long balance=-1,round_coins;
static unsigned next_seconds;
static Connection connection;
static Eligibility eligibility;
static bool daily_limit;

void rewards_focus_time(double elapsed,bool focusing,bool in_round,bool allowed){
 if(test_mode)return;
 if(in_round&&!round_open){
  current=(FocusReport){.serial=current.serial+1};
  round_coins=0;retry_checkpoint=false;phase_changed=true;
 }
 eligibility=ELIGIBLE;
 if(focusing||elapsed>0){
  if(!allowed)eligibility=OUTSIDE_ALLOWLIST;
 }
 if(eligibility==ELIGIBLE)current.elapsed+=elapsed;
 bool recording=focusing&&eligibility==ELIGIBLE;
 phase_changed|=current.active!=recording;
 current.active=recording;
 if(round_open&&!in_round&&current.run[0]){
  closing=current;phase_changed=true;
 }
 round_open=in_round;
}

static DWORD fetch(RewardRequest *job,const wchar_t *route){
 memset(job->reply,0,sizeof(job->reply));
 job->started=GetTickCount64();
 return account_request(&job->server,job->checkpoint?L"POST":L"GET",route,job->token,
  job->checkpoint?L"application/x-www-form-urlencoded":NULL,
  job->form,(DWORD)strlen(job->form),0,job->reply,sizeof(job->reply)-1,&job->used,NULL);
}
static void release_request(RewardRequest *job){
 if(InterlockedDecrement(&job->references)==0)free(job);
}
static DWORD WINAPI fetch_rewards(void *argument){
 RewardRequest *job=argument;
 job->version=5;
 job->status=fetch(job,job->checkpoint?L"/v5/focus":L"/v5/rewards");
 if(job->status==404){
  job->version=4;
  job->status=fetch(job,job->checkpoint?L"/v4/focus":L"/v4/rewards");
 }
 if(job->status==404){
  job->version=3;
  job->status=fetch(job,job->checkpoint?L"/v3/focus":L"/v3/rewards");
 }
 if(job->status==404){
  job->version=2;
  job->status=fetch(job,job->checkpoint?L"/v2/focus":L"/v2/rewards");
  if(job->status==404){
   job->version=1;
   job->status=fetch(job,job->checkpoint?L"/v1/focus":L"/v1/rewards");
  }
 }
 SecureZeroMemory(job->token,sizeof(job->token));
 release_request(job);
 return 0;
}

static bool read_number(const char **cursor,long long *value){
 char *end;
 if(**cursor<'0'||**cursor>'9')return false;
 *value=_strtoi64(*cursor,&end,10);
 if(*value<0||*end!='\n')return false;
 *cursor=end+1;return true;
}
static RewardAccessory accessory(const char *start,size_t length){
 for(RewardAccessory item=REWARD_STAR;item<REWARD_ITEM_COUNT;item++)
  if(strlen(items[item].sku)==length&&!memcmp(items[item].sku,start,length))return item;
 return REWARD_NONE;
}
static bool read_accessory(const char **cursor,RewardAccessory *item){
 const char *end=strchr(*cursor,'\n');
 if(!end)return false;
 *item=accessory(*cursor,(size_t)(end-*cursor));
 if(!*item&&(end-*cursor!=1||**cursor!='-'))return false;
 *cursor=end+1;
 return true;
}
static bool read_equipment(const char **cursor,RewardEquipment *equipment){
 const char *part=*cursor,*end=strchr(part,'\n');
 if(!end)return false;
 *cursor=end+1;
 if(end-part==1&&*part=='-')return true;
 do{
  const char *separator=memchr(part,',',(size_t)(end-part));
  const char *stop=separator?separator:end;
  RewardAccessory item=accessory(part,(size_t)(stop-part));
  if(!item)return false;
  RewardSlot slot=items[item].slot;
  if(equipment->item[slot])return false;
  equipment->item[slot]=item;
  if(!separator)return true;
  part=separator+1;
 }while(part<end);
 return false;
}
static RewardEquipment display_equipment(RewardEquipment base,RewardAccessory trial){
 if(trial)base.item[items[trial].slot]=trial;
 return base;
}
static bool equipment_changed(RewardEquipment before){
 RewardEquipment after=rewards_equipment();
 return memcmp(before.item,after.item,sizeof(before.item))!=0;
}
static bool apply_reply(const RewardRequest *job){
 const char *cursor=job->reply;
 long long amount,remaining=0,earned=0,preview_ms=0,limit=0;
 RewardEquipment next={0};
 RewardAccessory trial=REWARD_NONE;
 if(!read_number(&cursor,&amount)||!read_equipment(&cursor,&next))return false;
 if(job->version>=2&&(!read_number(&cursor,&remaining)||remaining<1||remaining>60000||
    !read_number(&cursor,&earned)||!read_accessory(&cursor,&trial)||
    !read_number(&cursor,&preview_ms)||preview_ms>60000))return false;
 if(job->version>=3&&(!read_number(&cursor,&limit)||limit>1))return false;
 if(*cursor)return false;
 RewardEquipment before=rewards_equipment();
 balance=amount;
 equipped=next;
 next_seconds=(unsigned)(remaining+999)/1000;
 if(job->checkpoint&&job->report.serial==current.serial&&job->version>=2)round_coins=earned;
 preview_until=job->started+(ULONGLONG)preview_ms;
 preview=preview_until>GetTickCount64()?trial:REWARD_NONE;
 if(equipment_changed(before))pet_refresh_appearance();
 daily_limit=limit!=0;
 connection=job->version>=3?ONLINE:UPGRADE;
 return true;
}
static void collect(const RewardRequest *job){
 if(job->status==200&&apply_reply(job))return;
 if(job->status==409&&job->checkpoint)connection=OTHER_DEVICE;
 else if(job->status==401)connection=EXPIRED;
 else if(job->status==404)connection=UPGRADE;
 else if(!job->status||job->status>=500){
  connection=OFFLINE;
  if(job->checkpoint&&job->final&&!round_open&&!closing.run[0])closing=job->report;
  else if(job->checkpoint&&!job->final&&round_open&&job->report.serial==current.serial)retry_checkpoint=true;
 }else connection=UNAVAILABLE;
}
static bool create_run(FocusReport *report){
 GUID id;
 if(FAILED(CoCreateGuid(&id)))return false;
 snprintf(report->run,sizeof(report->run),"%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
  id.Data1,id.Data2,id.Data3,id.Data4[0],id.Data4[1],id.Data4[2],id.Data4[3],
  id.Data4[4],id.Data4[5],id.Data4[6],id.Data4[7]);
 return true;
}

void rewards_tick(void){
 if(test_mode)return;
 ULONGLONG now=GetTickCount64();
 if(preview!=REWARD_NONE&&now>=preview_until){
  RewardEquipment before=rewards_equipment();
  preview=REWARD_NONE;
  if(equipment_changed(before))pet_refresh_appearance();
 }
 if(worker){
  if(WaitForSingleObject(worker,0)!=WAIT_OBJECT_0)return;
  CloseHandle(worker);worker=NULL;
  collect(request);
  release_request(request);request=NULL;
 }
 if(!phase_changed&&now<next_request)return;
 bool final=closing.run[0]!=0;
 bool checkpoint=final||(round_open&&(current.active||phase_changed||retry_checkpoint));
 if(checkpoint&&!final&&!current.run[0]&&!create_run(&current))return;
 next_request=now+(current.active?8000:5000);
 RewardRequest *job=calloc(1,sizeof(*job));
 if(!job)return;
 if(!account_connection(&job->server,job->token)){free(job);return;}
 job->checkpoint=checkpoint;job->final=final;job->report=final?closing:current;
 if(checkpoint)snprintf(job->form,sizeof(job->form),"run=%s&elapsed=%llu&active=%d",
  job->report.run,(unsigned long long)job->report.elapsed,job->report.active);
 job->references=2;
 worker=CreateThread(NULL,0,fetch_rewards,job,0,NULL);
 if(!worker){SecureZeroMemory(job,sizeof(*job));free(job);return;}
 request=job;
 if(final){
  closing=(FocusReport){0};
  if(job->report.serial==current.serial)phase_changed=false;
 }else phase_changed=retry_checkpoint=false;
}

void rewards_shutdown(void){
 if(worker){
  CloseHandle(worker);worker=NULL;release_request(request);request=NULL;
 }
 current=closing=(FocusReport){0};
 round_open=phase_changed=retry_checkpoint=daily_limit=false;
 next_request=preview_until=0;
 equipped=(RewardEquipment){0};preview=REWARD_NONE;
 balance=-1;round_coins=0;next_seconds=0;
 connection=WAITING;eligibility=ELIGIBLE;
}

void rewards_progress(wchar_t *text,size_t capacity){
 const wchar_t *status;
 switch(connection){
 case ONLINE:
  if(daily_limit)status=L"今日有效专注已达 180 分钟，代币明日继续累计";
  else if(eligibility==OUTSIDE_ALLOWLIST)status=L"当前窗口不在白名单内，暂不记账";
  else status=current.active?L"有效专注记账中":round_open?L"专注暂停，代币进度已保留":L"代币进度已同步";
  break;
 case OFFLINE:status=L"服务暂不可达，正在重试记账";break;
 case OTHER_DEVICE:status=L"另一台设备正在记账，本机暂未累计";break;
 case UPGRADE:status=L"请升级服务端以使用有效专注记账规则";break;
 case EXPIRED:status=L"登录会话已失效";break;
 case UNAVAILABLE:status=L"代币状态暂不可用";break;
 default:status=L"等待服务端确认代币进度";break;
 }
 if(next_seconds&&current.serial)swprintf(text,capacity,L"本轮到账 %lld 枚 · 距下一枚还差 %u 秒\r\n%ls",
                                          round_coins,next_seconds,status);
 else if(next_seconds)swprintf(text,capacity,L"距下一枚代币还差 %u 秒\r\n%ls",next_seconds,status);
 else swprintf(text,capacity,L"专注代币\r\n%ls",status);
}

RewardEquipment rewards_equipment(void){return display_equipment(equipped,preview);}
bool rewards_animate(void){
 RewardEquipment visible=rewards_equipment();
 for(int slot=0;slot<REWARD_SLOT_COUNT;slot++)if(visible.item[slot]!=REWARD_NONE)return true;
 return false;
}
const wchar_t *rewards_sprite_name(RewardAccessory item){return items[item].sprite;}

bool rewards_selftest(FILE *log){
 RewardEquipment layout={0};
 const char *list="lamp,bonsai,hourglass,star,tea,bookmark\n";
 bool ok=read_equipment(&list,&layout)&&!*list;
 for(int i=0;i<REWARD_SLOT_COUNT;i++)ok=ok&&layout.item[i]!=REWARD_NONE;
 RewardEquipment trial=display_equipment(layout,REWARD_HEART);
 ok=ok&&trial.item[REWARD_SLOT_PLAQUE]==REWARD_HEART&&trial.item[REWARD_SLOT_TEA]==REWARD_TEA&&
        layout.item[REWARD_SLOT_PLAQUE]==REWARD_STAR;
 const char *themed="starlamp,fern,prism,star,ivorytea,bookmark\n";
 RewardEquipment themes={0};
 ok=ok&&read_equipment(&themed,&themes)&&themes.item[REWARD_SLOT_LAMP]==REWARD_STARLAMP&&
    themes.item[REWARD_SLOT_BONSAI]==REWARD_FERN&&themes.item[REWARD_SLOT_HOURGLASS]==REWARD_PRISM&&
    themes.item[REWARD_SLOT_TEA]==REWARD_IVORYTEA;
 const char *legacy="moon\n";
 const char *invalid="star,heart\n";
 layout=(RewardEquipment){0};
 ok=ok&&read_equipment(&legacy,&layout)&&layout.item[REWARD_SLOT_PLAQUE]==REWARD_MOON;
 layout=(RewardEquipment){0};
 ok=ok&&!read_equipment(&invalid,&layout);
 fprintf(log,"%s: six accessory slots, trial override and legacy reply.\n",ok?"PASS":"FAIL");
 return ok;
}
long long rewards_balance(void){return balance;}
