#include "ui_theme.h"
#include <commctrl.h>
#include <wctype.h>

#define PANEL_SUBCLASS 0x4c505550u
#define CONTROL_SUBCLASS 0x4c505543u
static const wchar_t panel_property[]=L"LetterPet.UI.Panel";
static const wchar_t control_property[]=L"LetterPet.UI.Control";
#define HEX(n) RGB(((n)>>16)&255,((n)>>8)&255,(n)&255)
static const UiColors themes[UI_THEME_COUNT]={
 { .background=HEX(0x191318),.surface=HEX(0x2a1d25),.input=HEX(0x21181f),.border=HEX(0x805660),
   .accent=HEX(0xe7bf7d),.ink=HEX(0xfaefe5),.muted=HEX(0xcab5b8),
   .primary=HEX(0x77323e),.on_primary=HEX(0xffedcc),.hero=HEX(0x3b2430),.highlight=HEX(0x4a2c38),
   .warning=HEX(0xffaea3),.on_accent=HEX(0x24141a),.radius=9 },
 { .background=HEX(0x111b30),.surface=HEX(0x1a2943),.input=HEX(0x142036),.border=HEX(0x526f99),
   .accent=HEX(0xaacfff),.ink=HEX(0xf0f5ff),.muted=HEX(0xb8c7e3),
   .primary=HEX(0x2f5288),.on_primary=HEX(0xf4f8ff),.hero=HEX(0x21365b),.highlight=HEX(0x2c4367),
   .warning=HEX(0xffb6bf),.on_accent=HEX(0x111b30),.radius=7 },
 { .background=HEX(0xf3e8da),.surface=HEX(0xfff7eb),.input=HEX(0xfffcf5),.border=HEX(0xa08370),
   .accent=HEX(0x8b4334),.ink=HEX(0x392822),.muted=HEX(0x665047),
   .primary=HEX(0x8b4334),.on_primary=HEX(0xfffaf2),.hero=HEX(0xf2ddca),.highlight=HEX(0xeed8c5),
   .warning=HEX(0x8c2e26),.on_accent=HEX(0xfffaf2),.radius=11,.light=true },
 { .background=HEX(0x182720),.surface=HEX(0x22372b),.input=HEX(0x1b2c23),.border=HEX(0x607e68),
   .accent=HEX(0xc4e2a4),.ink=HEX(0xf0f6e9),.muted=HEX(0xbfcebb),
   .primary=HEX(0x3e5c3d),.on_primary=HEX(0xf5f9e9),.hero=HEX(0x2b4331),.highlight=HEX(0x365141),
   .warning=HEX(0xffb5a3),.on_accent=HEX(0x182720),.radius=12 },
 { .background=HEX(0x0b0b0b),.surface=HEX(0x171717),.input=HEX(0x101010),.border=HEX(0xa0a0a0),
   .accent=HEX(0xffe45d),.ink=HEX(0xffffff),.muted=HEX(0xe1e1e1),
   .primary=HEX(0xffe45d),.on_primary=HEX(0x0b0b0b),.hero=HEX(0x171717),.highlight=HEX(0x303030),
   .warning=HEX(0xffa9ba),.on_accent=HEX(0x0b0b0b),.radius=0 }
};
#undef HEX
const UiColors *ui_colors=&themes[0];
const UiColors *ui_theme_colors(int index){return &themes[index>=0&&index<UI_THEME_COUNT?index:0];}
COLORREF ui_theme_mix(COLORREF a,COLORREF b,int weight){
 return RGB((GetRValue(a)*(100-weight)+GetRValue(b)*weight)/100,
            (GetGValue(a)*(100-weight)+GetGValue(b)*weight)/100,
            (GetBValue(a)*(100-weight)+GetBValue(b)*weight)/100);
}
typedef struct {
 HWND window;int dpi;
 HFONT body,label,title,number,value,timer;
 HBRUSH background,input;
 const UiColors *colors;
 bool preview;
} UiPanel;
typedef enum {K_STATIC,K_BUTTON,K_COMBO,K_HEADER,K_TRACK,K_EDIT,K_LIST,K_OTHER} UiKind;
typedef struct {UiPanel *panel;UiKind kind;UiRole role;bool primary,hover;} UiControl;
static bool font_checked,font_private;
static wchar_t font_path[MAX_PATH],font_face[LF_FACESIZE];
static int scale(UiPanel *p,int n){return MulDiv(n,p->dpi,96);}
static LRESULT CALLBACK control_proc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);

static int CALLBACK font_found(const LOGFONTW *lf,const TEXTMETRICW *tm,DWORD type,LPARAM data){(void)lf;(void)tm;(void)type;*(bool*)data=true;return 0;}
static bool available_font(const wchar_t *name){
 LOGFONTW lf={0};lf.lfCharSet=DEFAULT_CHARSET;lstrcpynW(lf.lfFaceName,name,LF_FACESIZE);
 bool found=false;HDC dc=GetDC(NULL);EnumFontFamiliesExW(dc,&lf,font_found,(LPARAM)&found,0);ReleaseDC(NULL,dc);return found;
}
const wchar_t *ui_theme_face(void){
 if(!font_checked){
  font_checked=true;
  const wchar_t *suffix=L"\\assets\\fonts\\MiSans-Regular.otf";
  if(wcslen(app_dir)+wcslen(suffix)<MAX_PATH){swprintf(font_path,MAX_PATH,L"%ls%ls",app_dir,suffix);font_private=AddFontResourceExW(font_path,FR_PRIVATE,NULL)>0;}
  const wchar_t *choices[]={L"MiSans",L"Noto Sans SC",L"Microsoft YaHei UI"};
  lstrcpyW(font_face,L"Microsoft YaHei UI");
  for(int i=0;i<3;i++)if(available_font(choices[i])){lstrcpynW(font_face,choices[i],LF_FACESIZE);break;}
 }
 return font_face;
}
HFONT ui_theme_font(int height,int weight,bool numeric){
 return CreateFontW(height>0?-height:height,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,numeric?L"Segoe UI":ui_theme_face());
}
void ui_theme_release(void){if(font_private){RemoveFontResourceExW(font_path,FR_PRIVATE,NULL);font_private=false;}font_checked=false;font_face[0]=0;}
void ui_theme_fill(HDC dc,const RECT *r,COLORREF color){HBRUSH b=CreateSolidBrush(color);FillRect(dc,r,b);DeleteObject(b);}
static void outline(HDC dc,RECT r,COLORREF color){HBRUSH b=CreateSolidBrush(color);FrameRect(dc,&r,b);DeleteObject(b);}
static void rounded(HDC dc,RECT r,COLORREF fill,COLORREF edge,int radius){
 if(!radius){ui_theme_fill(dc,&r,fill);outline(dc,r,edge);return;}
 HBRUSH b=CreateSolidBrush(fill);HPEN pen=CreatePen(PS_SOLID,1,edge);HGDIOBJ ob=SelectObject(dc,b),op=SelectObject(dc,pen);
 RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(pen);DeleteObject(b);
}
static void text(HDC dc,const wchar_t *s,RECT r,HFONT font,COLORREF color,UINT flags){
 HGDIOBJ old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,s,-1,&r,flags|DT_NOPREFIX);SelectObject(dc,old);
}
static void meter(HDC dc,RECT r,int value,const UiColors *colors){ui_theme_fill(dc,&r,colors->border);r.right=r.left+MulDiv(r.right-r.left,max(0,min(100,value)),100);ui_theme_fill(dc,&r,colors->accent);}
static void apply_caption(HWND window,const UiColors *colors){
 typedef HRESULT (WINAPI *DwmAttribute)(HWND,DWORD,LPCVOID,DWORD);
 HMODULE dll=LoadLibraryExW(L"dwmapi.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
 if(dll){union {FARPROC raw;DwmAttribute call;} fn;fn.raw=GetProcAddress(dll,"DwmSetWindowAttribute");if(fn.raw){BOOL dark=!colors->light;fn.call(window,20,&dark,sizeof(dark));fn.call(window,35,&colors->background,sizeof(COLORREF));fn.call(window,36,&colors->ink,sizeof(COLORREF));}FreeLibrary(dll);}
}
void ui_theme_caption(HWND window){apply_caption(window,ui_colors);}
static void native_theme(HWND window,const UiColors *colors){
 typedef HRESULT (WINAPI *SetTheme)(HWND,LPCWSTR,LPCWSTR);
 HMODULE dll=LoadLibraryExW(L"uxtheme.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
 if(dll){union{FARPROC raw;SetTheme call;}fn;fn.raw=GetProcAddress(dll,"SetWindowTheme");if(fn.raw)fn.call(window,colors->light?L"Explorer":L"DarkMode_Explorer",NULL);FreeLibrary(dll);}
}
static void free_panel(UiPanel *p){
 DeleteObject(p->body);DeleteObject(p->label);DeleteObject(p->title);DeleteObject(p->number);DeleteObject(p->value);DeleteObject(p->timer);
 DeleteObject(p->background);DeleteObject(p->input);free(p);
}
static void hero(HDC dc,HWND window,UiControl *c,RECT r){
 UiPanel *p=c->panel;wchar_t s[512],phase[160],clock[32]=L"00:00";GetWindowTextW(window,s,512);lstrcpynW(phase,s,160);
 wchar_t *line2=wcschr(s,L'\n'),*line3=line2?wcschr(line2+1,L'\n'):NULL;
 wchar_t *first_end=wcschr(phase,L'\r');if(first_end)*first_end=0;
 wchar_t *colon=wcschr(phase,L':');if(colon){wchar_t *start=colon;while(start>phase&&iswdigit(start[-1]))start--;lstrcpynW(clock,start,32);while(start>phase&&iswspace(start[-1]))start--;*start=0;}
 rounded(dc,r,p->colors->hero,p->colors->border,scale(p,p->colors->radius));int pad=scale(p,16);
 RECT top={pad,scale(p,10),r.right-pad,scale(p,35)};
 text(dc,phase,top,p->label,p->colors->accent,DT_SINGLELINE|DT_VCENTER);
 if(line2){wchar_t rounds[120];lstrcpynW(rounds,line2+1,120);wchar_t *end=wcschr(rounds,L'\r');if(end)*end=0;RECT rr=top;rr.left=r.right/2;text(dc,rounds,rr,p->label,p->colors->muted,DT_SINGLELINE|DT_RIGHT|DT_VCENTER);}
 RECT clock_rect={pad,scale(p,37),r.right-pad,r.bottom-scale(p,34)};text(dc,clock,clock_rect,p->timer,p->colors->accent,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
 RECT task_rect={pad,r.bottom-scale(p,33),r.right-pad,r.bottom-scale(p,8)};if(line3)text(dc,line3+1,task_rect,p->body,p->colors->muted,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}
static void care(HDC dc,HWND window,UiControl *c,RECT r){
 UiPanel *p=c->panel;wchar_t s[512];GetWindowTextW(window,s,512);int values[3];
 if(swscanf(s,L"心情 %d / 100     精力 %d / 100     亲密度 %d / 100",&values[0],&values[1],&values[2])!=3){text(dc,s,r,p->body,p->colors->ink,DT_WORDBREAK);return;}
 const wchar_t *labels[]={L"心情",L"精力",L"亲密度"};int column=r.right/3,gap=scale(p,17);
 for(int i=0;i<3;i++){
  int x=i*column;RECT label={x,0,x+column-gap,scale(p,21)};text(dc,labels[i],label,p->label,p->colors->muted,DT_SINGLELINE);
  wchar_t value[16];swprintf(value,16,L"%d",values[i]);RECT n={x,scale(p,21),x+column-gap,scale(p,54)};text(dc,value,n,p->value,p->colors->ink,DT_SINGLELINE|DT_VCENTER);
  HGDIOBJ old=SelectObject(dc,p->value);SIZE size;GetTextExtentPoint32W(dc,value,(int)wcslen(value),&size);SelectObject(dc,old);RECT total=n;total.left=x+size.cx+scale(p,5);text(dc,L"/ 100",total,p->label,p->colors->muted,DT_SINGLELINE|DT_BOTTOM);
  meter(dc,(RECT){x,scale(p,60),x+column-gap,scale(p,63)},values[i],p->colors);
 }
 wchar_t *note=wcschr(s,L'\n');if(note){RECT n={0,scale(p,73),r.right,r.bottom};text(dc,note+1,n,p->label,wcsstr(note,L"失败")?p->colors->warning:p->colors->muted,DT_WORDBREAK);}
}
static void paint_button(HWND window,HDC dc,UiControl *c,RECT r){
 UiPanel *p=c->panel;wchar_t label[512];GetWindowTextW(window,label,512);bool enabled=IsWindowEnabled(window)!=0;
 UINT style=(UINT)GetWindowLongPtrW(window,GWL_STYLE)&BS_TYPEMASK;UINT state=(UINT)SendMessageW(window,BM_GETSTATE,0,0);
 bool check=style==BS_CHECKBOX||style==BS_AUTOCHECKBOX||style==BS_3STATE||style==BS_AUTO3STATE||style==BS_RADIOBUTTON||style==BS_AUTORADIOBUTTON;
 ui_theme_fill(dc,&r,p->colors->background);
 if(check){
  int side=scale(p,14),x=scale(p,2),y=(r.bottom-side)/2;RECT box={x,y,x+side,y+side};bool checked=SendMessageW(window,BM_GETCHECK,0,0)!=BST_UNCHECKED;
  rounded(dc,box,checked&&enabled?p->colors->accent:p->colors->input,enabled?(c->hover?p->colors->accent:p->colors->muted):p->colors->border,scale(p,min(4,p->colors->radius)));
  if(checked){HPEN pen=CreatePen(PS_SOLID,max(1,scale(p,2)),enabled?p->colors->on_accent:p->colors->muted);HGDIOBJ old=SelectObject(dc,pen);MoveToEx(dc,x+side/5,y+side/2,NULL);LineTo(dc,x+side*2/5,y+side*4/5);LineTo(dc,x+side*4/5,y+side/5);SelectObject(dc,old);DeleteObject(pen);}
  RECT t={box.right+scale(p,8),0,r.right,r.bottom};text(dc,label,t,p->body,enabled?p->colors->ink:p->colors->muted,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
 }else{
  RECT box=r;InflateRect(&box,-1,-1);COLORREF bg=c->primary?p->colors->primary:p->colors->surface;
  if(c->hover&&enabled)bg=c->primary?ui_theme_mix(bg,p->colors->on_primary,8):p->colors->highlight;
  if(state&BST_PUSHED)bg=ui_theme_mix(bg,p->colors->background,13);
  if(!enabled)bg=ui_theme_mix(p->colors->surface,p->colors->background,40);
  COLORREF edge=enabled&&(c->primary||c->hover)?p->colors->accent:p->colors->border;
  rounded(dc,box,bg,edge,scale(p,p->colors->radius));
  InflateRect(&box,-scale(p,8),0);text(dc,label,box,c->primary?p->label:p->body,enabled?(c->primary?p->colors->on_primary:p->colors->ink):ui_theme_mix(p->colors->muted,p->colors->surface,32),DT_SINGLELINE|DT_CENTER|DT_VCENTER|DT_END_ELLIPSIS);
 }
 if(GetFocus()==window&&!(SendMessageW(window,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)){RECT focus=r;InflateRect(&focus,-scale(p,4),-scale(p,4));SetTextColor(dc,p->colors->accent);DrawFocusRect(dc,&focus);}
}
static void paint_header(HWND window,HDC dc,UiControl *c,RECT r){
 UiPanel *p=c->panel;ui_theme_fill(dc,&r,p->colors->highlight);
 for(int i=0;i<Header_GetItemCount(window);i++){
  RECT cell;Header_GetItemRect(window,i,&cell);wchar_t value[256];HDITEMW item={0};item.mask=HDI_TEXT|HDI_FORMAT;item.pszText=value;item.cchTextMax=256;
  if(!Header_GetItem(window,i,&item)||cell.right-cell.left<scale(p,18))continue;
  cell.left+=scale(p,9);cell.right-=scale(p,8);UINT align=(item.fmt&HDF_RIGHT)?DT_RIGHT:(item.fmt&HDF_CENTER)?DT_CENTER:DT_LEFT;
  text(dc,value,cell,p->label,p->colors->accent,align|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
 }
 RECT line={0,r.bottom-1,r.right,r.bottom};ui_theme_fill(dc,&line,p->colors->border);
}
static void paint_combo(HWND window,HDC dc,UiControl *c,RECT r){
 UiPanel *p=c->panel;ui_theme_fill(dc,&r,p->colors->background);rounded(dc,r,p->colors->input,c->hover||GetFocus()==window?p->colors->accent:p->colors->border,scale(p,p->colors->radius));wchar_t value[512];GetWindowTextW(window,value,512);RECT t=r;t.left+=scale(p,10);t.right-=scale(p,24);text(dc,value,t,p->body,IsWindowEnabled(window)?p->colors->ink:p->colors->muted,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
 int x=r.right-scale(p,13),y=r.bottom/2;HPEN pen=CreatePen(PS_SOLID,max(1,scale(p,1)),p->colors->accent);HGDIOBJ old=SelectObject(dc,pen);MoveToEx(dc,x-scale(p,4),y-scale(p,2),NULL);LineTo(dc,x,y+scale(p,2));LineTo(dc,x+scale(p,4),y-scale(p,2));SelectObject(dc,old);DeleteObject(pen);
 if(GetFocus()==window){RECT f=r;InflateRect(&f,-scale(p,4),-scale(p,4));DrawFocusRect(dc,&f);}
}
static void paint_track(HWND window,HDC dc,UiControl *c,RECT r){
 UiPanel *p=c->panel;ui_theme_fill(dc,&r,p->colors->background);RECT channel,thumb;SendMessageW(window,TBM_GETCHANNELRECT,0,(LPARAM)&channel);SendMessageW(window,TBM_GETTHUMBRECT,0,(LPARAM)&thumb);
 int x=(thumb.left+thumb.right)/2,y=(r.bottom-r.top)/2;if(channel.right<=channel.left)channel=(RECT){scale(p,9),y-2,r.right-scale(p,9),y+2};
 RECT line={channel.left,y-scale(p,2),channel.right,y+scale(p,2)};ui_theme_fill(dc,&line,p->colors->border);line.right=max(line.left,min(line.right,x));ui_theme_fill(dc,&line,IsWindowEnabled(window)?p->colors->accent:p->colors->muted);
 int radius=scale(p,6);HBRUSH b=CreateSolidBrush(IsWindowEnabled(window)?p->colors->accent:p->colors->border);HGDIOBJ ob=SelectObject(dc,b),op=SelectObject(dc,GetStockObject(NULL_PEN));Ellipse(dc,x-radius,y-radius,x+radius,y+radius);SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(b);
 if(GetFocus()==window){RECT f=r;InflateRect(&f,-1,-1);DrawFocusRect(dc,&f);}
}
static void paint_control(HWND window,HDC dc,UiControl *c){
 RECT r;GetClientRect(window,&r);if(c->kind==K_BUTTON){paint_button(window,dc,c,r);return;}if(c->kind==K_HEADER){paint_header(window,dc,c,r);return;}if(c->kind==K_COMBO){paint_combo(window,dc,c,r);return;}if(c->kind==K_TRACK){paint_track(window,dc,c,r);return;}
 ui_theme_fill(dc,&r,c->panel->colors->background);
 if(c->role==UI_FOCUS_HERO){hero(dc,window,c,r);return;}if(c->role==UI_CARE_METRICS){care(dc,window,c,r);return;}
 wchar_t value[1024];GetWindowTextW(window,value,1024);
 if(c->role==UI_READOUT){
  rounded(dc,r,c->panel->colors->highlight,c->panel->colors->border,scale(c->panel,c->panel->colors->radius));
  InflateRect(&r,-scale(c->panel,10),0);
  LONG_PTR align=GetWindowLongPtrW(window,GWL_STYLE)&SS_TYPEMASK;
  text(dc,value,r,c->panel->label,c->panel->colors->accent,DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|(align==SS_CENTER?DT_CENTER:DT_LEFT));
  return;
 }
 if(c->role==UI_SURFACE_TEXT){rounded(dc,r,c->panel->colors->surface,c->panel->colors->border,scale(c->panel,c->panel->colors->radius));InflateRect(&r,-scale(c->panel,10),-scale(c->panel,5));}
 if(c->role==UI_TITLE){
  RECT rule={r.left,r.top+scale(c->panel,5),r.left+scale(c->panel,3),r.bottom-scale(c->panel,5)};
  ui_theme_fill(dc,&rule,c->panel->colors->accent);r.left+=scale(c->panel,11);
  text(dc,value,r,c->panel->title,c->panel->colors->ink,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
 }else text(dc,value,r,c->panel->label,c->panel->colors->ink,DT_WORDBREAK);
}
static void paint_buffered_control(HWND window,HDC dc,UiControl *c){
 RECT r;GetClientRect(window,&r);
 if(r.right<=0||r.bottom<=0)return;

 HDC buffer=CreateCompatibleDC(dc);
 HBITMAP bitmap=buffer?CreateCompatibleBitmap(dc,r.right,r.bottom):NULL;
 HGDIOBJ old=bitmap?SelectObject(buffer,bitmap):NULL;
 bool copied=false;
 if(old&&old!=HGDI_ERROR){
  paint_control(window,buffer,c);
  copied=BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY)!=0;
  SelectObject(buffer,old);
 }
 if(bitmap)DeleteObject(bitmap);
 if(buffer)DeleteDC(buffer);
 if(!copied)paint_control(window,dc,c);
}
static LRESULT CALLBACK control_proc(HWND window,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
 UiControl *c=(UiControl*)data;bool custom=c->kind==K_BUTTON||c->kind==K_COMBO||c->kind==K_HEADER||c->kind==K_TRACK||c->role!=UI_NORMAL;
 bool buffered=custom;
 if(msg==WM_NCDESTROY){RemovePropW(window,control_property);RemoveWindowSubclass(window,control_proc,id);free(c);return DefSubclassProc(window,msg,wp,lp);}
 if(msg==WM_SETTEXT&&(c->kind==K_BUTTON||(c->kind==K_STATIC&&c->role!=UI_NORMAL))){
  // Keep the native window text, but defer painting to the buffered WM_PAINT path.
  LRESULT result=DefWindowProcW(window,msg,wp,lp);
  if(result)InvalidateRect(window,NULL,FALSE);
  return result;
 }
 if(msg==WM_ERASEBKGND&&custom)return 1;
 if(msg==WM_PAINT&&custom){
  PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);
  if(dc){if(buffered)paint_buffered_control(window,dc,c);else paint_control(window,dc,c);}
  EndPaint(window,&ps);return 0;
 }
 if(msg==WM_PRINTCLIENT&&custom){paint_control(window,(HDC)wp,c);return 0;}
 if(msg==WM_CTLCOLORLISTBOX||msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORSTATIC){SetTextColor((HDC)wp,c->panel->colors->ink);SetBkColor((HDC)wp,c->panel->colors->input);return (LRESULT)c->panel->input;}
 if(msg==WM_MOUSEMOVE&&!c->hover){c->hover=true;TRACKMOUSEEVENT t={sizeof(t),TME_LEAVE,window,0};TrackMouseEvent(&t);InvalidateRect(window,NULL,FALSE);}
 if(msg==WM_MOUSELEAVE){c->hover=false;InvalidateRect(window,NULL,FALSE);}
 LRESULT result=DefSubclassProc(window,msg,wp,lp);
 if(IsWindow(window)&&(msg==WM_SETTEXT||msg==WM_ENABLE||msg==WM_SETFOCUS||msg==WM_KILLFOCUS||msg==BM_SETCHECK||msg==BM_SETSTATE||msg==WM_LBUTTONDOWN||msg==WM_LBUTTONUP||msg==WM_KEYDOWN||msg==WM_KEYUP||msg==CB_SETCURSEL||msg==TBM_SETPOS||msg==WM_UPDATEUISTATE))InvalidateRect(window,NULL,FALSE);
 if(IsWindow(window)&&(c->kind==K_EDIT||c->kind==K_LIST)&&
    (GetWindowLongPtrW(window,GWL_STYLE)&WS_BORDER)&&
    (msg==WM_NCPAINT||msg==WM_SETFOCUS||msg==WM_KILLFOCUS)){
  HDC dc=GetWindowDC(window);RECT r;GetWindowRect(window,&r);OffsetRect(&r,-r.left,-r.top);
  if(dc){outline(dc,r,GetFocus()==window?c->panel->colors->accent:c->panel->colors->border);ReleaseDC(window,dc);}
 }
 return result;
}
static LRESULT list_draw(NMLVCUSTOMDRAW *draw,UiPanel *p){
 HWND list=draw->nmcd.hdr.hwndFrom;if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
 if(draw->nmcd.dwDrawStage!=CDDS_ITEMPREPAINT)return CDRF_DODEFAULT;
 int index=(int)draw->nmcd.dwItemSpec;RECT row;ListView_GetItemRect(list,index,&row,LVIR_BOUNDS);bool selected=(ListView_GetItemState(list,index,LVIS_SELECTED)&LVIS_SELECTED)!=0;
 COLORREF row_color=selected?p->colors->primary:(index%2?ui_theme_mix(p->colors->input,p->colors->surface,55):p->colors->input);
 ui_theme_fill(draw->nmcd.hdc,&row,row_color);
 if(selected){RECT mark={row.left,row.top+1,row.left+scale(p,3),row.bottom-1};ui_theme_fill(draw->nmcd.hdc,&mark,p->colors->accent==p->colors->primary?p->colors->on_primary:p->colors->accent);}
 HWND header=ListView_GetHeader(list);int count=Header_GetItemCount(header);
 for(int col=0;col<count;col++){
  RECT cell;Header_GetItemRect(header,col,&cell);MapWindowPoints(header,list,(POINT*)&cell,2);cell.top=row.top;cell.bottom=row.bottom;
  if(ListView_GetExtendedListViewStyle(list)&LVS_EX_GRIDLINES){RECT line={cell.right-1,cell.top,cell.right,cell.bottom};ui_theme_fill(draw->nmcd.hdc,&line,p->colors->border);}
  wchar_t value[1024];ListView_GetItemText(list,index,col,value,1024);cell.left+=scale(p,8);cell.right-=scale(p,8);LVCOLUMNW column={0};column.mask=LVCF_FMT;ListView_GetColumn(list,col,&column);UINT align=(column.fmt&LVCFMT_RIGHT)?DT_RIGHT:(column.fmt&LVCFMT_CENTER)?DT_CENTER:DT_LEFT;
  text(draw->nmcd.hdc,value,cell,iswdigit(value[0])?p->number:p->body,selected?p->colors->on_primary:p->colors->ink,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|align);
 }
 RECT line={row.left,row.bottom-1,row.right,row.bottom};ui_theme_fill(draw->nmcd.hdc,&line,ui_theme_mix(row_color,p->colors->border,60));
 if(selected&&GetFocus()==list&&!(SendMessageW(list,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)){RECT f=row;InflateRect(&f,-1,-1);DrawFocusRect(draw->nmcd.hdc,&f);}
 return CDRF_SKIPDEFAULT;
}
static LRESULT CALLBACK panel_proc(HWND window,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
 UiPanel *p=(UiPanel*)data;
 if(msg==WM_NCDESTROY){RemovePropW(window,panel_property);RemoveWindowSubclass(window,panel_proc,id);LRESULT result=DefSubclassProc(window,msg,wp,lp);free_panel(p);return result;}
 if(msg==WM_ERASEBKGND){RECT r;GetClientRect(window,&r);FillRect((HDC)wp,&r,p->background);return 1;}
 if(msg==WM_CTLCOLORSTATIC||msg==WM_CTLCOLORBTN||msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORLISTBOX){
  HDC dc=(HDC)wp;HWND child=(HWND)lp;
  UiControl *c=(UiControl*)GetPropW(child,control_property);
  bool input=msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORLISTBOX||(c&&c->kind==K_EDIT);
  SetTextColor(dc,IsWindowEnabled(child)?p->colors->ink:p->colors->muted);
  SetBkColor(dc,input?p->colors->input:p->colors->background);
  return (LRESULT)(input?p->input:p->background);
 }
 if(msg==WM_NOTIFY){NMHDR *n=(NMHDR*)lp;wchar_t name[64];GetClassNameW(n->hwndFrom,name,64);if(n->code==NM_CUSTOMDRAW&&!_wcsicmp(name,WC_LISTVIEWW))return list_draw((NMLVCUSTOMDRAW*)lp,p);}
 return DefSubclassProc(window,msg,wp,lp);
}
static void bind_control(HWND window,UiPanel *p){
 if(GetPropW(window,control_property))return;
 UiControl *c=calloc(1,sizeof(*c));if(!c)return;c->panel=p;wchar_t name[64],value[128];GetClassNameW(window,name,64);GetWindowTextW(window,value,128);
 if(!_wcsicmp(name,L"STATIC"))c->kind=K_STATIC;else if(!_wcsicmp(name,L"BUTTON"))c->kind=K_BUTTON;else if(!_wcsicmp(name,L"COMBOBOX"))c->kind=K_COMBO;else if(!_wcsicmp(name,WC_HEADERW))c->kind=K_HEADER;else if(!_wcsicmp(name,TRACKBAR_CLASSW))c->kind=K_TRACK;else if(!_wcsicmp(name,L"EDIT")||!_wcsicmp(name,HOTKEY_CLASSW))c->kind=K_EDIT;else if(!_wcsicmp(name,WC_LISTVIEWW))c->kind=K_LIST;else c->kind=K_OTHER;
 int control_id=GetDlgCtrlID(window);c->primary=control_id==401||control_id==501||control_id==603||control_id==701||control_id==802||control_id==901||control_id==903||control_id==1001||control_id==1005||control_id==2002||((GetWindowLongPtrW(window,GWL_STYLE)&BS_TYPEMASK)==BS_DEFPUSHBUTTON&&c->kind==K_BUTTON)||!wcscmp(value,L"打开课程表");
 const wchar_t *headings[]={L"专注陪伴",L"任务与学习统计",L"桌面行为与快捷键",L"应用白名单",L"选择应用",L"课程提醒",L"桌宠预览",L"场景与界面主题",L"登录桌宠",L"我的账号"};
 if(c->kind==K_STATIC)for(int i=0;i<ARRAYSIZE(headings);i++)if(!wcscmp(value,headings[i])){c->role=UI_TITLE;break;}
 if(!SetWindowSubclass(window,control_proc,CONTROL_SUBCLASS,(DWORD_PTR)c)){free(c);return;}
 if(!SetPropW(window,control_property,(HANDLE)c)){RemoveWindowSubclass(window,control_proc,CONTROL_SUBCLASS);free(c);return;}
 bool numeric=c->kind==K_EDIT&&((GetWindowLongPtrW(window,GWL_STYLE)&ES_NUMBER)||control_id==1032||control_id==1033||!_wcsicmp(name,HOTKEY_CLASSW));
 HFONT font=c->role==UI_TITLE?p->title:numeric?p->number:c->kind==K_STATIC||c->kind==K_HEADER?p->label:p->body;SendMessageW(window,WM_SETFONT,(WPARAM)font,FALSE);
 if(c->kind==K_LIST){ListView_SetBkColor(window,p->colors->input);ListView_SetTextBkColor(window,p->colors->input);ListView_SetTextColor(window,p->colors->ink);native_theme(window,p->colors);HWND header=ListView_GetHeader(window);if(header)bind_control(header,p);}
 if(!_wcsicmp(name,L"EDIT"))SendMessageW(window,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(scale(p,7),scale(p,7)));
 if(c->kind==K_EDIT)native_theme(window,p->colors);
 if(c->kind==K_COMBO){COMBOBOXINFO info={sizeof(info)};if(GetComboBoxInfo(window,&info)&&info.hwndList){SendMessageW(info.hwndList,WM_SETFONT,(WPARAM)p->body,FALSE);native_theme(info.hwndList,p->colors);}}
}
static BOOL CALLBACK bind_child(HWND window,LPARAM value){UiPanel *p=(UiPanel*)value;if(GetParent(window)==p->window)bind_control(window,p);return TRUE;}
void ui_theme_apply(HWND window){
 if(!window||GetPropW(window,panel_property))return;
 UiPanel *p=calloc(1,sizeof(*p));if(!p)return;p->window=window;p->colors=ui_colors;HDC dc=GetDC(window);p->dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(window,dc);
 p->body=ui_theme_font(scale(p,14),FW_NORMAL,false);p->label=ui_theme_font(scale(p,13),FW_MEDIUM,false);p->title=ui_theme_font(scale(p,19),FW_SEMIBOLD,false);p->number=ui_theme_font(scale(p,14),FW_NORMAL,true);p->value=ui_theme_font(scale(p,24),FW_NORMAL,true);p->timer=ui_theme_font(scale(p,60),FW_NORMAL,true);
 p->background=CreateSolidBrush(p->colors->background);p->input=CreateSolidBrush(p->colors->input);
 if(!p->body||!p->label||!p->title||!p->number||!p->value||!p->timer||!p->background||!p->input||!SetWindowSubclass(window,panel_proc,PANEL_SUBCLASS,(DWORD_PTR)p)){free_panel(p);return;}
 if(!SetPropW(window,panel_property,(HANDLE)p)){RemoveWindowSubclass(window,panel_proc,PANEL_SUBCLASS);free_panel(p);return;}
 EnumChildWindows(window,bind_child,(LPARAM)p);ui_theme_caption(window);RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
}
void ui_theme_role(HWND window,UiRole role){UiControl *c=window?(UiControl*)GetPropW(window,control_property):NULL;if(c){c->role=role;if(role==UI_TITLE)SendMessageW(window,WM_SETFONT,(WPARAM)c->panel->title,FALSE);InvalidateRect(window,NULL,FALSE);}}

static BOOL CALLBACK refresh_control(HWND window,LPARAM value){
 UiPanel *p=(UiPanel*)value;
 UiControl *c=(UiControl*)GetPropW(window,control_property);
 if(!c||c->panel!=p)return TRUE;
 if(c->kind==K_LIST){
  ListView_SetBkColor(window,p->colors->input);ListView_SetTextBkColor(window,p->colors->input);ListView_SetTextColor(window,p->colors->ink);
  native_theme(window,p->colors);
 }else if(c->kind==K_EDIT)native_theme(window,p->colors);
 else if(c->kind==K_COMBO){
  COMBOBOXINFO info={sizeof(info)};
  if(GetComboBoxInfo(window,&info)&&info.hwndList)native_theme(info.hwndList,p->colors);
 }
 return TRUE;
}
static void set_panel_colors(UiPanel *p,const UiColors *colors){
 if(p->colors==colors)return;
 HBRUSH background=CreateSolidBrush(colors->background),input=CreateSolidBrush(colors->input);
 if(!background||!input){DeleteObject(background);DeleteObject(input);return;}
 DeleteObject(p->background);DeleteObject(p->input);
 p->background=background;p->input=input;p->colors=colors;
 apply_caption(p->window,colors);
 EnumChildWindows(p->window,refresh_control,(LPARAM)p);
 RedrawWindow(p->window,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN);
}
void ui_theme_preview(HWND window,int index){
 UiPanel *p=window?(UiPanel*)GetPropW(window,panel_property):NULL;
 if(p){p->preview=true;set_panel_colors(p,ui_theme_colors(index));}
}
static BOOL CALLBACK refresh_panel(HWND window,LPARAM unused){
 (void)unused;
 UiPanel *p=(UiPanel*)GetPropW(window,panel_property);
 if(p){if(!p->preview)set_panel_colors(p,ui_colors);}
 else if(window!=pet_window&&((GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_APPWINDOW)||
         (pet_window&&GetWindow(window,GW_OWNER)==pet_window))){
  ui_theme_caption(window);
  RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
 }
 return TRUE;
}
void ui_theme_select(int index){
 const UiColors *next=ui_theme_colors(index);
 if(next==ui_colors)return;
 ui_colors=next;EnumThreadWindows(GetCurrentThreadId(),refresh_panel,0);
}

bool ui_theme_selftest(FILE *log){
 bool ok=true;DWORD baseline=0,after=0;
 for(int pass=0;pass<4;pass++){
  HWND host=CreateWindowW(L"STATIC",L"主题检查",WS_POPUP,0,0,400,300,NULL,NULL,pet_instance,NULL);
  HWND edit=CreateWindowW(L"EDIT",L"中文 😀",WS_CHILD|WS_VISIBLE|WS_BORDER,10,10,200,30,host,(HMENU)1,pet_instance,NULL);
  HWND check=CreateWindowW(L"BUTTON",L"检查复选框",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,10,50,250,30,host,(HMENU)2,pet_instance,NULL);
  HWND combo=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|CBS_DROPDOWNLIST,10,90,250,100,host,(HMENU)3,pet_instance,NULL);
  HWND list=CreateWindowW(WC_LISTVIEWW,L"",WS_CHILD|LVS_REPORT,10,130,250,100,host,(HMENU)4,pet_instance,NULL);
  LVCOLUMNW column={0};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=L"内容";column.cx=230;ListView_InsertColumn(list,0,&column);LVITEMW row={0};row.mask=LVIF_TEXT;row.pszText=L"中文项目";ListView_InsertItem(list,&row);
  SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)L"第一项");SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)L"第二项");SendMessageW(combo,CB_SETCURSEL,1,0);
  ui_theme_apply(host);ui_theme_apply(host);SendMessageW(check,BM_CLICK,0,0);wchar_t value[64];GetWindowTextW(edit,value,64);
  ok=ok&&host&&edit&&check&&combo&&list&&!wcscmp(value,L"中文 😀")&&SendMessageW(check,BM_GETCHECK,0,0)==BST_CHECKED&&SendMessageW(combo,CB_GETCURSEL,0,0)==1&&ListView_GetItemCount(list)==1;
  HDC dc=GetDC(edit);HFONT font=(HFONT)SendMessageW(edit,WM_GETFONT,0,0);HGDIOBJ old=SelectObject(dc,font);wchar_t face[64];GetTextFaceW(dc,64,face);SelectObject(dc,old);ReleaseDC(edit,dc);ok=ok&&!_wcsicmp(face,ui_theme_face());
  EnableWindow(edit,FALSE);ok=ok&&!IsWindowEnabled(edit);DestroyWindow(host);after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);if(pass==0)baseline=after;
 }
 bool handles=after<=baseline+2;
 fprintf(log,"%s: themed native Unicode controls preserve checkbox, combo, list and enabled state; font=%ls\n",ok?"PASS":"FAIL",ui_theme_face());
 fprintf(log,"%s: theme repeated creation and destruction releases GDI resources (%lu -> %lu)\n",handles?"PASS":"FAIL",baseline,after);

 HWND host=CreateWindowW(L"STATIC",L"",WS_POPUP,0,0,320,200,NULL,NULL,pet_instance,NULL);
 HWND list=host?CreateWindowW(WC_LISTVIEWW,L"",WS_CHILD|LVS_REPORT,0,0,240,100,host,NULL,pet_instance,NULL):NULL;
 ui_theme_apply(host);
 const UiColors *saved=ui_colors;
 int previous=0;
 for(int i=0;i<UI_THEME_COUNT;i++)if(ui_theme_colors(i)==saved)previous=i;
 bool switching=host&&list;
 for(int i=0;i<UI_THEME_COUNT&&switching;i++){
  ui_theme_select(i);
  switching=ListView_GetBkColor(list)==ui_theme_colors(i)->input;
 }
 ui_theme_select(previous);
 baseline=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 bool preview=switching;
 for(int i=0;i<UI_THEME_COUNT&&preview;i++){
  ui_theme_preview(host,i);
  preview=ListView_GetBkColor(list)==ui_theme_colors(i)->input&&ui_colors==saved;
 }
 after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 if(host)DestroyWindow(host);
 fprintf(log,"%s: all five themes refresh native controls, including isolated previews\n",switching&&preview?"PASS":"FAIL");
 fprintf(log,"%s: theme switching keeps GDI handle count stable (%lu -> %lu)\n",after<=baseline+2?"PASS":"FAIL",baseline,after);
 return ok&&handles&&switching&&preview&&after<=baseline+2;
}
