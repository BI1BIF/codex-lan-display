#pragma once
#include <Arduino.h>

struct LanState {
  bool wifi = false, linked = false, portal = false;
  bool metrics_valid = false, credits_valid = false, tasks_valid = false;
  int remaining = 0, credits = 0, running = 0, waiting = 0;
  uint32_t received = 0, reset_seconds = 0;
  uint32_t unix_time = 0;
  int32_t utc_offset = 0;
  char state[16] = "unknown";
  char ssid[33] = "", ip[16] = "", host[16] = "";
  char ap[32] = "", password[20] = "";
  bool header_valid = false;
  uint8_t header_bitmap[1600] = {}; // 200 x 32, four alpha levels.
};
void lanBegin();
LanState lanSnapshot();
void lanOpenPortal();
