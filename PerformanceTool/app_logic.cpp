#include <TeensyThreads.h>
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
    MidiEvent event;
    while (MidiIO::popEvent(event)) {
      if      (event == EVENT_START) Serial.println("onStart");
      else if (event == EVENT_STOP)  Serial.println("onStop");
      else if (event == EVENT_CONT)  Serial.println("onContinue");
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

      //CAUSED A STACK OVERFLOW
      //Serial.printf("BPM=%.2f running=%d\n", bpm, MidiIO::running());

      //instead, convert to fixed-point number
      int bpm_x100 = (int)(bpm * 100.0f + 0.5f);
      Serial.print("BPM="); Serial.print(bpm_x100/100); Serial.print('.');
      int frac = abs(bpm_x100 % 100);
      if (frac < 10) Serial.print('0');
      Serial.print(frac);
      Serial.print(" running=");
      Serial.println(MidiIO::running());

      // TODO: write to your 7-seg here using latest bpm
    }

    threads.delay(2); // small sleep
  }
}
