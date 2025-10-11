#include "app_logic.h"
#include "midi_io.h"
#include "choke_io.h"
#include "display_io.h"
#include "audio_choke.h"
#include "trace.h"
#include "timekeeper.h"
#include <TeensyThreads.h>

// External reference to choke audio effect (defined in main.cpp)
extern AudioEffectChoke choke;

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
 */

// LED configuration
static constexpr uint8_t LED_PIN = 31;

// Beat tracking state
static uint8_t tickCount = 0;         // Current tick within beat (0-23)
static bool transportActive = false;  // Is sequencer running?

// Timestamp-based timing state
static uint32_t beatStartMicros = 0;  // Timestamp of last beat start (tick 0)
static uint32_t lastTickMicros = 0;   // Timestamp of last tick (for BPM calc)
//initialized to default value so no errors/crashes
static uint32_t avgTickPeriodUs = 20833; // Average period between ticks (~20.8ms @ 120BPM)
                                          // Updated dynamically via exponential moving average

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
     * 1. Process choke events (button press/release)
     * 2. Process transport events (START/STOP affects LED state)
     * 3. Process clock ticks (update beat position, drive LED)
     * 4. Periodic debug output
     * 5. Yield CPU
     */
    for (;;) {
        // ========== 1. DRAIN CHOKE EVENTS ==========
        /**
         * WHY PROCESS CHOKE FIRST?
         * - Button response should be immediate (user perception)
         * - Choke is independent of MIDI transport state
         * - Processing early minimizes latency
         */
        ChokeEvent chokeEvent;
        while (ChokeIO::popEvent(chokeEvent)) {
            switch (chokeEvent) {
                case ChokeEvent::BUTTON_PRESS:
                    // Button pressed → engage choke (mute audio)
                    choke.engage();
                    ChokeIO::setLED(true);  // Red LED
                    DisplayIO::showChoke();  // Show choke bitmap
                    Serial.println("🔇 Choke ENGAGED");
                    break;

                case ChokeEvent::BUTTON_RELEASE:
                    // Button released → release choke (unmute audio)
                    choke.releaseChoke();
                    ChokeIO::setLED(false);  // Green LED
                    DisplayIO::showDefault();  // Return to default bitmap
                    Serial.println("🔊 Choke RELEASED");
                    break;
            }
        }

        // ========== 2. DRAIN TRANSPORT EVENTS ==========
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
                     * - Reset timestamps (start fresh timing)
                     * - Reset TimeKeeper (sample 0, beat 0)
                     * - Enable transport
                     * - Turn on LED (start of beat)
                     */
                    tickCount = 0;
                    beatStartMicros = micros();
                    lastTickMicros = 0;  // Reset for fresh EMA
                    transportActive = true;

                    // Reset TimeKeeper to beat 0, sample 0
                    TimeKeeper::reset();
                    TimeKeeper::setTransportState(TimeKeeper::TransportState::PLAYING);

                    digitalWrite(LED_PIN, HIGH);
                    TRACE(TRACE_MIDI_START);
                    Serial.println("▶ START");
                    break;

                case MidiEvent::STOP:
                    /**
                     * STOP: Pause playback
                     * - Disable transport (ignore clock ticks)
                     * - Update TimeKeeper transport state
                     * - Turn off LED (visual feedback of stopped state)
                     * - Keep tick counter (CONTINUE will resume from here)
                     */
                    transportActive = false;
                    TimeKeeper::setTransportState(TimeKeeper::TransportState::STOPPED);

                    digitalWrite(LED_PIN, LOW);
                    TRACE(TRACE_MIDI_STOP);
                    Serial.println("■ STOP");
                    break;

                case MidiEvent::CONTINUE:
                    /**
                     * CONTINUE: Resume from pause
                     * - Enable transport
                     * - Update TimeKeeper transport state
                     * - Don't reset tick counter (resume from last position)
                     * - Update LED based on current tick position
                     */
                    transportActive = true;
                    TimeKeeper::setTransportState(TimeKeeper::TransportState::PLAYING);

                    // Set LED state based on current position
                    digitalWrite(LED_PIN, (tickCount < 12) ? HIGH : LOW);
                    TRACE(TRACE_MIDI_CONTINUE);
                    Serial.println("▶ CONTINUE");
                    break;
            }
        }

        // ========== 3. DRAIN CLOCK TICKS ==========
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
             * TIMESTAMP-BASED BEAT TRACKING
             *
             * APPROACH:
             * 1. Calculate tick period (time between ticks) using exponential moving average
             * 2. Track beat start time (tick 0)
             * 3. Update LED based on elapsed time from beat start, not just tick count
             *
             * BENEFITS:
             * - Immune to MIDI jitter (smooths out irregular tick arrival)
             * - LED timing stays rock-solid even with jittery MIDI
             * - Foundation for sample-accurate quantization (future)
             *
             * MATH:
             * - MIDI clock: 24 PPQN (Pulses Per Quarter Note)
             * - At 120 BPM: 24 ticks per beat, 2 beats/sec = 48 ticks/sec
             * - Tick period: 1/48 = 20.833ms = 20833µs
             * - Half beat (LED transition): 12 ticks × 20833µs = 250ms
             */

            // Update tick period estimate (exponential moving average)
            // Alpha = 0.1 (smooth jitter, converge in ~10 ticks)
            if (lastTickMicros > 0) {
                uint32_t tickPeriod = clockMicros - lastTickMicros;
                // Sanity check: 10ms - 50ms range (60-300 BPM)
                if (tickPeriod >= 10000 && tickPeriod <= 50000) {
                    //multiply equation constants by 10 to avoid float math
                    //alpha = 0.1
                    avgTickPeriodUs = (avgTickPeriodUs * 9 + tickPeriod) / 10;

                    // Sync TimeKeeper to MIDI clock (converts ticks to samples)
                    TimeKeeper::syncToMIDIClock(avgTickPeriodUs);

                    TRACE(TRACE_TICK_PERIOD_UPDATE, avgTickPeriodUs / 10);  // Store in units of 10µs to fit in uint16_t
                }
            }
            lastTickMicros = clockMicros;

            // Increment tick counter (both local and TimeKeeper)
            tickCount++;
            TimeKeeper::incrementTick();  // Tracks ticks 0-23, auto-advances beat at 24

            // Reset at beat boundary and capture beat start timestamp
            if (tickCount >= 24) {
                tickCount = 0;
                beatStartMicros = clockMicros;
                digitalWrite(LED_PIN, HIGH);  // Turn on immediately at beat start
                TRACE(TRACE_BEAT_START);
                TRACE(TRACE_BEAT_LED_ON);
                // Beat counter already incremented by TimeKeeper::incrementTick()
            }

            // Turn off LED after short pulse (2 ticks = ~40ms @ 120 BPM)
            // Using tick count is simpler and avoids timestamp calculation every tick
            if (tickCount == 2) {
                digitalWrite(LED_PIN, LOW);
                TRACE(TRACE_BEAT_LED_OFF);
            }
        }

        // ========== 4. PERIODIC DEBUG OUTPUT ==========
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
        }

        // ========== 5. YIELD CPU ==========
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
