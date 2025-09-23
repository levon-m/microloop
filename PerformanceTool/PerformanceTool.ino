#include <Audio.h>
#include <MIDI.h>
#include "SGTL5000.h"
#include "midi_io.h"
#include "app_logic.h"

AudioInputI2S i2s1;
AudioOutputI2S i2s2;
AudioConnection patchCord1(i2s1, 0, i2s2, 0);
AudioConnection patchCord2(i2s1, 1, i2s2, 1);
SGTL5000 codec;

void setup() {
  Serial.begin(115200);

  AudioMemory(12);
  codec.enable();

  // DIN MIDI on Serial8 (RX8=34, TX8=35)
  MidiIO::beginDIN8();
  AppLogic::begin();

  // Start threads
  threads.addThread([]{ MidiIO::threadLoop(); }, 2048);   // I/O thread
  threads.addThread([]{ AppLogic::threadLoop(); }, 3072); // App/UI thread

  Serial.println("TeensyThreads: MIDI I/O + App threads started.");
}

void loop() {
  // empty; all work in threads (Audio runs in its own ISR)
}
