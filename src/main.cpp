#include <Arduino.h>
#include "board_config.h"
#include <Arduino_GFX_Library.h>
#include <TouchDrvCSTXXX.hpp>
#include <SensorQMI8658.hpp>
#include <Wire.h>
#include <XPowersLib.h>
#include <lvgl.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_intr_alloc.h>
#include <hal/gpio_ll.h>
#include <soc/gpio_struct.h>
#include <soc/interrupts.h>
#include "lan_bridge.h"
#include "ui.h"
#include "display_logic.h"
#include "usb_qa.h"
#include "sound.h"

namespace {
Arduino_DataBus* bus=new Arduino_ESP32QSPI(board::kLcdChipSelect,board::kLcdClock,board::kLcdSdio0,board::kLcdSdio1,board::kLcdSdio2,board::kLcdSdio3);
Arduino_CO5300* display=new Arduino_CO5300(bus,board::kLcdReset,0,480,480,0,0,0,0);
TouchDrvCST92xx touch;
SensorQMI8658 imu;
XPowersPMU power;
Preferences prefs;
lv_disp_draw_buf_t draw_buffer;
lv_color_t* rotated_pixels=nullptr;
volatile bool touch_pending=false;
intr_handle_t touch_interrupt=nullptr;
bool touch_ready=false,power_ready=false,screen_on=true,imu_ready=false,touch_pressed=false;
int brightness=70,saved_brightness=70;
int saved_volume=45;
uint32_t volume_changed=0;
uint32_t brightness_changed=0,last_ui=0,last_battery=0;
bool boot_down=false,key_down=false;
uint32_t boot_pressed=0;
uint32_t rendered_frames=0,render_ms=0,max_render_ms=0;
void monitor(lv_disp_drv_t*,uint32_t time,uint32_t){++rendered_frames;render_ms+=time;if(time>max_render_ms)max_render_ms=time;}
void orientation(uint32_t now){
 static uint32_t last_sample=0,candidate_since=0,last_log=0;
 static uint8_t rotation=0,candidate=0;
 static float x=0,y=0,z=0;static bool first=true;
 if(usbQaActive() || !imu_ready || now-last_sample<50)return;
 last_sample=now;IMUdata acc;
 if(!imu.getDataReady() || !imu.getAccelerometer(acc.x,acc.y,acc.z))return;
 if(first){x=acc.x;y=acc.y;z=acc.z;first=false;}
 else{x+=.3f*(acc.x-x);y+=.3f*(acc.y-y);z+=.3f*(acc.z-z);}
 uint8_t next=display_logic::orientation(x,y,z,rotation);
 if(next!=candidate || touch_pressed){candidate=next;candidate_since=now;}
 if(next!=rotation && now-candidate_since>=500 && !touch_pressed){
  rotation=next;lv_indev_reset(nullptr,nullptr);lv_disp_set_rotation(nullptr,static_cast<lv_disp_rot_t>(rotation));
  Serial.printf("[display] rotation=%u accel=%.2f,%.2f,%.2f\n",rotation*90,x,y,z);
 }
 if(now-last_log>=15000 && now<65000){last_log=now;Serial.printf("[imu] accel=%.2f,%.2f,%.2f rotation=%u\n",x,y,z,rotation*90);}
}
void setBrightness(int value){brightness=constrain(value,10,100);brightness_changed=millis();if(screen_on)display->setBrightness(brightness*255/100);}
void setVolume(int value){soundVolume(value);volume_changed=millis();}
void IRAM_ATTR onTouch(void*){gpio_ll_clear_intr_status(&GPIO,1U<<board::kTouchInterrupt);touch_pending=true;}
bool startTouchInterrupt(){
 // Register on this core directly: the core 3.3.8 GPIO service uses an undersized IPC stack.
 gpio_set_intr_type(static_cast<gpio_num_t>(board::kTouchInterrupt),GPIO_INTR_NEGEDGE);
 if(esp_intr_alloc(ETS_GPIO_INTR_SOURCE,ESP_INTR_FLAG_IRAM|ESP_INTR_FLAG_LEVEL1,onTouch,nullptr,&touch_interrupt)!=ESP_OK)return false;
 gpio_ll_clear_intr_status(&GPIO,1U<<board::kTouchInterrupt);
 gpio_ll_intr_enable_on_core(&GPIO,xPortGetCoreID(),board::kTouchInterrupt);
 return true;
}
void flush(lv_disp_drv_t* drv,const lv_area_t* area,lv_color_t* pixels){
 int w=lv_area_get_width(area),h=lv_area_get_height(area),x=area->x1,y=area->y1;
 const uint8_t rotation=drv->rotated;
 if(rotation){
  // Rotate the whole aligned tile; LVGL's smaller rotation chunks can have odd widths on this panel.
  for(int sy=0;sy<h;++sy)for(int sx=0;sx<w;++sx)rotated_pixels[display_logic::rotatedIndex(sx,sy,w,h,rotation)]=pixels[sy*w+sx];
  pixels=rotated_pixels;
  if(rotation==1){x=area->y1;y=479-area->x2;}
  else if(rotation==2){x=479-area->x2;y=479-area->y2;}
  else{x=479-area->y2;y=area->x1;}
  if(rotation!=2){int old_w=w;w=h;h=old_w;}
 }
 static bool warned=false;
 if(((x|y|w|h)&1)&&!warned){Serial.printf("[display] unaligned tile %d,%d %dx%d\n",x,y,w,h);warned=true;}
 usbQaRecord(x,y,w,h,pixels);
 display->draw16bitRGBBitmap(x,y,reinterpret_cast<uint16_t*>(pixels),w,h);lv_disp_flush_ready(drv);
}
void roundArea(lv_disp_drv_t*,lv_area_t* a){a->x1&=~1;a->y1&=~1;a->x2|=1;a->y2|=1;}
uint32_t last_screen_activity=0;
bool battery_only=false, auto_blanked=false;
void setScreen(bool on,bool automatic=false){
 auto_blanked=!on&&automatic;
 if(on)last_screen_activity=millis();
 if(on==screen_on)return;
 screen_on=on;
 if(on){display->displayOn();display->setBrightness(brightness*255/100);lv_obj_invalidate(lv_scr_act());}
 else{display->setBrightness(0);display->displayOff();}
}
void screenPower(uint32_t now){
 static uint32_t last_poll=0;
 if(!power_ready||now-last_poll<100)return;
 last_poll=now;
 const bool on_battery=power.isBatteryConnect()&&!power.isVbusGood()&&power.isDischarge();
 if(on_battery!=battery_only){
  battery_only=on_battery;last_screen_activity=now;
  if(!on_battery&&auto_blanked)setScreen(true);
 }
 if(power.getIrqStatus()){
  const bool pressed=power.isPekeyShortPressIrq();
  power.clearIrqStatus();
  if(pressed){setScreen(true);return;}
 }
 if(screen_on&&touch_pressed)last_screen_activity=now;
 if(screen_on&&display_logic::batteryScreenTimeout(battery_only,now,last_screen_activity))setScreen(false,true);
}
void readTouch(lv_indev_drv_t*,lv_indev_data_t* data){
 if(usbQaTouch(data))return;
 static int16_t x[2]={},y[2]={};
 if(touch_ready&&touch_pending){
  noInterrupts();touch_pending=false;interrupts();
  touch_pressed=touch.getPoint(x,y,2)>0;
  if(touch_pressed&&!screen_on&&!auto_blanked)setScreen(true);
 }
 data->state=screen_on&&touch_pressed?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;data->point.x=x[0];data->point.y=y[0];
}
void buttons(){
 bool boot=digitalRead(board::kBootButton)==LOW,key=digitalRead(board::kCodexButton)==LOW;
 if(boot&&!boot_down)boot_pressed=millis();
 if(!boot&&boot_down&&millis()-boot_pressed>35)setScreen(!screen_on);
 if(key&&!key_down){setScreen(true);uiSettings();}
 boot_down=boot;key_down=key;
}
}
void usbQaPowerReport(){
 if(!power_ready){Serial.println("@ERROR power-unavailable");return;}
 // Read status and configuration only; preserve charging and fuel-gauge state.
 constexpr uint8_t registers[]={0x00,0x01,0x03,0x14,0x15,0x16,0x17,0x18,0x30,0x34,0x35,0x38,0x39,0x48,0x49,0x4a,0x50,0x58,0x61,0x62,0x63,0x64,0x65,0x67,0x68,0xa1,0xa2,0xa4};
 int values[sizeof(registers)];
 for(size_t i=0;i<sizeof(registers);++i){
  Wire.beginTransmission(AXP2101_SLAVE_ADDRESS);Wire.write(registers[i]);
  if(Wire.endTransmission(false)!=0 || Wire.requestFrom(uint8_t(AXP2101_SLAVE_ADDRESS),uint8_t(1))!=1){Serial.println("@ERROR power-read");return;}
  values[i]=Wire.read();
 }
 Serial.printf("@POWER uptime_ms=%lu percent=%d battery_mv=%u present=%d usb_good=%d charging=%d discharging=%d charger_state=%u",
  (unsigned long)millis(),power.getBatteryPercent(),power.getBattVoltage(),power.isBatteryConnect(),power.isVbusGood(),power.isCharging(),power.isDischarge(),unsigned(power.getChargerStatus()));
 for(size_t i=0;i<sizeof(registers);++i)Serial.printf(" r%02x=%d",registers[i],values[i]);
 Serial.println();
}
void setup(){
 Serial.begin(115200);delay(300);Serial.println("\nCodex LAN Display v2 starting");
 pinMode(board::kBootButton,INPUT_PULLUP);pinMode(board::kCodexButton,INPUT_PULLUP);
 prefs.begin("codexscreen",false);brightness=constrain(prefs.getUChar("brightness",70),10,100);saved_brightness=brightness;
 Wire.begin(board::kI2cSda,board::kI2cScl);
 saved_volume=constrain(prefs.getUChar("volume",45),0,100);
 Serial.printf("[audio] ready=%d volume=%d%% E2 notes=2/2/4\n",soundBegin(saved_volume),saved_volume);
 power_ready=power.begin(Wire,AXP2101_SLAVE_ADDRESS,board::kI2cSda,board::kI2cScl);
 if(power_ready){
  power.clearIrqStatus();power.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
  power.setPowerKeyPressOnTime(XPOWERS_POWERON_128MS);power.setPowerKeyPressOffTime(XPOWERS_POWEROFF_6S);power.enableLongPressShutdown();power.setLongPressPowerOFF();power.enableBattDetection();power.enableBattVoltageMeasure();
  // Waveshare's 2.16-inch board example: stock battery, 400 mA CC, 4.2 V CV, 25 mA termination.
  power.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
  power.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_400MA);
  power.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
  power.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
  power.enableChargerTerminationLimit();
  power.enableCellbatteryCharge();
  bool configured=power.getChargerConstantCurr()==XPOWERS_AXP2101_CHG_CUR_400MA &&
   power.getChargeTargetVoltage()==XPOWERS_AXP2101_CHG_VOL_4V2 &&
   power.getChargerTerminationCurr()==XPOWERS_AXP2101_CHG_ITERM_25MA && power.isChargerTerminationLimit();
  Serial.printf("[power] charge_config_verified=%d target=4200mV cc=400mA termination=25mA\n",configured);
 }
 if(!display->begin()){Serial.println("[fatal] display initialization failed");while(true)delay(1000);}
 bus->writeC8D8(0x36,0xA0);display->fillScreen(RGB565_BLACK);display->setBrightness(brightness*255/100);
 touch.setPins(board::kTouchReset,board::kTouchInterrupt);
 touch_ready=touch.begin(Wire,CST92XX_SLAVE_ADDRESS,board::kI2cSda,board::kI2cScl);
 if(touch_ready){touch.setMaxCoordinates(480,480);touch.setSwapXY(true);touch.setMirrorXY(true,false);pinMode(board::kTouchInterrupt,INPUT_PULLUP);touch_ready=startTouchInterrupt();}
 imu_ready=imu.begin(Wire,QMI8658_L_SLAVE_ADDRESS,board::kI2cSda,board::kI2cScl);
 if(imu_ready){imu.configAccelerometer(SensorQMI8658::ACC_RANGE_2G,SensorQMI8658::ACC_ODR_62_5Hz,SensorQMI8658::LPF_MODE_0);imu.enableAccelerometer();}
 lv_init();constexpr size_t count=480*40;
 auto* a=static_cast<lv_color_t*>(heap_caps_malloc(count*sizeof(lv_color_t),MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));
 rotated_pixels=static_cast<lv_color_t*>(heap_caps_malloc(count*sizeof(lv_color_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!a || !rotated_pixels){Serial.println("[fatal] LVGL/rotation memory allocation failed");while(true)delay(1000);}
 // Synchronous display flush needs one buffer; keep internal RAM available for Wi-Fi.
 lv_disp_draw_buf_init(&draw_buffer,a,nullptr,count);
 static lv_disp_drv_t drv;lv_disp_drv_init(&drv);drv.hor_res=480;drv.ver_res=480;drv.flush_cb=flush;drv.rounder_cb=roundArea;drv.draw_buf=&draw_buffer;drv.monitor_cb=monitor;lv_disp_drv_register(&drv);
 static lv_indev_drv_t input;lv_indev_drv_init(&input);input.type=LV_INDEV_TYPE_POINTER;input.read_cb=readTouch;input.long_press_time=1000;lv_indev_drv_register(&input);
 uiCreate(setBrightness,brightness,setVolume,saved_volume,soundPreview);lanBegin();
 Serial.printf("[ready] display=ok touch=%d pmu=%d imu=%d brightness=%d\n",touch_ready,power_ready,imu_ready,brightness);
}
void loop(){
 static bool was_qa=false;static int before_qa=0,before_qa_volume=0;
 usbQaPoll();bool qa=usbQaActive();
 if(qa&&!was_qa){setScreen(true);before_qa=brightness;before_qa_volume=soundVolume();soundReset();}
 if(!qa&&was_qa){setBrightness(before_qa);uiBrightness(before_qa);setVolume(before_qa_volume);uiVolume(before_qa_volume);soundReset();}
 was_qa=qa;
 if(!qa){buttons();screenPower(millis());}lv_timer_handler();uint32_t now=millis();
 orientation(now);
 static uint32_t last_perf=0;
 if(now-last_perf>=15000){Serial.printf("[display] fps=%.1f render_avg_ms=%.1f render_max_ms=%u heap=%u\n",rendered_frames*1000.0f/(now-last_perf),rendered_frames?float(render_ms)/rendered_frames:0,max_render_ms,ESP.getFreeHeap());last_perf=now;rendered_frames=render_ms=max_render_ms=0;}
 if(!qa&&now-last_ui>=500){last_ui=now;auto state=lanSnapshot();uiUpdate(state);soundUpdate(state);}
 if(!qa&&now-last_battery>=5000){last_battery=now;if(power_ready)uiBattery(power.getBatteryPercent(),power.isCharging(),power.isBatteryConnect());else uiBattery(-1,false,false);}
 if(!qa&&brightness!=saved_brightness&&now-brightness_changed>=800){if(prefs.putUChar("brightness",brightness)==1){saved_brightness=brightness;Serial.printf("[display] brightness saved: %d%%\n",brightness);}}
 if(!qa&&soundVolume()!=saved_volume&&now-volume_changed>=800){int value=soundVolume();if(prefs.putUChar("volume",value)==1){saved_volume=value;Serial.printf("[audio] volume saved: %d%%\n",value);}}
 delay(5);
}
