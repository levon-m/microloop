#include "app_logic.h"
#include "midi_io.h"

// App-side state
static float emaTickUs = 0.0f;  // exponential moving avg of tick period
static float bpm = 0.0f;

void AppLogic::begin() {
  // Init any displays/LEDs here later (e.g., HT16K33, GPIO setups)
}

void AppLogic::threadLoop() {
  const float alpha = 0.15f;  // EMA smoothing; higher = snappier BPM
  uint32_t lastPrint = 0;
  uint32_t lastTick = 0;

  for (;;) {
    // 1) Drain transport events
    MidiEvent ev;
    while (MidiIO::popEvent(ev)) {
      if      (ev == EV_START) Serial.println("onStart");
      else if (ev == EV_STOP)  Serial.println("onStop");
      else if (ev == EV_CONT)  Serial.println("onContinue");
      // TODO: update UI flags for running state if you want
    }

    // 2) Consume clock ticks and update BPM
    uint32_t t;
    while (MidiIO::popClock(t)) {
      if (lastTick) {
        uint32_t dt = t - lastTick; // us per tick
        emaTickUs = (emaTickUs == 0.0f) ? dt : (1.f - alpha)*emaTickUs + alpha*dt;
        bpm = (emaTickUs > 0) ? (60.f * 1e6f) / (emaTickUs * 24.f) : 0.f;
      }
      lastTick = t;

      // Example: LED on downbeat (every 24 ticks)
      // Keep a local counter if you want beat flashes
      // (or compute from cumulative tick count you keep here)
    }

    // 3) Periodic output / UI refresh (Serial, 7-seg, LED)
    if (millis() - lastPrint > 250) {
      lastPrint = millis();
      Serial.printf("BPM=%.2f running=%d\n", bpm, MidiIO::running());
      // TODO: write to your 7-seg here using latest bpm
    }

    delay(2); // small sleep
  }
}
