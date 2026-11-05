#pragma once
#include "lan_bridge.h"
struct SoundStatus { bool ready,busy;int volume;uint32_t plays,errors,bytes; };
bool soundBegin(int volume);
void soundVolume(int volume);
int soundVolume();
void soundPreview();
void soundUpdate(const LanState& state);
void soundReset();
SoundStatus soundStatus();
