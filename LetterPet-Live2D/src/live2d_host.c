#include "pet.h"
#include "live2d_bridge.h"

static HMODULE module;
static void *runtime;
static PetL2DCreate create_model;
static PetL2DRender render_model;
static PetL2DDestroy destroy_model;
static PetL2DRestart restart_model;
static PetL2DEye eye_value;
static PetL2DKey input_key;
static PetL2DMouse input_mouse;
static PetL2DClear clear_input;
static PetL2DInspect inspect_model;
static double previous,click_until;
static wchar_t last_error[768];

static void report(const char *error){
 MultiByteToWideChar(CP_UTF8,0,error,-1,last_error,768);last_error[767]=0;
 wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\live2d-host.log",app_dir);
 FILE *f=_wfopen(path,L"a");if(f){fprintf(f,"%s\n",error);fclose(f);}
}
void live2d_free(void){
 if(runtime&&destroy_model)destroy_model(runtime);runtime=NULL;
 if(module)FreeLibrary(module);module=NULL;previous=click_until=0;
}
void live2d_init(int size){
 wchar_t folder[MAX_PATH],model_path[MAX_PATH],dll[MAX_PATH];
 char error[768]={0};live2d_free();last_error[0]=0;
 swprintf(folder,MAX_PATH,L"%ls\\live2d-runtime",app_dir);
 swprintf(dll,MAX_PATH,L"%ls\\PetLive2D.dll",folder);
 swprintf(model_path,MAX_PATH,L"%ls\\assets\\live2d\\runtime\\pet",app_dir);
 module=LoadLibraryExW(dll,NULL,LOAD_WITH_ALTERED_SEARCH_PATH);
 if(!module){snprintf(error,sizeof(error),"Cannot load PetLive2D.dll (Windows error %lu). Keep live2d-runtime beside the EXE and install the VC++ x64 runtime if required.",GetLastError());report(error);return;}
 PetL2DVersion version=(PetL2DVersion)(void*)GetProcAddress(module,"pet_l2d_version");
 create_model=(PetL2DCreate)(void*)GetProcAddress(module,"pet_l2d_create");
 render_model=(PetL2DRender)(void*)GetProcAddress(module,"pet_l2d_render");
 destroy_model=(PetL2DDestroy)(void*)GetProcAddress(module,"pet_l2d_destroy");
 restart_model=(PetL2DRestart)(void*)GetProcAddress(module,"pet_l2d_restart");
 eye_value=(PetL2DEye)(void*)GetProcAddress(module,"pet_l2d_eye");
 input_key=(PetL2DKey)(void*)GetProcAddress(module,"pet_l2d_key");
 input_mouse=(PetL2DMouse)(void*)GetProcAddress(module,"pet_l2d_mouse");
 clear_input=(PetL2DClear)(void*)GetProcAddress(module,"pet_l2d_clear");
 inspect_model=(PetL2DInspect)(void*)GetProcAddress(module,"pet_l2d_inspect");
 if(!version||version()!=PET_LIVE2D_ABI||!create_model||!render_model||!destroy_model||!restart_model||!eye_value||!input_key||!input_mouse||!clear_input||!inspect_model){report("Live2D bridge ABI mismatch");live2d_free();return;}
 runtime=create_model(folder,model_path,size,error,sizeof(error));
 if(!runtime){report(error);live2d_free();}
}
bool live2d_available(void){return runtime!=NULL;}
void live2d_key(unsigned vk,bool down){if(runtime&&input_key)input_key(runtime,vk,down);}
void live2d_mouse(int button,bool down){if(runtime&&input_mouse)input_mouse(runtime,button,down);}
void live2d_clear_input(void){if(runtime&&clear_input)clear_input(runtime);}
bool live2d_input_selftest(FILE *log){
 if(!runtime){fprintf(log,"FAIL: %ls\n",last_error);return false;}
 uint32_t *pixels=malloc(720*720*4);if(!pixels)return false;char error[768]={0};PetL2DState s={0};bool ok=true;
 live2d_clear_input();inspect_model(runtime,&s,sizeof(s));ok=ok&&s.visible_keys==60&&s.held_visible==0;
 live2d_key('A',true);live2d_key('Q',true);inspect_model(runtime,&s,sizeof(s));ok=ok&&s.held_visible==2&&s.last_vk=='Q';
 for(int f=0;f<60;f++){live2d_key('Q',true);ok=render_model(runtime,240,.033f,-1,pixels,240*4,error,sizeof(error))&&ok;}
 inspect_model(runtime,&s,sizeof(s));ok=ok&&s.held_visible==2&&s.last_vk=='Q'&&s.arrived&&s.key_depth>.95f;
 live2d_key('Q',false);inspect_model(runtime,&s,sizeof(s));ok=ok&&s.held_visible==1&&s.last_vk=='Q';
 live2d_key(VK_F1,true);inspect_model(runtime,&s,sizeof(s));ok=ok&&s.held_visible==1&&s.last_vk==0;live2d_key(VK_F1,false);
 live2d_clear_input();const char sequence[]="QWERTYASDFGHZXCVBN";
 for(int i=0;sequence[i];i++){live2d_key(sequence[i],true);live2d_key(sequence[i],false);ok=render_model(runtime,240,.016f,-1,pixels,240*4,error,sizeof(error))&&ok;inspect_model(runtime,&s,sizeof(s));ok=ok&&s.last_vk==sequence[i]&&s.held_visible==0;}
 for(int b=0;b<3;b++){live2d_mouse(b,true);inspect_model(runtime,&s,sizeof(s));ok=ok&&(s.mouse_mask&(1<<b));ok=render_model(runtime,240,.02f,-1,pixels,240*4,error,sizeof(error))&&ok;live2d_mouse(b,false);}inspect_model(runtime,&s,sizeof(s));ok=ok&&s.mouse_mask==0;
 fprintf(log,"%s: 86 C logical keys / 60 visible keys; latest wins, chords, hold/repeat, unmapped F1, mouse three buttons.\n",ok?"PASS":"FAIL");
 live2d_clear_input();restart_model(runtime);float minimum=1,maximum=0;
 for(int i=0;i<125;i++){ok=render_model(runtime,240,.1f,-1,pixels,240*4,error,sizeof(error))&&ok;float eye=eye_value(runtime);minimum=fminf(minimum,eye);maximum=fmaxf(maximum,eye);}
 ok=ok&&minimum<.1f&&maximum>.9f;fprintf(log,"Blink minimum %.3f maximum %.3f\n",minimum,maximum);
 live2d_key('H',true);for(int i=0;i<25;i++)ok=render_model(runtime,240,.05f,-1,pixels,240*4,error,sizeof(error))&&ok;
 PetL2DState before;inspect_model(runtime,&before,sizeof(before));
 for(int size=240;size<=720;size+=120){ok=render_model(runtime,size,0,-1,pixels,size*4,error,sizeof(error))&&ok;inspect_model(runtime,&s,sizeof(s));for(int j=0;j<5;j++)ok=ok&&fabsf(before.joints[j]-s.joints[j])<1e-4f;unsigned filled=0;for(int j=0;j<size*size;j++){uint32_t a=pixels[j]>>24;filled+=a>20;for(int c=0;c<3;c++)if(((pixels[j]>>(c*8))&255)>a)ok=false;}ok=ok&&filled>(unsigned)(size*size/10)&&filled<(unsigned)(size*size*9/10);}
 live2d_key('H',false);for(int i=0;i<30;i++)ok=render_model(runtime,240,.033f,-1,pixels,240*4,error,sizeof(error))&&ok;inspect_model(runtime,&s,sizeof(s));ok=ok&&s.held_visible==0&&s.key_depth<.002f;
 live2d_clear_input();fprintf(log,"%s: blink, release, five sizes, invariant joint pose and premultiplied alpha; no screenshots. %s\n",ok?"PASS":"FAIL",error);free(pixels);return ok;
}
bool live2d_active(void){return settings.live2d&&live2d_available();}
const wchar_t *live2d_error(void){return last_error;}
void live2d_reset_clock(void){previous=click_until=0;}
void live2d_click(double now){click_until=now+140;}
bool live2d_draw(uint32_t *pixels,int size,double now){
 if(!live2d_active())return false;
 float dt=previous?(float)clampd((now-previous)/1000,0,.1):0;previous=now;
 char error[768]={0};
 if(render_model(runtime,size,dt,now<click_until?0:-1,pixels,size*4,error,sizeof(error)))return true;
 report(error);live2d_free();
 // Fall back visibly to the existing renderer without changing saved preference.
 if(!test_mode&&pet_window)PostMessageW(pet_window,WM_APP+3,0,0);
 return false;
}
static bool save_frame(const wchar_t *name,const uint32_t *data,int size){
 wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\test-live2d-%ls.bmp",app_dir,name);
 FILE *f=_wfopen(path,L"wb");if(!f)return false;
 BITMAPFILEHEADER bf={0};BITMAPINFOHEADER bi={0};
 bf.bfType=0x4d42;bf.bfOffBits=sizeof(bf)+sizeof(bi);bf.bfSize=bf.bfOffBits+size*size*4;
 bi.biSize=sizeof(bi);bi.biWidth=size;bi.biHeight=-size;bi.biPlanes=1;bi.biBitCount=32;
 bi.biSizeImage=size*size*4;
 bool ok=fwrite(&bf,sizeof(bf),1,f)==1&&fwrite(&bi,sizeof(bi),1,f)==1&&fwrite(data,4,(size_t)size*size,f)==(size_t)size*size;
 fclose(f);return ok;
}
bool live2d_selftest(FILE *log){
 if(!runtime){fprintf(log,"FAIL: Live2D initialization: %ls\n",last_error);return false;}
 int n=480;size_t bytes=(size_t)720*720*4;
 uint32_t *data=malloc(bytes),*opened=malloc(bytes);
 if(!data||!opened){free(data);free(opened);return false;}
 bool ok=true;char error[768]={0};restart_model(runtime);
 const double times[]={0,2.5,2.5666667,2.9,5.9666667,7.9,8.1,10.5666667,16.1};double current=0;
 unsigned changed=0,alpha0=0,partial=0,opaque=0,bad_premul=0;
 double start=now_ms();int frames=0;
 for(int i=0;i<9&&ok;i++){
  do {double dt=fmin(.05,times[i]-current);current+=dt;
   ok=render_model(runtime,n,(float)dt,-1,data,n*4,error,sizeof(error))!=0;frames++;
  }while(current+1e-7<times[i]&&ok);
  float eye=eye_value(runtime);bool closed=i==2||i==4||i==7;
  fprintf(log,"Live2D t=%.3f eye=%.3f\n",current,eye);
  ok=ok&&(closed?eye<.1f:(i==1||eye>.9f));
  if(i==0){memcpy(opened,data,(size_t)n*n*4);ok=save_frame(L"open",data,n)&&ok;}
  if(i==2){for(int j=0;j<n*n;j++)changed+=data[j]!=opened[j];ok=save_frame(L"closed",data,n)&&ok;}
 }
 for(int j=0;j<n*n;j++){uint32_t p=opened[j],a=p>>24;alpha0+=a==0;partial+=a>0&&a<255;opaque+=a==255;for(int c=0;c<3;c++)bad_premul+=((p>>(c*8))&255)>a;}
 ok=ok&&changed>10&&alpha0>10000&&opaque>10000&&partial>0&&bad_premul==0;
 fprintf(log,"Live2D alpha: transparent=%u partial=%u opaque=%u invalid-premultiplied=%u, eye-changed=%u\n",alpha0,partial,opaque,bad_premul,changed);
 for(int size=240;size<=720&&ok;size+=120){
  ok=render_model(runtime,size,0,-1,data,size*4,error,sizeof(error))!=0;
  unsigned filled=0;for(int j=0;j<size*size;j++)filled+=(data[j]>>24)>20;
  ok=ok&&filled>(unsigned)(size*size/10)&&filled<(unsigned)(size*size*9/10);
  wchar_t name[32];swprintf(name,32,L"size-%d",size);ok=save_frame(name,data,size)&&ok;
 }
 // Test manual click response and restoration to the authored motion.
 ok=render_model(runtime,n,0,0,data,n*4,error,sizeof(error))&&eye_value(runtime)<.1f&&ok;
 ok=render_model(runtime,n,.05f,-1,data,n*4,error,sizeof(error))&&eye_value(runtime)>.9f&&ok;
 double render_elapsed=now_ms()-start;
 // A failed load must release SDK/device state so a subsequent retry succeeds.
 wchar_t folder[MAX_PATH],model_path[MAX_PATH],missing[MAX_PATH];
 swprintf(folder,MAX_PATH,L"%ls\\live2d-runtime",app_dir);
 swprintf(model_path,MAX_PATH,L"%ls\\assets\\live2d\\runtime\\pet",app_dir);
 swprintf(missing,MAX_PATH,L"%ls\\__missing_model_for_selftest__",folder);
 destroy_model(runtime);runtime=NULL;
 void *unexpected=create_model(folder,missing,n,error,sizeof(error));
 bool rejected=unexpected==NULL;if(unexpected)destroy_model(unexpected);
 runtime=create_model(folder,model_path,n,error,sizeof(error));
 ok=ok&&rejected&&runtime!=NULL;
 if(runtime)ok=render_model(runtime,n,0,-1,data,n*4,error,sizeof(error))&&ok;
 fprintf(log,"%s: missing model rejected; SDK/device recreated.\n",rejected&&runtime?"PASS":"FAIL");
 fprintf(log,"Live2D render average %.2f ms/frame (%d frames, includes readback; excludes SDK reload).\n",render_elapsed/frames,frames);
 if(!ok)fprintf(log,"FAIL details: %s\n",error);
 fprintf(log,"%s: Native Live2D, two loops, alpha/BGRA, 5 sizes, click blink.\n",ok?"PASS":"FAIL");
 free(data);free(opened);if(runtime)restart_model(runtime);live2d_reset_clock();return ok;
}
