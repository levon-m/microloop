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
 * - Global quantization settings
 * - Future: BPM calculation, looper logic, UI
 *
 * THREAD CONTEXT:
 * - Runs in App thread (lower priority than I/O)
 * - Can afford to be slower (not real-time critical)
 * - Safe to do Serial.print, UI updates, etc.
 */

/**
 * Global quantization options
 * Order matches visual layout from left to right: 1/32, 1/16, 1/8, 1/4
 */
enum class Quantization : uint8_t {
    QUANT_32 = 0,  // 1/32 note
    QUANT_16 = 1,  // 1/16 note (default)
    QUANT_8 = 2,   // 1/8 note
    QUANT_4 = 3    // 1/4 note
};

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
     * - Handle encoder input for quantization menu
     * - Periodic status printing (debug)
     *
     * PERFORMANCE:
     * - Delays 2ms per loop to yield CPU
     * - At 120 BPM, clocks arrive every ~20ms (plenty of headroom)
     */
    void threadLoop();

    /**
     * @brief Get current global quantization setting
     * @return Current quantization value
     */
    Quantization getGlobalQuantization();

    /**
     * @brief Set global quantization setting
     * @param quant New quantization value
     */
    void setGlobalQuantization(Quantization quant);
}
