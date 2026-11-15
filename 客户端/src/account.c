#include "account.h"
#include "account_net.h"
#include "rewards.h"
#include "ui_theme.h"
#include <commctrl.h>
#include <shellapi.h>
#include <wincrypt.h>

#define PATH_CAP 32768
#define FILE_LIMIT (256u*1024)
#define PACK_LIMIT (12u+2*FILE_LIMIT)
#define PACK_MAGIC 0x3153504cu
#define PROFILE_MAGIC 0x3150464cu
#define SESSION_MAGIC 0x3253454cu
#define LOGIN_ID 1301
#define ACCOUNT_START_CLASS L"LetterPet.Account.Startup"
#define WM_ACCOUNT_ACTIVATE (WM_APP+1)
enum {SYNC_ID=1302,PASSWORD_ID,LOGOUT_ID,REGISTER_ID,SHOP_ID,AUTO_LOGIN_ID,SHOW_PASSWORD_ID,SHOW_NEW_PASSWORD_ID};
typedef enum {LOGIN,REGISTER,RESUME,SYNC,PUSH,PASSWORD,LOGOUT} Operation;
typedef enum {FAILED_RESULT,AGREED,UPLOADED,DOWNLOAD,IMPORTED,MERGED,CONFLICT,DONE} Result;
typedef struct {BYTE *data;DWORD size;} Pack;
typedef struct {const BYTE *data;DWORD size;} PackPart;
typedef struct {
 DWORD magic;
 wchar_t url[256],username[33];
 char token[65],id[33],server_id[33];
} Session;
// The entire record is protected by Windows DPAPI for the current Windows user.
// Keep the old Session shape so existing token-only records still resume.
typedef struct {Session session;wchar_t password[129];} RememberedSession;
typedef struct {
 wchar_t root[PATH_CAP],settings[PATH_CAP];
 DWORD slot,revision;
 Pack base;
} Profile;
typedef struct {
 Operation operation;
 Result result;
 DWORD status,revision;
 bool interactive,account_created;
 Session session;
 AccountServer server;
 Profile profile;
 Pack local,remote,migration;
 char form[4096];
 wchar_t password[129];
} Job;

static wchar_t guest_path[PATH_CAP],app_root[PATH_CAP],session_file[PATH_CAP],last_url[256]=L"http://127.0.0.1:8765";
static Session session;
static wchar_t session_password[129];
static Profile profile;
static Job *pending;
static HANDLE worker;
static ULONGLONG next_sync;
static HWND panel,url_edit,user_edit,password_label,password_edit,new_label,new_edit,auto_login_box,migrate_box,show_password,show_new_password;
static HWND status_label,login_button,register_button,sync_button,password_button,logout_button;
static HWND balance_label,shop_button;
static long long displayed_balance;
static wchar_t status_text[240];
static bool startup_login;
static wchar_t last_username[33];
static int dpi=96;
static void start(Operation operation,bool interactive);
static bool signing_in(Operation operation){return operation==LOGIN||operation==REGISTER;}
static bool opening_session(Operation operation){return signing_in(operation)||operation==RESUME;}

static void free_pack(Pack *pack){free(pack->data);*pack=(Pack){0};}
static void free_profile(Profile *p){free_pack(&p->base);p->root[0]=p->settings[0]=0;p->slot=p->revision=0;}
static void free_job(Job *job){
 if(!job)return;
 free_profile(&job->profile);free_pack(&job->local);free_pack(&job->remote);free_pack(&job->migration);
 SecureZeroMemory(job,sizeof(*job));free(job);
}
static bool path_join(wchar_t *dest,const wchar_t *root,const wchar_t *name){
 int n=swprintf(dest,PATH_CAP,L"%ls\\%ls",root,name);
 return n>0&&n<PATH_CAP-8;
}
static bool same_pack(const Pack *a,const Pack *b){
 return a->size==b->size&&(!a->size||!memcmp(a->data,b->data,a->size));
}
static bool pack_valid(const Pack *pack){
 if(pack->size<12||pack->size>PACK_LIMIT)return false;
 DWORD header[3];memcpy(header,pack->data,sizeof(header));
 if(header[0]!=PACK_MAGIC||header[1]>FILE_LIMIT||header[2]>FILE_LIMIT||pack->size!=12+header[1]+header[2])return false;
 const wchar_t *sections[]={L"\xfeff[companion]\r\n",L"\xfeff[timetable]\r\n"};
 DWORD start=12;
 for(int i=0;i<2;i++){
  DWORD size=header[i+1],prefix=(DWORD)(wcslen(sections[i])*sizeof(wchar_t));
  if(size&&(size<prefix||size%2||memcmp(pack->data+start,sections[i],prefix)))return false;
  start+=size;
 }
 return true;
}
static void pack_parts(const Pack *pack,PackPart parts[2]){
 DWORD lengths[2];
 memcpy(lengths,pack->data+sizeof(DWORD),sizeof(lengths));
 parts[0]=(PackPart){pack->data+12,lengths[0]};
 parts[1]=(PackPart){pack->data+12+lengths[0],lengths[1]};
}
static bool same_part(PackPart a,PackPart b){
 return a.size==b.size&&(!a.size||!memcmp(a.data,b.data,a.size));
}
static bool merge_pack(const Pack *base,const Pack *local,const Pack *remote,Pack *merged){
 if(!base->size)return false;
 PackPart prior[2],here[2],there[2],chosen[2];
 pack_parts(base,prior);pack_parts(local,here);pack_parts(remote,there);
 for(int i=0;i<2;i++){
  if(same_part(here[i],there[i])||same_part(there[i],prior[i]))chosen[i]=here[i];
  else if(same_part(here[i],prior[i]))chosen[i]=there[i];
  else return false;
 }
 DWORD header[]={PACK_MAGIC,chosen[0].size,chosen[1].size};
 DWORD size=12+chosen[0].size+chosen[1].size;
 BYTE *data=malloc(size);
 if(!data)return false;
 memcpy(data,header,12);
 memcpy(data+12,chosen[0].data,chosen[0].size);
 memcpy(data+12+chosen[0].size,chosen[1].data,chosen[1].size);
 *merged=(Pack){data,size};
 return true;
}
static bool read_file(const wchar_t *path,DWORD limit,Pack *pack){
 HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
 if(file==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND;
 LARGE_INTEGER size;bool ok=GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=limit;
 if(ok){
  pack->size=(DWORD)size.QuadPart;pack->data=malloc(pack->size);
  DWORD read=0;ok=pack->data&&ReadFile(file,pack->data,pack->size,&read,NULL)&&read==pack->size;
 }
 CloseHandle(file);
 if(!ok)free_pack(pack);
 return ok;
}
static bool write_file(const wchar_t *path,const void *data,DWORD size){
 wchar_t temp[PATH_CAP];int n=swprintf(temp,PATH_CAP,L"%ls.tmp",path);
 if(n<=0||n>=PATH_CAP-1)return false;
 HANDLE file=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
 if(file==INVALID_HANDLE_VALUE)return false;
 DWORD wrote;
 bool ok=WriteFile(file,data,size,&wrote,NULL)&&wrote==size&&FlushFileBuffers(file);
 CloseHandle(file);
 if(ok)ok=MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
 if(!ok)DeleteFileW(temp);
 return ok;
}
static bool directory(const wchar_t *path){
 if(CreateDirectoryW(path,NULL))return true;
 DWORD attributes=GetFileAttributesW(path);
 return attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
}
static bool hex_id(const char *value,size_t length){
 for(size_t i=0;i<length;i++)if(!((value[i]>='0'&&value[i]<='9')||(value[i]>='a'&&value[i]<='f')))return false;
 return value[length]==0;
}
static bool session_valid(const Session *value){
 AccountServer endpoint;
 return value->magic==SESSION_MAGIC&&value->url[255]==0&&value->username[32]==0&&value->username[0]&&
        hex_id(value->token,64)&&hex_id(value->id,32)&&hex_id(value->server_id,32)&&account_server_parse(value->url,&endpoint);
}
static bool forget_session(void){
 if(DeleteFileW(session_file))return true;
 return GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND;
}
static bool remember_session(const Session *value,const wchar_t *password){
 RememberedSession record={0};record.session=*value;
 lstrcpynW(record.password,password,ARRAYSIZE(record.password));
 DATA_BLOB plain={sizeof(record),(BYTE*)&record},sealed={0};
 bool protected=CryptProtectData(&plain,L"LetterPet account",NULL,NULL,NULL,CRYPTPROTECT_UI_FORBIDDEN,&sealed)!=0;
 SecureZeroMemory(&record,sizeof(record));
 if(!protected)return false;
 bool ok=write_file(session_file,sealed.pbData,sealed.cbData);
 LocalFree(sealed.pbData);return ok;
}
static bool recall_session(Session *value,wchar_t password[129]){
 Pack sealed={0};
 bool ok=false;
 SecureZeroMemory(password,129*sizeof(wchar_t));
 if(read_file(session_file,16384,&sealed)&&sealed.size){
  DATA_BLOB blob={sealed.size,sealed.data},plain={0};
  if(CryptUnprotectData(&blob,NULL,NULL,NULL,NULL,CRYPTPROTECT_UI_FORBIDDEN,&plain)){
   if(plain.cbData==sizeof(*value)){memcpy(value,plain.pbData,sizeof(*value));ok=session_valid(value);}
   else if(plain.cbData==sizeof(RememberedSession)){
    const RememberedSession *record=(const RememberedSession*)plain.pbData;
    ok=session_valid(&record->session)&&record->password[128]==0;
    if(ok){*value=record->session;memcpy(password,record->password,sizeof(record->password));}
   }
   SecureZeroMemory(plain.pbData,plain.cbData);LocalFree(plain.pbData);
  }
 }
 free_pack(&sealed);
 if(!ok)SecureZeroMemory(value,sizeof(*value));
 return ok;
}
static bool slot_path(const Profile *p,DWORD slot,wchar_t *folder){
 return path_join(folder,p->root,slot?L"b":L"a")&&directory(folder);
}
static bool profile_open(const Session *value,Profile *p){
 if(!session_valid(value))return false;
 wchar_t accounts[PATH_CAP],name[70],path[PATH_CAP];
 swprintf(name,70,L"%hs-%hs",value->server_id,value->id);
 if(!path_join(accounts,app_root,L"accounts")||!directory(accounts)||
    !path_join(p->root,accounts,name)||!directory(p->root)||!path_join(path,p->root,L"sync.dat"))return false;
 Pack stored={0};
 if(!read_file(path,PACK_LIMIT+12,&stored))return false;
 bool ok=true;
 if(stored.size){
  DWORD header[3];
  if(stored.size<24)ok=false;
  else{
   memcpy(header,stored.data,12);
   ok=header[0]==PROFILE_MAGIC&&header[1]<=1;
   if(ok){
    p->slot=header[1];p->revision=header[2];p->base=stored;stored=(Pack){0};
    p->base.size-=12;memmove(p->base.data,p->base.data+12,p->base.size);
    ok=pack_valid(&p->base);
   }
  }
 }
 free_pack(&stored);
 return ok&&slot_path(p,p->slot,accounts)&&path_join(p->settings,accounts,L"settings.ini");
}
static void normalize_care_timer(Pack *file){
 if(!file->data||file->size%2)return;
 wchar_t *data=(wchar_t*)file->data;
 const wchar_t key[]=L"\ncare_ms=";
 size_t length=file->size/2,k=wcslen(key);
 for(size_t i=0;i+k<length;i++){
  if(wmemcmp(data+i,key,k))continue;
  size_t start=i+k,end=start;
  while(end<length&&data[end]>=L'0'&&data[end]<=L'9')end++;
  if(end>start){
   data[start]=L'0';
   memmove(data+start+1,data+end,(length-end)*sizeof(wchar_t));
   file->size-=(DWORD)((end-start-1)*sizeof(wchar_t));
  }
  return;
 }
}
static bool read_snapshot(const wchar_t *settings_path,Pack *pack){
 wchar_t directory_path[PATH_CAP],path[PATH_CAP];
 lstrcpynW(directory_path,settings_path,PATH_CAP);
 wchar_t *slash=wcsrchr(directory_path,L'\\');
 if(!slash)return false;
 *slash=0;
 Pack files[2]={0};
 bool ok=path_join(path,directory_path,L"companion.ini")&&read_file(path,FILE_LIMIT,&files[0])&&
         path_join(path,directory_path,L"timetable.ini")&&read_file(path,FILE_LIMIT,&files[1]);
 if(ok){
  normalize_care_timer(&files[0]);
  DWORD header[]={PACK_MAGIC,files[0].size,files[1].size};
  pack->size=12+files[0].size+files[1].size;
  pack->data=malloc(pack->size);
  ok=pack->data!=NULL;
  if(ok){
   memcpy(pack->data,header,12);
   if(files[0].size)memcpy(pack->data+12,files[0].data,files[0].size);
   if(files[1].size)memcpy(pack->data+12+files[0].size,files[1].data,files[1].size);
   ok=pack_valid(pack);
  }
 }
 free_pack(&files[0]);free_pack(&files[1]);
 if(!ok)free_pack(pack);
 return ok;
}
static bool store_profile(Profile *p,DWORD revision,const Pack *data,bool replace){
 if(!replace&&p->revision==revision&&same_pack(&p->base,data))return true;
 DWORD slot=replace?1-p->slot:p->slot;
 wchar_t folder[PATH_CAP],path[PATH_CAP];
 if(!slot_path(p,slot,folder))return false;
 if(replace){
  DWORD header[3],offset=12;memcpy(header,data->data,12);
  const wchar_t *files[]={L"companion.ini",L"timetable.ini"};
  for(int i=0;i<2;i++){
   if(!path_join(path,folder,files[i]))return false;
   if(header[i+1]){
    if(!write_file(path,data->data+offset,header[i+1]))return false;
   }else if(!DeleteFileW(path)&&GetLastError()!=ERROR_FILE_NOT_FOUND)return false;
   offset+=header[i+1];
  }
 }
 if(!path_join(path,p->root,L"sync.dat"))return false;
 DWORD header[]={PROFILE_MAGIC,slot,revision};
 BYTE *combined=malloc(12+data->size);
 if(!combined)return false;
 memcpy(combined,header,12);memcpy(combined+12,data->data,data->size);
 bool ok=write_file(path,combined,12+data->size);
 free(combined);
 if(!ok)return false;
 p->slot=slot;p->revision=revision;
 return path_join(p->settings,folder,L"settings.ini");
}

static void post_status(const wchar_t *value){
 lstrcpynW(status_text,value,240);
 if(status_label)SetWindowTextW(status_label,status_text);
}
static bool auto_login_selected(void){
 return SendMessageW(auto_login_box,BM_GETCHECK,0,0)==BST_CHECKED;
}
static void hide_password(HWND edit,HWND toggle){
 SendMessageW(edit,EM_SETPASSWORDCHAR,0x25cf,0);
 InvalidateRect(edit,NULL,TRUE);
 SetWindowTextW(toggle,L"显示");
}
static void toggle_password(HWND edit,HWND toggle){
 if(SendMessageW(edit,EM_GETPASSWORDCHAR,0,0)){
  SendMessageW(edit,EM_SETPASSWORDCHAR,0,0);
  InvalidateRect(edit,NULL,TRUE);
  SetWindowTextW(toggle,L"隐藏");
 }else hide_password(edit,toggle);
 SetFocus(edit);
}
static void refresh(void){
 if(!panel)return;
 bool logged=session.token[0]!=0,busy=pending!=NULL;
 EnableWindow(url_edit,!logged&&!busy);EnableWindow(user_edit,!logged&&!busy);
 EnableWindow(password_edit,!busy);EnableWindow(new_edit,!busy);
 EnableWindow(show_password,!busy);EnableWindow(show_new_password,!busy);
 SetWindowTextW(password_label,logged?L"当前密码":L"密码");
 SetWindowTextW(new_label,logged?L"新密码":L"确认密码（注册）");
 if(auto_login_box)EnableWindow(auto_login_box,!logged&&!busy);
 if(migrate_box)EnableWindow(migrate_box,!logged&&!busy);
 bool can_sign_in=!logged&&!busy&&auto_login_selected();
 EnableWindow(login_button,can_sign_in);EnableWindow(register_button,can_sign_in);
 EnableWindow(sync_button,logged&&!busy);EnableWindow(password_button,logged&&!busy);EnableWindow(logout_button,logged&&!busy);
 ShowWindow(login_button,logged?SW_HIDE:SW_SHOW);ShowWindow(register_button,logged?SW_HIDE:SW_SHOW);
 ShowWindow(sync_button,logged?SW_SHOW:SW_HIDE);ShowWindow(password_button,logged?SW_SHOW:SW_HIDE);
 ShowWindow(logout_button,logged?SW_SHOW:SW_HIDE);
 if(shop_button)ShowWindow(shop_button,logged?SW_SHOW:SW_HIDE);
}
static void show_reward_balance(void){
 if(!balance_label)return;
 long long value=rewards_balance();
 if(value==displayed_balance)return;
 displayed_balance=value;
 wchar_t label[80];
 if(value<0)wcscpy(label,L"商城代币 · 等待同步");
 else swprintf(label,80,L"商城代币 · %lld 枚",value);
 SetWindowTextW(balance_label,label);
}
static void end_session(const wchar_t *notice){
 bool removed=forget_session();
 SecureZeroMemory(&session,sizeof(session));
 SecureZeroMemory(session_password,sizeof(session_password));
 pet_end_session(removed?notice:L"桌宠已退出。Windows 未能清除本机登录凭据，请重新手动登录。");
}
static bool get_login(Job *job){
 char reply[132];
 DWORD used;
 bool registration=job->operation==REGISTER;
 job->status=account_request(&job->server,L"POST",registration?L"/v1/register":L"/v1/login",NULL,L"application/x-www-form-urlencoded",
                             job->form,(DWORD)strlen(job->form),0,reply,sizeof(reply),&used,NULL);
 SecureZeroMemory(job->form,sizeof(job->form));
 if(job->status!=(registration?201u:200u))return false;
 job->account_created=registration;
 bool ok=used==131&&reply[64]=='\n'&&reply[97]=='\n'&&reply[130]=='\n';
 if(ok){
  memcpy(job->session.token,reply,64);memcpy(job->session.id,reply+65,32);memcpy(job->session.server_id,reply+98,32);
  ok=profile_open(&job->session,&job->profile)&&read_snapshot(job->profile.settings,&job->local);
 }
 SecureZeroMemory(reply,sizeof(reply));
 if(!ok)job->status=500;
 return ok;
}
static void upload_pack(Job *job,const Pack *pack){
 DWORD used,revision;
 job->status=account_request(&job->server,L"PUT",L"/v1/data",job->session.token,L"application/octet-stream",
                             pack->data,pack->size,job->revision,NULL,0,&used,&revision);
 if(job->status==204){job->revision=revision;job->result=UPLOADED;}
}
static void upload(Job *job){upload_pack(job,&job->local);}
static DWORD WINAPI run_job(void *argument){
 Job *job=argument;
 if(job->operation==PASSWORD||job->operation==LOGOUT){
  DWORD used;
  bool change=job->operation==PASSWORD;
  job->status=account_request(&job->server,L"POST",change?L"/v1/password":L"/v1/logout",job->session.token,
                              change?L"application/x-www-form-urlencoded":NULL,job->form,(DWORD)strlen(job->form),
                              0,NULL,0,&used,NULL);
  SecureZeroMemory(job->form,sizeof(job->form));
  if(job->status==204)job->result=DONE;
  return 0;
 }
 if(job->operation==PUSH){upload(job);return 0;}
 if(signing_in(job->operation)&&!get_login(job))return 0;
 job->remote.data=malloc(PACK_LIMIT);
 if(!job->remote.data){job->status=500;return 0;}
 job->status=account_request(&job->server,L"GET",L"/v1/data",job->session.token,NULL,NULL,0,0,
                             job->remote.data,PACK_LIMIT,&job->remote.size,&job->revision);
 if(job->status!=200)return 0;
 if(!pack_valid(&job->remote)){job->status=500;return 0;}
 if(signing_in(job->operation)&&job->migration.size>12&&!job->revision&&job->local.size==12&&job->remote.size==12){
  free_pack(&job->local);job->local=job->migration;job->migration=(Pack){0};
  upload(job);
  if(job->result==UPLOADED)job->result=IMPORTED;
  return 0;
 }
 bool remote_at_base=same_pack(&job->remote,&job->profile.base);
 if(same_pack(&job->local,&job->remote))job->result=AGREED;
 else if(!job->revision||(remote_at_base&&job->revision==job->profile.revision))upload(job);
 else if(job->local.size==12||same_pack(&job->local,&job->profile.base))job->result=DOWNLOAD;
 else{
  Pack merged={0};
  // A newer revision with identical bytes may be an explicit cloud restore.
  if(remote_at_base||!merge_pack(&job->profile.base,&job->local,&job->remote,&merged))job->result=CONFLICT;
  else{
   upload_pack(job,&merged);
   if(job->result==UPLOADED){free_pack(&job->remote);job->remote=merged;job->result=MERGED;}
   else free_pack(&merged);
  }
 }
 return 0;
}
static bool launch(Job *job){
 worker=CreateThread(NULL,0,run_job,job,0,NULL);
 if(!worker)return false;
 pending=job;next_sync=GetTickCount64()+60000;
 if(job->interactive)post_status(job->operation==RESUME?L"正在向服务端验证记住的登录…":L"正在连接账号服务…");
 refresh();return true;
}
static bool capture_current(Pack *pack){
 return pet_flush_profile()&&read_snapshot(profile.settings,pack);
}
static bool local_still_current(const Job *job){
 Pack now={0};
 bool ok=capture_current(&now)&&same_pack(&now,&job->local);
 free_pack(&now);return ok;
}
static bool finish_sync(Job *job){
 bool replace=job->result==DOWNLOAD||job->result==IMPORTED||job->result==MERGED;
 const Pack *data=job->result==UPLOADED||job->result==IMPORTED?&job->local:&job->remote;
 if(replace&&!opening_session(job->operation)&&!local_still_current(job)){
  post_status(L"同步期间本机存档有变化。请暂停正在进行的专注后再同步。");return false;
 }
 if(!store_profile(&job->profile,job->revision,data,replace)){
  post_status(L"无法写入本机账号存档。请检查目录后再同步。");return false;
 }
 if(!opening_session(job->operation)){
  if(replace)pet_switch_profile(job->profile.settings);
  profile.slot=job->profile.slot;profile.revision=job->profile.revision;
  lstrcpynW(profile.settings,job->profile.settings,PATH_CAP);
 }
 post_status(job->result==MERGED?L"已合并两端对不同存档文件的改动。":L"同步完成。");return true;
}
static const wchar_t *problem(const Job *job){
 if(job->account_created)return L"账号已注册，首次同步未完成。请用新账号登录。";
 if(job->operation==REGISTER){
  if(job->status==409)return L"账号名已被使用，请换一个名字或直接登录。";
  if(job->status==400)return L"账号须以小写字母开头（3–32 位），密码须为 8–128 个字符。";
  if(job->status==404)return L"当前服务端还不支持注册，请更新并重启账号服务。";
 }
 if(job->status==401)return signing_in(job->operation)?L"账号、密码不正确，或账号已停用。":L"登录已失效，请重新登录。";
 if(job->status==403&&job->operation==PASSWORD)return L"当前密码不正确。";
 if(job->status==409)return L"云端在同步期间有新版本，请再试一次。";
 if(job->status==429)return L"尝试次数过多，请稍后再试。";
 if(job->status==400&&job->operation==PASSWORD)return L"新密码须为 8–128 个字符。";
 if(!job->status)return L"连接失败。请检查服务地址、服务是否运行和 HTTPS 证书。";
 return L"账号服务或存档不可用，请稍后再试。";
}
static void complete_sign_in(Job *job){
 if(job->result==CONFLICT)post_status(L"已登录。本机和云端都有变更，点「立即同步」处理。");
 else{
  if(!finish_sync(job))return;
  post_status(job->result==IMPORTED?L"已登录，本机进度已导入并同步。":
              job->migration.size>12?L"已登录并同步。该账号已有存档，原本机存档仍保留。":
              job->operation==REGISTER?L"注册成功，已登录并同步。":L"已登录并同步。");
 }
 session=job->session;free_profile(&profile);profile=job->profile;job->profile=(Profile){0};
 memcpy(session_password,job->password,sizeof(session_password));
 lstrcpynW(last_url,session.url,256);
 lstrcpynW(last_username,session.username,ARRAYSIZE(last_username));
 if(job->operation!=RESUME&&!remember_session(&session,session_password))
  MessageBoxW(panel,L"本次已登录，但 Windows 无法保存自动登录凭据。下次启动时需要重新输入密码。",
              L"自动登录",MB_OK|MB_ICONWARNING);
}
void account_poll(void){
 show_reward_balance();
 if(!worker){
  if(!pending&&session.token[0]&&GetTickCount64()>=next_sync){
   next_sync=GetTickCount64()+60000;start(SYNC,false);
  }
  return;
 }
 if(WaitForSingleObject(worker,0)!=WAIT_OBJECT_0)return;
 CloseHandle(worker);worker=NULL;
 Job *job=pending;
 // Keep the request marked pending while a conflict confirmation pumps window messages.
 if(job->operation==LOGOUT){
  end_session(job->result==DONE||job->status==401?L"已退出账号，请重新登录。":L"已退出本机登录。服务端会话将在到期后失效。");
 }else if(job->operation==PASSWORD&&job->result==DONE){
  end_session(L"密码已修改。请使用新密码登录。");
 }else if(job->result==FAILED_RESULT){
  post_status(problem(job));
  if(job->status==401){
   if(job->operation==RESUME)forget_session();
   else if(!signing_in(job->operation))end_session(L"账号登录已失效，请重新登录。");
  }
 }else if(opening_session(job->operation)){
  complete_sign_in(job);
 }else if(job->result==CONFLICT&&!job->interactive){
  post_status(L"本机和云端都有变更，点「立即同步」选择要使用的版本。");
 }else if(job->result==CONFLICT){
  int choice=MessageBoxW(panel?panel:pet_window,
    L"本机和云端有不能自动合并的改动。\n\n选「是」使用云端，选「否」把本机版本保存到云端，选「取消」稍后处理。\n\n商城的「云端存档」可以恢复之前的云端版本。",
    L"选择同步版本",MB_YESNOCANCEL|MB_ICONQUESTION|MB_DEFBUTTON3);
  if(choice==IDYES){job->result=DOWNLOAD;finish_sync(job);}
  else if(choice==IDNO){
   free_pack(&job->local);
   if(capture_current(&job->local)){
    job->operation=PUSH;job->result=FAILED_RESULT;
    if(launch(job))return;
   }
   post_status(L"无法保存本机版本，请检查存档目录后再试。");
  }else post_status(L"本机与云端版本均已保留。");
 }else finish_sync(job);
 pending=NULL;free_job(job);refresh();
}

void account_init(const wchar_t *guest_settings){
 lstrcpynW(guest_path,guest_settings,PATH_CAP);
 lstrcpynW(app_root,guest_settings,PATH_CAP);
 wchar_t *slash=wcsrchr(app_root,L'\\');
 if(!slash)return;
 *slash=0;
 if(!path_join(session_file,app_root,L"account.session"))session_file[0]=0;
}
const wchar_t *account_profile_settings(void){return profile.settings;}
bool account_connection(AccountServer *server,char token[65]){
 if(!session.token[0]||!account_server_parse(session.url,server))return false;
 memcpy(token,session.token,65);return true;
}

static void open_shop(void){
 AccountServer server;
 if(!session.token[0]||!account_server_parse(session.url,&server))return;
 size_t n=wcslen(session.url);
 if(session.url[n-1]==L'/')n--;
 wchar_t address[270];
 swprintf(address,270,L"%.*ls/shop",(int)n,session.url);
 if((INT_PTR)ShellExecuteW(panel,L"open",address,NULL,NULL,SW_SHOWNORMAL)<=32)
  post_status(L"无法打开浏览器，请在浏览器中访问服务端的商城页面。");
}

static void resume_login(const Session *saved,const wchar_t *password){
 Job *job=calloc(1,sizeof(*job));
 if(!job){post_status(L"内存不足，请稍后再试。");return;}
 job->operation=RESUME;job->interactive=true;job->session=*saved;
 lstrcpynW(job->password,password,ARRAYSIZE(job->password));
 if(account_server_parse(saved->url,&job->server)&&profile_open(saved,&job->profile)&&
    read_snapshot(job->profile.settings,&job->local)&&launch(job))return;
 free_job(job);post_status(L"无法恢复记住的登录，请手动登录。");
}

static void start(Operation operation,bool interactive){
 if(pending)return;
 if(signing_in(operation)&&!auto_login_selected()){
  post_status(L"请先勾选「自动登录」。");
  return;
 }
 Job *job=calloc(1,sizeof(*job));
 if(!job){post_status(L"内存不足，请稍后再试。");return;}
 job->operation=operation;job->interactive=interactive;
 bool ok=true;
 if(signing_in(operation)){
  wchar_t password[129],confirmation[129];
  job->session.magic=SESSION_MAGIC;
  GetWindowTextW(url_edit,job->session.url,256);
  GetWindowTextW(user_edit,job->session.username,33);
  GetWindowTextW(password_edit,password,129);SetWindowTextW(password_edit,L"");
  GetWindowTextW(new_edit,confirmation,129);SetWindowTextW(new_edit,L"");
  hide_password(password_edit,show_password);hide_password(new_edit,show_new_password);
  if(!account_server_parse(job->session.url,&job->server)){
   post_status(L"请输入服务端 HTTPS 地址。本机可用 http://127.0.0.1:8765。");ok=false;
  }else if(operation==REGISTER&&(wcslen(password)<8||wcscmp(password,confirmation))){
   post_status(L"注册密码至少 8 个字符，请确认两次输入一致。");ok=false;
  }else if(!*job->session.username||!*password||
           !account_form(job->form,sizeof(job->form),"username",job->session.username,"password",password)){
   post_status(L"请输入账号和密码。");ok=false;
  }
  if(ok)lstrcpynW(job->password,password,ARRAYSIZE(job->password));
  SecureZeroMemory(password,sizeof(password));
  SecureZeroMemory(confirmation,sizeof(confirmation));
  if(ok&&SendMessageW(migrate_box,BM_GETCHECK,0,0)==BST_CHECKED&&!read_snapshot(guest_path,&job->migration)){
   post_status(L"无法读取原本机进度，暂时无法导入。");ok=false;
  }
 }else{
  job->session=session;
  ok=account_server_parse(session.url,&job->server);
  if(operation==SYNC&&ok){
   ok=profile_open(&session,&job->profile)&&capture_current(&job->local);
   if(!ok)post_status(L"本机存档尚未保存成功。请检查目录后再试。");
  }
  if(operation==PASSWORD&&ok){
   wchar_t old[129],new_password[129];
   GetWindowTextW(password_edit,old,129);GetWindowTextW(new_edit,new_password,129);
   SetWindowTextW(password_edit,L"");SetWindowTextW(new_edit,L"");
   hide_password(password_edit,show_password);hide_password(new_edit,show_new_password);
   ok=*old&&wcslen(new_password)>=8&&account_form(job->form,sizeof(job->form),"old",old,"new",new_password);
   SecureZeroMemory(old,sizeof(old));SecureZeroMemory(new_password,sizeof(new_password));
   if(!ok)post_status(L"请输入当前密码和至少 8 个字符的新密码。");
  }
 }
 if(ok&&launch(job))return;
 if(ok)post_status(L"无法启动网络请求，请稍后再试。");
 free_job(job);refresh();
}
static int px(int value){return MulDiv(value,dpi,96);}
enum { ACCOUNT_HEADING_HEIGHT=40 };
static HWND control(HWND parent,const wchar_t *kind,const wchar_t *title,DWORD style,int x,int y,int w,int h,int id){
 return CreateWindowW(kind,title,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),parent,(HMENU)(INT_PTR)id,pet_instance,NULL);
}
static LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
 switch(message){
 case WM_CREATE:{
  control(window,L"STATIC",startup_login?L"登录桌宠":L"我的账号",0,20,14,484,29,0);
  control(window,L"STATIC",L"服务地址",0,20,ACCOUNT_HEADING_HEIGHT+20,100,24,0);
  url_edit=control(window,L"EDIT",session.token[0]?session.url:last_url,WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,120,ACCOUNT_HEADING_HEIGHT+16,384,31,0);
  control(window,L"STATIC",L"账号",0,20,ACCOUNT_HEADING_HEIGHT+64,100,24,0);
  user_edit=control(window,L"EDIT",session.token[0]?session.username:last_username,WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,120,ACCOUNT_HEADING_HEIGHT+60,384,31,0);
  password_label=control(window,L"STATIC",L"密码",0,20,ACCOUNT_HEADING_HEIGHT+108,100,24,0);
  password_edit=control(window,L"EDIT",session.token[0]?session_password:L"",WS_TABSTOP|WS_BORDER|ES_PASSWORD|ES_AUTOHSCROLL,120,ACCOUNT_HEADING_HEIGHT+104,304,31,0);
  show_password=control(window,L"BUTTON",L"显示",WS_TABSTOP,434,ACCOUNT_HEADING_HEIGHT+104,70,31,SHOW_PASSWORD_ID);
  new_label=control(window,L"STATIC",L"确认密码（注册）",0,20,ACCOUNT_HEADING_HEIGHT+152,100,36,0);
  new_edit=control(window,L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_PASSWORD|ES_AUTOHSCROLL,120,ACCOUNT_HEADING_HEIGHT+148,304,31,0);
  show_new_password=control(window,L"BUTTON",L"显示",WS_TABSTOP,434,ACCOUNT_HEADING_HEIGHT+148,70,31,SHOW_NEW_PASSWORD_ID);
  SendMessageW(url_edit,EM_SETLIMITTEXT,255,0);SendMessageW(user_edit,EM_SETLIMITTEXT,32,0);
  SendMessageW(password_edit,EM_SETLIMITTEXT,128,0);SendMessageW(new_edit,EM_SETLIMITTEXT,128,0);
  if(startup_login){
   auto_login_box=control(window,L"BUTTON",L"自动登录（必选）",WS_TABSTOP|BS_AUTOCHECKBOX,120,ACCOUNT_HEADING_HEIGHT+190,144,28,AUTO_LOGIN_ID);
   migrate_box=control(window,L"BUTTON",L"首次登录导入本机进度",WS_TABSTOP|BS_AUTOCHECKBOX,265,ACCOUNT_HEADING_HEIGHT+190,239,28,0);
  }else{
   control(window,L"STATIC",session_password[0]?L"密码已加密保存在本机，可点击「显示」查看。":
     L"本机尚未保存密码。退出后重新登录，即可保存并查看。",0,20,ACCOUNT_HEADING_HEIGHT+190,484,36,0);
   if(!session_password[0])SendMessageW(password_edit,EM_SETCUEBANNER,0,(LPARAM)L"请输入当前密码，或退出后重新登录");
  }
  hide_password(password_edit,show_password);hide_password(new_edit,show_new_password);
  login_button=control(window,L"BUTTON",L"登录",WS_TABSTOP|WS_DISABLED|BS_DEFPUSHBUTTON,20,ACCOUNT_HEADING_HEIGHT+231,113,34,LOGIN_ID);
  register_button=control(window,L"BUTTON",L"注册",WS_TABSTOP|WS_DISABLED,144,ACCOUNT_HEADING_HEIGHT+231,113,34,REGISTER_ID);
  sync_button=control(window,L"BUTTON",L"立即同步",WS_TABSTOP,20,ACCOUNT_HEADING_HEIGHT+231,113,34,SYNC_ID);
  password_button=control(window,L"BUTTON",L"修改密码",WS_TABSTOP,144,ACCOUNT_HEADING_HEIGHT+231,113,34,PASSWORD_ID);
  logout_button=control(window,L"BUTTON",L"退出登录",WS_TABSTOP,267,ACCOUNT_HEADING_HEIGHT+231,113,34,LOGOUT_ID);
  if(startup_login)control(window,L"BUTTON",L"取消",WS_TABSTOP,391,ACCOUNT_HEADING_HEIGHT+231,113,34,IDCANCEL);
  else{
   shop_button=control(window,L"BUTTON",L"打开商城",WS_TABSTOP,391,ACCOUNT_HEADING_HEIGHT+231,113,34,SHOP_ID);
   balance_label=control(window,L"STATIC",L"",0,20,ACCOUNT_HEADING_HEIGHT+278,484,24,0);
   displayed_balance=-2;show_reward_balance();
  }
  status_label=control(window,L"STATIC",status_text,0,20,ACCOUNT_HEADING_HEIGHT+(startup_login?285:308),484,58,0);
  return 0;
 }
 case WM_COMMAND:
  if(HIWORD(wp)==BN_CLICKED)switch(LOWORD(wp)){
   case AUTO_LOGIN_ID:refresh();return 0;
   case SHOW_PASSWORD_ID:toggle_password(password_edit,show_password);return 0;
   case SHOW_NEW_PASSWORD_ID:toggle_password(new_edit,show_new_password);return 0;
   case LOGIN_ID:if(!session.token[0])start(LOGIN,true);return 0;
   case REGISTER_ID:if(!session.token[0])start(REGISTER,true);return 0;
   case SYNC_ID:if(session.token[0])start(SYNC,true);return 0;
   case PASSWORD_ID:if(session.token[0])start(PASSWORD,true);return 0;
   case LOGOUT_ID:if(session.token[0])start(LOGOUT,true);return 0;
   case SHOP_ID:open_shop();return 0;
   case IDCANCEL:DestroyWindow(window);return 0;
  }
  break;
 case WM_CLOSE:DestroyWindow(window);return 0;
 case WM_ACTIVATE:
  if(LOWORD(wp)==WA_INACTIVE){hide_password(password_edit,show_password);hide_password(new_edit,show_new_password);}
  break;
 case WM_ACCOUNT_ACTIVATE:ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);return 0;
 case WM_DESTROY:panel=status_label=shop_button=balance_label=auto_login_box=migrate_box=NULL;return 0;
 }
 return DefWindowProcW(window,message,wp,lp);
}
void account_show(void){
 if(panel){pet_show_panel(panel,SW_RESTORE);return;}
 HDC dc=GetDC(pet_window);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(pet_window,dc);
 WNDCLASSW c={0};c.lpfnWndProc=window_proc;c.hInstance=pet_instance;
 c.lpszClassName=startup_login?ACCOUNT_START_CLASS:L"LetterPet.Account";
 c.hCursor=LoadCursorW(NULL,IDC_ARROW);c.hIcon=pet_app_icon();c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&c);
 DWORD style=WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN;
 RECT r={0,0,px(524),px((startup_login?346:370)+ACCOUNT_HEADING_HEIGHT)};AdjustWindowRectEx(&r,style,FALSE,WS_EX_CONTROLPARENT|WS_EX_APPWINDOW);
 HMONITOR monitor=pet_window?MonitorFromWindow(pet_window,MONITOR_DEFAULTTONEAREST):
                             MonitorFromPoint((POINT){0,0},MONITOR_DEFAULTTOPRIMARY);
 MONITORINFO info={sizeof(info)};GetMonitorInfoW(monitor,&info);
 int w=r.right-r.left,h=r.bottom-r.top;
 panel=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_APPWINDOW,c.lpszClassName,
    startup_login?L"LetterPet · 登录桌宠":L"LetterPet · 我的账号",style,
    info.rcWork.left+max(0,(info.rcWork.right-info.rcWork.left-w)/2),
    info.rcWork.top+max(0,(info.rcWork.bottom-info.rcWork.top-h)/2),w,h,NULL,NULL,pet_instance,NULL);
 if(panel){ui_theme_apply(panel);ui_theme_role(balance_label,UI_READOUT);ui_theme_role(status_label,UI_SURFACE_TEXT);refresh();pet_show_panel(panel,SW_SHOW);}
}
bool account_require_login(const wchar_t *return_notice){
 Session saved={0};
 wchar_t password[129]={0};
 bool remembered=!return_notice&&session_file[0]&&recall_session(&saved,password);
 if(remembered){
  lstrcpynW(last_url,saved.url,ARRAYSIZE(last_url));
  lstrcpynW(last_username,saved.username,ARRAYSIZE(last_username));
 }
 startup_login=true;
 post_status(return_notice&&*return_notice?return_notice:L"请勾选「自动登录」，再注册或登录账号。");
 account_show();
 if(panel&&remembered){
  SendMessageW(auto_login_box,BM_SETCHECK,BST_CHECKED,0);
  refresh();
  resume_login(&saved,password);
 }
 SecureZeroMemory(&saved,sizeof(saved));
 SecureZeroMemory(password,sizeof(password));
 while(panel&&!session.token[0]){
  if(MsgWaitForMultipleObjectsEx(worker?1:0,worker?&worker:NULL,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE)==WAIT_FAILED)break;
  MSG message;
  while(panel&&PeekMessageW(&message,NULL,0,0,PM_REMOVE)){
   if(message.message==WM_QUIT){DestroyWindow(panel);break;}
   if(!account_dialog_message(&message)){TranslateMessage(&message);DispatchMessageW(&message);}
  }
  if(panel)account_poll();
 }
 if(panel)DestroyWindow(panel);
 startup_login=false;
 return session.token[0]!=0;
}
void account_activate_startup(void){
 HWND window=FindWindowW(ACCOUNT_START_CLASS,NULL);
 if(window){
  DWORD process_id=0;GetWindowThreadProcessId(window,&process_id);
  if(process_id)AllowSetForegroundWindow(process_id);
  PostMessageW(window,WM_ACCOUNT_ACTIVATE,0,0);
 }
}
bool account_dialog_message(MSG *message){return panel&&IsDialogMessageW(panel,message)!=0;}
void account_close(void){
 if(panel)DestroyWindow(panel);
 // The network job owns its buffers until process exit. Never free them while it is running.
 if(worker){
  if(WaitForSingleObject(worker,0)==WAIT_OBJECT_0)free_job(pending);
  CloseHandle(worker);worker=NULL;pending=NULL;
 }
 free_profile(&profile);SecureZeroMemory(&session,sizeof(session));
 SecureZeroMemory(session_password,sizeof(session_password));
}
