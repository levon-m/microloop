#include "app_logic.h"
#include "midi_io.h"
#include "input_io.h"
#include "display_io.h"
#include "audio_choke.h"
#include "effect_manager.h"
#include "trace.h"
#include "timekeeper.h"
#include <TeensyThreads.h>

// External reference to choke audio effect (defined in main.cpp)
extern AudioEffectChoke choke;

/**
 * Application Logic Implementation
 *
 * FEATURE: LED Beat Indicator (TimeKeeper-based)
 * - Blinks LED on every beat using TimeKeeper's atomic beat flag
 * - LED on at beat start (tick 0), off after 2 ticks (~40ms @ 120 BPM)
 * - Pauses on MIDI STOP, resumes on START/CONTINUE
 * - Sample-accurate beat tracking (foundation for loop quantization)
 *
 * KEY DESIGN DECISIONS:
 *
 * 1. WHY TIMEKEEPER BEAT FLAG?
 *    - Single source of timing truth (MIDI clock ↔ audio samples)
 *    - Sample-accurate beat boundaries (±128 samples ≈ ±2.9ms)
 *    - Atomic operations for lock-free cross-thread communication
 *    - Never misses a beat (flag persists until polled)
 *
 * 2. WHY SHORT PULSE (2 ticks ≈ 40ms)?
 *    - Clear visual feedback without distraction
 *    - Easy to see beat at a glance
 *    - Short enough to not interfere with performance
 *
 * 3. WHY POLL BEAT FLAG IN APP THREAD?
 *    - TimeKeeper::incrementTick() sets flag when beat advances
 *    - App thread polls every 2ms (low latency)
 *    - LED control separated from MIDI clock processing
 *    - Clean separation of concerns
 *
 */

// LED configuration
static constexpr uint8_t LED_PIN = 31;

// Transport state
static bool transportActive = false;  // Is sequencer running?

// MIDI clock timing state (for TimeKeeper sync)
static uint32_t lastTickMicros = 0;   // Timestamp of last tick (for BPM calc)
//initialized to default value so no errors/crashes
static uint32_t avgTickPeriodUs = 20833; // Average period between ticks (~20.8ms @ 120BPM)
                                          // Updated dynamically via exponential moving average

// LED beat indicator state
static uint64_t ledOffSample = 0;  // Sample position when LED should turn off (0 = LED off)

// Debug output rate limiting
static uint32_t lastPrint = 0;
static constexpr uint32_t PRINT_INTERVAL_MS = 1000;  // Print status every 1s

void AppLogic::begin() {
    // Configure LED pin
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);  // Start with LED off

    // Initialize state
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
        // ========== 1. DRAIN COMMANDS ==========
        /**
         * WHY PROCESS COMMANDS?
         * - Generic command dispatch via EffectManager
         * - Table-driven button mappings (no hardcoded switches)
         * - Supports multiple effects (not just choke)
         * - Decouples input from DSP operations
         */
        Command cmd;
        while (InputIO::popCommand(cmd)) {
            // Execute command via EffectManager
            if (EffectManager::executeCommand(cmd)) {
                // Command executed successfully

                // Update visual feedback (LED, display)
                if (cmd.type == CommandType::EFFECT_TOGGLE ||
                    cmd.type == CommandType::EFFECT_ENABLE ||
                    cmd.type == CommandType::EFFECT_DISABLE) {

                    AudioEffectBase* effect = EffectManager::getEffect(cmd.targetEffect);
                    if (effect) {
                        bool enabled = effect->isEnabled();

                        // Update LED (Key 0 = choke)
                        InputIO::setLED(cmd.targetEffect, enabled);

                        // Update display (choke-specific for now)
                        if (cmd.targetEffect == EffectID::CHOKE) {
                            if (enabled) {
                                DisplayIO::showChoke();
                            } else {
                                DisplayIO::showDefault();
                            }
                        }

                        // Debug output
                        Serial.print(effect->getName());
                        Serial.println(enabled ? " ENABLED" : " DISABLED");
                    }
                }
            } else {
                // Command failed (effect not found, invalid params, etc.)
                Serial.print("ERROR: Command failed (type=");
                Serial.print(static_cast<uint8_t>(cmd.type));
                Serial.print(", effect=");
                Serial.print(static_cast<uint8_t>(cmd.targetEffect));
                Serial.println(")");
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
                case MidiEvent::START: {
                    /**
                     * START: Reset to beginning
                     * - Reset timestamps (start fresh timing)
                     * - Reset TimeKeeper (sample 0, beat 0, tick 0)
                     * - Enable transport
                     * - Turn on LED immediately (we're at beat 0, tick 0)
                     */
                    lastTickMicros = 0;  // Reset for fresh EMA
                    transportActive = true;

                    // Reset TimeKeeper to beat 0, sample 0, tick 0
                    TimeKeeper::reset();
                    TimeKeeper::setTransportState(TimeKeeper::TransportState::PLAYING);

                    // Turn on LED for beat 0 (first beat starts immediately)
                    digitalWrite(LED_PIN, HIGH);
                    // Calculate when to turn off: 2 ticks = (2/24) of a beat
                    uint32_t spb = TimeKeeper::getSamplesPerBeat();
                    uint32_t pulseSamples = (spb * 2) / 24;  // 2 ticks worth of samples
                    ledOffSample = TimeKeeper::getSamplePosition() + pulseSamples;
                    TRACE(TRACE_BEAT_LED_ON);

                    TRACE(TRACE_MIDI_START);
                    Serial.println("▶ START");
                    break;
                }

                case MidiEvent::STOP:
                    /**
                     * STOP: Pause playback
                     * - Disable transport (ignore clock ticks)
                     * - Update TimeKeeper transport state
                     * - Turn off LED (visual feedback of stopped state)
                     * - Reset LED state machine
                     */
                    transportActive = false;
                    TimeKeeper::setTransportState(TimeKeeper::TransportState::STOPPED);

                    digitalWrite(LED_PIN, LOW);
                    ledOffSample = 0;  // Reset LED state (0 = LED off)
                    TRACE(TRACE_MIDI_STOP);
                    Serial.println("■ STOP");
                    break;

                case MidiEvent::CONTINUE:
                    /**
                     * CONTINUE: Resume from pause
                     * - Enable transport
                     * - Update TimeKeeper transport state
                     * - TimeKeeper maintains beat/tick position across STOP/CONTINUE
                     * - LED will update automatically based on TimeKeeper state
                     */
                    transportActive = true;
                    TimeKeeper::setTransportState(TimeKeeper::TransportState::PLAYING);

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
             * MIDI CLOCK TO TIMEKEEPER SYNC
             *
             * APPROACH:
             * 1. Calculate tick period (time between ticks) using exponential moving average
             * 2. Sync TimeKeeper with tick period (converts to samples per beat)
             * 3. TimeKeeper handles all beat/bar tracking and sample-accurate timing
             *
             * BENEFITS:
             * - TimeKeeper provides single source of timing truth
             * - Sample-accurate beat tracking (for future loop quantization)
             * - LED driven by TimeKeeper beat flag (perfect accuracy)
             *
             * MATH:
             * - MIDI clock: 24 PPQN (Pulses Per Quarter Note)
             * - At 120 BPM: 24 ticks per beat, 2 beats/sec = 48 ticks/sec
             * - Tick period: 1/48 = 20.833ms = 20833µs
             * - TimeKeeper converts to samples: 22050 samples/beat @ 120 BPM
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

            // Increment TimeKeeper tick counter
            // TimeKeeper tracks ticks 0-23, auto-advances beat at 24, sets beat flag
            TimeKeeper::incrementTick();
        }

        // ========== 3A. TIMEKEEPER BEAT LED CONTROL ==========
        /**
         * SAMPLE-BASED LED TIMING (ROBUST UNDER LOAD)
         *
         * WHY SAMPLE-BASED (NOT TICK-BASED)?
         * - Tick-based approach requires frequent polling to catch tick=2
         * - Under heavy load (MIDI spam, choke button), app thread gets starved
         * - Missing the tick=2 window causes LED to stay on too long or shut off
         * - Sample-based approach calculates OFF time once, just checks if reached
         *
         * APPROACH:
         * 1. Poll beat flag (never misses a beat - flag persists until polled)
         * 2. Calculate sample position when LED should turn off (beatSample + pulseSamples)
         * 3. Every loop, check if current sample >= ledOffSample
         * 4. Turn off LED when threshold reached
         *
         * BENEFITS:
         * - ✅ Deterministic: LED turns off at exact sample position
         * - ✅ Load-independent: Works even if app thread starves for 100ms+
         * - ✅ Sample-accurate: Same precision as loop quantization
         * - ✅ Simple state: Single 64-bit variable (ledOffSample)
         *
         * PULSE DURATION:
         * - 2 ticks = (2/24) of a beat = ~40ms @ 120 BPM
         * - At 44.1kHz: ~1837 samples
         * - LED stays on for exactly this duration, regardless of thread timing
         */
        uint64_t currentSample = TimeKeeper::getSamplePosition();

        // Check for new beat
        if (TimeKeeper::pollBeatFlag()) {
            // Beat boundary crossed - turn on LED and calculate OFF time
            digitalWrite(LED_PIN, HIGH);

            // Calculate pulse duration: 2 ticks = (2/24) of a beat
            uint32_t spb = TimeKeeper::getSamplesPerBeat();
            uint32_t pulseSamples = (spb * 2) / 24;  // 2 ticks worth of samples
            ledOffSample = currentSample + pulseSamples;

            TRACE(TRACE_BEAT_LED_ON);
        }

        // Turn off LED when sample position reaches ledOffSample
        if (ledOffSample > 0 && currentSample >= ledOffSample) {
            digitalWrite(LED_PIN, LOW);
            ledOffSample = 0;  // Mark LED as off
            TRACE(TRACE_BEAT_LED_OFF);
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
