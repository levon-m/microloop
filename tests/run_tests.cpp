/**
 * run_tests.cpp - Main test entry point
 *
 * USAGE:
 * 1. Comment out src/main.cpp from build (or create separate test build)
 * 2. Build with this file as entry point
 * 3. Upload to Teensy
 * 4. Open Serial Monitor @ 115200 baud
 * 5. Observe test results
 *
 * See tests/TESTING.md for detailed instructions
 */

#include <Arduino.h>
#include "test_runner.h"
#include "timekeeper.h"
#include "trace.h"

// Include test files (they auto-register via TEST() macro)
#include "test_timekeeper.cpp"
#include "test_trace.cpp"
#include "test_spsc_queue.cpp"

void setup() {
    // Initialize serial
    Serial.begin(115200);
    while (!Serial && millis() < 3000);  // Wait up to 3s for serial

    Serial.println();
    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║    MicroLoop On-Device Test Suite     ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println();

    // Initialize subsystems needed for tests
    TimeKeeper::begin();
    Trace::clear();

    // Run all tests
    RUN_ALL_TESTS();

    Serial.println();
    Serial.println("════════════════════════════════════════");
    Serial.println("Test run complete. Press reset to rerun.");
    Serial.println("════════════════════════════════════════");
}

void loop() {
    // Tests run once in setup()
    delay(1000);
}
