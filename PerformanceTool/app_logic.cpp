#include <TeensyThreads.h>
#include "app_logic.h"
#include "midi_io.h"

// App states, keep only in app thread to avoid sharing with ISRs
static float emaTickMicros = 0.0f; //smoothed microseconds per clock tick (exponential moving avg)
//EMA = (Current BPM × alpha) + (Previous EMA × (1 – alpha))
static float bpm = 0.0f;

void AppLogic::begin() {
  // Init any displays/LEDs here later (e.g., HT16K33, GPIO setups)
}

void AppLogic::threadLoop() {
  //alpha (smoothing constant) is calculated as (2 / (n + 1))
  const float alpha = 0.15f;  // EMA smoothing; higher = snappier but more jitter; lower = smoother but laggier
  //could be better to move to per-beat timing and reduce alpha a bit
  uint32_t lastPrint = 0; //rate-limits serial prints
  uint32_t lastTick = 0;

  for (;;) {
    //does it matter if you pop event or clock queue first???

    // 1) Drain transport events to completion so UI never lags
    // In production build: 1. cache a running flag 2. start/stop BPM settling indicator 3. reset smoothing on START, hold BPM display during STOP
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
        uint32_t dt = t - lastTick; //microseconds per tick (delta)
        //why does it work across wraparound? It's unsigned math, wrap is ~71 minutes at 1 microsecond resolution, well beyond any dt. What does this mean???
        emaTickMicros = (emaTickMicros == 0.0f) ? dt : (1.f - alpha)*emaTickMicros + alpha*dt; //calculate EMA for smoothing
        bpm = (emaTickMicros > 0) ? (60.f * 1e6f) / (emaTickMicros * 24.f) : 0.f; //24 MIDI clocks per beat = microseconds per beat = emaTickMicros * 24
        //for production, average per beat
        //sum 24 dt's, or subtract tick[0] from tick[24] to get microseconds/beat, then EMA the beat-level number
        //that knocks down jitter dramatically vs per-tick. there's jitter since the ticks are parsed in a thead, not with an interrupt at arrival time
        //what does this mean???
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
      //Why? %f has a big call chain with double math, big temporary buffers, etc.
      //Big stack frame = more stack per interrupt = easier to overflow.

      //instead, convert to fixed-point integer
      //avoids heavy float math
      //uses tiny stack, and detirminisic
      int bpm_x100 = (int)(bpm * 100.0f + 0.5f);
      Serial.print("BPM="); Serial.print(bpm_x100/100); Serial.print('.');
      int frac = abs(bpm_x100 % 100);
      if (frac < 10) Serial.print('0');
      Serial.print(frac);
      Serial.print(" running=");
      Serial.println(MidiIO::running());

      // TODO: write to your 7-seg here using latest bpm
    }

    threads.delay(2); //yields for ~2ms so the thread doesn't hog a full timeslice when idle
    //why delay instead of yield???
  }
}
