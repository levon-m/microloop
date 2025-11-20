/**
 * trace.h - Lightweight lock-free trace utility for real-time debugging
 *
 * USAGE:
 *   TRACE(EVENT_ID, value);  // Record event with timestamp
 *   Trace::dump();           // Print trace buffer to Serial (in app thread only!)
 *   Trace::clear();          // Reset trace buffer
 *
 * DESIGN:
 * - Wait-free: Safe to call from ISR, I/O thread, app thread
 * - Zero allocation: Static circular buffer (power-of-2 size)
 * - Minimal overhead: ~10-20 CPU cycles per trace
 * - Overflow handling: Overwrites oldest events
 *
 * PERFORMANCE:
 * - Each trace event: 8 bytes (timestamp + id + value)
 * - Buffer size: 1024 events = 8KB RAM
 * - At 1000 traces/sec: ~1 second of history before wraparound
 *
 * COMPILE-TIME CONTROL:
 * - Define TRACE_ENABLED=0 to compile out all tracing (zero overhead)
 * - Default: Enabled in all builds
 */

#pragma once

#include <Arduino.h>

// Compile-time enable/disable
#ifndef TRACE_ENABLED
#define TRACE_ENABLED 1
#endif

// Trace event IDs (add your own!)
enum TraceEventId : uint16_t {
    // MIDI events (1-99)
    TRACE_MIDI_CLOCK_RECV = 1,      // MIDI clock tick received in ISR
    TRACE_MIDI_CLOCK_QUEUED = 2,    // Clock tick queued (value = queue size)
    TRACE_MIDI_CLOCK_DROPPED = 3,   // Clock tick dropped (queue full)
    TRACE_MIDI_START = 10,
    TRACE_MIDI_STOP = 11,
    TRACE_MIDI_CONTINUE = 12,

    // Beat tracking (100-199)
    TRACE_BEAT_START = 100,         // New beat started (value = beat number)
    TRACE_BEAT_LED_ON = 101,
    TRACE_BEAT_LED_OFF = 102,
    TRACE_TICK_PERIOD_UPDATE = 103, // Updated avgTickPeriodUs (value = period/10 in µs)

    // App thread (200-299)
    TRACE_APP_LOOP_START = 200,     // App thread loop iteration
    TRACE_APP_CLOCK_DRAIN = 201,    // Draining clock queue (value = count drained)
    TRACE_APP_EVENT_DRAIN = 202,    // Draining event queue (value = count drained)

    // Audio (300-399)
    TRACE_AUDIO_CALLBACK = 300,     // Audio callback invoked
    TRACE_AUDIO_UNDERRUN = 301,     // Audio buffer underrun

    // TimeKeeper (400-499)
    TRACE_TIMEKEEPER_SYNC = 400,         // TimeKeeper synced to MIDI (value = BPM)
    TRACE_TIMEKEEPER_TRANSPORT = 401,    // Transport state change (value = new state)
    TRACE_TIMEKEEPER_BEAT_ADVANCE = 402, // Beat counter advanced (value = new beat number)
    TRACE_TIMEKEEPER_SAMPLE_POS = 403,   // Sample position (value = low 16 bits)

    // Choke (500-599)
    TRACE_CHOKE_BUTTON_PRESS = 500,      // Choke button pressed (value = key index)
    TRACE_CHOKE_BUTTON_RELEASE = 501,    // Choke button released (value = key index)
    TRACE_CHOKE_ENGAGE = 502,            // Choke engaged (muting audio)
    TRACE_CHOKE_RELEASE = 503,           // Choke released (unmuting audio)
    TRACE_CHOKE_FADE_START = 504,        // Fade started (value = target gain * 100)
    TRACE_CHOKE_FADE_COMPLETE = 505,     // Fade completed

    // User-defined (600+)
    TRACE_USER = 600,
};

#if TRACE_ENABLED

/**
 * Trace event structure (8 bytes, cache-line friendly)
 */
struct TraceEvent {
    uint32_t timestamp;  // Microsecond timestamp (from micros())
    uint16_t eventId;    // Event ID (see TraceEventId enum)
    uint16_t value;      // Optional event-specific data
};

/**
 * Trace buffer (static singleton)
 */
class Trace {
public:
    // Circular buffer size (must be power of 2 for fast masking)
    static constexpr size_t BUFFER_SIZE = 1024;

    /**
     * Record a trace event (wait-free, safe in ISR)
     *
     * @param eventId Event identifier (see TraceEventId)
     * @param value   Optional 16-bit value (default 0)
     */
    static inline void record(uint16_t eventId, uint16_t value = 0) {
        // Atomically increment index and wrap (bitwise AND is faster than modulo)
        size_t idx = __atomic_fetch_add(&s_writeIdx, 1, __ATOMIC_RELAXED) & (BUFFER_SIZE - 1);

        // Write event (no locks needed - single writer per slot)
        s_buffer[idx].timestamp = micros();
        s_buffer[idx].eventId = eventId;
        s_buffer[idx].value = value;
    }

    /**
     * Dump trace buffer to Serial (ONLY call from app thread!)
     *
     * Prints events in chronological order (oldest to newest).
     * Format: timestamp(µs) | event_id | value | event_name
     */
    static void dump() {
        Serial.println("\n=== TRACE DUMP ===");
        Serial.println("Timestamp(µs) | ID  | Value | Event");
        Serial.println("--------------|-----|-------|------");

        size_t currentIdx = __atomic_load_n(&s_writeIdx, __ATOMIC_RELAXED);
        size_t startIdx = (currentIdx >= BUFFER_SIZE) ? (currentIdx & (BUFFER_SIZE - 1)) : 0;

        for (size_t i = 0; i < BUFFER_SIZE; i++) {
            size_t idx = (startIdx + i) & (BUFFER_SIZE - 1);
            const TraceEvent& e = s_buffer[idx];

            // Skip unwritten slots (timestamp == 0)
            if (e.timestamp == 0) continue;

            // Print event
            Serial.print(e.timestamp);
            Serial.print(" | ");
            Serial.print(e.eventId);
            Serial.print(" | ");
            Serial.print(e.value);
            Serial.print(" | ");
            Serial.println(eventName(e.eventId));
        }

        Serial.println("=== END TRACE ===\n");
    }

    /**
     * Clear trace buffer
     */
    static void clear() {
        for (size_t i = 0; i < BUFFER_SIZE; i++) {
            s_buffer[i].timestamp = 0;
            s_buffer[i].eventId = 0;
            s_buffer[i].value = 0;
        }
        __atomic_store_n(&s_writeIdx, 0, __ATOMIC_RELAXED);
    }

    /**
     * Get human-readable event name (for debugging)
     */
    static const char* eventName(uint16_t eventId) {
        switch (eventId) {
            case TRACE_MIDI_CLOCK_RECV: return "MIDI_CLOCK_RECV";
            case TRACE_MIDI_CLOCK_QUEUED: return "MIDI_CLOCK_QUEUED";
            case TRACE_MIDI_CLOCK_DROPPED: return "MIDI_CLOCK_DROPPED";
            case TRACE_MIDI_START: return "MIDI_START";
            case TRACE_MIDI_STOP: return "MIDI_STOP";
            case TRACE_MIDI_CONTINUE: return "MIDI_CONTINUE";
            case TRACE_BEAT_START: return "BEAT_START";
            case TRACE_BEAT_LED_ON: return "BEAT_LED_ON";
            case TRACE_BEAT_LED_OFF: return "BEAT_LED_OFF";
            case TRACE_TICK_PERIOD_UPDATE: return "TICK_PERIOD_UPDATE";
            case TRACE_APP_LOOP_START: return "APP_LOOP_START";
            case TRACE_APP_CLOCK_DRAIN: return "APP_CLOCK_DRAIN";
            case TRACE_APP_EVENT_DRAIN: return "APP_EVENT_DRAIN";
            case TRACE_AUDIO_CALLBACK: return "AUDIO_CALLBACK";
            case TRACE_AUDIO_UNDERRUN: return "AUDIO_UNDERRUN";
            case TRACE_TIMEKEEPER_SYNC: return "TIMEKEEPER_SYNC";
            case TRACE_TIMEKEEPER_TRANSPORT: return "TIMEKEEPER_TRANSPORT";
            case TRACE_TIMEKEEPER_BEAT_ADVANCE: return "TIMEKEEPER_BEAT_ADVANCE";
            case TRACE_TIMEKEEPER_SAMPLE_POS: return "TIMEKEEPER_SAMPLE_POS";
            case TRACE_CHOKE_BUTTON_PRESS: return "CHOKE_BUTTON_PRESS";
            case TRACE_CHOKE_BUTTON_RELEASE: return "CHOKE_BUTTON_RELEASE";
            case TRACE_CHOKE_ENGAGE: return "CHOKE_ENGAGE";
            case TRACE_CHOKE_RELEASE: return "CHOKE_RELEASE";
            case TRACE_CHOKE_FADE_START: return "CHOKE_FADE_START";
            case TRACE_CHOKE_FADE_COMPLETE: return "CHOKE_FADE_COMPLETE";
            default: return "UNKNOWN";
        }
    }

private:
    // Circular buffer (oldest events get overwritten)
    static TraceEvent s_buffer[BUFFER_SIZE];

    // Write index (atomically incremented, wraps at BUFFER_SIZE)
    static volatile size_t s_writeIdx;
};

// Macro for convenient tracing
#define TRACE(eventId, ...) Trace::record(eventId, ##__VA_ARGS__)

#else  // TRACE_ENABLED == 0

// Compile out tracing entirely (zero overhead)
class Trace {
public:
    static inline void record(uint16_t, uint16_t = 0) {}
    static void dump() {}
    static void clear() {}
    static const char* eventName(uint16_t) { return ""; }
};

#define TRACE(eventId, ...) ((void)0)

#endif  // TRACE_ENABLED
