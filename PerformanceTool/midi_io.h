#pragma once
#include <Arduino.h>

// Simple event ids (transport)
enum MidiEvent : uint8_t { EVENT_START=1, EVENT_STOP=2, EVENT_CONT=3 };

// Init/start the MIDI I/O layer (DIN on Serial8 by default)
namespace MidiIO {
  // Call once from setup (before starting threads)
  void begin();  // config Serial8 @ 31250, attach handlers

  // Thread entry: pumps the MIDI parser; returns only if the thread ends.
  void threadLoop();

  // --- Consumer-side (app thread) non-blocking drains ---
  // Returns true if an event was popped into outEv
  bool popEvent(MidiEvent &outEvent);

  // Returns true if a clock timestamp (micros) was popped into outUs
  bool popClock(uint32_t &outMicros);

  // Current running flag (set by transport handlers)
  bool running();
}
