#pragma once
#include <Arduino.h>
#include <lvgl.h>

// Eight small canvases keep animation damage off the text and battery regions.
void loadingRingCreate(lv_obj_t* parent);
void loadingRingFrame(bool visible, uint32_t phase_ms, bool clock=false);
