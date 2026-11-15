#include "dialogue_core.h"
#include "companion_core.h"
#include <string.h>
#include <wctype.h>
static const wchar_t *builtin[TALK_CATEGORIES][3]={
 {L"一点点进展，也值得开心。",L"我在这里，陪你把今天过好。",L"先做眼前的一小步吧。"},
 {L"早安！今天想先完成哪件小事？",L"新的一天，我们慢慢开始吧。",L"早上的第一步，也可以很轻松。"},
 {L"一步一步来，我会一直陪着你。",L"忙碌的时候，也记得喝口水。",L"今天的努力，正在一点点积累。"},
 {L"傍晚啦，给今天留一点轻松的时间吧。",L"天色慢慢变了，我还在这里。",L"回头看看，今天已经走了不少路呢。"},
 {L"夜深了，记得照顾好自己。",L"安静的夜里，也有我的陪伴。",L"不必急着做完所有事，给休息留点时间吧。"},
 {L"我陪你一起专注「{任务}」。",L"先把注意力放在「{任务}」上，一小步就好。",L"你认真做事的样子，我都看见啦。"},
 {L"暂停一下没关系，准备好再继续。",L"我会帮你守着进度，不用着急。",L"调整一下节奏，我们随时可以接着来。"},
 {L"给眼睛和肩膀一点放松的时间吧。",L"休息也是计划的一部分。",L"站起来走两步，再回来找我吧。"},
 {L"我的心情有点低，想和你待一会儿。",L"可以摸摸我吗？我想要一点陪伴。",L"有你在旁边，我会慢慢开心起来。"},
 {L"我的精力有点低，想补充一点能量。",L"肚子有点空啦，可以给我喂食吗？",L"补充好精力，我就能继续陪你啦。"},
 {L"嘿嘿，又被你摸到啦。",L"收到你的摸摸，今天也很开心！",L"谢谢你的陪伴，我记住这份温柔啦。"},
 {L"吃饱啦！继续陪你努力。",L"能量补充完毕，谢谢款待！",L"这一口真满足，我又有精神啦。"}
};
void talk_defaults(TalkCatalog *c){memset(c,0,sizeof(*c));c->enabled=c->builtins=true;c->interval=10;c->next_id=1;}
TalkLine *talk_line(TalkCatalog *c,uint32_t id){if(!id)return NULL;for(int i=0;i<c->count;i++)if(c->lines[i].id==id)return &c->lines[i];return NULL;}
static bool clean(const wchar_t *source,wchar_t *out){
 if(!source||wcslen(source)>=TALK_TEXT)return false;int n=0;bool space=true;
 for(;*source;source++){wchar_t ch=*source;if(iswspace(ch)){if(!space){out[n++]=L' ';space=true;}}else if(ch>=32){out[n++]=ch;space=false;}}
 if(n&&out[n-1]==L' ')n--;if(n&&out[n-1]>=0xd800&&out[n-1]<=0xdbff)n--;out[n]=0;return n>0;
}
bool talk_add(TalkCatalog *c,int category,const wchar_t *text){
 if(c->count>=TALK_MAX_LINES||category<0||category>=TALK_CATEGORIES)return false;wchar_t value[TALK_TEXT];if(!clean(text,value))return false;
 while(!c->next_id||c->next_id>0x7fffffffu||talk_line(c,c->next_id)){if(c->next_id>=0x7fffffffu)c->next_id=1;else c->next_id++;}
 TalkLine *line=&c->lines[c->count++];line->id=c->next_id++;line->category=category;wcscpy(line->text,value);return true;
}
bool talk_edit(TalkCatalog *c,uint32_t id,int category,const wchar_t *text){TalkLine *line=talk_line(c,id);wchar_t value[TALK_TEXT];if(!line||category<0||category>=TALK_CATEGORIES||!clean(text,value))return false;line->category=category;wcscpy(line->text,value);return true;}
bool talk_delete(TalkCatalog *c,uint32_t id){for(int i=0;i<c->count;i++)if(c->lines[i].id==id){memmove(&c->lines[i],&c->lines[i+1],(c->count-i-1)*sizeof(TalkLine));c->count--;return true;}return false;}
int talk_context(int hour,int phase,int mood,int energy){
 if(phase==FOCUS_WORK)return TALK_FOCUS;if(phase==FOCUS_PAUSED_WORK||phase==FOCUS_PAUSED_REST)return TALK_PAUSED;if(phase==FOCUS_BREAK_READY||phase==FOCUS_REST)return TALK_REST;
 if(energy<30)return TALK_LOW_ENERGY;if(mood<30)return TALK_LOW_MOOD;
 if(hour>=6&&hour<11)return TALK_MORNING;if(hour>=11&&hour<17)return TALK_DAY;if(hour>=17&&hour<23)return TALK_EVENING;return TALK_NIGHT;
}
static uint32_t random_value(TalkRuntime *r){uint32_t value=r->random?r->random:0x9e3779b9u;value^=value<<13;value^=value>>17;value^=value<<5;r->random=value;return value;}
int talk_choose(const TalkCatalog *c,TalkRuntime *r,int category,wchar_t *output,int capacity){
 if(!output||capacity<1)return 0;output[0]=0;if(category<0||category>=TALK_CATEGORIES)return 0;
 const wchar_t *texts[TALK_MAX_LINES+6];int ids[TALK_MAX_LINES+6],count=0;
 bool interactive=category==TALK_PET||category==TALK_FEED;
 if(c->builtins){for(int j=0;j<3;j++){texts[count]=builtin[category][j];ids[count++]=-(category*3+j+1);}if(!interactive&&category!=TALK_GENERAL){for(int j=0;j<3;j++){texts[count]=builtin[TALK_GENERAL][j];ids[count++]=-(j+1);}}}
 for(int i=0;i<c->count;i++)if(c->lines[i].category==category||(!interactive&&c->lines[i].category==TALK_GENERAL)){texts[count]=c->lines[i].text;ids[count++]=(int)c->lines[i].id;}
 if(!count)return 0;
 int eligible[TALK_MAX_LINES+6],n=0;for(int i=0;i<count;i++)if(count==1||(ids[i]!=r->last_id&&wcscmp(texts[i],r->last_text)))eligible[n++]=i;
 if(!n)for(int i=0;i<count;i++)eligible[n++]=i;
 int choice=eligible[random_value(r)%(unsigned)n];int length=(int)wcslen(texts[choice]);if(length>=capacity)length=capacity-1;
 wmemcpy(output,texts[choice],length);output[length]=0;r->last_id=ids[choice];wcscpy(r->last_text,texts[choice]);return ids[choice];
}
void talk_expand(const wchar_t *text,const wchar_t *task,wchar_t *out,int capacity){
 if(!out||capacity<1)return;int n=0;const wchar_t token[]=L"{任务}";const int token_length=4;
 while(text&&*text&&n<capacity-1){if(!wcsncmp(text,token,token_length)){const wchar_t *value=task&&*task?task:L"眼前的小目标";while(*value&&n<capacity-1)out[n++]=*value++;text+=token_length;}else out[n++]=*text++;}
 if(n&&out[n-1]>=0xd800&&out[n-1]<=0xdbff)n--;out[n]=0;
}
void talk_schedule(TalkRuntime *r,double now,int interval,bool displayed){if(interval<1||interval>120)interval=10;r->next=now+(displayed?interval*60000.0:60000.0);}
bool talk_ready(TalkRuntime *r,double now,int interval,bool enabled,bool allowed){
 if(!r->next){r->next=now+60000;return false;}if(now<r->next)return false;if(!enabled||!allowed){talk_schedule(r,now,interval,false);return false;}return true;
}
