#include "sound.h"
#include "board_config.h"
#include "sound_logic.h"
#include "sound_samples.h"
#include "audio_codec/es8311.h"
#include <ESP_I2S.h>
#include <algorithm>
#include <atomic>

namespace {
I2SClass i2s;
QueueHandle_t queue=nullptr;
bool ready=false;
std::atomic<int> volume{45};
std::atomic<bool> busy{false};
std::atomic<uint32_t> plays{0},errors{0},bytes{0};
sound_logic::Transitions transitions;
using Cue=sound_logic::Cue;
struct Clip {const int16_t* data;size_t count;};
Clip clip(Cue cue){
 if(cue==Cue::Running)return {sound_running,sizeof(sound_running)/2};
 if(cue==Cue::Question)return {sound_question,sizeof(sound_question)/2};
 return {sound_done,sizeof(sound_done)/2};
}
bool write(const int16_t* buffer,size_t length){
 size_t size=length*sizeof(int16_t);
 if(i2s.write(reinterpret_cast<const uint8_t*>(buffer),size)!=size){++errors;return false;}
 return true;
}
void worker(void*){
 int16_t stereo[512]={};
 for(;;){
  Cue cue; xQueueReceive(queue,&cue,portMAX_DELAY);
  if(volume.load()==0)continue;
  busy=true;bool ok=true,interrupted=false;Clip source=clip(cue);
  memset(stereo,0,sizeof(stereo));
  // Prime the codec with zeros before enabling the amplifier.
  for(int i=0;i<8 && ok;++i)ok=write(stereo,512);
  if(ok){
   digitalWrite(board::kAudioAmplifier,HIGH);
   // Allow amplifier startup while I2S clocks silence. A wall-clock guard is
   // intentional: queued DMA silence alone can return before it has played.
   ok=write(stereo,512);
   if(ok)vTaskDelay(pdMS_TO_TICKS(160));
  }
  for(size_t offset=0;offset<source.count && ok;offset+=256){
   if(uxQueueMessagesWaiting(queue)){interrupted=true;break;}
   size_t count=std::min(size_t(256),source.count-offset);int gain=volume.load();
   for(size_t i=0;i<count;++i)stereo[2*i]=stereo[2*i+1]=int32_t(source.data[offset+i])*gain/100;
   ok=write(stereo,count*2);if(ok)bytes.fetch_add(count*4);
  }
  memset(stereo,0,sizeof(stereo));
  for(int i=0;i<8 && ok;++i)ok=write(stereo,512);
  digitalWrite(board::kAudioAmplifier,LOW);
  if(ok&&!interrupted)++plays;
  busy=false;
 }
}
void play(Cue cue){if(ready&&cue!=Cue::None&&volume.load()>0)xQueueOverwrite(queue,&cue);}
}
bool soundBegin(int initial_volume){
 volume=constrain(initial_volume,0,100);
 pinMode(board::kAudioAmplifier,OUTPUT);digitalWrite(board::kAudioAmplifier,LOW);
 i2s.setPins(board::kAudioClock,board::kAudioWordSelect,board::kAudioData,-1,board::kAudioMasterClock);
 if(!i2s.begin(I2S_MODE_STD,16000,I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO,I2S_STD_SLOT_BOTH))return false;
 auto codec=es8311_create(0,ES8311_ADDRESS_0);
 if(!codec){i2s.end();return false;}
 const es8311_clock_config_t clock={false,false,true,16000*256,16000};
 esp_err_t result=es8311_init(codec,&clock,ES8311_RESOLUTION_16,ES8311_RESOLUTION_16);
 if(result==ESP_OK)result=es8311_voice_volume_set(codec,90,nullptr);
 es8311_delete(codec);
 if(result!=ESP_OK){i2s.end();Serial.printf("[audio] codec error=%d\n",result);return false;}
 queue=xQueueCreate(1,sizeof(Cue));
 if(!queue){i2s.end();return false;}
 if(xTaskCreatePinnedToCore(worker,"codex-audio",4096,nullptr,1,nullptr,0)!=pdPASS){vQueueDelete(queue);queue=nullptr;i2s.end();return false;}
 ready=true;return true;
}
void soundVolume(int value){volume=constrain(value,0,100);}
int soundVolume(){return volume.load();}
void soundPreview(){play(Cue::Running);}
void soundReset(){transitions={};if(queue)xQueueReset(queue);}
void soundUpdate(const LanState& s){
 using State=sound_logic::State;State next=State::Unknown;
 if(s.wifi&&s.linked&&s.tasks_valid&&!s.portal){
  if(!strcmp(s.state,"running"))next=State::Running;
  else if(!strcmp(s.state,"waiting"))next=State::Waiting;
  else if(!strcmp(s.state,"done"))next=State::Done;
  else if(!strcmp(s.state,"idle"))next=State::Idle;
  else if(!strcmp(s.state,"error"))next=State::Error;
 }
 play(transitions.update(next));
}
SoundStatus soundStatus(){return {ready,busy.load(),volume.load(),plays.load(),errors.load(),bytes.load()};}
