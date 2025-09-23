#pragma once
#include <Arduino.h>

namespace AppLogic {
  // Call once from setup after MIDI I/O begin
  void begin();

  // Thread entry: drains queues, computes BPM, prints/updates UI
  void threadLoop();
}
