#include "dialogue_core.h"
#include "companion_core.h"
#include <stdio.h>
bool dialogue_selftest(FILE *log){
 int failures=0;
 #define CHECK(condition,label) do{bool pass=(condition);fprintf(log,"%s: %s\n",pass?"PASS":"FAIL",label);if(!pass)failures++;}while(0)
 TalkCatalog c;talk_defaults(&c);CHECK(c.enabled&&c.builtins&&c.interval==10,"dialogue defaults are enabled with ten-minute cadence");
 CHECK(!talk_add(&c,TALK_GENERAL,L" \r\n\t "),"blank dialogue is rejected");
 CHECK(talk_add(&c,TALK_FOCUS,L"  你好\n世界  ")&&!wcscmp(c.lines[0].text,L"你好 世界"),"custom dialogue trims and normalizes whitespace");
 uint32_t first=c.lines[0].id;CHECK(talk_edit(&c,first,TALK_PET,L"摸摸 😀")&&c.lines[0].category==TALK_PET,"custom dialogue can change category and Unicode text");
 CHECK(!talk_edit(&c,first,99,L"错误")&&!wcscmp(c.lines[0].text,L"摸摸 😀"),"invalid category does not damage an existing line");
 CHECK(talk_delete(&c,first)&&c.count==0&&!talk_delete(&c,first),"deleting a custom line is bounded and repeat-safe");
 wchar_t long_text[TALK_TEXT+1];for(int i=0;i<TALK_TEXT;i++)long_text[i]=L'字';long_text[TALK_TEXT]=0;CHECK(!talk_add(&c,TALK_GENERAL,long_text),"overlong dialogue is rejected rather than silently truncated");
 CHECK(talk_context(7,FOCUS_IDLE,70,80)==TALK_MORNING&&talk_context(12,FOCUS_IDLE,70,80)==TALK_DAY&&talk_context(18,FOCUS_IDLE,70,80)==TALK_EVENING&&talk_context(2,FOCUS_IDLE,70,80)==TALK_NIGHT,"local time chooses morning/day/evening/night");
 CHECK(talk_context(7,FOCUS_WORK,0,0)==TALK_FOCUS,"focus context takes precedence over low-care prompts");
 CHECK(talk_context(12,FOCUS_PAUSED_WORK,80,80)==TALK_PAUSED&&talk_context(12,FOCUS_REST,80,80)==TALK_REST&&talk_context(12,FOCUS_BREAK_READY,80,80)==TALK_REST,"pause and rest phases select matching dialogue");
 CHECK(talk_context(12,FOCUS_IDLE,10,80)==TALK_LOW_MOOD&&talk_context(12,FOCUS_IDLE,80,10)==TALK_LOW_ENERGY,"pet mood and energy select care dialogue");
 TalkRuntime r={0};wchar_t text[221],previous[221];int id=talk_choose(&c,&r,TALK_MORNING,text,221);wcscpy(previous,text);int next=talk_choose(&c,&r,TALK_MORNING,text,221);
 CHECK(id&&next&&id!=next&&wcscmp(previous,text),"available dialogue avoids immediate repeated content");
 talk_defaults(&c);c.builtins=false;talk_add(&c,TALK_GENERAL,L"通用自定义");r=(TalkRuntime){0};
 CHECK(talk_choose(&c,&r,TALK_FOCUS,text,221)>0&&!wcscmp(text,L"通用自定义"),"general custom dialogue participates in ordinary contexts");
 CHECK(!talk_choose(&c,&r,TALK_FEED,text,221),"generic chatter does not replace a missing feeding response");
 talk_add(&c,TALK_FEED,L"谢谢款待");CHECK(talk_choose(&c,&r,TALK_FEED,text,221)>0&&!wcscmp(text,L"谢谢款待"),"interaction responses use their matching custom category");
 talk_expand(L"一起完成{任务}，再看{未知}。",L"软件报告",text,221);CHECK(!wcscmp(text,L"一起完成软件报告，再看{未知}。"),"task placeholder expands while unknown tokens remain literal");
 talk_expand(L"{任务}",L"",text,221);CHECK(!wcscmp(text,L"眼前的小目标"),"empty task names have a readable fallback");
 wchar_t tiny[4];talk_expand(L"abcdef",L"x",tiny,4);CHECK(!wcscmp(tiny,L"abc"),"expanded dialogue respects output capacity");
 r=(TalkRuntime){0};CHECK(!talk_ready(&r,1000,10,true,true)&&r.next==61000,"startup has a one-minute grace period");
 CHECK(!talk_ready(&r,61000,10,true,false)&&r.next==121000,"quiet/hidden/game or important notices defer rather than queue chatter");
 CHECK(talk_ready(&r,121000,10,true,true),"eligible dialogue becomes ready after deferral");talk_schedule(&r,121000,10,true);
 CHECK(r.next==721000&&!talk_ready(&r,130000,10,true,true),"successful speech schedules the configured interval");
 CHECK(!talk_ready(&r,721000,10,false,true)&&r.next==781000,"disabled dialogue never emits and does not accumulate backlog");
 talk_defaults(&c);for(int i=0;i<TALK_MAX_LINES;i++)talk_add(&c,TALK_GENERAL,L"自定义");CHECK(c.count==TALK_MAX_LINES&&!talk_add(&c,TALK_GENERAL,L"超额"),"custom catalogue is limited to thirty-two lines");
 fprintf(log,"DIALOGUE OVERALL: %s (%d failures)\n",failures?"FAIL":"PASS",failures);
 #undef CHECK
 return failures==0;
}
