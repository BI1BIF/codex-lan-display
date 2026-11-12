#include "ui.h"
#include "display_logic.h"
#include "loading_ring.h"
#include <lvgl.h>
#include <cmath>
#include <time.h>
LV_FONT_DECLARE(font_cn_24);
LV_FONT_DECLARE(font_cn_40);
LV_FONT_DECLARE(font_clock_80);
LV_FONT_DECLARE(font_clock_28);
namespace {
constexpr uint32_t white=0xf5f8f6, muted=0xc4d1ca, green=0x62d7a0, blue=0x51a9ff, yellow=0xffce61;
lv_obj_t *home,*title,*detail,*symbol,*quota,*credits,*reset,*net,*battery,*settings,*slider,*brightness_label,*battery_info,*network_info,*qr,*portal_text,*network_button;
lv_obj_t* border[8];
lv_obj_t *volume_slider,*volume_label,*settings_hint;
lv_obj_t *header_label,*header_image;
lv_obj_t *quota_panel,*short_quota;
lv_obj_t *status_panel,*clock_panel,*clock_time,*clock_seconds,*clock_date;
bool standby_eligible=false;
uint32_t standby_since=0,test_age=0,test_clock=0;
int last_fade=-1;
display_logic::ClockRing clock_ring;
uint8_t header_pixels[1600];
lv_img_dsc_t header_descriptor;
void (*brightness_callback)(int)=nullptr;
void (*volume_callback)(int)=nullptr;
void (*preview_callback)()=nullptr;
bool volume_edited=false;
LanState latest;
uint32_t accent=0x7f9c8b;
bool animate=false, blink=false, loading=false;
bool test_phase=false;uint32_t phase_ms=0;
const lv_font_t* small=&font_cn_24;
void text(lv_obj_t* obj,const char* value){if(strcmp(lv_label_get_text(obj),value))lv_label_set_text(obj,value);}
void number(lv_obj_t* obj,const char* format,int value){char buf[32];snprintf(buf,sizeof(buf),format,value);text(obj,buf);}
void clockText(const char* value){
 if(!strcmp(lv_label_get_text(clock_time),value))return;
 text(clock_time,value);
 lv_point_t size;lv_txt_get_size(&size,value,&font_clock_80,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
 lv_font_glyph_dsc_t first,last;size_t length=strlen(value);
 lv_font_get_glyph_dsc(&font_clock_80,&first,value[0],value[1]);
 lv_font_get_glyph_dsc(&font_clock_80,&last,value[length-1],0);
 // Center visible segments rather than the empty side bearing of a leading 1.
 int left=first.ofs_x,right=size.x-last.adv_w+last.ofs_x+last.box_w;
 int x=(420-left-right)/2;lv_obj_set_x(clock_time,x);
 lv_obj_set_x(clock_seconds,x+right+6);
}
lv_obj_t* box(lv_obj_t* p,int x,int y,int w,int h,uint32_t bg,int r=0){
 auto* o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
 lv_obj_set_style_bg_color(o,lv_color_hex(bg),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
 lv_obj_set_style_radius(o,r,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
lv_obj_t* label(lv_obj_t* p,const char* t,int x,int y,int w,const lv_font_t* f,uint32_t c=white){
 auto* o=lv_label_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_obj_set_style_text_font(o,f,0);
 lv_obj_set_style_text_color(o,lv_color_hex(c),0);lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);lv_label_set_text(o,t);return o;
}
lv_obj_t* button(lv_obj_t* p,const char* t,int x,int y,int w,lv_event_cb_t cb){
 auto* b=lv_btn_create(p);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,48);lv_obj_set_style_radius(b,10,0);
 lv_obj_set_style_bg_color(b,lv_color_hex(0x22352c),0);lv_obj_set_style_shadow_width(b,0,0);
 lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr);auto* l=label(b,t,0,0,w,small);lv_obj_center(l);return b;
}
void createBorder(){
 border[0]=box(home,60,15,360,4,blue);border[1]=box(home,60,461,360,4,blue);
 border[2]=box(home,15,60,4,360,blue);border[3]=box(home,461,60,4,360,blue);
 const int xs[]={15,420,420,15},ys[]={15,15,420,420};
 for(int i=0;i<4;++i){auto* corner=box(home,xs[i],ys[i],45,45,0x050807);border[i+4]=corner;
  lv_obj_set_style_arc_color(corner,lv_color_hex(blue),0);lv_obj_set_style_arc_opa(corner,LV_OPA_COVER,0);
  // Each quarter circle invalidates only its 45x45 visible quadrant, not a complete 90x90 arc widget.
  lv_obj_add_event_cb(corner,[](lv_event_t* e){
   int n=static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));auto* obj=lv_event_get_target(e);
   lv_area_t a;lv_obj_get_coords(obj,&a);lv_point_t center={static_cast<lv_coord_t>(a.x1+((n==0||n==3)?45:0)),static_cast<lv_coord_t>(a.y1+(n<2?45:0))};
   const int angles[]={180,270,0,90};lv_draw_arc_dsc_t d;lv_draw_arc_dsc_init(&d);d.width=4;
   d.color=lv_obj_get_style_arc_color(obj,LV_PART_MAIN);d.opa=lv_obj_get_style_arc_opa(obj,LV_PART_MAIN);
   lv_draw_arc(lv_event_get_draw_ctx(e),&d,&center,45,angles[n],angles[n]+90);
  },LV_EVENT_DRAW_MAIN,reinterpret_cast<void*>(static_cast<intptr_t>(i)));
 }
 for(auto* part:border){lv_obj_clear_flag(part,LV_OBJ_FLAG_CLICKABLE);lv_obj_clear_flag(part,LV_OBJ_FLAG_SCROLLABLE);}
}
void showPortal(){
 if(!latest.portal)return;
 lv_obj_clear_flag(qr,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(portal_text,LV_OBJ_FLAG_HIDDEN);
 String data="WIFI:T:WPA;S:"+String(latest.ap)+";P:"+String(latest.password)+";;";
 lv_qrcode_update(qr,data.c_str(),data.length());
 String t="手机扫码连接热点\n"+String(latest.ap)+"\n12位数字密码 "+String(latest.password)+"\n再打开 192.168.4.1 配网";
 lv_label_set_text(portal_text,t.c_str());
 for(auto* o:{slider,brightness_label,volume_slider,volume_label,battery_info,network_info,network_button,settings_hint})lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);
}
void pulse(lv_timer_t*){
 if(!lv_obj_has_flag(settings,LV_OBJ_FLAG_HIDDEN))return;
 uint32_t now=millis();
 int fade=standby_eligible?display_logic::standbyOpacity(test_phase?test_age:now-standby_since):0;
 if(fade!=last_fade){
  for(auto* o:{status_panel,clock_panel})lv_obj_clear_flag(o,LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_opa(status_panel,255-fade,0);lv_obj_set_style_opa(clock_panel,fade,0);
  if(!fade)lv_obj_add_flag(clock_panel,LV_OBJ_FLAG_HIDDEN);
  if(fade==255)lv_obj_add_flag(status_panel,LV_OBJ_FLAG_HIDDEN);
  last_fade=fade;
 }
 uint64_t stamp=uint64_t(latest.unix_time)*1000+uint32_t(now-latest.received);
 if(test_phase)stamp=uint64_t(test_clock)*1000+phase_ms%1000;
 if(fade){
  if(latest.unix_time||test_clock){
   time_t local=time_t(stamp/1000)+latest.utc_offset;struct tm date;gmtime_r(&local,&date);
   char buf[96];snprintf(buf,sizeof(buf),"%02d:%02d",date.tm_hour,date.tm_min);clockText(buf);
   snprintf(buf,sizeof(buf),"%02d",date.tm_sec);text(clock_seconds,buf);
   const char* days[]={"星期日","星期一","星期二","星期三","星期四","星期五","星期六"};
   snprintf(buf,sizeof(buf),"%04d年%02d月%02d日  %s",date.tm_year+1900,date.tm_mon+1,date.tm_mday,days[date.tm_wday]);text(clock_date,buf);
  }else{clockText("--:--");text(clock_seconds,"--");text(clock_date,"正在同步电脑时间");}
 }
 if(!fade)clock_ring.active=false;
 uint32_t ring_phase=fade?(test_phase?uint32_t(stamp%60000):clock_ring.phase(now,uint32_t(stamp%60000))):(test_phase?phase_ms:now);
 loadingRingFrame(loading||fade>0,ring_phase,fade>0);
 int opacity=255;
 if(animate){uint32_t period=blink?1000:2000;float p=((test_phase?phase_ms:millis())%period)/float(period);opacity=90+int(165*(0.5f-0.5f*cosf(p*6.2831853f)));}
 static int previous=-1;if(previous==opacity)return;previous=opacity;
 lv_obj_set_style_text_opa(symbol,opacity,0);
 for(int i=0;i<8;++i){if(i<4)lv_obj_set_style_bg_opa(border[i],opacity,0);else lv_obj_set_style_arc_opa(border[i],opacity,0);}
}
}
void uiHome(){lv_obj_add_flag(settings,LV_OBJ_FLAG_HIDDEN);}
bool uiSettingsVisible(){return !lv_obj_has_flag(settings,LV_OBJ_FLAG_HIDDEN);}
void uiTestPhase(bool enabled,uint32_t phase){test_phase=enabled;phase_ms=phase;pulse(nullptr);}
void uiTestStandby(uint32_t age,uint32_t epoch){test_age=age;test_clock=epoch;pulse(nullptr);}
void uiBrightness(int value){lv_slider_set_value(slider,value,LV_ANIM_OFF);lv_label_set_text_fmt(brightness_label,"屏幕亮度   %d%%",value);}
int uiBrightnessValue(){return lv_slider_get_value(slider);}
void uiVolume(int value){lv_slider_set_value(volume_slider,value,LV_ANIM_OFF);if(value)lv_label_set_text_fmt(volume_label,"提示音量   %d%%",value);else text(volume_label,"提示音量   0% · 静音");}
void uiSettings(){
 lv_obj_clear_flag(settings,LV_OBJ_FLAG_HIDDEN);
 for(auto* o:{qr,portal_text})lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);
 for(auto* o:{slider,brightness_label,volume_slider,volume_label,battery_info,network_info,network_button,settings_hint})lv_obj_clear_flag(o,LV_OBJ_FLAG_HIDDEN);
 String t=latest.wifi?"Wi-Fi "+String(latest.ssid)+"\n屏幕 "+String(latest.ip)+"\n电脑 "+String(latest.host):"Wi-Fi 未连接\n点击下方按钮重新配网";
 lv_label_set_text(network_info,t.c_str());
 if(latest.portal)showPortal();
}
void uiCreate(void (*cb)(int),int brightness,void (*volume_cb)(int),int volume,void (*preview_cb)()){
 brightness_callback=cb;volume_callback=volume_cb;preview_callback=preview_cb;auto* root=lv_scr_act();lv_obj_set_style_bg_color(root,lv_color_hex(0x050807),0);
 home=box(root,0,0,480,480,0x050807);lv_obj_add_flag(home,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_event_cb(home,[](lv_event_t*){uiSettings();},LV_EVENT_LONG_PRESSED,nullptr);
 createBorder();
 loadingRingCreate(home);
 header_label=label(home,"CODEX",43,42,200,small);lv_obj_set_style_text_align(header_label,LV_TEXT_ALIGN_LEFT,0);
 header_descriptor.header.cf=LV_IMG_CF_ALPHA_2BIT;header_descriptor.header.w=200;header_descriptor.header.h=32;
 header_descriptor.data_size=sizeof(header_pixels);header_descriptor.data=header_pixels;
 header_image=lv_img_create(home);lv_obj_set_pos(header_image,43,42);lv_img_set_src(header_image,&header_descriptor);
 lv_obj_set_style_img_recolor(header_image,lv_color_hex(white),0);lv_obj_set_style_img_recolor_opa(header_image,LV_OPA_COVER,0);
 lv_obj_add_flag(header_image,LV_OBJ_FLAG_HIDDEN);
 battery=label(home,"USB",300,42,138,&lv_font_montserrat_24,muted);
 status_panel=box(home,0,90,480,202,0x050807);lv_obj_set_style_bg_opa(status_panel,LV_OPA_TRANSP,0);lv_obj_clear_flag(status_panel,LV_OBJ_FLAG_CLICKABLE);
 auto* bg=box(status_panel,201,12,78,78,0x101b16,39);lv_obj_clear_flag(bg,LV_OBJ_FLAG_CLICKABLE);
 symbol=label(status_panel,LV_SYMBOL_REFRESH,201,28,78,&lv_font_montserrat_48,blue);
 title=label(status_panel,"正在连接",40,110,400,&font_cn_40);
 detail=label(status_panel,"正在连接局域网",35,167,410,small,muted);
 clock_panel=box(home,30,90,420,202,0x050807);lv_obj_set_style_bg_opa(clock_panel,LV_OPA_TRANSP,0);lv_obj_clear_flag(clock_panel,LV_OBJ_FLAG_CLICKABLE);
 clock_time=label(clock_panel,"",0,18,420,&font_clock_80);
 lv_obj_set_style_text_align(clock_time,LV_TEXT_ALIGN_LEFT,0);
 lv_point_t clock_size;lv_txt_get_size(&clock_size,"88:88",&font_clock_80,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
 int seconds_x=(420+clock_size.x)/2+6;
 int seconds_y=18+font_clock_80.line_height-font_clock_80.base_line-font_clock_28.line_height+font_clock_28.base_line;
 clock_seconds=label(clock_panel,"--",seconds_x,seconds_y,56,&font_clock_28,muted);
 lv_obj_set_style_text_align(clock_seconds,LV_TEXT_ALIGN_LEFT,0);
 clockText("--:--");
 clock_date=label(clock_panel,"正在同步电脑时间",0,143,420,small,muted);lv_obj_add_flag(clock_panel,LV_OBJ_FLAG_HIDDEN);
 quota_panel=box(home,0,296,480,131,0x050807);lv_obj_set_style_bg_opa(quota_panel,LV_OPA_TRANSP,0);lv_obj_clear_flag(quota_panel,LV_OBJ_FLAG_CLICKABLE);
 auto* div=box(quota_panel,47,0,386,1,0x28352e);lv_obj_clear_flag(div,LV_OBJ_FLAG_CLICKABLE);
 label(quota_panel,"剩余用量",90,7,146,small,muted);label(quota_panel,"可用重置",255,7,146,small,muted);
 quota=label(quota_panel,"--",75,39,175,&lv_font_montserrat_48);credits=label(quota_panel,"--",255,39,150,&lv_font_montserrat_48);
 reset=label(quota_panel,"额度恢复 待同步",30,96,420,small,muted);
 short_quota=label(home,"",28,394,424,small,muted);lv_obj_add_flag(short_quota,LV_OBJ_FLAG_HIDDEN);
 net=label(home,"Wi-Fi 未连接",35,427,410,small,muted);
 settings=box(root,0,0,480,480,0x050807);label(settings,"显示与声音",32,35,180,small);
 button(settings,"完成",358,25,82,[](lv_event_t*){lv_obj_add_flag(settings,LV_OBJ_FLAG_HIDDEN);});
 brightness_label=label(settings,"",32,89,416,small);
 slider=lv_slider_create(settings);lv_obj_set_pos(slider,47,136);lv_obj_set_size(slider,386,16);
 lv_obj_set_ext_click_area(slider,18);
 lv_slider_set_range(slider,10,100);lv_slider_set_value(slider,brightness,LV_ANIM_OFF);
 lv_obj_set_style_bg_color(slider,lv_color_hex(green),LV_PART_INDICATOR);
 lv_obj_set_style_bg_color(slider,lv_color_hex(white),LV_PART_KNOB);
 lv_label_set_text_fmt(brightness_label,"屏幕亮度   %d%%",brightness);
 lv_obj_add_event_cb(slider,[](lv_event_t*){int v=lv_slider_get_value(slider);lv_label_set_text_fmt(brightness_label,"屏幕亮度   %d%%",v);if(brightness_callback)brightness_callback(v);},LV_EVENT_VALUE_CHANGED,nullptr);
 volume_label=label(settings,"",32,173,416,small);
 volume_slider=lv_slider_create(settings);lv_obj_set_pos(volume_slider,47,220);lv_obj_set_size(volume_slider,386,16);
 lv_obj_set_ext_click_area(volume_slider,18);lv_slider_set_range(volume_slider,0,100);uiVolume(volume);
 lv_obj_set_style_bg_color(volume_slider,lv_color_hex(green),LV_PART_INDICATOR);
 lv_obj_set_style_bg_color(volume_slider,lv_color_hex(white),LV_PART_KNOB);
 lv_obj_add_event_cb(volume_slider,[](lv_event_t* e){
  if(lv_event_get_code(e)==LV_EVENT_VALUE_CHANGED){int v=lv_slider_get_value(volume_slider);uiVolume(v);volume_edited=true;if(volume_callback)volume_callback(v);}
  else if(lv_event_get_code(e)==LV_EVENT_RELEASED&&volume_edited){volume_edited=false;if(preview_callback)preview_callback();}
 },LV_EVENT_ALL,nullptr);
 battery_info=label(settings,"电池状态正在读取",32,260,416,small,muted);
 network_info=label(settings,"",32,294,416,small,muted);
 network_button=button(settings,"更换 Wi-Fi / 重新配网",47,386,386,[](lv_event_t*){lanOpenPortal();});
 settings_hint=label(settings,"自动保存 · 0% 静音",20,442,440,small,muted);
 qr=lv_qrcode_create(settings,196,lv_color_black(),lv_color_white());lv_obj_set_pos(qr,142,86);
 lv_obj_set_style_border_width(qr,8,0);lv_obj_set_style_border_color(qr,lv_color_white(),0);
 portal_text=label(settings,"",30,292,420,small);
 for(auto* o:{qr,portal_text,settings})lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);
 lv_timer_create(pulse,16,nullptr);
}
void uiBattery(int percent,bool charging,bool present){
 if(!present){lv_label_set_text(battery,LV_SYMBOL_USB " USB");lv_label_set_text(battery_info,"USB 供电 · 未检测到电池");return;}
 if(percent<0||percent>100){lv_label_set_text(battery,"--%");lv_label_set_text(battery_info,"电池电量暂不可用");return;}
 lv_label_set_text_fmt(battery,"%s %d%%",charging?LV_SYMBOL_CHARGE:LV_SYMBOL_BATTERY_3,percent);
 lv_obj_set_style_text_color(battery,lv_color_hex(charging?green:percent<=15?yellow:muted),0);
 lv_label_set_text_fmt(battery_info,"电量 %d%% · %s",percent,charging?"正在充电":"电池已连接");
}
void uiUpdate(const LanState& s){
 bool new_portal=s.portal&&!latest.portal;latest=s;
 bool eligible=s.wifi&&s.linked&&s.tasks_valid&&!s.portal&&(!strcmp(s.state,"done")||!strcmp(s.state,"idle"));
 if(eligible&&!standby_eligible)standby_since=millis();
 standby_eligible=eligible;
 if(test_phase){test_age=0;test_clock=0;}
 if(s.header_valid){if(memcmp(header_pixels,s.header_bitmap,sizeof(header_pixels))){memcpy(header_pixels,s.header_bitmap,sizeof(header_pixels));lv_obj_invalidate(header_image);}
  lv_obj_add_flag(header_label,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(header_image,LV_OBJ_FLAG_HIDDEN);
 }
 if(new_portal){uiSettings();showPortal();}
 if(!s.portal&&!lv_obj_has_flag(qr,LV_OBJ_FLAG_HIDDEN))lv_obj_add_flag(settings,LV_OBJ_FLAG_HIDDEN);
 const char* t="状态未知";const char* icon=LV_SYMBOL_MINUS;String sub;accent=0x7f9c8b;animate=false;blink=false;loading=false;
 if(!s.wifi){t="等待连接";sub=s.portal?"长按屏幕查看配网二维码":"长按屏幕设置 Wi-Fi";}
 else if(!s.linked){t="电脑未连接";sub="Wi-Fi 已连接，正在寻找电脑";}
 else if(!s.tasks_valid){t="状态待同步";sub="正在重新读取 Codex 任务";}
 else if(!strcmp(s.state,"waiting")){t="需要你回答";accent=yellow;icon=LV_SYMBOL_WARNING;animate=true;blink=true;sub=String(s.waiting)+" 个待回答 · "+String(s.running)+" 个运行中";}
 else if(!strcmp(s.state,"running")){t="正在执行";accent=blue;icon=LV_SYMBOL_REFRESH;animate=true;loading=true;sub=String(s.running)+" 个任务运行中";}
 else if(!strcmp(s.state,"done")){t="任务已完成";accent=green;icon=LV_SYMBOL_OK;sub="本轮任务已全部结束";}
 else if(!strcmp(s.state,"error")){t="任务异常";accent=0xff6b6b;icon=LV_SYMBOL_CLOSE;sub="请在电脑上查看详情";}
 else if(!strcmp(s.state,"idle")){t="等待新任务";sub="Codex 当前空闲";}
 else sub="尚未读取到明确任务状态";
 text(title,t);text(detail,sub.c_str());text(symbol,icon);
 static uint32_t last_accent=0;
 if(accent!=last_accent){for(int i=0;i<8;++i){if(i<4)lv_obj_set_style_bg_color(border[i],lv_color_hex(accent),0);else lv_obj_set_style_arc_color(border[i],lv_color_hex(accent),0);}lv_obj_set_style_text_color(symbol,lv_color_hex(accent),0);last_accent=accent;}
 const bool show_short=s.linked&&s.short_remaining>=0;
 if(show_short!=!lv_obj_has_flag(short_quota,LV_OBJ_FLAG_HIDDEN)){
  lv_obj_set_y(status_panel,show_short?64:90);lv_obj_set_y(clock_panel,show_short?64:90);
  lv_obj_set_y(quota_panel,show_short?266:296);
  if(show_short)lv_obj_clear_flag(short_quota,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(short_quota,LV_OBJ_FLAG_HIDDEN);
 }
 const uint32_t elapsed=(millis()-s.received)/1000;
 if(show_short){
  char buf[100];
  if(s.short_reset_known){
   const auto t=display_logic::shortResetTime(s.short_reset_seconds>elapsed?s.short_reset_seconds-elapsed:0);
   snprintf(buf,sizeof(buf),"5小时剩余 %d%% · %lu小时%lu分后重置",s.short_remaining,(unsigned long)t.hours,(unsigned long)t.minutes);
  }else snprintf(buf,sizeof(buf),"5小时剩余 %d%% · 恢复时间待同步",s.short_remaining);
  text(short_quota,buf);
 }
 if(s.linked&&s.metrics_valid){
  number(quota,"%d%%",s.remaining);if(s.credits_valid)number(credits,"%d",s.credits);else text(credits,"--");
  uint32_t sec=s.reset_seconds>elapsed?s.reset_seconds-elapsed:0;
  if(s.reset_seconds){
   const auto time=display_logic::resetTime(sec);char buf[80];
   if(time.days)snprintf(buf,sizeof(buf),"额度恢复 %lu天 %lu小时",(unsigned long)time.days,(unsigned long)time.hours);
   else if(sec>0&&sec<60)snprintf(buf,sizeof(buf),"额度恢复 不到1分");
   else snprintf(buf,sizeof(buf),"额度恢复 %lu小时 %lu分",(unsigned long)time.hours,(unsigned long)time.minutes);
   text(reset,buf);
  }else text(reset,"额度恢复时间待同步");
 }else{text(quota,"--");text(credits,"--");text(reset,"用量暂不可用");}
 text(net,s.linked?"局域网已连接":s.wifi?"Wi-Fi 已连接 · 电脑未连接":"Wi-Fi 未连接");
 pulse(nullptr);
}
