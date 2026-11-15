#define COBJMACROS
#include "appearance.h"
#include "rewards.h"
#include "ui_theme.h"
#include <commctrl.h>
#include <commdlg.h>
#include <wincodec.h>

#define PHOTO_LIMIT 1024
#define FILE_LIMIT (12 * 1024 * 1024)
#define APPEARANCE_MAGIC 0x3159504c
#define APPEARANCE_MAGIC_V2 0x3259504c
#define APPLY_ID 1201
enum {IMPORT_ID=1202, REMOVE_ID, GREEN_ID, THEME_ID, MOTION_ID, RESTORE_ID, CHARACTER_ID};
enum {MOTION_BREATH, MOTION_SWAY, MOTION_BOUNCE};

typedef struct {
 bool enabled,green;
 int theme,motion,zoom,x,y,character;
 Image photo,sprite,scene,scaled;
 RECT crop;
 double tap_at,frame_at;
 bool model_failed;
} Appearance;

static const wchar_t *theme_names[]={L"红金",L"午夜蓝",L"奶油白",L"森林绿",L"高对比"};
static const wchar_t *theme_files[]={L"wine",L"blue",L"cream",L"forest",L"contrast"};
_Static_assert(ARRAYSIZE(theme_names)==UI_THEME_COUNT&&ARRAYSIZE(theme_files)==UI_THEME_COUNT,"DIY theme inventory");
static const wchar_t *motion_names[]={L"呼吸",L"轻晃",L"点击弹跳"};
static const wchar_t *character_names[]={L"原版桌宠",L"阿蒙 · Amun",L"知更鸟 · Robin",L"桑多涅 · Sandone"};
_Static_assert(ARRAYSIZE(character_names)==PET_CHARACTER_COUNT,"Live2D character choices");
static Appearance current,draft;
static wchar_t save_path[32768];
static HWND panel,preview,theme_box,character_box,motion_box,green_check,zoom_track,zoom_label,photo_label,remove_button;
static int dpi=96;
static Image preview_pixels;
static Image original_frame;
static Image scene_character_frame;
static Image layout_frame;
static double original_frame_at;
static bool dragging,drag_moved;
static POINT drag_start;
static int start_x,start_y;

static int px(int value){return MulDiv(value,dpi,96);}
static int bound(int value,int low,int high){return max(low,min(high,value));}
static void image_free(Image *image){free(image->pixels);*image=(Image){0};}
static bool image_alloc(Image *image,int width,int height){
 uint32_t *pixels=malloc((size_t)width*height*sizeof(*pixels));
 if(!pixels)return false;
 image_free(image);*image=(Image){width,height,pixels};
 return true;
}
static void state_free(Appearance *state){
 image_free(&state->photo);image_free(&state->sprite);image_free(&state->scene);image_free(&state->scaled);
 *state=(Appearance){0};
}
static Appearance defaults(void){return (Appearance){.zoom=100,.x=57,.y=0};}

static bool decode_image(const wchar_t *path,int limit,Image *image){
 WIN32_FILE_ATTRIBUTE_DATA attributes;
 if(!GetFileAttributesExW(path,GetFileExInfoStandard,&attributes)||
    (attributes.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||attributes.nFileSizeHigh||attributes.nFileSizeLow>FILE_LIMIT)return false;
 HRESULT initialized=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
 if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE)return false;
 IWICImagingFactory *factory=NULL;
 IWICBitmapDecoder *decoder=NULL;
 IWICBitmapFrameDecode *frame=NULL;
 IWICBitmapScaler *scaler=NULL;
 IWICFormatConverter *converter=NULL;
 Image result={0};
 bool ok=false;
 UINT width=0,height=0;
 GUID format;
 if(FAILED(CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory,(void**)&factory))||
    FAILED(IWICImagingFactory_CreateDecoderFromFilename(factory,path,NULL,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder))||
    FAILED(IWICBitmapDecoder_GetContainerFormat(decoder,&format))||
    (!IsEqualGUID(&format,&GUID_ContainerFormatPng)&&!IsEqualGUID(&format,&GUID_ContainerFormatJpeg))||
    FAILED(IWICBitmapDecoder_GetFrame(decoder,0,&frame))||
    FAILED(IWICBitmapFrameDecode_GetSize(frame,&width,&height))||
    !width||!height||width>8192||height>8192)goto done;
 IWICBitmapSource *source=(IWICBitmapSource*)frame;
 UINT largest=max(width,height);
 if(largest>(UINT)limit){
  width=max(1,width*(UINT)limit/largest);height=max(1,height*(UINT)limit/largest);
  if(FAILED(IWICImagingFactory_CreateBitmapScaler(factory,&scaler))||
     FAILED(IWICBitmapScaler_Initialize(scaler,source,width,height,WICBitmapInterpolationModeFant)))goto done;
  source=(IWICBitmapSource*)scaler;
 }
 if(FAILED(IWICImagingFactory_CreateFormatConverter(factory,&converter))||
    FAILED(IWICFormatConverter_Initialize(converter,source,&GUID_WICPixelFormat32bppBGRA,
     WICBitmapDitherTypeNone,NULL,0,WICBitmapPaletteTypeCustom))||
    !image_alloc(&result,(int)width,(int)height))goto done;
 ok=SUCCEEDED(IWICFormatConverter_CopyPixels(converter,NULL,width*4,width*height*4,(BYTE*)result.pixels));
done:
 if(converter)IWICFormatConverter_Release(converter);
 if(scaler)IWICBitmapScaler_Release(scaler);
 if(frame)IWICBitmapFrameDecode_Release(frame);
 if(decoder)IWICBitmapDecoder_Release(decoder);
 if(factory)IWICImagingFactory_Release(factory);
 if(SUCCEEDED(initialized))CoUninitialize();
 if(ok){image_free(image);*image=result;}else image_free(&result);
 return ok;
}

static uint32_t premultiply(uint32_t color,bool key);
bool appearance_load_sprite(const wchar_t *path,Image *image){
 if(!decode_image(path,256,image))return false;
 for(int i=0;i<image->w*image->h;i++)image->pixels[i]=premultiply(image->pixels[i],false);
 return true;
}

static uint32_t premultiply(uint32_t color,bool key){
 unsigned a=color>>24,r=(color>>16)&255,g=(color>>8)&255,b=color&255;
 int other=(int)max(r,b),excess=(int)g-other;
 if(key&&excess>25){
  a=a*(unsigned)(65-min(excess-25,65))/65;
  g=min(g,(unsigned)(other+18));
 }
 return (a<<24)|(((r*a+127)/255)<<16)|(((g*a+127)/255)<<8)|((b*a+127)/255);
}
static bool has_green_corners(const Image *image){
 int w=image->w,h=image->h;
 uint32_t corners[]={image->pixels[0],image->pixels[w-1],image->pixels[(h-1)*w],image->pixels[h*w-1]};
 int green=0;
 for(size_t i=0;i<sizeof(corners)/sizeof(*corners);i++){
  uint32_t p=corners[i];
  if((p>>24)>240&&(int)((p>>8)&255)-(int)max((p>>16)&255,p&255)>65)green++;
 }
 return green>=3;
}
static bool make_sprite(Appearance *state,bool green){
 const Image *source=&state->photo;
 if(!source->pixels){image_free(&state->sprite);state->crop=(RECT){0};state->green=green;return true;}
 if(!image_alloc(&state->sprite,source->w,source->h))return false;
 RECT visible={source->w,source->h,0,0};
 for(int y=0;y<source->h;y++)for(int x=0;x<source->w;x++){
  int index=y*source->w+x;
  uint32_t p=premultiply(source->pixels[index],green);
  state->sprite.pixels[index]=p;
  if((p>>24)>8){
   visible.left=min(visible.left,x);visible.top=min(visible.top,y);
   visible.right=max(visible.right,x+1);visible.bottom=max(visible.bottom,y+1);
  }
 }
 state->crop=visible;state->green=green;return true;
}
static void paint_keyboard(Appearance *state);
static bool load_scene(Appearance *state,int theme){
 wchar_t path[MAX_PATH+40];
 swprintf(path,MAX_PATH+40,L"%ls\\assets\\diy\\%ls.png",app_dir,theme_files[theme]);
 if(!decode_image(path,1536,&state->scene))return false;
 for(int i=0;i<state->scene.w*state->scene.h;i++)state->scene.pixels[i]=premultiply(state->scene.pixels[i],false);
 state->theme=theme;paint_keyboard(state);image_free(&state->scaled);return true;
}

static bool write_state(const Appearance *state){
 if(!save_path[0])return false;
 wchar_t temp[32768];
 swprintf(temp,32768,L"%ls.tmp",save_path);
 int32_t values[]={APPEARANCE_MAGIC_V2,state->enabled,state->theme,state->motion,state->zoom,state->x,state->y,
                  state->green,state->photo.w,state->photo.h,state->character};
 DWORD written,bytes=(DWORD)((size_t)state->photo.w*state->photo.h*4);
 HANDLE file=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
 if(file==INVALID_HANDLE_VALUE)return false;
 bool ok=WriteFile(file,values,sizeof(values),&written,NULL)&&written==sizeof(values)&&
         (!bytes||(WriteFile(file,state->photo.pixels,bytes,&written,NULL)&&written==bytes))&&FlushFileBuffers(file);
 CloseHandle(file);
 if(ok)ok=MoveFileExW(temp,save_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
 if(!ok)DeleteFileW(temp);
 return ok;
}
static bool save_state(const Appearance *state){return !test_mode&&write_state(state);}

static bool read_state(Appearance *state){
 FILE *file=_wfopen(save_path,L"rb");
 if(!file)return false;
 int32_t v[10],character=PET_CHARACTER_ORIGINAL;
 bool ok=fread(v,sizeof(v),1,file)==1&&(v[0]==APPEARANCE_MAGIC||v[0]==APPEARANCE_MAGIC_V2)&&v[1]>=0&&v[1]<=1&&v[2]>=0&&v[2]<UI_THEME_COUNT&&
         v[3]>=MOTION_BREATH&&v[3]<=MOTION_BOUNCE&&v[4]>=50&&v[4]<=150&&v[5]>=15&&v[5]<=85&&v[6]>=-35&&v[6]<=25&&
         v[7]>=0&&v[7]<=1&&v[8]>=0&&v[8]<=PHOTO_LIMIT&&v[9]>=0&&v[9]<=PHOTO_LIMIT&&((v[8]==0)==(v[9]==0));
 if(ok&&v[0]==APPEARANCE_MAGIC_V2)ok=fread(&character,sizeof(character),1,file)==1&&character>=0&&character<PET_CHARACTER_COUNT;
 if(ok&&v[8]){
  size_t count=(size_t)v[8]*v[9];
  ok=image_alloc(&state->photo,v[8],v[9])&&fread(state->photo.pixels,4,count,file)==count;
 }
 if(ok)ok=fgetc(file)==EOF&&!ferror(file);
 fclose(file);
 if(!ok)return false;
 state->enabled=v[1]!=0;state->theme=v[2];state->motion=v[3];state->zoom=v[4];state->x=v[5];state->y=v[6];state->green=v[7]!=0;state->character=character;
 return !state->enabled||(load_scene(state,state->theme)&&make_sprite(state,state->green));
}

void appearance_init(const wchar_t *settings_path){
 current=defaults();
 const wchar_t *slash=wcsrchr(settings_path,L'\\');
 if(!slash||slash-settings_path>32740)return;
 swprintf(save_path,32768,L"%.*ls\\appearance.dat",(int)(slash-settings_path),settings_path);
 if(!read_state(&current)){state_free(&current);current=defaults();}
 if(current.enabled)ui_theme_select(current.theme);
}

static uint32_t composite(uint32_t source,uint32_t dest){
 unsigned a=source>>24;
 if(!a)return dest;
 if(a==255)return source;
 uint32_t result=0;
 for(unsigned shift=0;shift<=24;shift+=8){
  unsigned value=((source>>shift)&255)+(((dest>>shift)&255)*(255-a)+127)/255;
  result|=min(value,255u)<<shift;
 }
 return result;
}
static uint32_t argb(COLORREF color,unsigned alpha){
 return premultiply((alpha<<24)|((uint32_t)GetRValue(color)<<16)|((uint32_t)GetGValue(color)<<8)|GetBValue(color),false);
}
static uint32_t interpolate(uint32_t a,uint32_t b,unsigned weight){
 uint32_t lo=((a&0xff00ff)*(256-weight)+(b&0xff00ff)*weight+0x800080u)>>8;
 uint32_t hi=(((a>>8)&0xff00ff)*(256-weight)+((b>>8)&0xff00ff)*weight+0x800080u)>>8;
 return (lo&0xff00ff)|((hi&0xff00ff)<<8);
}
static uint32_t sample(const Image *image,RECT bounds,double x,double y){
 x=clampd(x,bounds.left,bounds.right-1);y=clampd(y,bounds.top,bounds.bottom-1);
 int left=(int)x,top=(int)y,right=min(left+1,bounds.right-1),bottom=min(top+1,bounds.bottom-1);
 unsigned u=(unsigned)((x-left)*256),v=(unsigned)((y-top)*256);
 const uint32_t *a=image->pixels+top*image->w,*b=image->pixels+bottom*image->w;
 return interpolate(interpolate(a[left],a[right],u),interpolate(b[left],b[right],u),v);
}

static bool fit_scene(uint32_t *pixels,int size){
 if(layout_frame.w!=size&&!image_alloc(&layout_frame,size,size))return false;
 memcpy(layout_frame.pixels,pixels,(size_t)size*size*4);
 RECT bounds={0,0,size,size};
 for(int y=0;y<size;y++)for(int x=0;x<size;x++){
  double sx=((x+.5)/size-.5)/DIY_SCENE_SCALE*size+size*.5-.5;
  double sy=((y+.5)/size-DIY_SCENE_Y_OFFSET)/DIY_SCENE_SCALE*size-.5;
  pixels[y*size+x]=sx<0||sy<0||sx>size-1||sy>size-1?0:sample(&layout_frame,bounds,sx,sy);
 }
 return true;
}

static int scene_top(int size){return size/5;}
static int scene_height(const Appearance *state,int size){return MulDiv(size,state->scene.h,state->scene.w);}
static int desk_top(const Appearance *state,int size){return scene_top(size)+MulDiv(scene_height(state,size),552,1000);}

static bool cache_scene(Appearance *state,int size){
 if(state->scaled.w==size)return true;
 if(!image_alloc(&state->scaled,size,size))return false;
 memset(state->scaled.pixels,0,(size_t)size*size*4);
 int top=scene_top(size),height=scene_height(state,size);
 const Image *scene=&state->scene;RECT bounds={0,0,scene->w,scene->h};
 double sx=(double)scene->w/size,sy=(double)scene->h/height;
 for(int y=0;y<height&&y+top<size;y++)for(int x=0;x<size;x++){
  double xa=x+.25,xb=x+.75;
  if(y>=MulDiv(height,552,1000)){
   xa=appearance_desk_x(xa/size,true,true)*size;
   xb=appearance_desk_x(xb/size,true,true)*size;
  }
  uint32_t a=sample(scene,bounds,xa*sx-.5,(y+.25)*sy-.5);
  uint32_t b=sample(scene,bounds,xb*sx-.5,(y+.25)*sy-.5);
  uint32_t c=sample(scene,bounds,xa*sx-.5,(y+.75)*sy-.5);
  uint32_t d=sample(scene,bounds,xb*sx-.5,(y+.75)*sy-.5);
  state->scaled.pixels[(y+top)*size+x]=interpolate(interpolate(a,b,128),interpolate(c,d,128),128);
 }
 return true;
}

static void draw_sprite(uint32_t *pixels,int size,const Image *image,RECT crop,int left,int top,int w,int h,int clip_bottom){
 int crop_width=crop.right-crop.left,crop_height=crop.bottom-crop.top;
 for(int y=max(0,top);y<min(clip_bottom,top+h);y++){
  double sy=crop.top+(y-top+.5)*crop_height/h-.5;
  for(int x=max(0,left);x<min(size,left+w);x++){
   double sx=crop.left+(x-left+.5)*crop_width/w-.5;
   pixels[y*size+x]=composite(sample(image,crop,sx,sy),pixels[y*size+x]);
  }
 }
}
static void draw_character(const Appearance *state,uint32_t *pixels,int size,double now){
 int crop_width=state->crop.right-state->crop.left,crop_height=state->crop.bottom-state->crop.top;
 if(crop_width<=0||crop_height<=0)return;
 double width=size*.44*state->zoom/100.0,height=width*crop_height/crop_width;
 if(height>size*.64*state->zoom/100){height=size*.64*state->zoom/100;width=height*crop_width/crop_height;}
 double wave=sin(now/520),center=size*state->x/100.0,base=desk_top(state,size)+size*state->y/100.0;
 if(state->motion==MOTION_BREATH){width*=1+.018*wave;height*=1+.032*wave;}
 if(state->motion==MOTION_SWAY)center+=size*.017*wave;
 double tap=(now-state->tap_at)/390;
 if(state->motion==MOTION_BOUNCE&&state->tap_at>0&&tap>=0&&tap<1)base-=size*.05*sin(PI*tap);
 int w=max(1,(int)width),h=max(1,(int)height);
 int left=(int)center-w/2,top=min((int)base-h,desk_top(state,size)-min(h,12));
 draw_sprite(pixels,size,&state->sprite,state->crop,left,top,w,h,desk_top(state,size));
}
static bool update_original(double now){
 int size=settings.size;
 if(original_frame.w!=size){
  original_frame_at=0;
  if(!image_alloc(&original_frame,size,size))return false;
 }
 if(original_frame_at>0&&now-original_frame_at<24)return true;
 original_frame_at=0;
 if(!live2d_draw_character(original_frame.pixels,size,now))return false;
 original_frame_at=now;return true;
}
static void draw_original(const Appearance *state,uint32_t *pixels,int size){
 // Anchor the authored Live2D character at the tabletop, including its hands over the keys.
 int extent=max(1,(int)(size*.84*state->zoom/100));
 int left=(int)(size*state->x/100.0-extent*.55);
 int top=(int)(desk_top(state,size)+size*state->y/100.0-extent*.61);
 RECT source={0,0,original_frame.w,original_frame.h};
 draw_sprite(pixels,size,&original_frame,source,left,top,extent,extent,size);
}

typedef struct {UINT vk;const wchar_t *label;int units;} Cap;
// The desk is viewed from the front, so physical key order is right to left.
static const Cap caps[][15]={
 {{VK_RCONTROL,L"Ctrl",5},{VK_RWIN,L"Win",5},{VK_RMENU,L"Alt",5},{VK_SPACE,L"",24},
  {VK_LMENU,L"Alt",5},{VK_LWIN,L"Win",5},{VK_LCONTROL,L"Ctrl",5}},
 {{VK_RSHIFT,L"Shift",10},{VK_OEM_2,L"/",4},{VK_OEM_PERIOD,L".",4},{VK_OEM_COMMA,L",",4},
  {'M',L"M",4},{'N',L"N",4},{'B',L"B",4},{'V',L"V",4},{'C',L"C",4},{'X',L"X",4},{'Z',L"Z",4},{VK_LSHIFT,L"Shift",9}},
 {{VK_RETURN,L"Enter",9},{VK_OEM_7,L"'",4},{VK_OEM_1,L";",4},{'L',L"L",4},{'K',L"K",4},
  {'J',L"J",4},{'H',L"H",4},{'G',L"G",4},{'F',L"F",4},{'D',L"D",4},{'S',L"S",4},{'A',L"A",4},{VK_CAPITAL,L"Caps",7}},
 {{VK_OEM_5,L"\\",6},{VK_OEM_6,L"]",4},{VK_OEM_4,L"[",4},{'P',L"P",4},{'O',L"O",4},
  {'I',L"I",4},{'U',L"U",4},{'Y',L"Y",4},{'T',L"T",4},{'R',L"R",4},{'E',L"E",4},{'W',L"W",4},{'Q',L"Q",4},{VK_TAB,L"Tab",6}},
 {{VK_BACK,L"Back",8},{VK_OEM_PLUS,L"=",4},{VK_OEM_MINUS,L"-",4},{'0',L"0",4},
  {'9',L"9",4},{'8',L"8",4},{'7',L"7",4},{'6',L"6",4},{'5',L"5",4},{'4',L"4",4},
  {'3',L"3",4},{'2',L"2",4},{'1',L"1",4},{VK_OEM_3,L"`",4}}
};

static RECT cap_rect(int row,const Cap *cap){
 const int left[]={338,330,322,315,307};
 const int right[]={864,867,870,873,875};
 int total=0,start=0;
 for(const Cap *key=caps[row];key->vk;key++){
  if(key<cap)start+=key->units;
  total+=key->units;
 }
 int width=right[row]-left[row],top=622+28*row;
 return (RECT){left[row]+MulDiv(width,start,total),top,left[row]+MulDiv(width,start+cap->units,total)-3,top+25};
}
static bool key_rect(UINT key,RECT *out){
 for(int row=0;row<ARRAYSIZE(caps);row++)for(int col=0;col<ARRAYSIZE(caps[row])&&caps[row][col].vk;col++){
  if(caps[row][col].vk==key){*out=cap_rect(row,&caps[row][col]);return true;}
 }
 return false;
}
static bool inside_cap(double x,double y,RECT r,int radius){
 if(x<r.left||x>=r.right||y<r.top||y>=r.bottom)return false;
 double dx=x-clampd(x,r.left+radius,r.right-radius),dy=y-clampd(y,r.top+radius,r.bottom-radius);
 return dx*dx+dy*dy<=radius*radius;
}
static void paint_keyboard(Appearance *state){
 Image *scene=&state->scene;
 if(scene->w!=1536||scene->h!=1024)return;
 const UiColors *colors=ui_theme_colors(state->theme);
 COLORREF cap=colors->accent;
 COLORREF edge=ui_theme_mix(cap,colors->background,33);
 const RECT board={298,617,879,765};
 BITMAPINFO info={0};
 int width=board.right-board.left,height=board.bottom-board.top;
 info.bmiHeader.biSize=sizeof(info.bmiHeader);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
 info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
 void *bits=NULL;HDC dc=CreateCompatibleDC(NULL);
 HBITMAP bitmap=dc?CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,NULL,0):NULL;
 if(!bitmap){if(dc)DeleteDC(dc);return;}
 HGDIOBJ previous=SelectObject(dc,bitmap);
 HFONT font=CreateFontW(-15,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
 HGDIOBJ old_font=font?SelectObject(dc,font):NULL;
 SetBkMode(dc,TRANSPARENT);SetTextColor(dc,colors->on_accent);
 memset(bits,0,(size_t)width*height*4);
 for(int y=board.top;y<board.bottom;y++)for(int x=board.left;x<board.right;x++){
  int left=335-(y-board.top)*35/height;
  if(x>=left&&inside_cap(x+.5,y+.5,board,15))
   ((uint32_t*)bits)[(y-board.top)*width+x-board.left]=argb(colors->background,255);
 }
 for(int row=0;row<ARRAYSIZE(caps);row++)for(int col=0;col<ARRAYSIZE(caps[row])&&caps[row][col].vk;col++){
  const Cap *key=&caps[row][col];RECT r=cap_rect(row,key);
  for(int y=r.top;y<r.bottom;y++)for(int x=r.left;x<r.right;x++){
   if(!inside_cap(x+.5,y+.5,r,5))continue;
   int shade=10+(y-r.top)*27/(r.bottom-r.top);
   COLORREF color=y>=r.bottom-4?edge:ui_theme_mix(cap,edge,shade);
   ((uint32_t*)bits)[(y-board.top)*width+x-board.left]=argb(color,255);
  }
  RECT label=r;OffsetRect(&label,-board.left,-board.top-1);
  DrawTextW(dc,key->label,-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
 }
 GdiFlush();
 for(int y=board.top;y<board.bottom;y++)for(int x=board.left;x<board.right;x++){
  uint32_t color=((uint32_t*)bits)[(y-board.top)*width+x-board.left];
  if(color)scene->pixels[y*scene->w+x]=color|0xff000000u;
 }
 if(old_font)SelectObject(dc,old_font);
 if(font)DeleteObject(font);
 SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
}

static bool inside_mouse(double x,double y,int button){
 if(button==2){
  const RECT wheel={1084,714,1111,760};return inside_cap(x,y,wheel,10);
 }
 double left=x-1097,up=y-740;
 double button_top=718+16*(left*left)/(64*64);
 if(y<button_top||left*left/(64*64)+up*up/(40*40)>1)return false;
 return button==0?left>=16:left<=-16;
}
static const Vec mouse_contacts[]={{1125,743},{1068,743},{1097,736}};
static void press_mouse(uint32_t *pixels,int size,int height,RECT bounds,double amount,COLORREF color,int button){
 if(amount<.015)return;
 int x0=MulDiv(bounds.left,size,1536),x1=MulDiv(bounds.right,size,1536);
 int y0=scene_top(size)+MulDiv(bounds.top,height,1024),y1=scene_top(size)+MulDiv(bounds.bottom,height,1024);
 for(int y=y0;y<min(size,y1);y++)for(int x=x0;x<x1;x++){
  double sx=(x+.5)*1536/size,sy=(y-scene_top(size)+.5)*1024/height;
  if(!inside_mouse(sx,sy,button))continue;
  int offset=y*size+x;
  unsigned opacity=(unsigned)((sy<bounds.top+5?150:95)*amount);
  if((pixels[offset]>>24)>200)pixels[offset]=composite(argb(color,opacity),pixels[offset]);
 }
}
static void press_key(Appearance const *state,uint32_t *pixels,int size,int height,RECT cap,double amount){
 const Image *scene=&state->scene;RECT source={0,0,scene->w,scene->h};
 int x0=MulDiv(cap.left,size,1536),x1=MulDiv(cap.right,size,1536);
 int y0=scene_top(size)+MulDiv(cap.top,height,1024),y1=scene_top(size)+MulDiv(cap.bottom,height,1024);
 for(int y=y0;y<min(size,y1);y++)for(int x=x0;x<x1;x++){
  double sx=(x+.5)*1536/size,sy=(y-scene_top(size)+.5)*1024/height;
  if(!inside_cap(sx,sy,cap,5))continue;
  double distance=fmin(fmin(sx-cap.left,cap.right-sx),fmin(sy-cap.top,cap.bottom-sy));
  double weight=smooth(distance/6);
  uint32_t shifted=sample(scene,source,sx-.5,sy-.5-amount*weight*2.6);
  uint32_t shade=argb(ui_theme_colors(state->theme)->background,(unsigned)(amount*48));
  uint32_t *dest=&pixels[y*size+x];
  *dest=interpolate(*dest,composite(shade,shifted),(unsigned)(256*weight));
 }
}
static void draw_input(const Appearance *state,uint32_t *pixels,int size){
 int height=scene_height(state,size);
 const UiColors *palette=ui_theme_colors(state->theme);
 for(int i=0;i<model.count;i++){
  RECT r;const Key *key=&model.keys[i];
  if(key->amount>=.015&&key_rect(key->vk,&r))press_key(state,pixels,size,height,r,key->amount);
 }
 const RECT mouse={1033,713,1161,780};
 for(int button=0;button<3;button++)press_mouse(pixels,size,height,mouse,model.mouse[button].amount,palette->accent,button);
}
static bool update_scene_model(Appearance *state,int size,double now,bool previewing){
 if(scene_character_frame.w!=size&&!image_alloc(&scene_character_frame,size,size))return false;
 float dt=state->frame_at?(float)clampd((now-state->frame_at)/1000,0,.1):0;
 bool blink=state->tap_at>0&&now-state->tap_at<140;
 Vec key={0},mouse={0};double key_depth=0,mouse_depth=0;
 if(!previewing){
  const Key *active=NULL;RECT bounds;
  for(int i=0;i<model.count;i++){
   const Key *k=&model.keys[i];RECT candidate;
   if((!k->held&&now>=k->until)||!key_rect(k->vk,&candidate))continue;
   if(!active||k->order>active->order){active=k;bounds=candidate;}
  }
  if(active){key=(Vec){(bounds.left+bounds.right)*.5,(bounds.top+bounds.bottom)*.5};key_depth=active->amount;}
  for(int b=0;b<ARRAYSIZE(mouse_contacts);b++)if(model.mouse[b].amount>mouse_depth){mouse_depth=model.mouse[b].amount;mouse=mouse_contacts[b];}
 }
 if(!live2d_draw_scene_character(state->character,scene_character_frame.pixels,size,dt,state->zoom,state->x,state->y,blink,previewing,
                                key,key_depth,mouse,mouse_depth)){
  state->model_failed=true;return false;
 }
 state->frame_at=now;state->model_failed=false;return true;
}
static bool render(Appearance *state,uint32_t *pixels,int size,double now,bool keyboard){
 if(!state->scene.pixels||!cache_scene(state,size))return false;
 memcpy(pixels,state->scaled.pixels,(size_t)size*size*4);
 if(state->photo.pixels)draw_character(state,pixels,size,now);
 if(keyboard)draw_input(state,pixels,size);
 if(state->photo.pixels)return true;
 if(state->character!=PET_CHARACTER_ORIGINAL&&!state->model_failed&&update_scene_model(state,size,now,!keyboard)){
  for(int i=0;i<size*size;i++)pixels[i]=composite(scene_character_frame.pixels[i],pixels[i]);
  return true;
 }
 if(state!=&current&&state->character!=PET_CHARACTER_ORIGINAL)return false;
 if(!update_original(now))return false;
 draw_original(state,pixels,size);
 return true;
}

bool appearance_active(void){return current.enabled;}
int appearance_character(void){return current.photo.pixels?-1:current.character;}
bool appearance_animate(double now){
 if(!current.enabled)return false;
 if(!current.photo.pixels)return live2d_available();
 if(!current.sprite.pixels)return false;
 if(current.motion!=MOTION_BOUNCE)return true;
 if(current.tap_at<=0)return false;
 if(now-current.tap_at>=390)current.tap_at=0;
 return true;
}
void appearance_tap(double now){
 if(!current.enabled)return;
 if(current.photo.pixels||(current.character!=PET_CHARACTER_ORIGINAL&&!current.model_failed))current.tap_at=now;else live2d_click(now);
}
bool appearance_draw(uint32_t *pixels,int size,double now){
 return current.enabled&&render(&current,pixels,size,now,true)&&fit_scene(pixels,size);
}
bool appearance_restore(void){
 if(!current.enabled)return true;
 current.enabled=false;
 if(!save_state(&current)){current.enabled=true;return false;}
 ui_theme_select(0);
 pet_refresh_appearance();
 return true;
}

static void repaint_preview(void){if(preview)InvalidateRect(preview,NULL,FALSE);}
static LRESULT CALLBACK preview_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
 switch(message){
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{
  PAINTSTRUCT paint;
  HDC dc=BeginPaint(window,&paint);
  RECT bounds;GetClientRect(window,&bounds);
  int size=min(bounds.right,bounds.bottom);
  const UiColors *palette=ui_theme_colors(draft.theme);
  COLORREF background=palette->surface;
  COLORREF grid=ui_theme_mix(background,palette->border,19);
  if(size>0&&(preview_pixels.w==size||image_alloc(&preview_pixels,size,size))&&
     render(&draft,preview_pixels.pixels,size,now_ms(),false)&&fit_scene(preview_pixels.pixels,size)){
   renderer_draw_accessories(preview_pixels.pixels,size,rewards_equipment(),true,draft.photo.pixels?-1:draft.character);
   uint32_t tiles[]={argb(background,255),argb(grid,255)};
   int tile=max(1,MulDiv(size,1,16));
   for(int y=0;y<size;y++)for(int x=0;x<size;x++){
    int i=y*size+x;
    preview_pixels.pixels[i]=composite(preview_pixels.pixels[i],tiles[(x/tile+y/tile)%2]);
   }
   BITMAPINFO info={0};
   info.bmiHeader.biSize=sizeof(info.bmiHeader);info.bmiHeader.biWidth=size;info.bmiHeader.biHeight=-size;
   info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
   StretchDIBits(dc,0,0,size,size,0,0,size,size,preview_pixels.pixels,&info,DIB_RGB_COLORS,SRCCOPY);
  }else{
   ui_theme_fill(dc,&bounds,background);
   SetTextColor(dc,palette->muted);SetBkMode(dc,TRANSPARENT);
   DrawTextW(dc,L"无法加载所选角色，请检查模型文件。",-1,&bounds,DT_SINGLELINE|DT_CENTER|DT_VCENTER);
  }
  HBRUSH edge=CreateSolidBrush(GetFocus()==window?palette->accent:palette->border);
  if(edge){FrameRect(dc,&bounds,edge);DeleteObject(edge);}
  EndPaint(window,&paint);return 0;
 }
 case WM_LBUTTONDOWN:
  if(draft.photo.pixels||draft.frame_at>0||original_frame_at>0){
   SetFocus(window);
   drag_start=(POINT){GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};start_x=draft.x;start_y=draft.y;
   dragging=true;drag_moved=false;SetCapture(window);
  }
  return 0;
 case WM_MOUSEMOVE:
  if(dragging){
   int dx=GET_X_LPARAM(lp)-drag_start.x,dy=GET_Y_LPARAM(lp)-drag_start.y;
   if(abs(dx)+abs(dy)>=3)drag_moved=true;
   if(drag_moved){
    RECT bounds;GetClientRect(window,&bounds);
    draft.x=bound(start_x+(int)(dx*100.0/(max(1,bounds.right)*DIY_SCENE_SCALE)),15,85);
    draft.y=bound(start_y+(int)(dy*100.0/(max(1,bounds.bottom)*DIY_SCENE_SCALE)),-35,25);
    repaint_preview();
   }
  }
  return 0;
 case WM_LBUTTONUP:
  if(dragging){
   if(!drag_moved){if(draft.photo.pixels||draft.character!=PET_CHARACTER_ORIGINAL)draft.tap_at=now_ms();else live2d_click(now_ms());}
   dragging=false;ReleaseCapture();repaint_preview();
  }
  return 0;
 case WM_CAPTURECHANGED:dragging=false;return 0;
 case WM_GETDLGCODE:return DLGC_WANTARROWS;
 case WM_KEYDOWN:
  if(wp==VK_LEFT||wp==VK_RIGHT)draft.x=bound(draft.x+(wp==VK_LEFT?-2:2),15,85);
  else if(wp==VK_UP||wp==VK_DOWN)draft.y=bound(draft.y+(wp==VK_UP?-2:2),-35,25);
  else if(wp==VK_SPACE){if(draft.photo.pixels||draft.character!=PET_CHARACTER_ORIGINAL)draft.tap_at=now_ms();else live2d_click(now_ms());}
  else break;
  repaint_preview();return 0;
 case WM_SETFOCUS:case WM_KILLFOCUS:repaint_preview();return 0;
 }
 return DefWindowProcW(window,message,wp,lp);
}

static HWND control(HWND parent,const wchar_t *kind,const wchar_t *label,DWORD style,int x,int y,int w,int h,int id){
 return CreateWindowW(kind,label,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),parent,(HMENU)(INT_PTR)id,pet_instance,NULL);
}
static void update_photo_controls(const wchar_t *name){
 bool loaded=draft.photo.pixels!=NULL;
 SetWindowTextW(photo_label,name?name:loaded?L"已导入角色图片":L"可导入自定义角色图片");
 EnableWindow(remove_button,loaded);
 EnableWindow(green_check,loaded);
 EnableWindow(motion_box,loaded);
 SendMessageW(green_check,BM_SETCHECK,draft.green?BST_CHECKED:BST_UNCHECKED,0);
 repaint_preview();
}
static void zoom_text(void){
 wchar_t value[40];swprintf(value,40,L"角色大小  %d%%",draft.zoom);
 SetWindowTextW(zoom_label,value);
}
static void alert(const wchar_t *message){MessageBoxW(panel?panel:pet_window,message,L"外观 DIY",MB_OK|MB_ICONWARNING);}
static void import_photo(void){
 wchar_t path[32768]=L"";
 OPENFILENAMEW picker={0};
 picker.lStructSize=sizeof(picker);picker.hwndOwner=panel;picker.lpstrFile=path;picker.nMaxFile=32768;
 picker.lpstrFilter=L"角色图片（PNG、JPG）\0*.png;*.jpg;*.jpeg\0\0";
 picker.lpstrTitle=L"导入角色图片";picker.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
 pet_yield_to_panel(panel);
 if(!GetOpenFileNameW(&picker))return;
 Appearance loaded={0};
 if(!decode_image(path,PHOTO_LIMIT,&loaded.photo)||!make_sprite(&loaded,has_green_corners(&loaded.photo))){
  state_free(&loaded);alert(L"无法读取图片。请选择 12 MB 以内、宽高不超过 8192 像素的 PNG 或 JPG。");return;
 }
 image_free(&draft.photo);image_free(&draft.sprite);
 draft.photo=loaded.photo;draft.sprite=loaded.sprite;draft.crop=loaded.crop;draft.green=loaded.green;
 const wchar_t *file=wcsrchr(path,L'\\');
 update_photo_controls(file?file+1:path);
}
static void apply_draft(void){
 if(!draft.scene.pixels){alert(L"找不到所选主题素材。请保留程序旁的 assets\\diy 文件夹。");return;}
 if(!draft.photo.pixels){
  bool loaded=draft.character==PET_CHARACTER_ORIGINAL?update_original(now_ms()):
   update_scene_model(&draft,max(64,px(360)),now_ms(),true);
  if(!loaded){alert(L"无法加载所选角色。请检查 Live2D 运行库和模型文件，或选择其他角色。");return;}
 }
 if(draft.photo.pixels&&(draft.crop.right<=draft.crop.left||draft.crop.bottom<=draft.crop.top)){
  alert(L"这张图片没有可见内容。请关闭绿幕去背，或换一张角色图片。");return;
 }
 draft.enabled=true;
 if(!save_state(&draft)){alert(L"保存外观失败，请检查存档目录是否可写。");return;}
 state_free(&current);current=draft;draft=(Appearance){0};
 DestroyWindow(panel);
 ui_theme_select(current.theme);pet_refresh_appearance();
}
static LRESULT CALLBACK panel_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
 switch(message){
 case WM_CREATE:{
  control(window,L"STATIC",L"桌宠预览",0,18,18,122,25,0);
  control(window,L"STATIC",L"拖动调位置 · 点击试动作",SS_RIGHT,148,18,230,25,0);
  preview=control(window,L"LetterPet.DiyPreview",L"角色预览",WS_TABSTOP,18,48,360,360,0);
  control(window,L"STATIC",L"场景与界面主题",0,400,18,285,25,0);
  theme_box=control(window,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,400,44,280,210,THEME_ID);
  for(int i=0;i<UI_THEME_COUNT;i++)SendMessageW(theme_box,CB_ADDSTRING,0,(LPARAM)theme_names[i]);
  SendMessageW(theme_box,CB_SETCURSEL,draft.theme,0);
  control(window,L"STATIC",L"Live2D 角色",0,400,82,280,23,0);
  character_box=control(window,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,400,106,280,180,CHARACTER_ID);
  for(int i=0;i<PET_CHARACTER_COUNT;i++)SendMessageW(character_box,CB_ADDSTRING,0,(LPARAM)character_names[i]);
  SendMessageW(character_box,CB_SETCURSEL,draft.character,0);
  photo_label=control(window,L"STATIC",L"",SS_ENDELLIPSIS,400,144,280,24,0);
  control(window,L"BUTTON",L"导入图片…",WS_TABSTOP,400,170,132,33,IMPORT_ID);
  remove_button=control(window,L"BUTTON",L"使用所选模型",WS_TABSTOP,548,170,132,33,REMOVE_ID);
  green_check=control(window,L"BUTTON",L"移除绿幕背景",WS_TABSTOP|BS_AUTOCHECKBOX,400,212,280,28,GREEN_ID);
  control(window,L"STATIC",L"仅含人物的透明 PNG 效果最佳。",0,400,247,280,26,0);
  zoom_label=control(window,L"STATIC",L"",0,400,282,280,22,0);
  zoom_track=control(window,TRACKBAR_CLASSW,L"角色大小",WS_TABSTOP|TBS_HORZ|TBS_NOTICKS,400,308,280,28,0);
  SendMessageW(zoom_track,TBM_SETRANGE,TRUE,MAKELONG(50,150));
  SendMessageW(zoom_track,TBM_SETPOS,TRUE,draft.zoom);zoom_text();
  control(window,L"STATIC",L"角色图片动作",0,400,348,280,23,0);
  motion_box=control(window,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,400,372,280,120,MOTION_ID);
  for(int i=0;i<3;i++)SendMessageW(motion_box,CB_ADDSTRING,0,(LPARAM)motion_names[i]);
  SendMessageW(motion_box,CB_SETCURSEL,draft.motion,0);
  control(window,L"BUTTON",L"恢复原版桌宠",WS_TABSTOP,18,424,150,36,RESTORE_ID);
  control(window,L"BUTTON",L"应用外观",WS_TABSTOP|BS_DEFPUSHBUTTON,536,424,144,36,APPLY_ID);
  if(!preview||!theme_box||!character_box||!motion_box||!green_check||!zoom_track||!zoom_label||!photo_label||!remove_button)return -1;
  update_photo_controls(NULL);SetTimer(window,1,33,NULL);return 0;
 }
 case WM_TIMER:
  if(!draft.photo.pixels&&!draft.model_failed&&live2d_available())repaint_preview();
  else if(draft.sprite.pixels&&(draft.motion!=MOTION_BOUNCE||draft.tap_at>0)){
   if(draft.motion==MOTION_BOUNCE&&now_ms()-draft.tap_at>=390)draft.tap_at=0;
   repaint_preview();
  }
  return 0;
 case WM_HSCROLL:
  if((HWND)lp==zoom_track){draft.zoom=(int)SendMessageW(zoom_track,TBM_GETPOS,0,0);zoom_text();repaint_preview();}
  return 0;
 case WM_COMMAND:
  if(HIWORD(wp)==CBN_SELCHANGE){
   if(LOWORD(wp)==THEME_ID){
    int theme=(int)SendMessageW(theme_box,CB_GETCURSEL,0,0);
    if(theme>=0&&theme<UI_THEME_COUNT&&theme!=draft.theme&&!load_scene(&draft,theme)){
     SendMessageW(theme_box,CB_SETCURSEL,draft.theme,0);alert(L"无法载入这个主题。请检查 assets\\diy 文件夹。");
    }
    ui_theme_preview(window,draft.theme);
   }else if(LOWORD(wp)==CHARACTER_ID){
    int character=(int)SendMessageW(character_box,CB_GETCURSEL,0,0);
    if(character>=0&&character<PET_CHARACTER_COUNT){
     draft.character=character;draft.frame_at=draft.tap_at=0;draft.model_failed=false;
     image_free(&draft.photo);image_free(&draft.sprite);draft.crop=(RECT){0};draft.green=false;
     update_photo_controls(NULL);
    }
   }else if(LOWORD(wp)==MOTION_ID){
    int motion=(int)SendMessageW(motion_box,CB_GETCURSEL,0,0);
    if(motion>=MOTION_BREATH&&motion<=MOTION_BOUNCE){draft.motion=motion;draft.tap_at=now_ms();}
   }
   repaint_preview();return 0;
  }
  if(HIWORD(wp)!=BN_CLICKED)return 0;
  switch(LOWORD(wp)){
  case IMPORT_ID:import_photo();return 0;
  case REMOVE_ID:
   image_free(&draft.photo);make_sprite(&draft,false);draft.model_failed=false;draft.frame_at=0;update_photo_controls(NULL);return 0;
  case GREEN_ID:{
   bool green=SendMessageW(green_check,BM_GETCHECK,0,0)==BST_CHECKED;
   if(!make_sprite(&draft,green)){SendMessageW(green_check,BM_SETCHECK,draft.green?BST_CHECKED:BST_UNCHECKED,0);alert(L"内存不足，未更改去背设置。");}
   repaint_preview();return 0;
  }
  case APPLY_ID:apply_draft();return 0;
  case RESTORE_ID:
   if(appearance_restore())DestroyWindow(window);else alert(L"无法保存原版设置，请检查存档目录。");
   return 0;
  case IDCANCEL:DestroyWindow(window);return 0;
  }
  break;
 case WM_CLOSE:DestroyWindow(window);return 0;
 case WM_DESTROY:
  KillTimer(window,1);state_free(&draft);image_free(&preview_pixels);
  live2d_clear_preview();
  panel=preview=NULL;dragging=false;return 0;
 }
 return DefWindowProcW(window,message,wp,lp);
}

void appearance_show(void){
 if(panel){pet_show_panel(panel,SW_RESTORE);return;}
 draft=defaults();
 draft.theme=current.theme;draft.motion=current.motion;draft.zoom=current.zoom;draft.x=current.x;draft.y=current.y;draft.green=current.green;draft.character=current.character;
 if(current.photo.pixels){
  if(!image_alloc(&draft.photo,current.photo.w,current.photo.h)){alert(L"内存不足，无法打开外观预览。");return;}
  memcpy(draft.photo.pixels,current.photo.pixels,(size_t)current.photo.w*current.photo.h*4);
 }
 if(!make_sprite(&draft,current.green)||!load_scene(&draft,draft.theme)){
  state_free(&draft);alert(L"无法打开外观预览。请保留程序旁的 assets\\diy 文件夹。");return;
 }
 HDC dc=GetDC(pet_window);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(pet_window,dc);
 WNDCLASSW wc={0};
 wc.hInstance=pet_instance;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.lpfnWndProc=preview_proc;
 wc.lpszClassName=L"LetterPet.DiyPreview";RegisterClassW(&wc);
 wc.lpfnWndProc=panel_proc;wc.lpszClassName=L"LetterPet.DiyAppearance";wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);
 DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN;
 RECT r={0,0,px(698),px(476)};AdjustWindowRectEx(&r,style,FALSE,WS_EX_CONTROLPARENT|WS_EX_APPWINDOW);
 MONITORINFO monitor={sizeof(monitor)};
 GetMonitorInfoW(MonitorFromWindow(pet_window,MONITOR_DEFAULTTONEAREST),&monitor);
 int width=r.right-r.left,height=r.bottom-r.top;
 int x=monitor.rcWork.left+max(0,(monitor.rcWork.right-monitor.rcWork.left-width)/2);
 int y=monitor.rcWork.top+max(0,(monitor.rcWork.bottom-monitor.rcWork.top-height)/2);
 panel=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_APPWINDOW,wc.lpszClassName,L"LetterPet · 外观 DIY",
                       style,x,y,width,height,NULL,NULL,pet_instance,NULL);
 if(!panel){state_free(&draft);return;}
 ui_theme_apply(panel);ui_theme_preview(panel,draft.theme);pet_show_panel(panel,SW_SHOW);
}
bool appearance_dialog_message(MSG *message){return panel&&IsDialogMessageW(panel,message)!=0;}
void appearance_close(void){
 if(panel)DestroyWindow(panel);
 state_free(&current);
 image_free(&original_frame);image_free(&scene_character_frame);image_free(&layout_frame);original_frame_at=0;
}

bool appearance_selftest(FILE *log){
 wchar_t directory[MAX_PATH+48];
 swprintf(directory,ARRAYSIZE(directory),L"%ls\\appearance-test-%lu",app_dir,GetCurrentProcessId());
 bool storage=CreateDirectoryW(directory,NULL)!=0;
 if(storage){
  swprintf(save_path,ARRAYSIZE(save_path),L"%ls\\appearance.dat",directory);
  Appearance saved=defaults(),loaded=defaults();
  saved.enabled=true;saved.theme=UI_THEME_COUNT-1;saved.character=PET_CHARACTER_SANDONE;
  storage=write_state(&saved)&&read_state(&loaded)&&loaded.enabled&&loaded.theme==saved.theme&&loaded.character==saved.character;
  state_free(&loaded);
  FILE *old=_wfopen(save_path,L"wb");
  int32_t v[]={APPEARANCE_MAGIC,0,0,0,100,57,0,0,0,0};
  if(old){
   bool written=fwrite(v,sizeof(v),1,old)==1;
   bool closed=fclose(old)==0;
   storage=storage&&written&&closed&&read_state(&loaded)&&loaded.character==PET_CHARACTER_ORIGINAL;
  }else storage=false;
  state_free(&loaded);DeleteFileW(save_path);RemoveDirectoryW(directory);save_path[0]=0;
 }
 fprintf(log,"%s: role selection survives reload and older DIY data keeps the original model.\n",storage?"PASS":"FAIL");
 bool mapping=true;unsigned count=0;
 for(int row=0;row<ARRAYSIZE(caps);row++){
  int right=0;
  for(int col=0;caps[row][col].vk;col++){
   RECT r=cap_rect(row,&caps[row][col]),located;
   mapping=mapping&&r.left>=right&&r.right>r.left&&key_rect(caps[row][col].vk,&located)&&EqualRect(&r,&located);
   right=r.right;count++;
  }
 }
 for(int button=0;button<ARRAYSIZE(mouse_contacts);button++)for(int other=0;other<ARRAYSIZE(mouse_contacts);other++)
  mapping=mapping&&(inside_mouse(mouse_contacts[button].x,mouse_contacts[button].y,other)==(button==other));
 mapping=mapping&&count==60;
 Appearance scene=defaults();
 if(mapping&&load_scene(&scene,0)&&cache_scene(&scene,480)){
  RECT key,neighbor;
  mapping=key_rect('H',&key)&&key_rect('G',&neighbor);
  if(mapping){
   int y=scene_top(480)+MulDiv((key.top+key.bottom)/2,scene_height(&scene,480),1024);
   int x=MulDiv((key.left+key.right)/2,480,1536),other=MulDiv((neighbor.left+neighbor.right)/2,480,1536);
   uint32_t before=scene.scaled.pixels[y*480+x],adjacent=scene.scaled.pixels[y*480+other];
   press_key(&scene,scene.scaled.pixels,480,scene_height(&scene,480),key,1);
   mapping=scene.scaled.pixels[y*480+x]!=before&&scene.scaled.pixels[y*480+other]==adjacent&&!key_rect(VK_F1,&key);
  }
 }else mapping=false;
 state_free(&scene);
 fprintf(log,"%s: %u distinct painted keys and three separate mouse hit regions.\n",mapping?"PASS":"FAIL",count);

 const int sizes[]={240,480,720};
 enum {max_size=720};
 uint32_t *open=malloc((size_t)max_size*max_size*4),*closed=malloc((size_t)max_size*max_size*4);
 bool models=open&&closed;
 for(int character=PET_CHARACTER_AMUN;models&&character<PET_CHARACTER_COUNT;character++){
  for(int s=0;s<ARRAYSIZE(sizes)&&models;s++){
   int size=sizes[s];Vec idle={0};
   bool rendered=live2d_draw_scene_character(character,open,size,0,100,57,0,false,false,idle,0,idle,0)&&
                 live2d_draw_scene_character(character,closed,size,0,100,57,0,true,true,idle,0,idle,0);
   unsigned visible=0,changed=0,invalid=0;
   if(rendered)for(int i=0;i<size*size;i++){
    unsigned alpha=open[i]>>24,closed_alpha=closed[i]>>24;
    visible+=alpha>20;changed+=open[i]!=closed[i];
    for(int channel=0;channel<3;channel++){
     invalid+=((open[i]>>(channel*8))&255)>alpha;
     invalid+=((closed[i]>>(channel*8))&255)>closed_alpha;
    }
   }
   models=rendered&&!invalid&&visible>500&&changed>25;
   fprintf(log,"%s: theme-ready character %d at %d px, visible=%u, blink changes=%u, invalid alpha=%u.\n",
           models?"PASS":"FAIL",character,size,visible,changed,invalid);
  }
 }
 free(open);free(closed);live2d_clear_preview();
 if(models&&renderer_resize(480)){
  const double pulses[3]={0};
  current=defaults();current.enabled=true;
  for(int character=PET_CHARACTER_AMUN;character<PET_CHARACTER_COUNT;character++){
   current.character=character;current.model_failed=false;
   const int themes[]={0,3,1,2};
   if(!load_scene(&current,themes[character]))continue;
   renderer_draw(0,now_ms(),pulses);
   wchar_t path[MAX_PATH+32];
   swprintf(path,ARRAYSIZE(path),L"%ls\\test-diy-character-%d.bmp",app_dir,character);
   renderer_bmp(path);
  }
  const RewardEquipment layout={{REWARD_LAMP,REWARD_FERN,REWARD_PRISM,REWARD_STAR,REWARD_IVORYTEA,REWARD_BOOKMARK}};
  bool themed=true;
  for(int theme=0;theme<UI_THEME_COUNT;theme++)for(int character=0;character<PET_CHARACTER_COUNT;character++){
   current.character=character;
   current.model_failed=false;
   if(!load_scene(&current,theme)){themed=false;continue;}
   renderer_draw(0,now_ms(),pulses);
   wchar_t path[MAX_PATH+64];
   swprintf(path,ARRAYSIZE(path),L"%ls\\test-theme-accessories-%d-character-%d.bmp",app_dir,theme,character);
   themed=renderer_rewards_snapshot(layout,path)&&!current.model_failed&&themed;
  }
  current.enabled=false;
  settings.live2d=true;renderer_draw(0,now_ms(),pulses);
  wchar_t original_path[MAX_PATH+64];swprintf(original_path,ARRAYSIZE(original_path),L"%ls\\test-original-accessories.bmp",app_dir);
  themed=renderer_rewards_snapshot(layout,original_path)&&themed;settings.live2d=false;
  fprintf(log,"%s: accessory layouts on all five themes, four characters and the original desk.\n",
          themed?"PASS":"FAIL");
  models=models&&themed;
  state_free(&current);image_free(&scene_character_frame);
 }else models=false;
 return storage&&models&&mapping;
}
