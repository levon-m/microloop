/**
 * Encoder Test Program
 *
 * Tests 4 rotary encoders with push buttons via MCP23017 I2C expander.
 *
 * Hardware Setup:
 * - MCP23017 on Wire (SDA=Pin18, SCL=Pin19), address 0x20
 * - MCP23017 INTA (or INTB) → Teensy Pin 36 (interrupt on change)
 *   Note: In mirror mode, connect either INTA or INTB
 * - Encoder 1: A=GPA4, B=GPA3, SW=GPA2
 * - Encoder 2: A=GPB0, B=GPB1, SW=GPB2
 * - Encoder 3: A=GPB3, B=GPB4, SW=GPB5
 * - Encoder 4: A=GPA7, B=GPA6, SW=GPA5
 *
 * Expected Output:
 * - Turn encoder CW: "ENC[N] CW (pos=X, detents=Y)"
 * - Turn encoder CCW: "ENC[N] CCW (pos=X, detents=Y)"
 * - Press button: "ENC[N] PRESS (pos=X)"
 *
 * Upload this test, open Serial Monitor @ 115200 baud, and test all encoders.
 */

#include <Arduino.h>
#include "encoder_test.h"

void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(2000); // Wait for serial monitor to connect

    Serial.println();
    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║    MicroLoop Encoder Test             ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println();

    // Initialize encoders
    if (!EncoderTest::begin()) {
        Serial.println("FATAL: Encoder initialization failed!");
        Serial.println("Check wiring:");
        Serial.println("  - MCP23017 on Wire (SDA=Pin18, SCL=Pin19)");
        Serial.println("  - Address 0x20 (A0/A1/A2 tied to GND)");
        Serial.println("  - VDD→3.3V, VSS→GND, RESET→3.3V");
        Serial.println("  - 0.1µF cap between VDD/VSS");
        while (1) {
            digitalWrite(LED_BUILTIN, HIGH);
            delay(100);
            digitalWrite(LED_BUILTIN, LOW);
            delay(100);
        }
    }

    Serial.println("Ready! Turn encoders or press buttons to test.");
    Serial.println("Expected behavior:");
    Serial.println("  - Turn CW/CCW: Should register direction changes");
    Serial.println("  - Most encoders: 4 steps = 1 detent (tactile click)");
    Serial.println("  - Press button: Should register button press");
    Serial.println("  - Fast turns: Interrupt-driven, no missed steps!");
    Serial.println();
}

void loop() {
    // Update encoder readings (call frequently)
    EncoderTest::update();

    // Small delay to avoid overwhelming Serial output
    delay(1);
}
