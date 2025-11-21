#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP23X17.h>

namespace Mcp23017Input {

// Pin definitions (MCP23017 pins)
struct EncoderPins {
    uint8_t pinA;
    uint8_t pinB;
    uint8_t pinSW;
};

// Encoder state for quadrature decoding
struct EncoderState {
    int32_t position;           // Current position in raw steps
    uint8_t lastQuadState;      // Last AB state (2 bits)

    // Button state (NeoKey-style debouncing)
    bool     buttonLastState;       // Last stable button state
    uint32_t buttonLastEventTime;   // Time of last accepted state change
    bool     buttonPressed;         // One-shot flag: true when button press detected
};

// Preset button state (NeoKey-style debouncing)
// These buttons are reserved for future preset recall feature
struct PresetButtonState {
    bool     lastState;         // Last stable button state
    uint32_t lastEventTime;     // Time of last accepted state change
    bool     pressedFlag;       // One-shot flag: true when button press detected
};

bool begin();

void threadLoop();

void update();  // Alternative to threadLoop (if called from app thread)

int32_t getPosition(uint8_t encoderNum);

bool getEncoderButton(uint8_t encoderNum);  // Returns and consumes one-shot flag

bool getPresetButton(uint8_t buttonNum);  // Returns and consumes one-shot flag for preset buttons (1-4)

void resetPosition(uint8_t encoderNum);

}
