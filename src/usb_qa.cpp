#include "usb_qa.h"
#include "ui.h"
#include "sound.h"
#include <Arduino.h>
#include <mbedtls/base64.h>

namespace {
bool active=false,pressed=false;
int touch_x=240,touch_y=220;
lv_color_t* mirror=nullptr;
uint32_t last_command=0,tiles=0,bad_alignment=0,bad_bounds=0,phase=500;
lv_disp_rot_t original_rotation=LV_DISP_ROT_NONE;
void finish(){
 if(!active)return;
 active=false;pressed=false;uiHome();uiTestPhase(false,0);lv_indev_reset(nullptr,nullptr);
 lv_disp_set_rotation(nullptr,original_rotation);free(mirror);mirror=nullptr;
}
void refresh(){lv_obj_invalidate(lv_scr_act());lv_refr_now(nullptr);}
void capture(){
 uint32_t checksum=2166136261U;auto* bytes=reinterpret_cast<const uint8_t*>(mirror);
 for(size_t i=0;i<480*480*2;++i)checksum=(checksum^bytes[i])*16777619U;
 Serial.printf("@FRAME 480 480 RGB565LE %u %08lx\n",lv_disp_get_rotation(nullptr)*90,(unsigned long)checksum);
}
void captureRow(unsigned row){
 auto* bytes=reinterpret_cast<const uint8_t*>(mirror);
 char line[1320];
  int prefix=snprintf(line,sizeof(line),"@ROW %u ",row);size_t length=0;
  int result=mbedtls_base64_encode(reinterpret_cast<unsigned char*>(line+prefix),sizeof(line)-prefix-2,&length,bytes+row*960,960);
  if(result){Serial.println("@ERROR base64");return;}
  line[prefix+length]='\n';Serial.write(reinterpret_cast<uint8_t*>(line),prefix+length+1);
}
void command(const char* line){
 if(!strcmp(line,"QA POWER")){usbQaPowerReport();return;}
 if(!strcmp(line,"QA BEGIN")){
  if(!active){mirror=static_cast<lv_color_t*>(heap_caps_calloc(480*480,sizeof(lv_color_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
   if(!mirror){Serial.println("@ERROR memory");return;}
   original_rotation=lv_disp_get_rotation(nullptr);active=true;pressed=false;tiles=bad_alignment=bad_bounds=0;
  }
  last_command=millis();phase=500;uiHome();uiTestPhase(true,phase);refresh();Serial.println("@OK BEGIN");return;
 }
 if(!active){Serial.println("@ERROR begin-required");return;}
 last_command=millis();
 if(!strcmp(line,"QA END")){finish();Serial.println("@OK END");return;}
 if(!strcmp(line,"QA STATUS")){
  auto audio=soundStatus();
  Serial.printf("@STATUS rotation=%u settings=%d alignment=%lu bounds=%lu tiles=%lu heap=%u brightness=%d volume=%d audio_ready=%d audio_busy=%d audio_plays=%lu audio_errors=%lu audio_bytes=%lu\n",lv_disp_get_rotation(nullptr)*90,uiSettingsVisible(),(unsigned long)bad_alignment,(unsigned long)bad_bounds,(unsigned long)tiles,ESP.getFreeHeap(),uiBrightnessValue(),audio.volume,audio.ready,audio.busy,(unsigned long)audio.plays,(unsigned long)audio.errors,(unsigned long)audio.bytes);return;
 }
 if(!strcmp(line,"QA CAPTURE")){capture();return;}
 if(!strcmp(line,"QA HOME")){uiHome();refresh();Serial.println("@OK HOME");return;}
 int value=0,x=0,y=0,down=0,percent=0,charging=0,present=0;
 // Host requests one row at a time so USB/Windows queues cannot overflow during a full frame.
 if(sscanf(line,"QA ROW %d",&value)==1 && value>=0 && value<480){captureRow(value);return;}
 if(sscanf(line,"QA ROTATE %d",&value)==1 && value>=0 && value<=270 && value%90==0){
  pressed=false;lv_indev_reset(nullptr,nullptr);lv_disp_set_rotation(nullptr,static_cast<lv_disp_rot_t>(value/90));refresh();Serial.printf("@OK ROTATE %d\n",value);return;
 }
 if(sscanf(line,"QA PHASE %d",&value)==1 && value>=0 && value<=60000){phase=value;uiTestPhase(true,phase);lv_refr_now(nullptr);Serial.println("@OK PHASE");return;}
 unsigned age=0,epoch=0;
 if(sscanf(line,"QA STANDBY %u %u",&age,&epoch)==2){uiTestStandby(age,epoch);refresh();Serial.println("@OK STANDBY");return;}
 if(sscanf(line,"QA TOUCH %d %d %d",&x,&y,&down)==3 && x>=0 && x<480 && y>=0 && y<480 && (down==0 || down==1)){
  touch_x=x;touch_y=y;pressed=down;Serial.println("@OK TOUCH");return;
 }
 char state[16]={};
 if(sscanf(line,"QA AUDIO %15s",state)==1){
  if(!strcmp(state,"reset")){soundReset();Serial.println("@OK AUDIO");return;}
  if(strcmp(state,"running")&&strcmp(state,"waiting")&&strcmp(state,"done")&&strcmp(state,"idle")&&strcmp(state,"error")&&strcmp(state,"offline")){Serial.println("@ERROR audio-state");return;}
  LanState scene;scene.wifi=scene.linked=scene.tasks_valid=strcmp(state,"offline")!=0;strlcpy(scene.state,state,sizeof(scene.state));soundUpdate(scene);Serial.println("@OK AUDIO");return;
 }
 unsigned reset_seconds=6*86400+23*3600;
 if(sscanf(line,"QA SCENE %15s %d %d %d %u",state,&percent,&charging,&present,&reset_seconds)>=4 && percent>=-1 && percent<=100 && (charging==0 || charging==1) && (present==0 || present==1)){
  if(strcmp(state,"running")&&strcmp(state,"waiting")&&strcmp(state,"done")&&strcmp(state,"idle")&&strcmp(state,"error")&&strcmp(state,"offline")&&strcmp(state,"syncing")&&strcmp(state,"setup")){Serial.println("@ERROR state");return;}
  LanState scene;scene.wifi=scene.linked=scene.metrics_valid=scene.credits_valid=scene.tasks_valid=true;
  scene.utc_offset=28800;
  scene.remaining=99;scene.credits=1;scene.running=!strcmp(state,"running");scene.waiting=!strcmp(state,"waiting");
  scene.reset_seconds=reset_seconds;scene.received=millis();strlcpy(scene.state,state,sizeof(scene.state));
  if(!strcmp(state,"offline"))scene.linked=false;
  if(!strcmp(state,"syncing"))scene.tasks_valid=false;
  if(!strcmp(state,"setup")){scene.wifi=scene.linked=false;scene.portal=true;strlcpy(scene.ap,"Codex-Setup-TEST",sizeof(scene.ap));strlcpy(scene.password,"000000000000",sizeof(scene.password));}
  strlcpy(scene.ssid,"Example Wi-Fi",sizeof(scene.ssid));strlcpy(scene.ip,"192.0.2.20",sizeof(scene.ip));strlcpy(scene.host,"192.0.2.10",sizeof(scene.host));
  uiHome();uiUpdate(scene);uiBattery(percent,charging,present);uiTestPhase(true,phase);refresh();Serial.println("@OK SCENE");return;
 }
 Serial.println("@ERROR command");
}
}
bool usbQaActive(){return active;}
void usbQaPoll(){
 static char line[96];static unsigned used=0;static bool overflow=false;
 while(Serial.available()){
  char c=Serial.read();if(c=='\r')continue;
  if(c=='\n'){line[used]=0;if(!overflow && used && !strncmp(line,"QA ",3))command(line);used=0;overflow=false;}
  else if(used<sizeof(line)-1)line[used++]=c;else overflow=true;
 }
 if(active && millis()-last_command>120000){finish();Serial.println("@TIMEOUT END");}
}
bool usbQaTouch(lv_indev_data_t* data){
 if(!active)return false;data->point.x=touch_x;data->point.y=touch_y;data->state=pressed?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;return true;
}
void usbQaRecord(int x,int y,int w,int h,const lv_color_t* pixels){
 if(!active)return;++tiles;
 if((x|y|w|h)&1)++bad_alignment;
 if(x<0 || y<0 || w<=0 || h<=0 || x+w>480 || y+h>480){++bad_bounds;return;}
 for(int row=0;row<h;++row)memcpy(mirror+(y+row)*480+x,pixels+row*w,w*sizeof(lv_color_t));
}
