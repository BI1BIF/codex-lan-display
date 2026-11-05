#include "loading_ring.h"
#include <Arduino.h>
#include <cmath>

namespace {
struct Pixel {uint16_t position;uint8_t coverage;};
struct Part {int x,y,w,h;lv_obj_t* obj;lv_color_t* colors;Pixel* path;};
Part parts[]={{60,15,360,4},{60,461,360,4},{15,60,4,360},{461,60,4,360},
              {15,15,45,45},{420,15,45,45},{420,420,45,45},{15,420,45,45}};
// Preserve the outer edge; widen inward, with strip centers at 17 / 463.
constexpr float pi=3.14159265f, radius=43.f, quarter=pi*radius/2, perimeter=1440+4*quarter;
bool ready=false,shown=false;
int previous=-1;
lv_color_t palette[4096];
bool white_palette=false;

lv_color_t mix(uint32_t a,uint32_t b,float p){
 int r=int(((a>>16)&255)*(1-p)+((b>>16)&255)*p);
 int g=int(((a>>8)&255)*(1-p)+((b>>8)&255)*p);
 int blue=int((a&255)*(1-p)+(b&255)*p);
 return lv_color_make(r,g,blue);
}
}

namespace {
void makePalette(bool clock){
 for(int i=0;i<4096;++i){
  // A cyan head, blue/violet tail and a quiet blue track, continuous at wraparound.
  float tail=i/4096.f;float strength=0;
  if(tail<.035f)strength=tail/.035f;
  else if(tail<.22f)strength=1;
  else if(tail<.55f)strength=(.55f-tail)/.33f;
  // Interpolate color separately to avoid visible color steps along the trail.
  lv_color_t tint=tail<.22f?mix(0x9af7ff,0x579eff,fminf(1,tail/.22f)):mix(0x579eff,0x9278ff,fminf(1,(tail-.22f)/.33f));
  palette[i]=lv_color_mix(clock?lv_color_hex(0xf5f8f6):tint,lv_color_hex(clock?0x202725:0x101f39),uint8_t(255*strength));
 }
 white_palette=clock;
}
}
void loadingRingCreate(lv_obj_t* parent){
 makePalette(false);
 for(int n=0;n<8;++n){auto& p=parts[n];size_t size=p.w*p.h;
  p.colors=static_cast<lv_color_t*>(heap_caps_malloc(size*sizeof(lv_color_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  p.path=static_cast<Pixel*>(heap_caps_malloc(size*sizeof(Pixel),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!p.colors||!p.path)return;
  for(int y=0;y<p.h;++y)for(int x=0;x<p.w;++x){
   float gx=p.x+x+.5f,gy=p.y+y+.5f,distance=0,coverage=1;
   if(n==0)distance=gx-60;
   else if(n==1)distance=720+2*quarter+420-gx;
   else if(n==2)distance=1080+3*quarter+420-gy;
   else if(n==3)distance=360+quarter+gy-60;
   else{
    int corner=n-4;float cx=(corner==0||corner==3)?60:420,cy=corner<2?60:420;
    float dx=gx-cx,dy=gy-cy,angle=atan2f(dy,dx);if(angle<0)angle+=2*pi;
    // Supersample coverage only at creation; keep the same 4 px stroke at seams.
    int inside=0;
    for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx){
     float px=dx+(sx+.5f)/4-.5f,py=dy+(sy+.5f)/4-.5f;
     float r=sqrtf(px*px+py*py);if(r>=radius-2.f&&r<=radius+2.f)++inside;
    }
    coverage=inside/16.f;
    if(corner==0)distance=1440+3*quarter+(angle-pi)*radius;
    else if(corner==1)distance=360+(angle-1.5f*pi)*radius;
    else if(corner==2)distance=720+quarter+angle*radius;
    else distance=1080+2*quarter+(angle-.5f*pi)*radius;
   }
   p.path[y*p.w+x]={uint16_t(int(distance*4096/perimeter)&4095),uint8_t(coverage*255)};
  }
  p.obj=lv_canvas_create(parent);lv_obj_remove_style_all(p.obj);lv_obj_set_pos(p.obj,p.x,p.y);
  lv_canvas_set_buffer(p.obj,p.colors,p.w,p.h,LV_IMG_CF_TRUE_COLOR);
  lv_obj_clear_flag(p.obj,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(p.obj,LV_OBJ_FLAG_HIDDEN);
 }
 ready=true;
}

void loadingRingFrame(bool visible,uint32_t phase_ms,bool clock){
 if(!ready)return;
 if(shown!=visible){for(auto& p:parts){if(visible)lv_obj_clear_flag(p.obj,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(p.obj,LV_OBJ_FLAG_HIDDEN);}shown=visible;previous=-1;}
 if(!visible)return;
 if(white_palette!=clock){makePalette(clock);previous=-1;}
 const uint32_t period=clock?60000:1800;
 int head=((phase_ms%period)*4096/period+(clock?int(180*4096/perimeter):0))&4095;
 if(head==previous)return;previous=head;
 const auto background=lv_color_hex(0x050807);
 for(auto& p:parts){for(int i=0;i<p.w*p.h;++i){auto& sample=p.path[i];p.colors[i]=sample.coverage?
   lv_color_mix(palette[(head-sample.position+4096)&4095],background,sample.coverage):background;}
  lv_obj_invalidate(p.obj);
 }
}
