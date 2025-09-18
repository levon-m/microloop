#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <USBHost_t36.h>
#include "SGTL5000.h"

AudioInputI2S            i2s1;
AudioOutputI2S           i2s2;
AudioConnection          patchCord1(i2s1, 0, i2s2, 0);
AudioConnection          patchCord2(i2s1, 1, i2s2, 1);
SGTL5000                 codec;

USBHost usb;
MIDIDevice midi(myusb);

volatile uint32_t clkCount = 0; // ? Why unt32_t?: counter that may grow large. 32-bit unsigned so it doesn't overflow fast.
// 16-bit max overflows in 9 minutes. 32-bit max overflows in 414 days.
volatile bool running = false; // ? Why volatile?: value can change unexpectedly, don't cache it in a regsiter. always read/write memory

// ? Why do they take uint8_t?: virtual cable number, I don't actually use it. Difference between usbMIDI and USBHost_t36 API's. "You MUST match the signature expected by setHandleClock(...)"
void onClock(/* uint8_t */){ clkCount++; }
void onStart(/* uint8_t */){ running = true; }
void onStop(/* uint8_t */){ running = false; }
void onContinue(/* uint8_t */){ running = true; }

void setup() {
  // For debugging, USB speed, deafults to it anyways, just for convention
  Serial.begin(115200);

  // Audio connections require memory to work.  For more
  // detailed information, see the MemoryAndCpuUsage example
  AudioMemory(12);

  // Enable the audio shield
  codec.enable();

  // Enable MIDI input + clock handlers
  usb.begin();
  midi.setHandleClock(onClock);
  midi.setHandleStart(onStart);
  midi.setHandleStop(onStop);
  midi.setHandleContinue(onContinue);
}

uint32_t lastPrint = 0;

void loop() {
  // Services USB host controllers and drivers, moves bytes from USB hardware to driver's buffers and progresses device states
  // Since Arduino/Teensy is single threaded by default, call it repeatedly to update states and/or call callback functions
  usb.Task();
  // Asks the MIDI driver to parse pending USB-MIDI packets and then run MIDI handlers 
  midi.read();

  // Print counters every ~500 ms so you "see" traffic
  if (millis() - lastPrint > 500) {
    lastPrint = millis();
    Serial.printf("running: %d  clocks: %lu\n", running, clkCount);
  }
}

// -- Notes --

// Q: Does TeensyThreads just ‘simulate’ threads?
// A: Teensy 4.1 has one CPU core, but TeensyThreads is preemptive: a timer interrupt context-switches between your threads.
// It’s real multithreading in the scheduling sense (time-sliced on one core), not parallel execution on multiple cores.
// You don’t need to use loop() if you move work into your threads. 

// File Layout
// usb_io.cpp/.h - tight loop with myusb.Task(); midi.read();
// app_logic.cpp/.h - drains the event queue, computes BPM, blinks LED, updates display. loop() can be empty
// main.ino - pins, setup, start threads




