#include "midi_io.h"
#include <MIDI.h>
#include <TeensyThreads.h>
#include "spsc_queue.h"

/**
 * MIDI I/O Implementation
 *
 * KEY DESIGN DECISIONS:
 *
 * 1. WHY SPSC QUEUES?
 *    - Lock-free: No mutex overhead, no priority inversion
 *    - Wait-free: Bounded execution time (critical for real-time)
 *    - Single producer (I/O thread), single consumer (App thread)
 *
 * 2. WHY STORE TIMESTAMPS INSTEAD OF JUST COUNTING?
 *    - Sub-tick precision: Can measure jitter, drift
 *    - Flexible: Can low-pass filter tempo, detect swing
 *    - Future-proof: Enables advanced features (quantization, etc.)
 *
 * 3. WHY SEPARATE CLOCK AND EVENT QUEUES?
 *    - Different data types (uint32_t vs enum)
 *    - Different sizes (many clocks, few events)
 *    - Different lifetimes (clocks drain fast, events persist)
 *
 * 4. WHY 256 CLOCK SLOTS?
 *    - At 120 BPM: 24 ticks/beat × 2 beats/sec = 48 ticks/sec
 *    - 256 slots ≈ 5 seconds of buffer
 *    - Absorbs burst arrivals if app thread stalls briefly
 *    - Power of 2 for fast masking (& instead of %)
 *
 * 5. WHY 32 EVENT SLOTS?
 *    - Transport events are rare (start/stop/continue)
 *    - Even at crazy tempo changes: << 10 events/sec
 *    - 32 slots = 3+ seconds of headroom
 *    - Small footprint (32 bytes)
 */

// Create MIDI instance on Serial8 (RX8=pin34, TX8=pin35)
MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, DIN);

// Lock-free queues using our generic SPSC implementation
static SPSCQueue<uint32_t, 256> clockQueue;  // Timestamps in microseconds
static SPSCQueue<MidiEvent, 32> eventQueue;  // Transport events

// Transport state (volatile for cross-thread visibility)
static volatile bool transportRunning = false;

/**
 * MIDI Clock Handler
 *
 * CALLED BY: MIDI library when clock tick arrives (in I/O thread context)
 *
 * TIMING: Executed in <1 microsecond typically
 * - micros() call: ~0.1µs
 * - Queue push: ~0.5µs (check full, write, increment)
 * - Total: <1µs (well within real-time budget)
 *
 * JITTER NOTE:
 * We timestamp when the MIDI parser calls this handler, NOT when the
 * byte arrives at UART. This introduces ~100-500µs jitter due to:
 * - MIDI parser runs in thread (not ISR)
 * - Other threads/ISRs can preempt
 * - This is acceptable because we calculate BPM per-beat (24 ticks),
 *   which averages out the jitter
 */
static void onClock() {
    uint32_t timestamp = micros();

    // Push to queue (returns false if full, which we ignore)
    // TRADEOFF: Dropping ticks vs blocking
    // - Dropping is real-time safe (no blocking)
    // - We have 5s buffer, if app stalls that long, we have bigger problems
    // - Future improvement: Count overruns and report as error
    clockQueue.push(timestamp);
}

/**
 * Transport Handlers
 *
 * MIDI START: Sequencer started from beginning
 * MIDI CONTINUE: Sequencer resumed from pause
 * MIDI STOP: Sequencer stopped
 *
 * WHY SEPARATE START AND CONTINUE?
 * - START should reset state (tick counter, BPM, etc.)
 * - CONTINUE should resume with existing state
 * - App logic decides how to handle each
 */
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
    /**
     * I/O THREAD STRATEGY:
     *
     * 1. Pump MIDI parser until no more bytes
     * 2. Yield CPU to other threads
     * 3. Repeat forever
     *
     * WHY YIELD AFTER EACH READ BURST?
     * - MIDI bytes arrive slowly (31250 baud = ~32µs per byte)
     * - Once buffer is empty, no point busy-waiting
     * - Yielding lets app thread run, improves responsiveness
     * - We'll get scheduled again in ~2ms (time slice)
     *
     * TRADEOFF: Latency vs CPU efficiency
     * - No yield: <50µs latency, but wastes CPU in busy-wait
     * - Yield after burst: ~1-2ms latency, but efficient CPU usage
     * - For MIDI clock (arrives every ~20ms at 120 BPM), 2ms is fine
     */
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
