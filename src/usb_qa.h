#pragma once
#include <Arduino.h>
#include <lvgl.h>
bool usbQaActive();
void usbQaPoll();
void usbQaPowerReport();
bool usbQaTouch(lv_indev_data_t* data);
void usbQaRecord(int x,int y,int w,int h,const lv_color_t* pixels);
