#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP23X17.h>

/**
 * Encoder Test Module
 *
 * Tests 4 rotary encoders with push buttons connected via MCP23017 I2C expander.
 *
 * Hardware:
 * - MCP23017 on Wire (SDA=Pin18, SCL=Pin19), address 0x20
 * - MCP23017 INTA (or INTB) → Teensy Pin 36 (interrupt on any encoder change)
 *   Note: In mirror mode, INTA and INTB are tied together - connect either one
 * - Encoder 1: A=GPA4, B=GPA3, SW=GPA2
 * - Encoder 2: A=GPB0, B=GPB1, SW=GPB2
 * - Encoder 3: A=GPB3, B=GPB4, SW=GPB5
 * - Encoder 4: A=GPA7, B=GPA6, SW=GPA5
 *
 * Usage:
 *   EncoderTest::begin();
 *   // In loop:
 *   EncoderTest::update();
 *
 * Features:
 * - Interrupt-driven for fast response to encoder changes
 * - Can handle very fast encoder turns without missing steps
 * - Uses MCP23017 interrupt-on-change feature
 */

namespace EncoderTest {

// Pin definitions (MCP23017 pins)
struct EncoderPins {
    uint8_t pinA;
    uint8_t pinB;
    uint8_t pinSW;
};

// Encoder state for quadrature decoding
struct EncoderState {
    int32_t position;      // Current position in detents
    uint8_t lastState;     // Last AB state (2 bits)
    bool buttonPressed;    // Current button state
    bool buttonLastState;  // Previous button state for edge detection
    uint32_t lastDebounceTime; // For button debouncing
};

/**
 * Initialize MCP23017 and configure encoder pins
 * Returns true on success, false if MCP23017 not found
 */
bool begin();

/**
 * Read all encoders and buttons, print changes to Serial
 * Call this repeatedly in loop()
 */
void update();

/**
 * Get current position of encoder (in detents)
 */
int32_t getPosition(uint8_t encoderNum);

/**
 * Get current button state
 */
bool getButton(uint8_t encoderNum);

/**
 * Reset encoder position to zero
 */
void resetPosition(uint8_t encoderNum);

} // namespace EncoderTest
