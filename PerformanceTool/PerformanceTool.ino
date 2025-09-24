#include <Audio.h>
#include <MIDI.h>
#include <TeensyThreads.h>
#include "SGTL5000.h"
#include "midi_io.h"
#include "app_logic.h"

AudioInputI2S i2s1;
AudioOutputI2S i2s2;
AudioConnection patchCord1(i2s1, 0, i2s2, 0);
AudioConnection patchCord2(i2s1, 1, i2s2, 1);
SGTL5000 codec;

void IOThread() { MidiIO::threadLoop(); }
void AppThread() { AppLogic::threadLoop(); }

void setup() {
  Serial.begin(115200);

  if (CrashReport) { while (!Serial && millis() < 4000); Serial.print(CrashReport); }

  AudioMemory(12);
  codec.enable();

  // DIN MIDI on Serial8 (RX8=34, TX8=35)
  MidiIO::begin();
  AppLogic::begin();

  // Start threads
  int ioId = threads.addThread(IOThread, 2048);   // I/O thread
  int appId = threads.addThread(AppThread, 3072); // App/UI thread

  if (ioId < 0 || appId < 0) {
    Serial.println("addThread failed");
  } 

  //make io super responsive but polite, app gets a bit more contiguous time
  //smaller slices improve responsiveness at the cost of more context switches
  //currently app work is very light, smaller io slice gets more frequent turns
  //moderate slice for app so it can finish a display update without being chopped too often
  //reach for this kind of thing if UI work occasionally takes longer, like with big graphic/SD card writes, etc. and notice MIDI jitter or missed ticks. (could I measure that somehow?)
  //threads.setTimeSlice(ioId, 2);
  //threads.setTimeSlice(appId, 5);

  Serial.println("MIDI I/O + App threads started.");
}

void loop() {
  // empty; all work in threads (Audio runs in its own ISR)
}
