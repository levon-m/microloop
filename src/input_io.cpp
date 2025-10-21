/**
 * input_io.cpp - Generic input handler implementation
 */

#include "input_io.h"
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
static constexpr uint8_t NUM_KEYS = 4;             // Neokey has 4 keys

/**
 * LED COLORS (RGB values, full brightness)
 */
static constexpr uint32_t LED_COLOR_RED = 0xFF0000;       // Choke engaged
static constexpr uint32_t LED_COLOR_GREEN = 0x00FF00;     // Effect disabled (default)
static constexpr uint32_t LED_COLOR_CYAN = 0x00FFFF;      // Freeze engaged
static constexpr uint32_t LED_COLOR_BLUE = 0x0000FF;      // Delay enabled (future)
static constexpr uint32_t LED_COLOR_PURPLE = 0xFF00FF;    // Reverb enabled (future)
static constexpr uint32_t LED_COLOR_YELLOW = 0xFFFF00;    // Gain enabled (future)
static constexpr uint8_t LED_BRIGHTNESS = 255;            // Full brightness

/**
 * DEBOUNCE CONFIGURATION
 */
static constexpr uint32_t DEBOUNCE_MS = 20;  // Minimum time between events

/**
 * Neokey object (Seesaw-based I2C device)
 * Initialize with Wire2 bus (SDA2=pin 25, SCL2=pin 24)
 */
static Adafruit_NeoKey_1x4 neokey(NEOKEY_I2C_ADDR, &Wire2);

/**
 * Lock-free command queue (I/O thread → App thread)
 */
static SPSCQueue<Command, 32> commandQueue;

/**
 * Button state tracking (for edge detection)
 */
static bool lastKeyState[NUM_KEYS] = {false, false, false, false};
static uint32_t lastEventTime[NUM_KEYS] = {0, 0, 0, 0};

// =============================================================================
// BUTTON MAPPING TABLE
// =============================================================================

/**
 * Button Mapping - Maps physical keys to Commands
 *
 * DESIGN:
 * - Table-driven configuration (easy to modify)
 * - Each key has separate press and release commands
 * - NONE command = no-op (for disabled keys or ignored releases)
 *
 * CURRENT MAPPING:
 * - Key 0: Freeze (momentary - ENABLE on press, DISABLE on release)
 * - Key 1: Reserved (NONE commands, ready for future effect)
 * - Key 2: Choke (momentary - ENABLE on press, DISABLE on release)
 * - Key 3: Reserved (NONE commands, ready for future effect)
 */
struct ButtonMapping {
    uint8_t keyIndex;           // Which physical key (0-3)
    Command pressCommand;       // Command to emit on press
    Command releaseCommand;     // Command to emit on release
};

static const ButtonMapping buttonMappings[] = {
    // Key 0: Freeze (momentary behavior)
    {
        .keyIndex = 0,
        .pressCommand = Command(CommandType::EFFECT_ENABLE, EffectID::FREEZE),
        .releaseCommand = Command(CommandType::EFFECT_DISABLE, EffectID::FREEZE)
    },

    // Key 1: Reserved for future feature (currently disabled)
    {
        .keyIndex = 1,
        .pressCommand = Command(CommandType::NONE, EffectID::NONE),
        .releaseCommand = Command(CommandType::NONE, EffectID::NONE)
    },

    // Key 2: Choke (momentary behavior)
    {
        .keyIndex = 2,
        .pressCommand = Command(CommandType::EFFECT_ENABLE, EffectID::CHOKE),
        .releaseCommand = Command(CommandType::EFFECT_DISABLE, EffectID::CHOKE)
    },

    // Key 3: Reserved (future - currently disabled)
    {
        .keyIndex = 3,
        .pressCommand = Command(CommandType::NONE, EffectID::NONE),
        .releaseCommand = Command(CommandType::NONE, EffectID::NONE)
    }
};

static constexpr size_t NUM_MAPPINGS = sizeof(buttonMappings) / sizeof(buttonMappings[0]);

// =============================================================================
// PUBLIC API
// =============================================================================

bool InputIO::begin() {
    // Configure INT pin (input with pull-up, active LOW)
    pinMode(INT_PIN, INPUT_PULLUP);

    // Initialize Wire2 (I2C bus 2: SDA2=pin 25, SCL2=pin 24)
    Wire2.begin();
    Wire2.setClock(400000);  // 400kHz I2C speed

    // Initialize Neokey (Seesaw I2C communication)
    // Note: Wire2 bus was specified in constructor
    if (!neokey.begin(NEOKEY_I2C_ADDR)) {
        Serial.println("ERROR: InputIO - Neokey not detected on I2C!");
        return false;
    }

    // Configure all keys as input with internal pull-up
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        neokey.pinMode(i, INPUT_PULLUP);
    }

    // Enable interrupts on all keys (interrupt on change: press or release)
    // This makes INT pin go LOW when any key state changes
    neokey.enableKeypadInterrupt();

    // Set initial LED states
    neokey.pixels.setBrightness(LED_BRIGHTNESS);
    neokey.pixels.setPixelColor(0, LED_COLOR_GREEN);  // Key 0: Freeze inactive (green)
    neokey.pixels.setPixelColor(1, 0x000000);         // Key 1: Off (reserved)
    neokey.pixels.setPixelColor(2, LED_COLOR_GREEN);  // Key 2: Choke inactive (green)
    neokey.pixels.setPixelColor(3, 0x000000);         // Key 3: Off (reserved)
    neokey.pixels.show();

    Serial.println("InputIO: Neokey initialized (I2C 0x30 on Wire2, INT on pin 33)");
    return true;
}

void InputIO::threadLoop() {
    /**
     * CONTINUOUS POLLING STRATEGY:
     *
     * - Read I2C every 5ms (synchronizes with hardware state)
     * - Track state per-key for edge detection
     * - Rate-limit events per-key (20ms debounce)
     * - Emit commands based on button mapping table
     *
     * BENEFITS:
     * - Low latency: ~5-10ms from button press to command queued
     * - State synchronization: Always in sync with hardware
     * - Simple: No ISR complexity, just thread with polling
     */

    for (;;) {
        // Read all button states in one I2C transaction
        uint32_t buttons = neokey.read();

        // Check each button mapping
        for (size_t i = 0; i < NUM_MAPPINGS; i++) {
            const ButtonMapping& mapping = buttonMappings[i];
            uint8_t keyIndex = mapping.keyIndex;

            // Extract key state from bitmask
            bool pressed = (buttons & (1 << keyIndex)) != 0;

            // Detect state change (edge detection)
            if (pressed != lastKeyState[keyIndex]) {
                uint32_t now = millis();

                // Simple time-based debouncing: Only send event if enough time passed
                if (now - lastEventTime[keyIndex] >= DEBOUNCE_MS) {
                    // Update timestamp
                    lastEventTime[keyIndex] = now;

                    // Emit appropriate command
                    Command cmd = pressed ? mapping.pressCommand : mapping.releaseCommand;

                    // Only push non-NONE commands
                    if (cmd.type != CommandType::NONE) {
                        commandQueue.push(cmd);
                        TRACE(TRACE_CHOKE_BUTTON_PRESS + (pressed ? 0 : 1), keyIndex);
                    }
                }

                // Always update state (even if within debounce period)
                // This prevents stuck state if button is released quickly
                lastKeyState[keyIndex] = pressed;
            }
        }

        // Small delay to limit I2C traffic and give other threads time
        threads.delay(5);
    }
}

bool InputIO::popCommand(Command& outCmd) {
    return commandQueue.pop(outCmd);
}

void InputIO::setLED(EffectID effectID, bool enabled) {
    /**
     * Map EffectID to key index
     *
     * CURRENT MAPPING:
     * - FREEZE → Key 0 (cyan when frozen, green when inactive)
     * - Key 1 → Reserved (future)
     * - CHOKE → Key 2 (red when choked, green when inactive)
     * - Key 3 → Reserved (future)
     */
    uint8_t keyIndex = 0;
    uint32_t enabledColor = LED_COLOR_RED;
    uint32_t disabledColor = LED_COLOR_GREEN;

    switch (effectID) {
        case EffectID::FREEZE:
            keyIndex = 0;
            enabledColor = LED_COLOR_CYAN;
            disabledColor = LED_COLOR_GREEN;
            break;

        case EffectID::CHOKE:
            keyIndex = 2;
            enabledColor = LED_COLOR_RED;
            disabledColor = LED_COLOR_GREEN;
            break;

        case EffectID::DELAY:
            keyIndex = 1;  // Future: Key 1
            enabledColor = LED_COLOR_BLUE;
            disabledColor = LED_COLOR_GREEN;
            break;

        case EffectID::REVERB:
            keyIndex = 3;  // Future: Key 3
            enabledColor = LED_COLOR_PURPLE;
            disabledColor = LED_COLOR_GREEN;
            break;

        case EffectID::GAIN:
            keyIndex = 1;  // Future: Key 1 (or 3)
            enabledColor = LED_COLOR_YELLOW;
            disabledColor = LED_COLOR_GREEN;
            break;

        default:
            // Unknown effect ID - ignore
            return;
    }

    // Update LED color
    uint32_t color = enabled ? enabledColor : disabledColor;
    neokey.pixels.setPixelColor(keyIndex, color);
    neokey.pixels.show();  // Commit changes to hardware
}

bool InputIO::isKeyPressed(uint8_t keyIndex) {
    if (keyIndex >= NUM_KEYS) {
        return false;  // Invalid key index
    }

    // Direct I2C read (for debugging only, not real-time safe)
    uint32_t buttons = neokey.read();
    return (buttons & (1 << keyIndex)) != 0;
}
