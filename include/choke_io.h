/**
 * choke_io.h - Neokey 1x4 I2C button input handler
 *
 * PURPOSE:
 * Manages Adafruit Neokey 1x4 QT I2C keypad for choke control.
 * Uses interrupt-based wake strategy for low latency and minimal I2C traffic.
 *
 * HARDWARE:
 * - Adafruit Neokey 1x4 QT (Seesaw-based, I2C address 0x30)
 * - Connected to Wire2 (SDA2=pin 25, SCL2=pin 24)
 * - INT pin → Teensy pin 33 (optional, for wake-on-press)
 *
 * ARCHITECTURE:
 * - Dedicated I/O thread (high priority, like MIDI I/O)
 * - Hybrid interrupt approach: Poll only when INT pin indicates activity
 * - Pushes button events to lock-free SPSC queue
 * - Consumer (App thread) drains queue and controls AudioEffectChoke
 *
 * BUTTON EVENT TYPES:
 * - KEY_DOWN: Button pressed (engage choke)
 * - KEY_UP: Button released (release choke)
 * - Only key 0 (leftmost) generates events (keys 1-3 reserved for future)
 *
 * THREAD MODEL:
 * - Producer: ChokeIO thread (reads Neokey, pushes to queue)
 * - Consumer: App thread (drains queue, updates audio effect)
 *
 * LED FEEDBACK:
 * - Key 0 LED: Red when choked, Green when unmuted
 * - High brightness for stage visibility
 */

#pragma once

#include <Arduino.h>

/**
 * Button event types
 *
 * Note: Using BUTTON_PRESS/BUTTON_RELEASE to avoid naming conflicts
 * with Teensy's KEY_DOWN/KEY_UP macros in keylayouts.h
 */
enum class ChokeEvent : uint8_t {
    BUTTON_PRESS = 1,    // Button pressed (engage choke)
    BUTTON_RELEASE = 2   // Button released (release choke)
};

/**
 * ChokeIO subsystem - manages Neokey input
 */
namespace ChokeIO {
    /**
     * Initialize Neokey I2C communication
     *
     * WHAT IT DOES:
     * - Initializes Adafruit Seesaw library
     * - Configures key 0 for input with pull-up
     * - Enables interrupt-on-change for key 0
     * - Sets up INT pin on Teensy (input with pull-up)
     * - Sets initial LED state (green = unmuted)
     *
     * @return true if Neokey detected and configured, false on failure
     *
     * @note Must be called BEFORE starting I/O thread
     * @note Shares Wire bus with SGTL5000 codec (I2C address collision safe)
     */
    bool begin();

    /**
     * I/O thread entry point (runs forever)
     *
     * WHAT IT DOES:
     * - Hybrid interrupt approach:
     *   1. Check INT pin (fast digitalRead)
     *   2. If LOW (activity detected), read Neokey via I2C
     *   3. If HIGH (no activity), sleep 10ms and repeat
     * - Detects key press/release edges
     * - Pushes events to lock-free queue
     * - Updates LED color based on choke state
     *
     * PERFORMANCE:
     * - Idle: No I2C traffic (waits for INT pin)
     * - Active: ~1-2ms latency from button press to event queued
     * - I2C transaction: ~200-500µs per read
     *
     * @note Never returns (infinite loop)
     */
    void threadLoop();

    /**
     * Pop a button event from queue (CONSUMER side)
     *
     * Thread-safe: Call from App thread
     * Real-time safe: O(1), no blocking
     *
     * @param outEvent Output parameter for event
     * @return true if event was popped, false if queue empty
     */
    bool popEvent(ChokeEvent& outEvent);

    /**
     * Update LED color based on choke state
     *
     * Thread-safe: Can be called from any thread
     *
     * @param choked true = red LED (muted), false = green LED (unmuted)
     */
    void setLED(bool choked);

    /**
     * Get current button state (for debugging)
     *
     * @return true if key 0 is currently pressed
     */
    bool isKeyPressed();
}
