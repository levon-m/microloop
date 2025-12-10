#include "MidiInput.h"
#include <TeensyThreads.h>
#include "SpscQueue.h"
#include "Trace.h"

// MIDI Real-Time message bytes (single-byte, can appear anywhere in stream)
static constexpr uint8_t MIDI_CLOCK    = 0xF8;
static constexpr uint8_t MIDI_START    = 0xFA;
static constexpr uint8_t MIDI_CONTINUE = 0xFB;
static constexpr uint8_t MIDI_STOP     = 0xFC;

// Lock-free queues using our generic SPSC implementation
static SpscQueue<uint32_t, 256> clockQueue;  // Timestamps in microseconds
static SpscQueue<MidiEvent, 32> eventQueue;  // Transport events

// Transport state (volatile for cross-thread visibility)
static volatile bool transportRunning = false;

// Public API Implementation

void MidiInput::begin() {
    // Initialize Serial8 at MIDI baud rate (31250)
    // Using raw serial instead of MIDI library for minimal latency
    // This avoids parsing overhead from note/CC messages meant for other devices
    Serial8.begin(31250);
}

void MidiInput::threadLoop() {
    for (;;) {
        // Process all available bytes with minimal latency
        // Real-time messages (clock, start, stop, continue) are single-byte
        // and can appear anywhere in the MIDI stream, even mid-message
        while (Serial8.available()) {
            // Capture timestamp BEFORE reading byte for best accuracy
            uint32_t timestamp = micros();
            uint8_t byte = Serial8.read();

            // Only process real-time messages, ignore everything else
            // This makes us immune to note/CC traffic from other MIDI tracks
            switch (byte) {
                case MIDI_CLOCK:
                    TRACE(TRACE_MIDI_CLOCK_RECV);
                    if (clockQueue.push(timestamp)) {
                        TRACE(TRACE_MIDI_CLOCK_QUEUED, clockQueue.size());
                    } else {
                        TRACE(TRACE_MIDI_CLOCK_DROPPED);
                    }
                    break;

                case MIDI_START:
                    transportRunning = true;
                    eventQueue.push(MidiEvent::START);
                    break;

                case MIDI_STOP:
                    transportRunning = false;
                    eventQueue.push(MidiEvent::STOP);
                    break;

                case MIDI_CONTINUE:
                    transportRunning = true;
                    eventQueue.push(MidiEvent::CONTINUE);
                    break;

                default:
                    // Ignore all other MIDI data (notes, CCs, etc.)
                    // These are meant for other devices on the MIDI chain
                    break;
            }
        }

        // Yield to other threads
        threads.yield();
    }
}

bool MidiInput::popEvent(MidiEvent& outEvent) {
    // SPSC queue pop is lock-free and O(1)
    return eventQueue.pop(outEvent);
}

bool MidiInput::popClock(uint32_t& outMicros) {
    // SPSC queue pop is lock-free and O(1)
    return clockQueue.pop(outMicros);
}

bool MidiInput::running() {
    // Volatile read ensures we see latest value
    // No need for atomic/mutex because:
    // - Single-word read is atomic on ARM Cortex-M7
    // - Worst case: We're 1 tick stale (20ms at 120 BPM), negligible
    return transportRunning;
}