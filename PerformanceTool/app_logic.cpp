#include <TeensyThreads.h>
#include "app_logic.h"
#include "midi_io.h"

// App states, keep only in app thread to avoid sharing with ISRs
static float bpm = 0.0f;
static uint8_t tickCount = 0;
static uint32_t tickStart = 0;
static uint32_t lastPrint = 0; //rate-limits serial prints
constexpr float alpha = 0.30f; // EMA smoothing; higher = snappier but more jitter; lower = smoother but laggier

void AppLogic::begin() {
  // Init any displays/LEDs here later (e.g., HT16K33, GPIO setups)
}

void AppLogic::threadLoop() {
  for (;;) {
    //does it matter if you pop event or clock queue first???

    // 1) Drain transport events to completion so UI never lags
    // In production build: 1. cache a running flag 2. start/stop BPM settling indicator 3. reset smoothing on START, hold BPM display during STOP
    MidiEvent event;
    while (MidiIO::popEvent(event)) {
      if (event == EVENT_START) {
        Serial.println("onStart");
        bpm = 0.0f;
        tickCount = 0;
      }
      else if (event == EVENT_STOP) {
        Serial.println("onStop");
      }
      else if (event == EVENT_CONT) {
        Serial.println("onContinue");
      }
      // TODO: update UI flags for running state if you want
    }

    // 2) Consume clock ticks and update BPM
    //problem: calculating BPM from single tick deltas with a jittery incoming tick
    //(due to onClock() being called via thread, not at exact UART arrival), introduces amplified jitter
    //there's jitter since the ticks are parsed in a thead, not with an interrupt at arrival time
    //solution: derive BPM per-beat, not per-tick + smoothing
    uint32_t tick;
    while (MidiIO::popClock(tick)) {
      //record start of 24-tick interval window
      if (tickCount == 0) {
        tickStart = tick; //tick[0] timestamp
      }
      tickCount++;

      if (tickCount >= 25) { //ticks[0]..ticks[24] ⇒ 24 intervals, previously was '>= 24', gave an offset bpm since we have to measure the whole beat
        uint32_t beatMicros = tick - tickStart;
        tickCount = 1; //we've just seen tick[24], make it the new tick[0]
        tickStart = tick; //start next window at current tick

        //reject impossible tempos
        if (beatMicros > 20000 && beatMicros < 2000000) { //30–3000 BPM bounds
          float bpmRaw = (60.0f * 1e6f) / (float)beatMicros; //instantaneous BPM from most recent full-beat BEFORE smoothing
          bpm = (bpm == 0.0f) ? bpmRaw : (1.f - alpha) * bpm + alpha * bpmRaw;
        }
      }

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
