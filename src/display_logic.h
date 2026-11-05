#pragma once
#include <stdint.h>

namespace display_logic {
struct ClockRing {
 bool active=false;uint32_t started=0,origin=0;
 constexpr uint32_t phase(uint32_t now,uint32_t wall_phase){
  if(!active){active=true;started=now;origin=wall_phase%60000;}
  origin=(uint64_t(origin)+uint32_t(now-started))%60000;started=now;
  return origin;
 }
};
constexpr bool clockRingContinuous(){
 ClockRing ring;
 if(ring.phase(100,59000)!=59000)return false;
 if(ring.phase(600,58000)!=59500)return false;
 if(ring.phase(1100,2000)!=0)return false;
 ClockRing wrap;
 return wrap.phase(0xfffffff0U,1000)==1000 && wrap.phase(16,0)==1032;
}
static_assert(clockRingContinuous(),"clock ring ignores resync jitter, wraps at 60 seconds and handles millis rollover");
constexpr uint8_t standbyOpacity(uint32_t elapsed){
 return elapsed<=15000?0:elapsed>=15600?255:(elapsed-15000)*255/600;
}
static_assert(standbyOpacity(14999)==0&&standbyOpacity(15000)==0,"completion stays visible for 15 seconds");
static_assert(standbyOpacity(15300)==127&&standbyOpacity(15600)==255,"standby fades in over 600 ms");
struct ResetTime {uint32_t days,hours;};
constexpr ResetTime resetTime(uint32_t seconds){return {seconds/86400,seconds/3600%24};}
struct ShortResetTime {uint32_t hours,minutes;};
constexpr ShortResetTime shortResetTime(uint32_t seconds){
 const uint32_t minutes=seconds/60+(seconds%60!=0);
 return {minutes/60,minutes%60};
}
static_assert(shortResetTime(8279).hours==2 && shortResetTime(8279).minutes==18,"5-hour reset rounds up to minutes");
static_assert(shortResetTime(1).minutes==1 && shortResetTime(0).minutes==0,"last minute and reset");
constexpr float absolute(float v){return v<0?-v:v;}
constexpr int rotatedIndex(int x,int y,int w,int h,uint8_t rotation){
 return rotation==1?(w-1-x)*h+y:rotation==2?(h-1-y)*w+w-1-x:rotation==3?x*h+h-1-y:y*w+x;
}
constexpr uint8_t orientation(float x,float y,float z,uint8_t current){
 const float ax=absolute(x),ay=absolute(y),dominant=ax>ay?ax:ay,other=ax>ay?ay:ax;
 if(dominant<0.65f || dominant<other*1.35f || absolute(z)>dominant*1.4f)return current;
 // Board mounting puts +Y at the USB-down upright orientation.
 return ax>ay?(x>0?3:1):(y>0?0:2);
}
static_assert(resetTime(90061).days==1 && resetTime(90061).hours==1,"day/hour rollover");
static_assert(resetTime(86399).days==0 && resetTime(86399).hours==23,"before day rollover");
static_assert(resetTime(0).days==0 && resetTime(86400).hours==0,"zero and exact day");
static_assert(orientation(0,1,0,2)==0 && orientation(-1,0,0,0)==1,"upright and landscape");
static_assert(orientation(0,-1,0,0)==2 && orientation(1,0,0,0)==3,"inverted directions");
static_assert(orientation(0,0,1,2)==2 && orientation(.6f,.6f,0,3)==3,"flat/diagonal hold");
static_assert(rotatedIndex(0,0,2,4,1)==4 && rotatedIndex(1,3,2,4,1)==3,"90 degree rectangle corners");
static_assert(rotatedIndex(0,0,2,4,2)==7 && rotatedIndex(1,3,2,4,2)==0,"180 degree corners");
static_assert(rotatedIndex(0,0,2,4,3)==3 && rotatedIndex(1,3,2,4,3)==4,"270 degree rectangle corners");
constexpr bool rotationPreservesPixels(){
 for(uint8_t r=0;r<4;++r){bool seen[8]={};for(int y=0;y<4;++y)for(int x=0;x<2;++x){
  int i=rotatedIndex(x,y,2,4,r);if(i<0 || i>=8 || seen[i])return false;seen[i]=true;
 }}return true;
}
static_assert(rotationPreservesPixels(),"all four rotations preserve each rectangular tile pixel once");
}
