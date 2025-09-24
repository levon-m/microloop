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
  MidiIO::begin();
  AppLogic::begin();

  // Start threads
  int ioId = threads.addThread([]{ MidiIO::threadLoop(); }, 2048);   // I/O thread
  int appId = threads.addThread([]{ AppLogic::threadLoop(); }, 3072); // App/UI thread

  //make io super responsive but polite, app gets a bit more contiguous time
  //smaller slices improve responsiveness at the cost of more context switches
  //currently app work is very light, smaller io slice gets more frequent turns
  //moderate slice for app so it can finish a display update without being chopped too often
  //reach for this kind of thing if UI work occasionally takes longer, like with big graphic/SD card writes, etc. and notice MIDI jitter or missed ticks. (could I measure that somehow?)
  //threads.setTimeSlice(ioId, 2);
  //threads.setTimeSlice(appId, 5);

  Serial.println("TeensyThreads: MIDI I/O + App threads started.");
}

void loop() {
  // empty; all work in threads (Audio runs in its own ISR)
}
