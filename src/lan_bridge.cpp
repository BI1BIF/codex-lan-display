#include "lan_bridge.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <mbedtls/base64.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
LanState shared;
SemaphoreHandle_t mutex;
volatile bool open_requested = false;
WebServer web(80);
DNSServer dns;
Preferences prefs;
String configured_ssid, configured_pass, configured_host, host, csrf;
bool portal=false, mdns=false, was_wifi=false, auto_portal_attempted=false;
String scanned_networks[30];
int scanned_count=0;
uint32_t portal_started=0, next_poll=0, next_discovery=0, connect_started=0;
LanState local;

String html(const String& value) {
  String result=value;
  result.replace("&","&amp;"); result.replace("<","&lt;");
  result.replace(">","&gt;"); result.replace("\"","&quot;");
  result.replace("'","&#39;"); return result;
}
void publish() {
  xSemaphoreTake(mutex,portMAX_DELAY); shared=local; xSemaphoreGive(mutex);
}
bool onAp() {
  IPAddress ip=web.client().remoteIP();
  return portal && ip[0]==192 && ip[1]==168 && ip[2]==4;
}
void page() {
  if (!onAp()) {
    String body=R"HTML(<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Codex 状态屏</title><style>body{color-scheme:dark;background:#080d0b;color:#f5f8f6;font:18px system-ui;max-width:480px;margin:48px auto;padding:24px}h1{font-size:28px}p{line-height:1.8}a{display:block;padding:16px;margin-top:20px;background:#62d7a0;color:#08130d;border-radius:10px;text-align:center;font-weight:700}small{color:#c4d1ca}</style><h1>Codex 状态屏</h1>)HTML";
    body+="<p>屏幕地址："+html(local.ip)+"<br>电脑连接："+String(local.linked?"已连接":"正在重连")+"</p>";
    if(host.length())body+="<a href=\"http://"+html(host)+":8738/settings\">打开屏幕设置</a>";
    body+="<p><small>在电脑后台修改左上角文字；长按屏幕调节亮度和音量。更换 Wi-Fi 请在屏幕设置中选择重新配网。</small></p></html>";
    web.sendHeader("Cache-Control","no-store");web.send(200,"text/html; charset=utf-8",body);return;
  }
  String body=R"HTML(<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Codex 屏幕配网</title><style>body{background:#080d0a;color:#f4f7f5;font:18px system-ui;max-width:420px;margin:40px auto;padding:24px}h1{font-size:28px}label{display:block;margin:22px 0 8px}input,button,select{box-sizing:border-box;width:100%;font:inherit;padding:14px;border:1px solid #52635a;border-radius:8px;background:#132019;color:inherit}button{background:#62d7a0;color:#08130d;margin-top:24px;font-weight:700}p,small{color:#b5c4bb;line-height:1.7}small{font-size:14px}</style><h1>连接 Codex 屏幕</h1><p>选择 2.4 GHz Wi-Fi。电脑与屏幕需在同一个局域网。</p><form method="post" action="/save"><input type="hidden" name="csrf" value=")HTML";
  body+=csrf+"\"><label>Wi-Fi 名称</label><input name=ssid id=ssid maxlength=32 required value=\""+html(configured_ssid)+"\"><select id=nets aria-label=\"扫描到的网络\" onchange=\"document.getElementById('ssid').value=this.value\"><option>正在扫描网络…</option></select><label>Wi-Fi 密码</label><input name=password type=password maxlength=63 autocomplete=new-password placeholder=\"开放网络可留空\"><label>电脑地址（可留空自动发现）</label><input name=host inputmode=decimal placeholder=\"例如 192.0.2.10\" value=\""+html(configured_host)+"\"><small>只填写局域网 IPv4 地址。服务端口为 8738。</small><button>保存并连接</button></form><p>密码只保存在开发板本地。</p><script>async function scan(){try{let r=await fetch('/scan');let j=await r.json();if(j.busy){setTimeout(scan,1500);return;}let s=document.getElementById('nets');s.textContent='';let h=new Option('选择扫描到的 Wi-Fi','');s.add(h);j.networks.forEach(n=>s.add(new Option(n,n)));}catch{setTimeout(scan,2000)}}scan();</script></html>";
  web.sendHeader("Cache-Control","no-store"); web.send(200,"text/html; charset=utf-8",body);
}
void startPortal() {
  if(portal)return;
  auto_portal_attempted=true;
  snprintf(local.ap,sizeof(local.ap),"Codex-Setup2-%04X",(unsigned)(ESP.getEfuseMac()>>32)&0xffff);
  String saved=prefs.getString("setup_pwd","");
  if(saved.length()!=12){char generated[20];snprintf(generated,sizeof(generated),"%06lu%06lu",(unsigned long)(esp_random()%1000000),(unsigned long)(esp_random()%1000000));saved=generated;prefs.putString("setup_pwd",saved);}
  strlcpy(local.password,saved.c_str(),sizeof(local.password));
  csrf=String(esp_random(),HEX)+String(esp_random(),HEX);
  // Scan before starting the AP; STA reconnect/scan channel hopping can break phone authentication.
  WiFi.setAutoReconnect(false);WiFi.disconnect(false,false);WiFi.mode(WIFI_STA);
  int found=WiFi.scanNetworks(false);
  scanned_count=constrain(found,0,30);
  for(int i=0;i<scanned_count;++i)scanned_networks[i]=WiFi.SSID(i);
  WiFi.scanDelete();WiFi.mode(WIFI_AP);
  if(!WiFi.softAP(local.ap,local.password,6,0,4,false,WIFI_AUTH_WPA2_PSK,WIFI_CIPHER_TYPE_CCMP)){Serial.println("[lan] AP failed");return;}
  if(!WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0),IPAddress(192,168,4,2),IPAddress(192,168,4,1))){
    Serial.println("[lan] AP address/DHCP configuration failed");WiFi.softAPdisconnect(true);return;
  }
  esp_netif_dhcp_status_t dhcp=ESP_NETIF_DHCP_INIT;
  auto* ap=esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  bool dhcp_ok=ap && esp_netif_dhcps_get_status(ap,&dhcp)==ESP_OK && dhcp==ESP_NETIF_DHCP_STARTED;
  Serial.printf("[lan] AP IP=%s DHCP started=%d internal_heap=%u\n",WiFi.softAPIP().toString().c_str(),dhcp_ok,ESP.getFreeHeap());
  if(!dhcp_ok){WiFi.softAPdisconnect(true);return;}
  wifi_config_t actual={};
  bool match=esp_wifi_get_config(WIFI_IF_AP,&actual)==ESP_OK && !strcmp((char*)actual.ap.password,local.password);
  Serial.printf("[lan] AP credentials verified=%d length=%u auth=WPA2 channel=6\n",match,(unsigned)strlen(local.password));
  if(!match){WiFi.softAPdisconnect(true);return;}
  portal=true;local.portal=true;portal_started=millis();
  dns.start(53,"*",WiFi.softAPIP()); web.begin();
  Serial.println("[lan] setup portal active (credentials shown only on screen)");
}
void stopPortal() {
  dns.stop();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);
  portal=false;local.portal=false;local.password[0]=0;csrf="";
}
void pollServer() {
  if(host.isEmpty())return;
  WiFiClient client; HTTPClient http;
  http.setConnectTimeout(1200); http.setTimeout(1500);
  if(!http.begin(client,"http://"+host+":8738/api/display"))return;
  int code=http.GET();
  if(code==200 && http.getSize()>0 && http.getSize()<=4096){
    JsonDocument doc;
    auto err=deserializeJson(doc,http.getStream());
    if(!err && doc["v"].as<int>()==2 && doc["state"].is<const char*>() &&
       doc["remaining"].is<int>() && doc["remaining"].as<int>()>=0 && doc["remaining"].as<int>()<=100){
      const char* state=doc["state"];
      if(!strcmp(state,"running")||!strcmp(state,"waiting")||!strcmp(state,"done")||
         !strcmp(state,"idle")||!strcmp(state,"error")||!strcmp(state,"unknown")){
        local.remaining=doc["remaining"];local.credits=constrain(doc["credits"].as<int>(),0,999);
        local.running=constrain(doc["running"].as<int>(),0,999);local.waiting=constrain(doc["waiting"].as<int>(),0,999);
        uint32_t now=doc["now"]|0UL, reset=doc["reset_at"]|0UL;
        if(now>=1700000000UL){local.unix_time=now;local.utc_offset=constrain(doc["utc_offset"]|0,-50400,50400);}
        local.reset_seconds=reset>now?reset-now:0;
        local.metrics_valid=(doc["metrics_valid"]|false) && now-(doc["metrics_at"]|0UL)<120;
        local.credits_valid=doc["credits_valid"]|false;
        local.tasks_valid=(doc["tasks_valid"]|false) && now-(doc["tasks_at"]|0UL)<30;
        const char* bitmap=doc["header_bitmap"]|"";
        uint8_t decoded[sizeof(local.header_bitmap)];size_t decoded_size=0;
        if(strlen(bitmap)==2136 && mbedtls_base64_decode(decoded,sizeof(decoded),&decoded_size,
           reinterpret_cast<const unsigned char*>(bitmap),2136)==0 && decoded_size==sizeof(decoded)){
          memcpy(local.header_bitmap,decoded,sizeof(decoded));local.header_valid=true;
        }
        strlcpy(local.state,state,sizeof(local.state));
        local.received=millis();local.linked=true;
      }
    }
  }
  http.end();
}
void worker(void*) {
  WiFi.onEvent([](arduino_event_id_t event,arduino_event_info_t info){
    if(event==ARDUINO_EVENT_WIFI_AP_STACONNECTED)Serial.println("[lan] phone associated with setup AP");
    if(event==ARDUINO_EVENT_WIFI_AP_STADISCONNECTED)Serial.printf("[lan] phone disconnected reason=%u\n",info.wifi_ap_stadisconnected.reason);
    if(event==ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED){IPAddress ip(info.wifi_ap_staipassigned.ip.addr);Serial.printf("[lan] DHCP assigned %s\n",ip.toString().c_str());}
  });
  prefs.begin("codexlan",false);
  configured_ssid=prefs.getString("ssid",""); configured_pass=prefs.getString("pwd","");
  configured_host=prefs.getString("host","");host=configured_host;
  WiFi.mode(WIFI_STA); WiFi.setHostname("codex-display");WiFi.setAutoReconnect(true);WiFi.setSleep(false);
  if(configured_ssid.length())WiFi.begin(configured_ssid.c_str(),configured_pass.c_str());
  else WiFi.begin();
  connect_started=millis();
  web.on("/",HTTP_GET,page);
  web.on("/scan",HTTP_GET,[](){
    if(!onAp()){web.send(403);return;}
    JsonDocument d;auto nets=d["networks"].to<JsonArray>();
    for(int i=0;i<scanned_count;++i)nets.add(scanned_networks[i]);
    String data;serializeJson(d,data);web.send(200,"application/json",data);
  });
  web.on("/save",HTTP_POST,[](){
    if(!onAp() || web.arg("csrf")!=csrf){web.send(403);return;}
    String ssid=web.arg("ssid"), pass=web.arg("password"), manual=web.arg("host");
    IPAddress ip;
    if(ssid.isEmpty()||ssid.length()>32||pass.length()>63||
       (pass.length()>0 && pass.length()<8)||
       (manual.length() && (!ip.fromString(manual)||ip[0]==0||ip[0]==127||ip[0]>=224))){
      web.send(400,"text/plain; charset=utf-8","请检查 Wi-Fi 名称、密码（8–63 字符）及电脑 IPv4 地址。");return;
    }
    bool saved=prefs.putString("ssid",ssid)>0;
    saved=(prefs.putString("pwd",pass)==pass.length()+1 || prefs.getString("pwd")==pass) && saved;
    prefs.putString("host",manual);
    if(!saved || prefs.getString("host")!=manual){web.send(500,"text/plain","Could not save settings.");return;}
    configured_ssid=ssid;configured_pass=pass;configured_host=manual;host=manual;
    local.linked=false;local.received=0;
    web.send(200,"text/html; charset=utf-8","<meta charset=utf-8><meta name=viewport content=width=device-width><h2>已保存，正在连接</h2><p>请查看屏幕。若连接失败，可留在此热点重新填写；成功后热点会关闭。</p>");
    WiFi.mode(WIFI_AP_STA);WiFi.setAutoReconnect(true);WiFi.begin(ssid.c_str(),pass.c_str());connect_started=millis();
  });
  web.on("/health",HTTP_GET,[](){
    JsonDocument data;data["wifi"]=local.wifi;data["linked"]=local.linked;data["ip"]=local.ip;data["host"]=host;data["state"]=local.state;
    String body;serializeJson(data,body);web.sendHeader("Cache-Control","no-store");web.send(200,"application/json",body);
  });
  web.onNotFound([](){if(portal){web.sendHeader("Location","http://192.168.4.1/");web.send(302);}else web.send(404,"text/plain","Not found");});
  web.begin();
  for(;;){
    uint32_t now=millis();
    if(open_requested){open_requested=false;startPortal();}
    bool connected=WiFi.status()==WL_CONNECTED;
    local.wifi=connected;
    if(connected){
      strlcpy(local.ssid,WiFi.SSID().c_str(),sizeof(local.ssid));
      strlcpy(local.ip,WiFi.localIP().toString().c_str(),sizeof(local.ip));
      if(!was_wifi){mdns=MDNS.begin("codex-display");if(mdns)MDNS.addService("http","tcp",80);web.begin();next_discovery=0;next_poll=0;Serial.printf("[lan] Wi-Fi connected: %s\n",local.ip);}
      if(!local.linked && configured_host.isEmpty() && mdns && (int32_t)(now-next_discovery)>=0){
        int n=MDNS.queryService("codexdisplay","tcp");
        if(n>0)host=MDNS.address(0).toString();
        next_discovery=millis()+15000;
      }
      if((int32_t)(now-next_poll)>=0){pollServer();next_poll=millis()+2000;}
    }else{
      local.linked=false;local.ip[0]=0;
      if(!portal && !auto_portal_attempted && now-connect_started>15000 && !configured_ssid.length())startPortal();
    }
    if(local.received && millis()-local.received>12000)local.linked=false;
    strlcpy(local.host,host.c_str(),sizeof(local.host));
    web.handleClient();
    if(portal){dns.processNextRequest();
      if((local.linked && millis()-portal_started>15000)||millis()-portal_started>600000)stopPortal();}
    was_wifi=connected;publish();vTaskDelay(pdMS_TO_TICKS(15));
  }
}
}
void lanBegin(){mutex=xSemaphoreCreateMutex();xTaskCreatePinnedToCore(worker,"codex-lan",10000,nullptr,1,nullptr,0);}
LanState lanSnapshot(){LanState result;xSemaphoreTake(mutex,portMAX_DELAY);result=shared;xSemaphoreGive(mutex);return result;}
void lanOpenPortal(){open_requested=true;}
