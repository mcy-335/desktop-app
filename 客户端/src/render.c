#include "pet.h"
#include "appearance.h"
#include "rewards.h"
static Image scenes[8],arms[2],objects[3],glows[3],cache[8],reward_sprites[REWARD_ITEM_COUNT];
static Image desk_row;
static HDC dc;static HBITMAP bitmap,old_bitmap;static uint32_t *pixels;static int size;static double scale;
static const double rects[3][4]={{.172,.247,.105,.132},{.85,.355,.092,.135},{.791,.682,.102,.135}};
static bool load_image(Image *im,const wchar_t *name){wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\assets\\%ls.bgra",app_dir,name);FILE *f=_wfopen(path,L"rb");if(!f)return false;uint32_t dims[2];bool ok=fread(dims,4,2,f)==2&&dims[0]>0&&dims[0]<=4096&&dims[1]>0&&dims[1]<=4096;if(ok){im->w=dims[0];im->h=dims[1];size_t n=(size_t)im->w*im->h;im->pixels=malloc(n*4);ok=im->pixels&&fread(im->pixels,4,n,f)==n;}fclose(f);return ok;}
static uint32_t sample(const Image *im,double x,double y){
 if(x<0||y<0||x>im->w-1||y>im->h-1)return 0;
 int ix=(int)x,iy=(int)y,jx=ix+1<im->w?ix+1:ix,jy=iy+1<im->h?iy+1:iy;double u=x-ix,v=y-iy;
 uint32_t p[4]={im->pixels[iy*im->w+ix],im->pixels[iy*im->w+jx],im->pixels[jy*im->w+ix],im->pixels[jy*im->w+jx]},out=0;
 for(int k=0;k<4;k++){int shift=8*k;double a=(p[0]>>shift)&255,b=(p[1]>>shift)&255,c=(p[2]>>shift)&255,d=(p[3]>>shift)&255;out|=(uint32_t)(a*(1-u)*(1-v)+b*u*(1-v)+c*(1-u)*v+d*u*v+.5)<<shift;}return out;
}
static void over(int x,int y,uint32_t p){if(x<0||y<0||x>=size||y>=size)return;unsigned a=p>>24;if(!a)return;uint32_t *d=&pixels[y*size+x];if(a==255){*d=p;return;}uint32_t out=0;for(int k=0;k<4;k++){unsigned v=((p>>(k*8))&255)+(((*d>>(k*8))&255)*(255-a)+127)/255;out|=(v>255?255:v)<<(k*8);}*d=out;}
static void image_at(const Image *im,double x,double y,double w,double h,double opacity){int x0=(int)floor(x),y0=(int)floor(y),x1=(int)ceil(x+w),y1=(int)ceil(y+h);for(int py=y0;py<y1;py++)for(int px=x0;px<x1;px++){uint32_t p=sample(im,((px+.5-x)/w)*im->w-.5,((py+.5-y)/h)*im->h-.5);if(opacity<1){uint32_t n=0;for(int k=0;k<4;k++)n|=(uint32_t)(((p>>(k*8))&255)*opacity)<<(k*8);p=n;}over(px,py,p);}}
static Vec board(Vec p){return (Vec){(700-p.x)*scale,(985-.03*p.x-p.y)*scale};}
static uint32_t rgb(double r,double g,double b){return 0xff000000u|((uint32_t)clampd(r,0,255)<<16)|((uint32_t)clampd(g,0,255)<<8)|(uint32_t)clampd(b,0,255);}
static void round_key(double x,double y,double w,double h,double radius,uint32_t bottom,uint32_t top){
 Vec a=board((Vec){x,y}),b=board((Vec){x+w,y+h});int x0=(int)floor(b.x),x1=(int)ceil(a.x),y0=(int)floor(b.y),y1=(int)ceil(a.y);
 for(int py=y0;py<=y1;py++)for(int px=x0;px<=x1;px++){
  double lx=700-(px+.5)/scale-x,ly=985-.03*(700-(px+.5)/scale)-(py+.5)/scale-y;if(lx<0||ly<0||lx>w||ly>h)continue;
  double cx=clampd(lx,radius,w-radius),cy=clampd(ly,radius,h-radius);if(hypot(lx-cx,ly-cy)>radius)continue;
  double t=ly/h;uint32_t c=0xff000000;for(int k=0;k<3;k++)c|=(uint32_t)((((bottom>>(k*8))&255)*(1-t))+(((top>>(k*8))&255)*t))<<(8*k);over(px,py,c);
 }
}
static void line(Vec a,Vec b,uint32_t color){a=board(a);b=board(b);double len=hypot(b.x-a.x,b.y-a.y);int steps=(int)ceil(len*2);for(int i=0;i<=steps;i++){double t=steps?(double)i/steps:0;over((int)(a.x+(b.x-a.x)*t),(int)(a.y+(b.y-a.y)*t),color);}}
static void draw_board(void){
 round_key(-8,-8,473.5,151.7,6,0xff2c1818,0xff341d1d);round_key(-8,-5,473.5,148.7,6,0xff94704b,0xff94704b);round_key(-7,-4,471.5,146.7,5,0xff4c2d29,0xff54322c);round_key(-2,-2,461.5,139.7,3,0xff342525,0xff342525);
 for(int i=0;i<model.count;i++){Key *k=&model.keys[i];double a=k->amount,height=3-2.3*a,y=k->y+height,tone=1-.16*a;
  round_key(k->x-.5,k->y-.5,k->w+1,k->h+1,2.8,0xff251d1e,0xff251d1e);
  round_key(k->x,k->y,k->w,k->h+height,2.5,rgb(118*tone,95*tone,76*tone),rgb(156*tone,130*tone,101*tone));
  round_key(k->x,y,k->w,k->h,2.5,rgb(200*tone,181*tone,151*tone),rgb(232*tone,219*tone,196*tone));
  Vec c={k->x+k->w/2,y+k->h*.58};line((Vec){k->x+4,y+5},c,0xffb29d7f);line(c,(Vec){k->x+k->w-4,y+5},0xffb29d7f);
  if(k->vk==VK_SPACE){line((Vec){k->x+4,y+k->h-4},c,0xffb29d7f);line(c,(Vec){k->x+k->w-4,y+k->h-4},0xffb29d7f);round_key(c.x-2.4,c.y-2.4,4.8,4.8,2.4,rgb(99*tone,46*tone,45*tone),rgb(153*tone,77*tone,66*tone));}
 }
}
typedef struct {Vec pos,uv;} Vertex;
static double edge(Vec a,Vec b,Vec p){return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);}
static bool top_left(Vec a,Vec b){return b.y<a.y||(b.y==a.y&&b.x>a.x);}
static void triangle(const Image *im,Vertex a,Vertex b,Vertex c){
 double area=edge(a.pos,b.pos,c.pos);if(fabs(area)<1e-8)return;if(area<0){Vertex t=b;b=c;c=t;area=-area;}
 int x0=(int)fmax(0,floor(fmin(a.pos.x,fmin(b.pos.x,c.pos.x)))),x1=(int)fmin(size-1,ceil(fmax(a.pos.x,fmax(b.pos.x,c.pos.x))));
 int y0=(int)fmax(0,floor(fmin(a.pos.y,fmin(b.pos.y,c.pos.y)))),y1=(int)fmin(size-1,ceil(fmax(a.pos.y,fmax(b.pos.y,c.pos.y))));
 for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){Vec p={x+.5,y+.5};double u=edge(b.pos,c.pos,p),v=edge(c.pos,a.pos,p),w=edge(a.pos,b.pos,p);
  if((u>0||(u==0&&top_left(b.pos,c.pos)))&&(v>0||(v==0&&top_left(c.pos,a.pos)))&&(w>0||(w==0&&top_left(a.pos,b.pos))))over(x,y,sample(im,(u*a.uv.x+v*b.uv.x+w*c.uv.x)/area,(u*a.uv.y+v*b.uv.y+w*c.uv.y)/area));
 }
}
static void draw_arm(bool mouse){
 int x0=mouse?384:144,y0=mouse?112:480,x1=mouse?928:732,y1=mouse?1184:1116,step=mouse?16:12;
 int cols=(x1-x0+step-1)/step+1,rows=(y1-y0+step-1)/step+1;Vertex verts[5000];if(cols*rows>5000)return;
 Pose p=mouse?mouse_pose(&model):keyboard_pose(model.tip);
 for(int r=0;r<rows;r++)for(int c=0;c<cols;c++){Vec q={x0+c*step,y0+r*step},s=skin_point(q,&p,mouse);verts[r*cols+c]=(Vertex){{s.x*scale,s.y*scale},q};}
 for(int r=0;r<rows-1;r++)for(int c=0;c<cols-1;c++){int i=r*cols+c;triangle(&arms[mouse],verts[i],verts[i+1],verts[i+cols]);triangle(&arms[mouse],verts[i+1],verts[i+cols+1],verts[i+cols]);}
}
static void accessory_over(uint32_t *dest,uint32_t color);
static bool card_accessory(int item){
 return item==REWARD_STAR||item==REWARD_HEART||item==REWARD_MOON||item==REWARD_BONSAI||item==REWARD_TEA||item==REWARD_BOOKMARK;
}
static void prepare_accessory(Image *image,bool card){
 if(!image->pixels)return;
 int left=image->w,top=image->h,right=0,bottom=0;
 for(int y=0;y<image->h;y++)for(int x=0;x<image->w;x++)if((image->pixels[y*image->w+x]>>24)>8){
  left=min(left,x);top=min(top,y);right=max(right,x+1);bottom=max(bottom,y+1);
 }
 if(right<=left||bottom<=top)return;
 Image crop={right-left,bottom-top,NULL};crop.pixels=malloc((size_t)crop.w*crop.h*4);if(!crop.pixels)return;
 for(int y=0;y<crop.h;y++)memcpy(crop.pixels+y*crop.w,image->pixels+(y+top)*image->w+left,(size_t)crop.w*4);
 free(image->pixels);*image=crop;if(!card)return;
 Image paper={176,236,calloc(176*236,4)};if(!paper.pixels)return;
 for(int y=3;y<233;y++)for(int x=3;x<173;x++){
  double dx=fmax(fmax(9-x,x-166),0),dy=fmax(fmax(9-y,y-226),0);
  if(dx*dx+dy*dy>36)continue;
  uint32_t color=0xfff4e8d0;
  if(x<5||x>170||y<5||y>230)color=0xffb1874d;
  else if(x==12||x==163||y==14||y==221)color=0xffcfb88c;
  paper.pixels[y*176+x]=color;
 }
 double factor=fmin(128.0/crop.w,154.0/crop.h),w=crop.w*factor,h=crop.h*factor;
 for(int y=0;y<236;y++)for(int x=0;x<176;x++){
  uint32_t color=sample(&crop,(x+.5-(176-w)/2)/factor-.5,(y+.5-(236-h)/2)/factor-.5);
  if(color>>24)accessory_over(&paper.pixels[y*176+x],color);
  if((abs(x-88)+abs(y-25)<=4)||(abs(x-88)+abs(y-211)<=4))paper.pixels[y*176+x]=0xffb78c4b;
 }
 free(image->pixels);*image=paper;
}
static void load_rewards(void){
 for(RewardAccessory item=REWARD_STAR;item<REWARD_ITEM_COUNT;item++){
  wchar_t path[MAX_PATH+64];
  swprintf(path,ARRAYSIZE(path),L"%ls\\assets\\rewards\\%ls.png",app_dir,rewards_sprite_name(item));
  appearance_load_sprite(path,&reward_sprites[item]);
  prepare_accessory(&reward_sprites[item],card_accessory(item));
 }
}
bool renderer_init(void){wchar_t name[32];for(int i=0;i<8;i++){swprintf(name,32,L"scene%d",i);if(!load_image(&scenes[i],name))return false;}if(!load_image(&arms[0],L"keyboard-arm")||!load_image(&arms[1],L"mouse-arm"))return false;for(int i=0;i<3;i++){swprintf(name,32,L"object%d",i);if(!load_image(&objects[i],name))return false;swprintf(name,32,L"glow%d",i);if(!load_image(&glows[i],name))return false;}load_rewards();return renderer_resize(settings.size);}
bool renderer_resize(int n){
 uint32_t *row=malloc((size_t)n*4);if(!row)return false;
 free(desk_row.pixels);desk_row=(Image){n,1,row};
 if(bitmap){SelectObject(dc,old_bitmap);DeleteObject(bitmap);DeleteDC(dc);bitmap=NULL;dc=NULL;}
 size=n;scale=n/SCENE_SIZE;BITMAPINFO bi={0};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=n;bi.bmiHeader.biHeight=-n;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
 dc=CreateCompatibleDC(NULL);bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,(void**)&pixels,NULL,0);if(!dc||!bitmap)return false;old_bitmap=SelectObject(dc,bitmap);
 for(int i=0;i<8;i++){free(cache[i].pixels);cache[i]=(Image){n,n,malloc((size_t)n*n*4)};if(!cache[i].pixels)return false;for(int y=0;y<n;y++)for(int x=0;x<n;x++)cache[i].pixels[y*n+x]=sample(&scenes[i],(x+.5)/scale-.5,(y+.5)/scale-.5);}
 return true;
}
typedef struct {double x,y,width,height,tilt;} RewardPosition;
// Six hanging stations in canvas space. Table scaling must never move these stations.
static const RewardPosition reward_positions[REWARD_SLOT_COUNT]={
 [REWARD_SLOT_LAMP]={.200,.230,.075,.135,-.10},
 [REWARD_SLOT_BONSAI]={.840,.160,.082,.105,.09},
 [REWARD_SLOT_HOURGLASS]={.160,.600,.054,.095,-.09},
 [REWARD_SLOT_PLAQUE]={.100,.415,.080,.110,-.14},
 [REWARD_SLOT_TEA]={.925,.335,.087,.100,.11},
 [REWARD_SLOT_BOOKMARK]={.920,.600,.078,.100,.13}
};
static RewardPosition reward_position(int slot,bool diy,int character){
 RewardPosition pos=reward_positions[slot];
 if(diy&&character==PET_CHARACTER_AMUN){
  // The hat extends across the upper left; the raven remains below the right-hand stations.
  if(slot==REWARD_SLOT_LAMP){pos.x=.085;pos.y=.240;}
  if(slot==REWARD_SLOT_BONSAI){pos.x=.920;pos.y=.200;}
  if(slot==REWARD_SLOT_PLAQUE){pos.x=.075;pos.y=.430;}
  if(slot==REWARD_SLOT_TEA){pos.x=.940;pos.y=.425;}
  if(slot==REWARD_SLOT_BOOKMARK){pos.x=.935;pos.y=.580;}
 }
 return pos;
}
static void accessory_over(uint32_t *dest,uint32_t color){
 unsigned alpha=color>>24;uint32_t out=0;
 for(int channel=0;channel<4;channel++){
  unsigned value=((color>>(channel*8))&255)+(((*dest>>(channel*8))&255)*(255-alpha)+127)/255;
  out|=min(value,255)<<(channel*8);
 }
 *dest=out;
}
static void hanging_cord(uint32_t *target,int n,double ax,double ay,double bx,double by){
 double dx=bx-ax,dy=by-ay,length=dx*dx+dy*dy;
 for(int y=max(0,(int)floor(fmin(ay,by)-1));y<min(n,(int)ceil(fmax(ay,by)+1));y++)
  for(int x=max(0,(int)floor(fmin(ax,bx)-1));x<min(n,(int)ceil(fmax(ax,bx)+1));x++){
   double t=length?clampd(((x+.5-ax)*dx+(y+.5-ay)*dy)/length,0,1):0;
   double distance=hypot(x+.5-ax-t*dx,y+.5-ay-t*dy);
   if(distance<.85){unsigned a=(unsigned)(150*(1-distance/.85));
    accessory_over(&target[y*n+x],(a<<24)|((210*a/255)<<16)|((178*a/255)<<8)|(115*a/255));
   }
  }
}
void renderer_draw_accessories(uint32_t *target,int n,RewardEquipment equipment,bool diy,int character){
 double clock=test_mode?0:now_ms()/1000;
 for(int slot=0;slot<REWARD_SLOT_COUNT;slot++){
  RewardAccessory item=equipment.item[slot];
  if(item<=REWARD_NONE||item>=REWARD_ITEM_COUNT)continue;
  const Image *sprite=&reward_sprites[item];if(!sprite->pixels)continue;
  RewardPosition pos=reward_position(slot,diy,character);
  double factor=fmin(pos.width*n/sprite->w,pos.height*n/sprite->h);
  double width=sprite->w*factor,height=sprite->h*factor;
  double wave=clock?sin(clock*.85+slot*1.6):0;
  double angle=pos.tilt+wave*.035,c=cos(angle),s=sin(angle);
  double cx=pos.x*n+wave*n*.003,cy=pos.y*n-height/2+(clock?sin(clock*.65+slot)*n*.003:0);
  double rx=(fabs(c)*width+fabs(s)*height)/2,ry=(fabs(s)*width+fabs(c)*height)/2;
  hanging_cord(target,n,pos.x*n,pos.y*n-height-n*.025,cx+s*height/2,cy-c*height/2);
  for(int y=max(0,(int)floor(cy-ry-1));y<min(n,(int)ceil(cy+ry+1));y++)
   for(int x=max(0,(int)floor(cx-rx-1));x<min(n,(int)ceil(cx+rx+1));x++){
    double dx=x+.5-cx,dy=y+.5-cy;
    uint32_t color=sample(sprite,(c*dx+s*dy)/factor+sprite->w/2.0-.5,(-s*dx+c*dy)/factor+sprite->h/2.0-.5);
    if(color>>24)accessory_over(&target[y*n+x],color);
   }
 }
}
static void draw_rewards(RewardEquipment equipment,bool diy){renderer_draw_accessories(pixels,size,equipment,diy,appearance_character());}
bool renderer_rewards_snapshot(RewardEquipment equipment,const wchar_t *path){
 draw_rewards(equipment,appearance_active());
 return renderer_bmp(path);
}

bool renderer_rewards_selftest(FILE *log){
 RewardEquipment all={{REWARD_LAMP,REWARD_BONSAI,REWARD_HOURGLASS,REWARD_STAR,REWARD_TEA,REWARD_BOOKMARK}};
 uint32_t *saved=pixels;
 const int saved_size=size;
 const int sizes[]={240,480,720};
 bool ok=true;
 for(int mode=0;mode<3&&ok;mode++)for(int n=0;n<ARRAYSIZE(sizes)&&ok;n++){
  size=sizes[n];
  pixels=calloc((size_t)size*size,sizeof(*pixels));
  if(!pixels){ok=false;break;}
  int character=mode==2?PET_CHARACTER_AMUN:PET_CHARACTER_ORIGINAL;
  renderer_draw_accessories(pixels,size,all,mode!=0,character);
  for(int slot=0;slot<REWARD_SLOT_COUNT;slot++){
   RewardPosition p=reward_position(slot,mode!=0,character);
   double base=p.y,x_center=p.x;
   int seen=0;
   for(int y=(int)((base-p.height)*size);y<(int)(base*size);y++)
    for(int x=(int)((x_center-p.width/2)*size);x<(int)((x_center+p.width/2)*size);x++)
     seen+=(pixels[y*size+x]>>24)>128;
   ok=ok&&seen>12&&p.x-p.width/2>0&&p.x+p.width/2<1&&p.y-p.height>0&&p.y<1;
   for(int other=0;other<slot;other++){
    RewardPosition q=reward_position(other,mode!=0,character);
    ok=ok&&(p.x+p.width/2<=q.x-q.width/2||q.x+q.width/2<=p.x-p.width/2||p.y<=q.y-q.height||q.y<=p.y-p.height);
   }
  }
  free(pixels);
 }
 pixels=saved;size=saved_size;
 for(int i=REWARD_STAR;i<REWARD_ITEM_COUNT;i++)ok=ok&&reward_sprites[i].pixels;
 fprintf(log,"%s: six rendered accessory positions in original and DIY layouts at three sizes.\n",ok?"PASS":"FAIL");
 return ok;
}
static void widen_original_desk(void){
 for(int y=(int)ceil(size*.63);y<size;y++){
  uint32_t *row=pixels+y*size;memcpy(desk_row.pixels,row,(size_t)size*4);
  for(int x=0;x<size;x++){
   double position=(x+.5)/size;
   if(position>=.13&&position<=.89)continue;
   row[x]=sample(&desk_row,appearance_desk_x(position,false,true)*size-.5,0);
  }
 }
}
void renderer_draw(int frame,double now,const double pulses[3]){
 bool diy=appearance_draw(pixels,size,now);
 if(!diy&&!live2d_draw(pixels,size,now)){
 memcpy(pixels,cache[frame%8].pixels,(size_t)size*size*4);draw_board();draw_arm(false);draw_arm(true);
 for(int i=0;i<3;i++){double t=(now-pulses[i])/850;if(pulses[i]<=0||t<0||t>=1)continue;double a=sin(PI*t),k=1+.08*a,x=rects[i][0]*size,y=rects[i][1]*size,w=rects[i][2]*size,h=rects[i][3]*size;
  image_at(&glows[i],x-20*scale,y-20*scale,w+40*scale,h+40*scale,a*.9);image_at(&objects[i],x-w*(k-1)/2,y-h*(k-1)/2,w*k,h*k,1);
 }
 }
 if(!diy)widen_original_desk();
 draw_rewards(rewards_equipment(),diy);
}
void renderer_present(HWND w){RECT r;GetWindowRect(w,&r);POINT dst={r.left,r.top},src={0,0};SIZE s={size,size};BLENDFUNCTION blend={AC_SRC_OVER,0,255,AC_SRC_ALPHA};UpdateLayeredWindow(w,NULL,&dst,&s,dc,&src,0,&blend,ULW_ALPHA);}
bool renderer_hit(int x,int y){return x>=0&&y>=0&&x<size&&y<size&&(pixels[y*size+x]>>24)>20;}
int collectible_hit(int x,int y){for(int i=0;i<3;i++){double px=(double)x/size,py=(double)y/size;if(px>=rects[i][0]&&px<rects[i][0]+rects[i][2]&&py>=rects[i][1]&&py<rects[i][1]+rects[i][3])return i;}return -1;}
bool renderer_bmp(const wchar_t *path){FILE *f=_wfopen(path,L"wb");if(!f)return false;BITMAPFILEHEADER bf={0};BITMAPINFOHEADER bi={0};bf.bfType=0x4d42;bf.bfOffBits=sizeof(bf)+sizeof(bi);bf.bfSize=bf.bfOffBits+size*size*4;bi.biSize=sizeof(bi);bi.biWidth=size;bi.biHeight=-size;bi.biPlanes=1;bi.biBitCount=32;bi.biSizeImage=size*size*4;bool ok=fwrite(&bf,sizeof(bf),1,f)==1&&fwrite(&bi,sizeof(bi),1,f)==1&&fwrite(pixels,4,(size_t)size*size,f)==(size_t)size*size;fclose(f);return ok;}
static void release_render_image(Image *image){free(image->pixels);*image=(Image){0};}
void renderer_free(void){
 live2d_free();
 release_render_image(&desk_row);
 if(dc&&bitmap)SelectObject(dc,old_bitmap);
 if(bitmap)DeleteObject(bitmap);
 if(dc)DeleteDC(dc);
 dc=NULL;bitmap=old_bitmap=NULL;pixels=NULL;
 for(int i=REWARD_STAR;i<REWARD_ITEM_COUNT;i++)release_render_image(&reward_sprites[i]);
 for(int i=0;i<8;i++){release_render_image(&scenes[i]);release_render_image(&cache[i]);}
 for(int i=0;i<2;i++)release_render_image(&arms[i]);
 for(int i=0;i<3;i++){release_render_image(&objects[i]);release_render_image(&glows[i]);}
}
