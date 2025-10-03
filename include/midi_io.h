#pragma once

#include <Arduino.h>

/**
 * @file midi_io.h
 * @brief MIDI I/O layer with real-time safe clock and event handling
 *
 * ARCHITECTURE:
 * - Runs in dedicated I/O thread (high priority, minimal latency)
 * - Receives DIN MIDI on Serial8 (pins RX8=34, TX8=35)
 * - Parses MIDI messages via FortySevenEffects MIDI library
 * - Pushes clock ticks and transport events to lock-free SPSC queues
 * - Consumer (App thread) drains queues at leisure
 *
 * REAL-TIME SAFETY:
 * - No blocking, no allocation in handlers
 * - Lock-free queues for cross-thread communication
 * - Handlers execute in microseconds (ISR-like)
 *
 * WHY SEPARATE I/O THREAD?
 * - MIDI parsing is bursty (sudden flood of messages)
 * - Dedicated thread ensures we never miss clock ticks
 * - Decouples I/O latency from app logic (BPM calc, UI, etc.)
 * - If app thread stalls (e.g., Serial.print), MIDI keeps flowing
 */

// Transport event types
enum class MidiEvent : uint8_t {
    START = 1,    // Sequencer started
    STOP = 2,     // Sequencer stopped
    CONTINUE = 3  // Sequencer continued from pause
};

/**
 * @brief MIDI I/O subsystem
 *
 * THREAD MODEL:
 * - Producer: I/O thread calls push*() in MIDI handlers
 * - Consumer: App thread calls pop*() and running()
 */
namespace MidiIO {
    /**
     * @brief Initialize MIDI I/O (call once from setup)
     *
     * WHAT IT DOES:
     * - Configures Serial8 @ 31250 baud (MIDI standard)
     * - Registers clock/transport handlers
     * - Prepares queues for operation
     *
     * @note Must be called BEFORE starting threads
     */
    void begin();

    /**
     * @brief I/O thread entry point (runs forever)
     *
     * WHAT IT DOES:
     * - Continuously pumps MIDI parser via DIN.read()
     * - Yields CPU when no data available (efficient scheduling)
     * - Never returns (infinite loop)
     *
     * PERFORMANCE:
     * - Yields after each DIN.read() burst to avoid hogging CPU
     * - I/O thread gets ~2ms slices (configurable in main)
     * - At 120 BPM, clock ticks arrive every ~20ms (plenty of headroom)
     */
    void threadLoop();

    /**
     * @brief Pop a transport event (CONSUMER side)
     *
     * REAL-TIME SAFE: O(1), no blocking
     *
     * @param outEvent Output parameter for the event
     * @return true if event was popped, false if queue empty
     */
    bool popEvent(MidiEvent& outEvent);

    /**
     * @brief Pop a clock timestamp (CONSUMER side)
     *
     * REAL-TIME SAFE: O(1), no blocking
     *
     * @param outMicros Output parameter for timestamp in microseconds
     * @return true if timestamp was popped, false if queue empty
     */
    bool popClock(uint32_t& outMicros);

    /**
     * @brief Check if transport is running
     *
     * THREAD SAFE: Volatile read, always up-to-date
     *
     * @return true if START or CONTINUE received, false after STOP
     */
    bool running();
}
