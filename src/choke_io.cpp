/**
 * choke_io.cpp - Neokey 1x4 I2C button input implementation
 */

#include "choke_io.h"
#include "spsc_queue.h"
#include "trace.h"
#include <Adafruit_NeoKey_1x4.h>
#include <seesaw_neopixel.h>
#include <TeensyThreads.h>
#include <Wire.h>

/**
 * HARDWARE CONFIGURATION
 */
static constexpr uint8_t NEOKEY_I2C_ADDR = 0x30;  // Default Neokey address
static constexpr uint8_t INT_PIN = 33;             // Teensy pin for Neokey INT
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
static constexpr uint32_t DEBOUNCE_MS = 20;  // Minimum time between events (reduced for responsiveness)

/**
 * Neokey object (Seesaw-based I2C device)
 * Initialize with Wire2 bus (SDA2=pin 25, SCL2=pin 24)
 */
static Adafruit_NeoKey_1x4 neokey(NEOKEY_I2C_ADDR, &Wire2);

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

    // Initialize Wire2 (I2C bus 2: SDA2=pin 25, SCL2=pin 24)
    Wire2.begin();
    Wire2.setClock(400000);  // 400kHz I2C speed

    // Initialize Neokey (Seesaw I2C communication)
    // Note: Wire2 bus was specified in constructor
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

    Serial.println("ChokeIO: Neokey initialized (I2C 0x30 on Wire2, INT on pin 33)");
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
        // Read button state directly every loop iteration
        // This ensures we always stay in sync with hardware
        uint32_t buttons = neokey.read();
        bool keyPressed = (buttons & (1 << CHOKE_KEY)) != 0;

        // Detect state change
        if (keyPressed != lastKeyState) {
            uint32_t now = millis();

            // Simple time-based debouncing: Only send event if enough time passed
            if (now - lastEventTime >= DEBOUNCE_MS) {
                // Update state and timestamp
                lastKeyState = keyPressed;
                lastEventTime = now;

                // Send event
                if (keyPressed) {
                    eventQueue.push(ChokeEvent::BUTTON_PRESS);
                    TRACE(TRACE_CHOKE_BUTTON_PRESS, CHOKE_KEY);
                    TRACE(TRACE_CHOKE_ENGAGE);
                } else {
                    eventQueue.push(ChokeEvent::BUTTON_RELEASE);
                    TRACE(TRACE_CHOKE_BUTTON_RELEASE, CHOKE_KEY);
                    TRACE(TRACE_CHOKE_RELEASE);
                }
            } else {
                // Within debounce period, but still update our tracking state
                // to stay in sync with hardware (prevents state desync)
                lastKeyState = keyPressed;
            }
        }

        // Small delay to limit I2C traffic and give other threads time
        threads.delay(5);
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
