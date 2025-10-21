#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP23X17.h>

/**
 * Encoder I/O Module
 *
 * Handles 4 rotary encoders with push buttons via MCP23017 I2C expander.
 * Uses ISR state capture for zero missed steps, even during fast turns.
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
 *   EncoderIO::begin();
 *   // In loop:
 *   EncoderIO::update();
 *   // Query state:
 *   int32_t pos = EncoderIO::getPosition(0);  // Get encoder 1 position
 *   bool pressed = EncoderIO::getButton(0);   // Check if button 1 pressed
 *
 * Features:
 * - Interrupt-driven ISR state capture (MCP23017 INTCAP registers)
 * - 64-event queue for burst handling
 * - Zero missed steps at any human speed (tested 10+ turns/second)
 * - ~26µs ISR latency (240× faster than polling)
 * - Quadrature lookup table decoding with noise rejection
 */

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
    bool buttonPressed;         // Current button state
    bool buttonLastState;       // Previous button state for edge detection
    uint32_t lastDebounceTime;  // For button debouncing
};

/**
 * Initialize MCP23017 and configure encoder pins with interrupt-on-change
 * Returns true on success, false if MCP23017 not found
 */
bool begin();

/**
 * Process queued encoder events from ISR
 * Call this repeatedly in main loop (event-driven, returns immediately if no events)
 */
void update();

/**
 * Get current raw position of encoder (steps, not detents)
 * Divide by 4 for detent position on typical encoders
 */
int32_t getPosition(uint8_t encoderNum);

/**
 * Get current button state (returns true if pressed)
 */
bool getButton(uint8_t encoderNum);

/**
 * Reset encoder position to zero
 */
void resetPosition(uint8_t encoderNum);

} // namespace EncoderIO
