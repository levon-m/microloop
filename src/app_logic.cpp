#include "app_logic.h"
#include "midi_io.h"
#include <TeensyThreads.h>

/**
 * Application Logic Implementation
 *
 * FEATURE: LED Beat Indicator
 * - Blinks LED on every beat (24 clock ticks)
 * - LED on for first 12 ticks, off for last 12 ticks (50% duty cycle)
 * - Pauses on MIDI STOP, resumes on START/CONTINUE
 *
 * KEY DESIGN DECISIONS:
 *
 * 1. WHY COUNT TO 24 (NOT 23)?
 *    - MIDI clock: 24 PPQN (Pulses Per Quarter Note)
 *    - Tick 0-23 = one beat, tick 24 starts next beat
 *    - We reset counter when tickCount reaches 24
 *
 * 2. WHY 50% DUTY CYCLE?
 *    - Clear visual feedback (on-off pattern)
 *    - Easy to see beat at a glance
 *    - Alternative: Short pulse (10% duty) - harder to see
 *
 * 3. WHY RESET ON START (NOT CONTINUE)?
 *    - START = sequencer reset to beginning (tick 0)
 *    - CONTINUE = resume from pause (keep tick position)
 *    - This matches DAW behavior
 *
 * 4. WHY BUILT-IN LED (PIN 13)?
 *    - Every Teensy has it (no external wiring needed)
 *    - Easy to debug without hardware
 *    - Can change to external LED later
 */

// LED configuration
static constexpr uint8_t LED_PIN = LED_BUILTIN;  // Pin 13 on Teensy 4.x

// Beat tracking state
static uint8_t tickCount = 0;         // Current tick within beat (0-23)
static bool transportActive = false;  // Is sequencer running?

// Debug output rate limiting
static uint32_t lastPrint = 0;
static constexpr uint32_t PRINT_INTERVAL_MS = 1000;  // Print status every 1s

void AppLogic::begin() {
    // Configure LED pin
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);  // Start with LED off

    // Initialize state
    tickCount = 0;
    transportActive = false;
}

void AppLogic::threadLoop() {
    /**
     * APP THREAD MAIN LOOP
     *
     * EXECUTION ORDER (matters!):
     * 1. Process transport events (START/STOP affects LED state)
     * 2. Process clock ticks (update beat position, drive LED)
     * 3. Periodic debug output
     * 4. Yield CPU
     */
    for (;;) {
        // ========== 1. DRAIN TRANSPORT EVENTS ==========
        /**
         * WHY DRAIN COMPLETELY?
         * - Events are rare (only on start/stop)
         * - Always want latest state
         * - If multiple events queued (e.g., rapid start/stop), we want final state
         */
        MidiEvent event;
        while (MidiIO::popEvent(event)) {
            switch (event) {
                case MidiEvent::START:
                    /**
                     * START: Reset to beginning
                     * - Reset tick counter (we're at beat 0, tick 0)
                     * - Enable transport
                     * - Turn on LED (start of beat)
                     */
                    tickCount = 0;
                    transportActive = true;
                    digitalWrite(LED_PIN, HIGH);
                    Serial.println("► START");
                    break;

                case MidiEvent::STOP:
                    /**
                     * STOP: Pause playback
                     * - Disable transport (ignore clock ticks)
                     * - Turn off LED (visual feedback of stopped state)
                     * - Keep tick counter (CONTINUE will resume from here)
                     */
                    transportActive = false;
                    digitalWrite(LED_PIN, LOW);
                    Serial.println("■ STOP");
                    break;

                case MidiEvent::CONTINUE:
                    /**
                     * CONTINUE: Resume from pause
                     * - Enable transport
                     * - Don't reset tick counter (resume from last position)
                     * - Update LED based on current tick position
                     */
                    transportActive = true;
                    // Set LED state based on current position
                    digitalWrite(LED_PIN, (tickCount < 12) ? HIGH : LOW);
                    Serial.println("▶ CONTINUE");
                    break;
            }
        }

        // ========== 2. DRAIN CLOCK TICKS ==========
        /**
         * WHY DRAIN COMPLETELY (NOT JUST ONE)?
         * - If app thread stalls, ticks pile up in queue
         * - We want to catch up to current beat ASAP
         * - Processing all queued ticks keeps us in sync
         *
         * TRADEOFF: Responsiveness vs CPU usage
         * - Drain all: Catches up fast, but can hog CPU if backlogged
         * - Drain one: Smooth CPU usage, but falls behind if stalls
         * - We choose drain all because:
         *   a) Queue rarely has >1 tick (we run every 2ms, ticks every ~20ms)
         *   b) Staying in sync is critical for looper (future)
         */
        uint32_t clockMicros;
        while (MidiIO::popClock(clockMicros)) {
            // Only process ticks if transport is running
            if (!transportActive) {
                continue;  // Ignore ticks when stopped
            }

            /**
             * BEAT TRACKING LOGIC
             *
             * tickCount: 0 → 1 → 2 → ... → 23 → 0 → 1 → ...
             *                └─── 24 ticks ───┘   └─ next beat
             *
             * LED pattern:
             * - Ticks 0-11: LED ON  (first half of beat)
             * - Ticks 12-23: LED OFF (second half of beat)
             */

            // Update LED based on tick position
            // WHY < 12? 50% duty cycle (12 on, 12 off)
            if (tickCount < 12) {
                digitalWrite(LED_PIN, HIGH);
            } else {
                digitalWrite(LED_PIN, LOW);
            }

            // Increment tick counter
            tickCount++;

            // Reset at beat boundary
            // WHY >= 24? 24 ticks per beat (0-23), so 24 = start of next beat
            if (tickCount >= 24) {
                tickCount = 0;
                // Future: Increment beat counter here for bar tracking
            }

            // Future: Use clockMicros for BPM calculation
            // Example: Store last 24 timestamps, compute average period
        }

        // ========== 3. PERIODIC DEBUG OUTPUT ==========
        /**
         * WHY RATE LIMIT SERIAL OUTPUT?
         * - Serial.print is SLOW (~100-500µs per call)
         * - Can cause audio glitches if done too often
         * - 1 second interval is enough for debug monitoring
         *
         * WHY SAFE HERE (NOT IN MIDI HANDLER)?
         * - We're in app thread, not I/O thread
         * - App thread is lower priority (won't block MIDI)
         * - Audio runs in ISR (higher than both threads)
         */
        uint32_t now = millis();
        if (now - lastPrint >= PRINT_INTERVAL_MS) {
            lastPrint = now;

            // Status report
            Serial.print("Transport: ");
            Serial.print(transportActive ? "RUNNING" : "STOPPED");
            Serial.print(" | Beat tick: ");
            Serial.print(tickCount);
            Serial.print("/24");
            Serial.print(" | LED: ");
            Serial.println(digitalRead(LED_PIN) ? "ON" : "OFF");
        }

        // ========== 4. YIELD CPU ==========
        /**
         * WHY 2ms DELAY?
         * - MIDI clocks arrive every ~20ms at 120 BPM
         * - 2ms delay = we check 10x per clock interval (plenty of headroom)
         * - Shorter delay: More responsive, but wastes CPU
         * - Longer delay: Save CPU, but might miss events (if queue fills)
         *
         * WHY threads.delay() (NOT delay())?
         * - threads.delay() yields to other threads (cooperative)
         * - delay() blocks entire core (bad for multithreading)
         * - Even though we're on single-core Teensy, good practice for portability
         */
        threads.delay(2);
    }
}
