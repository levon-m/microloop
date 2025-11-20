#include "MidiIO.h"
#include <MIDI.h>
#include <TeensyThreads.h>
#include "SPSCQueue.h"
#include "Trace.h"

// Create MIDI instance on Serial8 (RX8=pin34, TX8=pin35)
MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, DIN);

// Lock-free queues using our generic SPSC implementation
static SPSCQueue<uint32_t, 256> clockQueue;  // Timestamps in microseconds
static SPSCQueue<MidiEvent, 32> eventQueue;  // Transport events

// Transport state (volatile for cross-thread visibility)
static volatile bool transportRunning = false;

static void onClock() {
    uint32_t timestamp = micros();
    TRACE(TRACE_MIDI_CLOCK_RECV);

    // Push to queue (returns false if full, which we ignore)
    // TRADEOFF: Dropping ticks vs blocking
    // - Dropping is real-time safe (no blocking)
    // - We have 5s buffer, if app stalls that long, we have bigger problems
    // - Future improvement: Count overruns and report as error
    if (clockQueue.push(timestamp)) {
        TRACE(TRACE_MIDI_CLOCK_QUEUED, clockQueue.size());
    } else {
        TRACE(TRACE_MIDI_CLOCK_DROPPED);
    }
}

static void onStart() {
    transportRunning = true;
    eventQueue.push(MidiEvent::START);
}

static void onStop() {
    transportRunning = false;
    eventQueue.push(MidiEvent::STOP);
}

static void onContinue() {
    transportRunning = true;
    eventQueue.push(MidiEvent::CONTINUE);
}

// Public API Implementation

void MidiIO::begin() {
    // Initialize MIDI library
    // MIDI_CHANNEL_OMNI = respond to all channels
    // This sets Serial8 to 31250 baud (MIDI standard)
    DIN.begin(MIDI_CHANNEL_OMNI);

    // Register handlers
    // These will be called from threadLoop() when messages are parsed
    DIN.setHandleClock(onClock);
    DIN.setHandleStart(onStart);
    DIN.setHandleStop(onStop);
    DIN.setHandleContinue(onContinue);
}

void MidiIO::threadLoop() {
    for (;;) {
        // Read and parse all pending MIDI bytes
        // DIN.read() returns true if a message was parsed
        // Handlers (onClock, etc.) are called inside DIN.read()
        while (DIN.read()) {
            // Keep pumping until UART buffer is empty
        }

        // Yield to other threads
        // This is TeensyThreads yield, NOT Arduino yield
        // Immediately gives up remaining time slice
        threads.yield();
    }
}

bool MidiIO::popEvent(MidiEvent& outEvent) {
    // SPSC queue pop is lock-free and O(1)
    return eventQueue.pop(outEvent);
}

bool MidiIO::popClock(uint32_t& outMicros) {
    // SPSC queue pop is lock-free and O(1)
    return clockQueue.pop(outMicros);
}

bool MidiIO::running() {
    // Volatile read ensures we see latest value
    // No need for atomic/mutex because:
    // - Single-word read is atomic on ARM Cortex-M7
    // - Worst case: We're 1 tick stale (20ms at 120 BPM), negligible
    return transportRunning;
}