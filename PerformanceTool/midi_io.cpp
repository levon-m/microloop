#include <MIDI.h>
#include "midi_io.h"

// Use Serial8 (pins RX8=34, TX8=35) to create global MIDI object DIN
MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, DIN);

// ---- 2 SPSC rings for clocks & transport events ----

static constexpr uint16_t CLK_SIZE = 256; //size, power of two so we can mask indices with & instead of % (index & (size - 1)), faster
//256 ticks is small (~2s at 120BPM), for heavier work consider 512 or 1024
//shock absorber between producer and consumer, if consumer ever falls behind the ticks piles up in the ring, buying time before we start losing data (~5s with 120BPM)
//larger ring doesn't add latency, just means we won't drop ticks if consumer stalls

static volatile uint32_t clkMicros[CLK_SIZE]; //stores timestamps in microseconds for each incoming tick
//timestamps better than counting ticks. Sub-BPM precision, can low-pass the period(?), can measure jitter(?), can schedule app logic exactly on downbeats

static volatile uint16_t clkWrite = 0; //incremented by producer, where the producer puts the next item
static volatile uint16_t clkRead = 0; //incremented by consumer, where the consumer takes the next item
//volatile since producer/consumer run in different contexts, want every access to go to memory, not regisers via the compiler
//lock-free because only one writer and one reader run concurrently, cannot access each other, no contention
//wait-free because neither blocks the other, producer never waits for the consumer to finish a print, consumer doesn't wait for interrupts
// TODO: add optional overrun detection (write - read) > size

static constexpr uint8_t EVENT_SIZE = 32; //events are sparse and small, tiny buffer is good enough
static volatile uint8_t eventBuffer[EVENT_SIZE];
static volatile uint8_t eventWrite = 0;
static volatile uint8_t eventRead = 0;

static volatile bool transport_running = false; //current transport state, volatile so both threads see updates immediately

// ---- Handlers ----

//when a tick arrives
static void onClock() {
  //with volatile, clkWrite would be read twice. Putting it into a local variable reads once and guarantees the same value is used in both lines,
  //which isn't really an issue in this case
  uint16_t w = clkWrite;
  clkMicros[w & (CLK_SIZE-1)] = micros();
  clkWrite = w + 1;

  //clkMicros[clkWrite & (CLK_SIZE-1)] = micros();
  //clkWrite++;
  
  //overrun guard would also go here
}

//for transport, push one-byte event
static void onStart() {
  transport_running = true;
  eventBuffer[eventWrite & (EVENT_SIZE-1)] = EVENT_START;
  eventWrite++;
}
static void onStop() {
  transport_running = false;
  eventBuffer[eventWrite & (EVENT_SIZE-1)] = EVENT_STOP;
  eventWrite++;
}
static void onContinue() {
  transport_running = true;
  eventBuffer[eventWrite & (EVENT_SIZE-1)] = EVENT_CONT;
  eventWrite++;
}

// ---- Public API ----
void MidiIO::begin() {
  DIN.begin(MIDI_CHANNEL_OMNI); //sets Serial8 to 31250 baud
  DIN.setHandleClock(onClock);
  DIN.setHandleStart(onStart);
  DIN.setHandleStop(onStop);
  DIN.setHandleContinue(onContinue);
}

void MidiIO::threadLoop() {
  //IMPORTANT: ISRs vs TeensyThreads
  //Teensy Audio Library engine runs on ISRs via hardware timers/DMA.
  //This pre-empts normal code, including TeensyThreads.
  //So Audio ISR is the highest priority work, runs even if thread is busy. My IO/app code are regular threads that get CPU BETWEEN interrupts

  //TeensyThreads is preemptive, each thread gets a time slice
  //switches threads from the SysTick ISR, default slice time is 10ms, might have to change that, older docs claim 100ms, recommended to override it
  //round-robin scheduling has equal priority
  //SPSC rings merely hold data until the app thread drains them
  //Due to audio interrupts, yields, and sleeps, the scheduling pattern can and will vary, although it tends to be alternating
  //This is fine since my design doesn't depend on a fixed order, SPSC queues decouple producer and consumer.

  for (;;) {
    while (DIN.read()) {} //parses pending UART bytes, runs handlers as recognized MIDI messages

    threads.yield(); //loops until INTERNAL buffer (pumping DIN.read()) is empty, then yield lets other threads run BEFORE its time slice ends for efficiency.
    //This way we don't burn CPU on a busy-wait
    //yield() (Teensy core) vs theads.yield() (TeensyThreads)? should be the same
  }
}

//non-blocking dequeues
bool MidiIO::popEvent(MidiEvent &outEvent) {
  if (eventRead == eventWrite) return false; //return false when queue is empty
  outEvent = static_cast<MidiEvent>(eventBuffer[eventRead & (EV_SZ-1)]);
  eventRead++;
  return true;
}
bool MidiIO::popClock(uint32_t &outMicros) {
  if (clkRead == clkWrite) return false; //return false when queue is empty
  outMicros = clkMicros[clkRead & (CLK_SIZE-1)];
  clkRead++;
  return true;
}
bool MidiIO::running() {
  return transport_running;
}
