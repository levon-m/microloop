#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP23X17.h>

namespace EncoderIO {

// Pin definitions (MCP23017 pins)
struct EncoderPins {
    uint8_t pinA;
    uint8_t pinB;
    uint8_t pinSW;
};

// Encoder state for quadrature decoding
struct EncoderState {
    int32_t position;           // Current position in raw steps
    uint8_t lastState;          // Last AB state (2 bits)
    bool buttonPressed;         // One-shot flag: true when button press detected
    bool buttonLastState;       // Previous button state for edge detection
    bool buttonIsHeld;          // True when button is actively held (prevents re-trigger)
    uint32_t lastDebounceTime;  // Time of last button event (press or release)
    uint32_t buttonReleaseTime; // Time when button was released (0 = not released yet)
};

bool begin();

void update();

int32_t getPosition(uint8_t encoderNum);

bool getButton(uint8_t encoderNum);

void resetPosition(uint8_t encoderNum);

}