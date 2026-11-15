#include "pet.h"
double clampd(double v,double lo,double hi){return v<lo?lo:v>hi?hi:v;}
double smooth(double v){v=clampd(v,0,1);return v*v*(3-2*v);}
double now_ms(void){
 LARGE_INTEGER counter,frequency;
 if(!QueryPerformanceFrequency(&frequency)||!QueryPerformanceCounter(&counter))return (double)GetTickCount64();
 return (double)counter.QuadPart*1000.0/frequency.QuadPart;
}
static Vec mixv(Vec a,Vec b,double t){return (Vec){a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};}
typedef struct {const char *name;UINT vk;double span;} Spec;
#define K(n,v) {n,v,1}
#define W(n,v,w) {n,v,w}
#define G(w) {NULL,0,w}
static const Spec rows[6][23]={
 {K("ESC",VK_ESCAPE),G(1),K("F1",VK_F1),K("F2",VK_F2),K("F3",VK_F3),K("F4",VK_F4),G(.5),K("F5",VK_F5),K("F6",VK_F6),K("F7",VK_F7),K("F8",VK_F8),G(.5),K("F9",VK_F9),K("F10",VK_F10),K("F11",VK_F11),K("F12",VK_F12),G(.3),K("PRINT_SCREEN",VK_SNAPSHOT),K("SCROLL_LOCK",VK_SCROLL),K("PAUSE",VK_PAUSE)},
 {K("BACKQUOTE",VK_OEM_3),K("1",'1'),K("2",'2'),K("3",'3'),K("4",'4'),K("5",'5'),K("6",'6'),K("7",'7'),K("8",'8'),K("9",'9'),K("0",'0'),K("MINUS",VK_OEM_MINUS),K("EQUAL",VK_OEM_PLUS),W("BACKSPACE",VK_BACK,2),G(.3),K("INSERT",VK_INSERT),K("HOME",VK_HOME),K("PAGE_UP",VK_PRIOR)},
 {W("TAB",VK_TAB,1.5),K("Q",'Q'),K("W",'W'),K("E",'E'),K("R",'R'),K("T",'T'),K("Y",'Y'),K("U",'U'),K("I",'I'),K("O",'O'),K("P",'P'),K("BRACKET_LEFT",VK_OEM_4),K("BRACKET_RIGHT",VK_OEM_6),W("BACKSLASH",VK_OEM_5,1.5),G(.3),K("DELETE",VK_DELETE),K("END",VK_END),K("PAGE_DOWN",VK_NEXT)},
 {W("CAPS_LOCK",VK_CAPITAL,1.75),K("A",'A'),K("S",'S'),K("D",'D'),K("F",'F'),K("G",'G'),K("H",'H'),K("J",'J'),K("K",'K'),K("L",'L'),K("SEMICOLON",VK_OEM_1),K("QUOTE",VK_OEM_7),W("ENTER",VK_RETURN,2.25)},
 {W("SHIFT_LEFT",VK_LSHIFT,2.25),K("Z",'Z'),K("X",'X'),K("C",'C'),K("V",'V'),K("B",'B'),K("N",'N'),K("M",'M'),K("COMMA",VK_OEM_COMMA),K("PERIOD",VK_OEM_PERIOD),K("SLASH",VK_OEM_2),W("SHIFT_RIGHT",VK_RSHIFT,2.75),G(1.3),K("ARROW_UP",VK_UP)},
 {W("CTRL_LEFT",VK_LCONTROL,1.25),W("META_LEFT",VK_LWIN,1.25),W("ALT_LEFT",VK_LMENU,1.25),W("SPACE",VK_SPACE,6.25),W("ALT_RIGHT",VK_RMENU,1.25),W("META_RIGHT",VK_RWIN,1.25),G(1.25),W("CTRL_RIGHT",VK_RCONTROL,1.25),G(.3),K("ARROW_LEFT",VK_LEFT),K("ARROW_DOWN",VK_DOWN),K("ARROW_RIGHT",VK_RIGHT)}
};
void model_init(Model *m){
 memset(m,0,sizeof(*m));m->active=-1;m->tip=(Vec){490,890};
 for(int r=0;r<6;r++){double col=0;for(int j=0;j<23&&rows[r][j].span;j++){Spec s=rows[r][j];if(s.name){Key *k=&m->keys[m->count++];k->name=s.name;k->vk=s.vk;k->x=col*25;k->y=(r?r+.35:0)*22;k->w=s.span*25-3;k->h=17;}col+=s.span;}}
}
Vec key_center(const Key *k,double a){double x=k->x+k->w/2;return (Vec){700-x,985-.03*x-k->y-k->h/2-3+2.3*a};}
enum {INPUT_PULSE_MS=65};
bool model_key(Model *m,UINT vk,bool down,double t){
 for(int i=0;i<m->count;i++){
  Key *key=&m->keys[i];
  if(key->vk!=vk)continue;
  if(key->held==down)return false;
  key->held=down&&vk!=VK_PAUSE;
  if(down){key->pressed=true;key->order=++m->order;key->until=t+INPUT_PULSE_MS;}
  return true;
 }
 return false;
}
bool model_mouse(Model *m,int b,bool down,double t){
 if(b<0||b>2)return false;
 MouseState *button=&m->mouse[b];
 if(button->held==down)return false;
 button->held=down;
 if(down){button->pressed=true;button->until=t+INPUT_PULSE_MS;}
 return true;
}
static double input_depth(double depth,bool active,bool pressed,double dt){
 // Every new press has a visible first frame, including a re-press during release.
 if(pressed)return .72;
 double target=active?1:0;
 depth+=(target-depth)*(1-exp(-dt/(active?12:26)));
 return fabs(target-depth)<.002?target:depth;
}
bool model_tick(Model *m,double t){
 double elapsed=m->last?fmax(0,t-m->last):16;
 double dt=fmin(elapsed,64);
 m->last=t;
 int next=-1;
 bool change=false;
 for(int i=0;i<m->count;i++){
  Key *key=&m->keys[i];
  if(key->pressed)key->until=fmax(key->until,t+INPUT_PULSE_MS);
  bool active=key->held||t<key->until;
  double depth=input_depth(key->amount,active,key->pressed,active?elapsed:fmin(elapsed,t-key->until));
  key->pressed=false;
  change|=depth!=key->amount;
  key->amount=depth;
  if(active&&(next<0||key->order>m->keys[next].order))next=i;
 }
 m->active=next;
 Vec to=next<0?(Vec){490,890}:key_center(&m->keys[next],m->keys[next].amount);
 Vec tip=hypot(to.x-m->tip.x,to.y-m->tip.y)<.05?to:mixv(m->tip,to,1-exp(-dt/(next<0?55:32)));
 if(tip.x!=m->tip.x||tip.y!=m->tip.y)change=true;
 m->tip=tip;
 for(int b=0;b<3;b++){
  MouseState *button=&m->mouse[b];
  if(button->pressed)button->until=fmax(button->until,t+INPUT_PULSE_MS);
  bool active=button->held||t<button->until;
  double depth=input_depth(button->amount,active,button->pressed,active?elapsed:fmin(elapsed,t-button->until));
  button->pressed=false;
  change|=depth!=button->amount;
  button->amount=depth;
 }
 return change;
}
Pose keyboard_pose(Vec tip){
 double u=clampd((tip.x-250)/440,0,1),v=clampd((tip.y-835)/145,0,1);Vec g={410+150*u,750+25*v};
 double d=hypot(tip.x-g.x,tip.y-g.y);Vec dir={(tip.x-g.x)/d,(tip.y-g.y)/d},w={tip.x-dir.x*65,tip.y-dir.y*65};
 double f=hypot(w.x-g.x,w.y-g.y),q=clampd(f,75,180*.97)/f;
 return (Pose){{535,623},{w.x+(g.x-w.x)*q,w.y+(g.y-w.y)*q},w,tip};
}
Pose mouse_pose(const Model *m){double l=m->mouse[0].amount,r=m->mouse[1].amount,c=m->mouse[2].amount,sum=l+r+c,a=fmin(1,sum),side=sum?(r-l)/sum:0,mid=sum?c/sum:0;return (Pose){{740,620},{820+side*a*.8,790+a*1.2},{840+side*a*1.3,885+a*2.2},{820+side*a*2,967+a*(3+mid)}};}
static Vec segment(Vec q,Vec a,Vec b,Vec A,Vec B,double width){double dx=b.x-a.x,dy=b.y-a.y,L=hypot(dx,dy),u=((q.x-a.x)*dx+(q.y-a.y)*dy)/(L*L),v=(-(q.x-a.x)*dy+(q.y-a.y)*dx)/L,ex=B.x-A.x,ey=B.y-A.y,len=fmax(1e-8,hypot(ex,ey));return (Vec){A.x+u*ex-v*ey/len*width,A.y+u*ey+v*ex/len*width};}
Vec skin_point(Vec q,const Pose *p,bool mouse){
 Vec s=mouse?(Vec){510,245}:(Vec){635,560},e=mouse?(Vec){625,550}:(Vec){520,750},w=mouse?(Vec){710,850}:(Vec){375,925},t=mouse?(Vec){775,1138}:(Vec){220,1070};double width=mouse?.44:.54;
 Vec U=segment(q,s,e,p->shoulder,p->elbow,width),F=segment(q,e,w,p->elbow,p->wrist,width),H=segment(q,w,t,p->wrist,p->tip,width);
 double a=mouse?smooth(((q.x-625)*.34+(q.y-550)*.94+64)/128):smooth(((q.x-520)*-.58+(q.y-750)*.815+60)/120),b=mouse?smooth(((q.x-710)*.2+(q.y-850)*.98+90)/180):smooth(((q.x-375)*-.7+(q.y-925)*.714+85)/60);
 return (Vec){U.x*(1-a)+F.x*a*(1-b)+H.x*a*b,U.y*(1-a)+F.y*a*(1-b)+H.y*a*b};
}
bool model_selftest(FILE *log){Model m;model_init(&m);if(m.count!=86)return false;for(int i=0;i<86;i++){for(int j=i+1;j<86;j++)if(m.keys[i].vk==m.keys[j].vk)return false;for(int k=0;k<2;k++){Vec t=key_center(&m.keys[i],k);Pose p=keyboard_pose(t);Vec actual=skin_point((Vec){220,1070},&p,false);if(hypot(actual.x-t.x,actual.y-t.y)>1e-6||fabs(hypot(p.tip.x-p.wrist.x,p.tip.y-p.wrist.y)-65)>1e-6)return false;}}
 model_key(&m,'A',true,1000);for(double t=1000;t<1700;t+=16)model_tick(&m,t);if(m.active<0||m.keys[m.active].vk!='A'||m.keys[m.active].amount!=1)return false;
 model_key(&m,'A',false,1700);for(double t=1700;t<2600;t+=16)model_tick(&m,t);if(m.active!=-1)return false;
 model_mouse(&m,0,true,3000);model_mouse(&m,0,false,3010);model_tick(&m,3050);if(m.mouse[0].amount<=0)return false;model_tick(&m,3400);if(m.mouse[0].amount!=0)return false;
 fprintf(log,"PASS: 86 keys, key release, short mouse clicks and rigid hand alignment.\n");
 model_init(&m);
 model_key(&m,'A',true,4000);model_key(&m,'A',false,4002);
 model_key(&m,'S',true,4004);model_key(&m,'S',false,4006);
 model_mouse(&m,0,true,4008);model_mouse(&m,0,false,4010);
 model_tick(&m,4085);
 int a=-1,b=-1;
 for(int i=0;i<m.count;i++){
  if(m.keys[i].vk=='A')a=i;
  if(m.keys[i].vk=='S')b=i;
 }
 bool short_presses=a>=0&&b>=0&&m.active==b&&m.keys[a].amount>.6&&m.keys[b].amount>.6&&m.mouse[0].amount>.6;
 model_tick(&m,4101);
 double previous_depth=short_presses?m.keys[b].amount:0;
 model_key(&m,'S',true,4102);model_key(&m,'S',false,4104);
 model_tick(&m,4117);
 bool repeated=short_presses&&m.keys[b].amount>.6&&m.keys[b].amount<previous_depth;
 for(double t=4133;t<4500;t+=16)model_tick(&m,t);
 bool released=m.active==-1&&m.mouse[0].amount==0&&short_presses&&m.keys[a].amount==0&&m.keys[b].amount==0;
 fprintf(log,"%s: batched short key and mouse presses are visible, same-key presses retrigger, and all feedback releases.\n",
         short_presses&&repeated&&released?"PASS":"FAIL");
 return short_presses&&repeated&&released;
}
