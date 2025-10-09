/**
 * choke_io.cpp - Neokey 1x4 I2C button input implementation
 */

#include "choke_io.h"
#include "spsc_queue.h"
#include "trace.h"
#include <Adafruit_NeoKey_1x4.h>
#include <seesaw_neopixel.h>
#include <TeensyThreads.h>

/**
 * HARDWARE CONFIGURATION
 */
static constexpr uint8_t NEOKEY_I2C_ADDR = 0x30;  // Default Neokey address
static constexpr uint8_t INT_PIN = 23;             // Teensy pin for Neokey INT
static constexpr uint8_t CHOKE_KEY = 0;            // Key 0 (leftmost) is choke button

/**
 * LED COLORS (RGB values, high brightness)
 */
static constexpr uint32_t LED_COLOR_CHOKED = 0xFF0000;    // Red (muted)
static constexpr uint32_t LED_COLOR_UNMUTED = 0x00FF00;   // Green (unmuted)
static constexpr uint8_t LED_BRIGHTNESS = 255;            // Full brightness

/**
 * DEBOUNCE CONFIGURATION
 */
static constexpr uint32_t DEBOUNCE_MS = 50;  // Ignore events within 50ms of last toggle

/**
 * Neokey object (Seesaw-based I2C device)
 */
static Adafruit_NeoKey_1x4 neokey;

/**
 * Lock-free event queue (I/O thread → App thread)
 */
static SPSCQueue<ChokeEvent, 32> eventQueue;

/**
 * Button state tracking (for edge detection)
 */
static bool lastKeyState = false;        // Last known state of key 0
static uint32_t lastEventTime = 0;       // Timestamp of last event (for debouncing)
static volatile bool isChoked = false;   // Current choke state (for LED updates)

// =============================================================================
// PUBLIC API
// =============================================================================

bool ChokeIO::begin() {
    // Configure INT pin (input with pull-up, active LOW)
    pinMode(INT_PIN, INPUT_PULLUP);

    // Initialize Neokey (Seesaw I2C communication)
    if (!neokey.begin(NEOKEY_I2C_ADDR)) {
        Serial.println("ERROR: Neokey not detected on I2C!");
        return false;
    }

    // Configure key 0 as input with internal pull-up
    neokey.pinMode(CHOKE_KEY, INPUT_PULLUP);

    // Enable interrupts on key 0 (interrupt on change: press or release)
    // This makes INT pin go LOW when key state changes
    neokey.enableKeypadInterrupt();

    // Set initial LED state (green = unmuted)
    neokey.pixels.setBrightness(LED_BRIGHTNESS);
    neokey.pixels.setPixelColor(CHOKE_KEY, LED_COLOR_UNMUTED);
    neokey.pixels.show();

    Serial.println("ChokeIO: Neokey initialized (I2C 0x30, INT on pin 23)");
    return true;
}

void ChokeIO::threadLoop() {
    /**
     * HYBRID INTERRUPT STRATEGY:
     *
     * 1. Check INT pin (fast digitalRead, no I2C)
     * 2. If INT is LOW: Button activity detected → read Neokey via I2C
     * 3. If INT is HIGH: No activity → sleep and repeat
     *
     * BENEFITS:
     * - Low latency: ~1-2ms from button press to event queued
     * - Low I2C traffic: Only read when buttons pressed (not every 10ms)
     * - Simple: No true ISR complexity, just thread with smart polling
     */

    for (;;) {
        // Fast check: Is INT pin LOW? (button activity detected)
        if (digitalRead(INT_PIN) == LOW) {
            // Activity detected! Read button states via I2C
            uint32_t buttons = neokey.read();  // Read all 4 keys (bitmask)

            // Extract key 0 state (bit 0 = key 0)
            // Note: Seesaw returns 1 = pressed, 0 = released
            bool keyPressed = (buttons & (1 << CHOKE_KEY)) != 0;

            // Debounce: Ignore rapid toggles within 50ms
            uint32_t now = millis();
            if (now - lastEventTime < DEBOUNCE_MS) {
                // Too soon after last event, ignore
                threads.yield();
                continue;
            }

            // Detect edge (state change)
            if (keyPressed != lastKeyState) {
                lastKeyState = keyPressed;
                lastEventTime = now;

                // Generate event
                if (keyPressed) {
                    // Key pressed → engage choke
                    eventQueue.push(ChokeEvent::BUTTON_PRESS);
                    TRACE(TRACE_CHOKE_BUTTON_PRESS, CHOKE_KEY);
                    TRACE(TRACE_CHOKE_ENGAGE);
                } else {
                    // Key released → release choke
                    eventQueue.push(ChokeEvent::BUTTON_RELEASE);
                    TRACE(TRACE_CHOKE_BUTTON_RELEASE, CHOKE_KEY);
                    TRACE(TRACE_CHOKE_RELEASE);
                }
            }

            // Clear interrupt flag (read clears it automatically on Seesaw)
            // INT pin will go HIGH again after we've read the state
        }

        // Yield to other threads (cooperative multitasking)
        // If no activity, we'll check INT pin again in ~10ms
        threads.delay(10);
    }
}

bool ChokeIO::popEvent(ChokeEvent& outEvent) {
    return eventQueue.pop(outEvent);
}

void ChokeIO::setLED(bool choked) {
    // Update internal state (for thread safety)
    isChoked = choked;

    // Update LED color
    uint32_t color = choked ? LED_COLOR_CHOKED : LED_COLOR_UNMUTED;
    neokey.pixels.setPixelColor(CHOKE_KEY, color);
    neokey.pixels.show();  // Commit changes to hardware
}

bool ChokeIO::isKeyPressed() {
    // Direct I2C read (for debugging only, not real-time safe)
    uint32_t buttons = neokey.read();
    return (buttons & (1 << CHOKE_KEY)) != 0;
}
