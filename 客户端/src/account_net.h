#ifndef LETTERPET_ACCOUNT_NET_H
#define LETTERPET_ACCOUNT_NET_H
#include "pet.h"
#include <winhttp.h>

typedef struct { wchar_t host[256]; INTERNET_PORT port; bool secure,loopback; } AccountServer;
bool account_server_parse(const wchar_t *url,AccountServer *server);
bool account_form(char *buffer,size_t capacity,const char *first,const wchar_t *a,const char *second,const wchar_t *b);
DWORD account_request(const AccountServer *server,const wchar_t *method,const wchar_t *route,const char *token,
                      const wchar_t *type,const void *body,DWORD length,DWORD expected,
                      void *output,DWORD capacity,DWORD *used,DWORD *revision);

#endif
