#pragma once

#include <Arduino.h>

/**
 * @file app_logic.h
 * @brief Application logic for MicroLoop
 *
 * RESPONSIBILITIES:
 * - Drain MIDI queues (clocks, transport events)
 * - Track beat position (24 ticks = 1 beat)
 * - Drive LED beat indicator
 * - Future: BPM calculation, looper logic, UI
 *
 * THREAD CONTEXT:
 * - Runs in App thread (lower priority than I/O)
 * - Can afford to be slower (not real-time critical)
 * - Safe to do Serial.print, UI updates, etc.
 */

namespace AppLogic {
    /**
     * @brief Initialize application logic
     *
     * WHAT IT DOES:
     * - Configure LED pin as output
     * - Initialize beat tracking state
     * - Set up any displays/UI (future)
     *
     * @note Call once from setup, AFTER MidiIO::begin()
     */
    void begin();

    /**
     * @brief Application thread entry point (runs forever)
     *
     * WHAT IT DOES:
     * - Drain transport event queue
     * - Drain clock queue and track beats
     * - Update LED beat indicator
     * - Periodic status printing (debug)
     *
     * PERFORMANCE:
     * - Delays 2ms per loop to yield CPU
     * - At 120 BPM, clocks arrive every ~20ms (plenty of headroom)
     */
    void threadLoop();
}
