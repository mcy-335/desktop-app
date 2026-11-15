#include "account_net.h"

bool account_server_parse(const wchar_t *url,AccountServer *server){
 URL_COMPONENTS parts={sizeof(parts)};
 parts.dwSchemeLength=parts.dwHostNameLength=parts.dwUserNameLength=parts.dwPasswordLength=1;
 parts.dwUrlPathLength=parts.dwExtraInfoLength=1;
 if(!WinHttpCrackUrl(url,0,0,&parts)||!parts.dwHostNameLength||parts.dwHostNameLength>=256||
    parts.dwUserNameLength||parts.dwPasswordLength||parts.dwExtraInfoLength||
    (parts.dwUrlPathLength&&(parts.dwUrlPathLength!=1||*parts.lpszUrlPath!=L'/')))return false;
 *server=(AccountServer){0};
 wmemcpy(server->host,parts.lpszHostName,parts.dwHostNameLength);
 server->port=parts.nPort;server->secure=parts.nScheme==INTERNET_SCHEME_HTTPS;
 server->loopback=!wcscmp(server->host,L"127.0.0.1")||!_wcsicmp(server->host,L"localhost");
 return server->secure||(server->loopback&&parts.nScheme==INTERNET_SCHEME_HTTP);
}

static bool form_value(char *buffer,size_t capacity,size_t *used,const wchar_t *value){
 char utf8[520];
 int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,utf8,sizeof(utf8),NULL,NULL);
 bool ok=count>0;
 const char hex[]="0123456789ABCDEF";
 for(int i=0;i<count-1&&ok;i++){
  unsigned c=(unsigned char)utf8[i];
  bool plain=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.';
  size_t need=plain?1:3;
  if(*used+need>=capacity){ok=false;break;}
  if(plain)buffer[(*used)++]=(char)c;
  else{buffer[(*used)++]='%';buffer[(*used)++]=hex[c>>4];buffer[(*used)++]=hex[c&15];}
 }
 SecureZeroMemory(utf8,sizeof(utf8));
 return ok;
}
bool account_form(char *buffer,size_t capacity,const char *first,const wchar_t *a,const char *second,const wchar_t *b){
 int n=snprintf(buffer,capacity,"%s=",first);
 if(n<0||(size_t)n>=capacity)return false;
 size_t used=(size_t)n;
 if(!form_value(buffer,capacity,&used,a))return false;
 n=snprintf(buffer+used,capacity-used,"&%s=",second);
 if(n<0||(size_t)n>=capacity-used)return false;
 used+=(size_t)n;
 if(!form_value(buffer,capacity,&used,b))return false;
 buffer[used]=0;return true;
}

DWORD account_request(const AccountServer *server,const wchar_t *method,const wchar_t *route,const char *token,
                      const wchar_t *type,const void *body,DWORD length,DWORD expected,
                      void *output,DWORD capacity,DWORD *used,DWORD *revision){
 HINTERNET session=NULL,connection=NULL,request=NULL;
 DWORD status=0,option=WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES;
 *used=0;
 session=WinHttpOpen(L"LetterPet/1.11",server->loopback?WINHTTP_ACCESS_TYPE_NO_PROXY:WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                     WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
 if(!session||!WinHttpSetTimeouts(session,3000,3500,3500,5000))goto done;
 connection=WinHttpConnect(session,server->host,server->port,0);
 if(!connection)goto done;
 request=WinHttpOpenRequest(connection,method,route,NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,
                            server->secure?WINHTTP_FLAG_SECURE:0);
 if(!request||!WinHttpSetOption(request,WINHTTP_OPTION_DISABLE_FEATURE,&option,sizeof(option)))goto done;
 wchar_t headers[512]=L"";
 if(token&&*token)swprintf(headers,512,L"Authorization: Bearer %hs\r\n",token);
 size_t offset=wcslen(headers);
 if(type)swprintf(headers+offset,512-offset,L"Content-Type: %ls\r\n",type);
 offset=wcslen(headers);
 if(!wcscmp(method,L"PUT"))swprintf(headers+offset,512-offset,L"X-LetterPet-Revision: %lu\r\n",expected);
 if(!WinHttpSendRequest(request,headers,(DWORD)-1,(void*)body,length,length,0)||!WinHttpReceiveResponse(request,NULL)){
  SecureZeroMemory(headers,sizeof(headers));goto done;
 }
 SecureZeroMemory(headers,sizeof(headers));
 DWORD bytes=sizeof(status);
 if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,NULL,&status,&bytes,NULL)){
  status=0;goto done;
 }
 if(status>=200&&status<300&&revision){
  wchar_t value[24],*end;
  bytes=sizeof(value);
  if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_CUSTOM,L"X-LetterPet-Revision",value,&bytes,NULL)){
   status=0;goto done;
  }
  unsigned long number=wcstoul(value,&end,10);
  if(end==value||*end){status=0;goto done;}
  *revision=(DWORD)number;
 }
 if((status==200||status==201)&&output){
  for(;;){
   BYTE extra;
   bool room=*used<capacity;
   DWORD read=0,requested=room?min(capacity-*used,32768u):1;
   void *destination=room?(BYTE*)output+*used:&extra;
   if(!WinHttpReadData(request,destination,requested,&read)||(!room&&read)){status=0;break;}
   if(!read)break;
   *used+=read;
  }
 }
done:
 if(request)WinHttpCloseHandle(request);
 if(connection)WinHttpCloseHandle(connection);
 if(session)WinHttpCloseHandle(session);
 return status;
}
