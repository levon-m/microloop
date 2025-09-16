#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include "SGTL5000.h"

AudioInputI2S            i2s1;
AudioOutputI2S           i2s2;
AudioConnection          patchCord1(i2s1, 0, i2s2, 0);
AudioConnection          patchCord2(i2s1, 1, i2s2, 1);
SGTL5000                 codec;

void setup() {
  // Audio connections require memory to work.  For more
  // detailed information, see the MemoryAndCpuUsage example
  AudioMemory(12);

  // Enable the audio shield
  codec.enable();
}

//elapsedMillis volmsec=0;

void loop() {
  // every 50 ms, adjust the volume
  //if (volmsec > 50) {
  //  float vol = analogRead(15);
  //  vol = vol / 1023.0;
  //  //audioShield.volume(vol); // <-- uncomment if you have the optional
  //  volmsec = 0;               //     volume pot on your audio shield
  //}
}

