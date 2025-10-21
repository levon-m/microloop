/**
 * input_io.h - Generic input handler that emits Commands
 *
 * PURPOSE:
 * Replaces the choke-specific ChokeIO with a generic input system that emits
 * Command structs. Supports flexible button mappings (momentary, toggle, combos)
 * via a table-driven configuration.
 *
 * ARCHITECTURE:
 * - Dedicated I/O thread (high priority, like MIDI I/O)
 * - Polls Neokey hardware → emits Command structs to lock-free queue
 * - Consumer (App thread) drains queue and routes commands via EffectManager
 *
 * HARDWARE:
 * - Adafruit Neokey 1x4 QT (Seesaw-based, I2C address 0x30)
 * - Connected to Wire2 (SDA2=pin 25, SCL2=pin 24)
 * - INT pin → Teensy pin 33 (optional, for wake-on-press)
 *
 * BUTTON MAPPING:
 * - Key 0 (leftmost): Choke (momentary - ENABLE on press, DISABLE on release)
 * - Key 1: Delay toggle (future)
 * - Key 2: Reverb toggle (future)
 * - Key 3: Reserved (future: loop record, sample trigger, etc.)
 *
 * DESIGN DIFFERENCES FROM ChokeIO:
 * - Emits Command structs (not ChokeEvent enums)
 * - Table-driven button mappings (easy to reconfigure)
 * - Supports multiple effects (not just choke)
 * - Generic LED control (accepts EffectID, not hardcoded choke)
 *
 * THREAD MODEL:
 * - Producer: InputIO thread (reads Neokey, pushes Commands to queue)
 * - Consumer: App thread (drains queue, executes via EffectManager)
 *
 * LED FEEDBACK:
 * - Each key has RGB LED (controlled via setLED)
 * - Key 0: Red=choke engaged, Green=choke released
 * - Keys 1-3: Can be customized per effect (blue=delay, purple=reverb, etc.)
 */

#pragma once

#include <Arduino.h>
#include "command.h"

/**
 * InputIO subsystem - manages Neokey input and Command emission
 */
namespace InputIO {
    /**
     * Initialize Neokey I2C communication
     *
     * WHAT IT DOES:
     * - Initializes Adafruit Seesaw library
     * - Configures keys for input with pull-up
     * - Enables interrupt-on-change
     * - Sets up INT pin on Teensy (input with pull-up)
     * - Sets initial LED states (green for Key 0)
     *
     * @return true if Neokey detected and configured, false on failure
     *
     * @note Must be called BEFORE starting I/O thread
     * @note Shares Wire2 bus with Neokey (separate from Audio Shield and Display)
     */
    bool begin();

    /**
     * I/O thread entry point (runs forever)
     *
     * WHAT IT DOES:
     * - Continuous polling strategy: Read I2C every 5ms
     * - Detects button press/release edges
     * - Looks up button mapping in table
     * - Pushes Commands to lock-free queue
     * - Updates LED colors based on effect state (via setLED calls from App thread)
     *
     * PERFORMANCE:
     * - Idle: ~0.01% CPU (5ms delays between polls)
     * - Active: ~1-2ms latency from button press to command queued
     * - I2C transaction: ~200-500µs per read
     *
     * @note Never returns (infinite loop)
     */
    void threadLoop();

    /**
     * Pop a command from queue (CONSUMER side)
     *
     * Thread-safe: Call from App thread
     * Real-time safe: O(1), no blocking
     *
     * @param outCmd Output parameter for command
     * @return true if command was popped, false if queue empty
     *
     * Example:
     *   Command cmd;
     *   while (InputIO::popCommand(cmd)) {
     *       EffectManager::executeCommand(cmd);
     *   }
     */
    bool popCommand(Command& outCmd);

    /**
     * Update LED color for a specific key based on effect state
     *
     * Thread-safe: Can be called from any thread
     *
     * @param effectID Which effect's LED to update
     * @param enabled true = effect active (red/blue/purple), false = inactive (green)
     *
     * LED COLOR MAPPING:
     * - EffectID::CHOKE: Red (enabled), Green (disabled)
     * - EffectID::DELAY: Blue (enabled), Green (disabled) - future
     * - EffectID::REVERB: Purple (enabled), Green (disabled) - future
     * - EffectID::GAIN: Yellow (enabled), Green (disabled) - future
     *
     * Note: This function maps EffectID to key index internally
     * (CHOKE=Key 0, DELAY=Key 1, REVERB=Key 2, etc.)
     *
     * Example (called from App thread after executing command):
     *   EffectManager::executeCommand(cmd);
     *   AudioEffectBase* effect = EffectManager::getEffect(cmd.targetEffect);
     *   if (effect) {
     *       InputIO::setLED(cmd.targetEffect, effect->isEnabled());
     *   }
     */
    void setLED(EffectID effectID, bool enabled);

    /**
     * Get current button state for debugging
     *
     * @param keyIndex Which key to check (0-3)
     * @return true if key is currently pressed
     */
    bool isKeyPressed(uint8_t keyIndex);
}
