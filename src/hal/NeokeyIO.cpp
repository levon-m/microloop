#include "NeokeyIO.h"
#include "SPSCQueue.h"
#include "Trace.h"
#include <Adafruit_NeoKey_1x4.h>
#include <seesaw_neopixel.h>
#include <TeensyThreads.h>
#include <Wire.h>

static constexpr uint8_t NEOKEY_I2C_ADDR = 0x30;  // Default Neokey address
static constexpr uint8_t INT_PIN = 33;             // Teensy pin for Neokey INT
static constexpr uint8_t NUM_KEYS = 4;             // Neokey has 4 keys

// Interrupt flag: Set by ISR, cleared by threadLoop after reading I2C
// This defers the I2C read (~20-50µs) out of the ISR context (~1µs)
static volatile bool interruptPending = false;

static constexpr uint32_t LED_COLOR_RED = 0xFF0000;       // Choke engaged
static constexpr uint32_t LED_COLOR_GREEN = 0x00FF00;     // Effect disabled (default)
static constexpr uint32_t LED_COLOR_CYAN = 0x00FFFF;      // Freeze engaged
static constexpr uint32_t LED_COLOR_BLUE = 0x0000FF;      // Delay enabled (future)
static constexpr uint32_t LED_COLOR_PURPLE = 0xFF00FF;    // Reverb enabled (future)
static constexpr uint32_t LED_COLOR_YELLOW = 0xFFFF00;    // Gain enabled (future)
static constexpr uint8_t LED_BRIGHTNESS = 255;            // Full brightness

static constexpr uint32_t DEBOUNCE_MS = 20;  // Minimum time between events

static Adafruit_NeoKey_1x4 neokey(NEOKEY_I2C_ADDR, &Wire2);

static SPSCQueue<Command, 32> commandQueue;

static bool lastKeyState[NUM_KEYS] = {false, false, false, false};
static uint32_t lastEventTime[NUM_KEYS] = {0, 0, 0, 0};

struct ButtonMapping {
    uint8_t keyIndex;           // Which physical key (0-3)
    Command pressCommand;       // Command to emit on press
    Command releaseCommand;     // Command to emit on release
};

static const ButtonMapping buttonMappings[] = {
    // Key 0: Stutter (momentary/capture behavior)
    {
        .keyIndex = 0,
        .pressCommand = Command(CommandType::EFFECT_ENABLE, EffectID::STUTTER),
        .releaseCommand = Command(CommandType::EFFECT_DISABLE, EffectID::STUTTER)
    },

    // Key 1: Freeze (momentary behavior)
    {
        .keyIndex = 1,
        .pressCommand = Command(CommandType::EFFECT_ENABLE, EffectID::FREEZE),
        .releaseCommand = Command(CommandType::EFFECT_DISABLE, EffectID::FREEZE)
    },

    // Key 2: Choke (momentary behavior)
    {
        .keyIndex = 2,
        .pressCommand = Command(CommandType::EFFECT_ENABLE, EffectID::CHOKE),
        .releaseCommand = Command(CommandType::EFFECT_DISABLE, EffectID::CHOKE)
    },

    // Key 3: FUNC modifier button
    {
        .keyIndex = 3,
        .pressCommand = Command(CommandType::EFFECT_ENABLE, EffectID::FUNC),
        .releaseCommand = Command(CommandType::EFFECT_DISABLE, EffectID::FUNC)
    }
};

static constexpr size_t NUM_MAPPINGS = sizeof(buttonMappings) / sizeof(buttonMappings[0]);

// ISR: Called when Neokey detects any button change
// OPTIMIZED: No I2C operations in ISR - just set a flag (<1µs)
static void neokeyISR() {
    // Simply flag that an interrupt occurred
    // The actual I2C read happens in threadLoop() outside ISR context
    interruptPending = true;
}

bool NeokeyIO::begin() {
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

    // Attach Teensy interrupt to Neokey INT pin (active LOW, falling edge)
    attachInterrupt(digitalPinToInterrupt(INT_PIN), neokeyISR, FALLING);

    // Set initial LED states
    neokey.pixels.setBrightness(LED_BRIGHTNESS);
    neokey.pixels.setPixelColor(0, LED_COLOR_GREEN);  // Key 0: Stutter inactive (green)
    neokey.pixels.setPixelColor(1, LED_COLOR_GREEN);  // Key 1: Freeze inactive (green)
    neokey.pixels.setPixelColor(2, LED_COLOR_GREEN);  // Key 2: Choke inactive (green)
    neokey.pixels.setPixelColor(3, LED_COLOR_GREEN);  // Key 3: FUNC inactive (green)
    neokey.pixels.show();

    Serial.println("NeokeyIO: Neokey initialized (I2C 0x30 on Wire2, INT on pin 33, interrupt-driven)");
    return true;
}

void NeokeyIO::threadLoop() {
    for (;;) {
        // Check if interrupt flag is set (deferred I2C read)
        if (interruptPending) {
            // Clear flag atomically to prevent race with ISR
            noInterrupts();
            interruptPending = false;
            interrupts();

            // Now perform the I2C read outside ISR context
            // Read all button states in one I2C transaction (~20-50µs)
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
        }

        // Sleep when no events pending (interrupt-driven = 0% CPU when idle)
        threads.delay(5);
    }
}

bool NeokeyIO::popCommand(Command& outCmd) {
    return commandQueue.pop(outCmd);
}

void NeokeyIO::setLED(EffectID effectID, bool enabled) {
    uint8_t keyIndex = 0;
    uint32_t enabledColor = LED_COLOR_RED;
    uint32_t disabledColor = LED_COLOR_GREEN;

    switch (effectID) {
        case EffectID::STUTTER:
            keyIndex = 0;
            enabledColor = LED_COLOR_PURPLE;  // Purple for stutter
            disabledColor = LED_COLOR_GREEN;
            break;

        case EffectID::FREEZE:
            keyIndex = 1;
            enabledColor = LED_COLOR_CYAN;
            disabledColor = LED_COLOR_GREEN;
            break;

        case EffectID::CHOKE:
            keyIndex = 2;
            enabledColor = LED_COLOR_RED;
            disabledColor = LED_COLOR_GREEN;
            break;

        case EffectID::FUNC:
            keyIndex = 3;
            enabledColor = LED_COLOR_YELLOW;  // Yellow for FUNC
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

bool NeokeyIO::isKeyPressed(uint8_t keyIndex) {
    if (keyIndex >= NUM_KEYS) {
        return false;  // Invalid key index
    }

    // Direct I2C read (for debugging only, not real-time safe)
    uint32_t buttons = neokey.read();
    return (buttons & (1 << keyIndex)) != 0;
}