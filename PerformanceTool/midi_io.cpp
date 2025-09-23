#include "midi_io.h"
#include <MIDI.h>

// Use Serial8 (pins RX8=34, TX8=35). Change here if you pick another UART.
MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, DIN);

// --- SPSC rings for clocks and events (power-of-two sizes) ---
static constexpr uint16_t CLK_SZ = 256;
static volatile uint32_t clkUs[CLK_SZ];
static volatile uint16_t clkW=0, clkR=0;

static constexpr uint8_t EV_SZ = 32;
static volatile uint8_t evBuf[EV_SZ];
static volatile uint8_t evW=0, evR=0;

static volatile bool g_running = false;

// ---- Handlers: tiny, no prints ----
static void onClock() {
  uint16_t w = clkW;
  clkUs[w & (CLK_SZ-1)] = micros();
  clkW = w + 1;
}
static void onStart()    { g_running = true;  evBuf[evW & (EV_SZ-1)] = EV_START; evW++; }
static void onStop()     { g_running = false; evBuf[evW & (EV_SZ-1)] = EV_STOP;  evW++; }
static void onContinue() { g_running = true;  evBuf[evW & (EV_SZ-1)] = EV_CONT;  evW++; }

// ---- Public API ----
void MidiIO::beginDIN8() {
  DIN.begin(MIDI_CHANNEL_OMNI);     // sets Serial8 to 31250 baud
  DIN.setHandleClock(onClock);
  DIN.setHandleStart(onStart);
  DIN.setHandleStop(onStop);
  DIN.setHandleContinue(onContinue);
}

void MidiIO::threadLoop() {
  for (;;) {
    while (DIN.read()) { /* handlers did the work */ }
    // be nice to others (adjust to taste):
    yield();
  }
}

bool MidiIO::popEvent(MidiEvent &outEv) {
  if (evR == evW) return false;
  outEv = static_cast<MidiEvent>(evBuf[evR & (EV_SZ-1)]);
  evR++;
  return true;
}
bool MidiIO::popClock(uint32_t &outUs) {
  if (clkR == clkW) return false;
  outUs = clkUs[clkR & (CLK_SZ-1)];
  clkR++;
  return true;
}
bool MidiIO::running() { return g_running; }
