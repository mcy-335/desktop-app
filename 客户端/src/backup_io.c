#include "backup_io.h"
#include <wincrypt.h>
#define DATA_LIMIT (512u*1024u)
static bool read_file(const wchar_t *path,BYTE **data,DWORD *length,DWORD limit){
 *data=NULL;*length=0;HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);if(file==INVALID_HANDLE_VALUE)return false;
 LARGE_INTEGER size;bool ok=GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=limit;
 DWORD got=0;if(ok){*length=(DWORD)size.QuadPart;*data=malloc(*length);ok=*data&&ReadFile(file,*data,*length,&got,NULL)&&got==*length;}CloseHandle(file);if(!ok){free(*data);*data=NULL;}return ok;
}
bool atomic_write(const wchar_t *path,const BYTE *data,DWORD length){
 if(wcslen(path)>32710)return false;wchar_t *temp=calloc(32768,sizeof(wchar_t));if(!temp)return false;
 swprintf(temp,32768,L"%ls.%lu.%llu.tmp",path,GetCurrentProcessId(),(unsigned long long)GetTickCount64());
 HANDLE file=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);DWORD wrote=0;bool ok=false;
 if(file!=INVALID_HANDLE_VALUE){ok=WriteFile(file,data,length,&wrote,NULL)&&wrote==length&&FlushFileBuffers(file);CloseHandle(file);}
 if(ok)ok=MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
 if(!ok)DeleteFileW(temp);free(temp);return ok;
}
static bool digest(const BYTE *data,DWORD length,BYTE output[32]){
 HCRYPTPROV provider=0;HCRYPTHASH hash=0;DWORD bytes=32;bool ok=CryptAcquireContextW(&provider,NULL,NULL,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)&&CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)&&CryptHashData(hash,data,length,0)&&CryptGetHashParam(hash,HP_HASHVAL,output,&bytes,0)&&bytes==32;
 if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);return ok;
}
static void put32(BYTE *p,DWORD value){for(int i=0;i<4;i++)p[i]=(BYTE)(value>>(8*i));}
static DWORD get32(const BYTE *p){return (DWORD)p[0]|((DWORD)p[1]<<8)|((DWORD)p[2]<<16)|((DWORD)p[3]<<24);}
bool data_backup_export(const wchar_t *source,const wchar_t *destination){
 if(!_wcsicmp(source,destination))return false;
 BYTE *data=NULL;DWORD length=0;if(!read_file(source,&data,&length,DATA_LIMIT))return false;
 BYTE *packed=calloc(1,length+48);bool ok=packed!=NULL;
 if(ok){memcpy(packed,"LPDATA01",8);put32(packed+8,length);put32(packed+12,1);ok=digest(data,length,packed+16);memcpy(packed+48,data,length);}
 if(ok)ok=atomic_write(destination,packed,length+48);free(packed);free(data);return ok;
}
bool data_backup_validate(const wchar_t *source,const wchar_t *staging){
 BYTE *packed=NULL;DWORD length=0;if(!read_file(source,&packed,&length,DATA_LIMIT+48))return false;
 BYTE hash[32];bool ok=length>50&&!memcmp(packed,"LPDATA01",8)&&get32(packed+12)==1&&get32(packed+8)==length-48;
 DWORD size=length>=48?length-48:0;
 if(ok)ok=size%2==0&&packed[48]==0xff&&packed[49]==0xfe&&digest(packed+48,size,hash)&&!memcmp(hash,packed+16,32);
 if(ok){const wchar_t *text=(const wchar_t*)(packed+48);for(DWORD i=1;i<size/2;i++)if(text[i]==0){ok=false;break;}}
 if(ok)ok=atomic_write(staging,packed+48,size);
 if(ok){unsigned version=GetPrivateProfileIntW(L"companion",L"version",0,staging),study=GetPrivateProfileIntW(L"study",L"version",0,staging),extras=GetPrivateProfileIntW(L"extras",L"version",0,staging);
  unsigned dialogue=GetPrivateProfileIntW(L"dialogue",L"version",0,staging);
  ok=version==1&&(study==0||study==1)&&(extras==0||extras==1)&&(dialogue==0||dialogue==1);
  if(ok&&dialogue)ok=GetPrivateProfileIntW(L"dialogue",L"count",0,staging)<=32;
  if(ok&&study)ok=GetPrivateProfileIntW(L"study",L"tasks",0,staging)<=64&&GetPrivateProfileIntW(L"study",L"days",0,staging)<=90;
 }
 if(!ok)DeleteFileW(staging);free(packed);return ok;
}
bool data_backup_restore(const wchar_t *source,const wchar_t *destination,const wchar_t *recovery){
 if(!_wcsicmp(source,destination)||!_wcsicmp(source,recovery)||!_wcsicmp(destination,recovery)||wcslen(destination)>32700)return false;
 wchar_t *staging=calloc(32768,sizeof(wchar_t));if(!staging)return false;swprintf(staging,32768,L"%ls.restore.%lu.%llu.tmp",destination,GetCurrentProcessId(),(unsigned long long)GetTickCount64());
 bool ok=data_backup_validate(source,staging);
 if(ok)ok=data_backup_export(destination,recovery);
 if(ok)ok=MoveFileExW(staging,destination,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
 if(!ok)DeleteFileW(staging);free(staging);return ok;
}
bool backup_selftest(FILE *log){
 wchar_t source[MAX_PATH],packed[MAX_PATH],restored[MAX_PATH],recovery[MAX_PATH],bad[MAX_PATH];
 swprintf(source,MAX_PATH,L"%ls\\backup-source-test.ini",app_dir);swprintf(packed,MAX_PATH,L"%ls\\backup-roundtrip-test.lpbackup",app_dir);swprintf(restored,MAX_PATH,L"%ls\\backup-target-test.ini",app_dir);swprintf(recovery,MAX_PATH,L"%ls\\backup-recovery-test.lpbackup",app_dir);swprintf(bad,MAX_PATH,L"%ls\\backup-bad-test.lpbackup",app_dir);
 const wchar_t contents[]=L"\xfeff[companion]\r\nversion=1\r\ncompleted=17\r\n[study]\r\nversion=1\r\ntasks=0\r\ndays=0\r\n[extras]\r\nversion=1\r\nunlocked=7\r\n[dialogue]\r\nversion=1\r\ncount=1\r\ninterval=15\r\n[dialogue_line0]\r\nid=1\r\ncategory=0\r\ntext_hex=4f60597d\r\n";
 const wchar_t original[]=L"\xfeff[companion]\r\nversion=1\r\ncompleted=3\r\n";
 bool ok=atomic_write(source,(const BYTE*)contents,sizeof(contents)-2)&&atomic_write(restored,(const BYTE*)original,sizeof(original)-2)&&data_backup_export(source,packed)&&data_backup_restore(packed,restored,recovery);
 ok=ok&&GetPrivateProfileIntW(L"companion",L"completed",0,restored)==17&&GetPrivateProfileIntW(L"dialogue",L"count",0,restored)==1&&GetPrivateProfileIntW(L"dialogue",L"interval",0,restored)==15;
 bool recovery_ok=data_backup_validate(recovery,source)&&GetPrivateProfileIntW(L"companion",L"completed",0,source)==3;
 wchar_t blocked[MAX_PATH];swprintf(blocked,MAX_PATH,L"%ls\\missing-%llu\\backup.lpbackup",app_dir,(unsigned long long)GetTickCount64());
 bool refuses=!data_backup_restore(packed,restored,blocked)&&GetPrivateProfileIntW(L"companion",L"completed",0,restored)==17;
 BYTE *bytes=NULL;DWORD size=0;bool rejects=false;
 if(read_file(packed,&bytes,&size,DATA_LIMIT+48)){bytes[size-1]^=1;atomic_write(bad,bytes,size);rejects=!data_backup_restore(bad,restored,recovery)&&GetPrivateProfileIntW(L"companion",L"completed",0,restored)==17;free(bytes);}
 atomic_write(source,(const BYTE*)L"garbage",14);data_backup_export(source,bad);rejects=rejects&&!data_backup_restore(bad,restored,recovery);
 const wchar_t unsupported[]=L"\xfeff[companion]\r\nversion=99\r\n";atomic_write(source,(const BYTE*)unsupported,sizeof(unsupported)-2);data_backup_export(source,bad);rejects=rejects&&!data_backup_restore(bad,restored,recovery);
 const wchar_t future_dialogue[]=L"\xfeff[companion]\r\nversion=1\r\n[dialogue]\r\nversion=99\r\n";atomic_write(source,(const BYTE*)future_dialogue,sizeof(future_dialogue)-2);data_backup_export(source,bad);rejects=rejects&&!data_backup_restore(bad,restored,recovery);
 fprintf(log,"%s: backup SHA256 roundtrip and automatic pre-restore recovery\n",ok&&recovery_ok?"PASS":"FAIL");
 fprintf(log,"%s: corrupt backup and unsupported schema leave existing data unchanged\n",rejects?"PASS":"FAIL");fprintf(log,"%s: failed recovery backup blocks replacement\n",refuses?"PASS":"FAIL");return ok&&recovery_ok&&rejects&&refuses;
}
