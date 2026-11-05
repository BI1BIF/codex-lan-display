#pragma once
#include "lan_bridge.h"
void uiCreate(void (*set_brightness)(int),int brightness,void (*set_volume)(int),int volume,void (*preview)());
void uiUpdate(const LanState& state);
void uiBattery(int percent, bool charging, bool present);
void uiSettings();
void uiHome();
bool uiSettingsVisible();
void uiTestPhase(bool enabled,uint32_t phase_ms);
void uiTestStandby(uint32_t age,uint32_t epoch);
void uiBrightness(int value);
void uiVolume(int value);
int uiBrightnessValue();
