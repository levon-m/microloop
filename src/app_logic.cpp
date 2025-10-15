#include "app_logic.h"
#include "midi_io.h"
#include "input_io.h"
#include "display_io.h"
#include "encoder_io.h"
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

// Display priority state (tracks last activated effect)
static EffectID lastActivatedEffect = EffectID::NONE;

// ========== GLOBAL QUANTIZATION STATE ==========
// Default quantization: 1/16 note
static Quantization globalQuantization = Quantization::QUANT_16;

// Encoder 4 state tracking (for global quantization menu)
static int32_t lastEncoder4Position = 0;      // Last raw position (for detecting movement)
static int32_t encoder4Accumulator = 0;       // Accumulated steps since last turn
static bool encoder4WasTouched = false;       // Tracks if encoder was recently touched
static uint32_t encoder4ReleaseTime = 0;      // Time when encoder was released
static constexpr uint32_t ENCODER_DISPLAY_COOLDOWN_MS = 2000;  // 2 second cooldown before returning to default

// ========== CHOKE MENU STATE ==========
// Encoder 3 state tracking (for choke length menu)
static int32_t lastEncoder3Position = 0;      // Last raw position (for detecting movement)
static int32_t encoder3Accumulator = 0;       // Accumulated steps since last turn
static bool encoder3WasTouched = false;       // Tracks if encoder was recently touched
static uint32_t encoder3ReleaseTime = 0;      // Time when encoder was released

// Helper function to convert Quantization to BitmapID
static BitmapID quantizationToBitmap(Quantization quant) {
    switch (quant) {
        case Quantization::QUANT_32: return BitmapID::QUANT_32;
        case Quantization::QUANT_16: return BitmapID::QUANT_16;
        case Quantization::QUANT_8:  return BitmapID::QUANT_8;
        case Quantization::QUANT_4:  return BitmapID::QUANT_4;
        default: return BitmapID::QUANT_16;  // Default fallback
    }
}

// Helper function to get quantization name string
static const char* quantizationName(Quantization quant) {
    switch (quant) {
        case Quantization::QUANT_32: return "1/32";
        case Quantization::QUANT_16: return "1/16";
        case Quantization::QUANT_8:  return "1/8";
        case Quantization::QUANT_4:  return "1/4";
        default: return "1/16";
    }
}

// Helper function to convert ChokeLength to BitmapID
static BitmapID chokeLengthToBitmap(ChokeLength length) {
    switch (length) {
        case ChokeLength::FREE:      return BitmapID::CHOKE_LENGTH_FREE;
        case ChokeLength::QUANTIZED: return BitmapID::CHOKE_LENGTH_QUANT;
        default: return BitmapID::CHOKE_LENGTH_FREE;  // Default fallback
    }
}

// Helper function to get choke length name string
static const char* chokeLengthName(ChokeLength length) {
    switch (length) {
        case ChokeLength::FREE:      return "Free";
        case ChokeLength::QUANTIZED: return "Quantized";
        default: return "Free";
    }
}

// Helper function to calculate quantized duration in samples
static uint32_t calculateQuantizedDuration(Quantization quant) {
    uint32_t samplesPerBeat = TimeKeeper::getSamplesPerBeat();

    switch (quant) {
        case Quantization::QUANT_32:
            return samplesPerBeat / 8;  // 1/32 note = 1/8 of a beat
        case Quantization::QUANT_16:
            return samplesPerBeat / 4;  // 1/16 note = 1/4 of a beat
        case Quantization::QUANT_8:
            return samplesPerBeat / 2;  // 1/8 note = 1/2 of a beat
        case Quantization::QUANT_4:
            return samplesPerBeat;      // 1/4 note = 1 full beat
        default:
            return samplesPerBeat / 4;  // Default: 1/16 note
    }
}

void AppLogic::begin() {
    // Configure LED pin
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);  // Start with LED off

    // Initialize state
    transportActive = false;

    // Initialize encoder positions
    lastEncoder3Position = EncoderIO::getPosition(2);  // Encoder 3 is index 2
    lastEncoder4Position = EncoderIO::getPosition(3);  // Encoder 4 is index 3
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
            // ========== CHOKE QUANTIZED MODE LOGIC ==========
            /**
             * SPECIAL HANDLING FOR CHOKE EFFECT IN QUANTIZED MODE
             *
             * In QUANTIZED mode:
             * - Button press: Engage choke AND schedule auto-release
             * - Button release: Ignored (choke releases after quantized duration)
             *
             * In FREE mode:
             * - Button press: Engage choke
             * - Button release: Release choke immediately
             */
            if (cmd.targetEffect == EffectID::CHOKE) {
                ChokeLength lengthMode = choke.getLengthMode();

                if (lengthMode == ChokeLength::QUANTIZED) {
                    // Quantized mode: Only handle button press, ignore release
                    if (cmd.type == CommandType::EFFECT_ENABLE ||
                        cmd.type == CommandType::EFFECT_TOGGLE) {

                        // Engage choke
                        choke.enable();

                        // Calculate quantized duration in samples
                        uint32_t durationSamples = calculateQuantizedDuration(globalQuantization);

                        // Schedule auto-release
                        uint64_t releaseSample = TimeKeeper::getSamplePosition() + durationSamples;
                        choke.scheduleRelease(releaseSample);

                        // Update visual feedback
                        bool enabled = choke.isEnabled();
                        InputIO::setLED(cmd.targetEffect, enabled);

                        if (enabled) {
                            lastActivatedEffect = cmd.targetEffect;
                            DisplayIO::showChoke();
                        }

                        // Debug output
                        Serial.print("Choke ENGAGED (Quantized, duration=");
                        Serial.print(quantizationName(globalQuantization));
                        Serial.println(")");

                        // Skip normal command processing
                        continue;
                    } else if (cmd.type == CommandType::EFFECT_DISABLE) {
                        // Button release in quantized mode: Ignore
                        Serial.println("Choke button released (ignored in Quantized mode)");
                        continue;
                    }
                }
                // FREE mode: Fall through to normal command processing
            }

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

                        // Update LED
                        InputIO::setLED(cmd.targetEffect, enabled);

                        // Track last activated effect for display priority
                        if (enabled) {
                            lastActivatedEffect = cmd.targetEffect;
                        }

                        // Update display with priority system
                        // Show the most recently activated effect, or default if none active
                        AudioEffectBase* freezeEffect = EffectManager::getEffect(EffectID::FREEZE);
                        AudioEffectBase* chokeEffect = EffectManager::getEffect(EffectID::CHOKE);

                        bool freezeActive = freezeEffect && freezeEffect->isEnabled();
                        bool chokeActive = chokeEffect && chokeEffect->isEnabled();

                        if (lastActivatedEffect == EffectID::FREEZE && freezeActive) {
                            DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);
                        } else if (lastActivatedEffect == EffectID::CHOKE && chokeActive) {
                            DisplayIO::showChoke();
                        } else if (freezeActive) {
                            // Freeze is active but not last activated (show it anyway)
                            DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);
                        } else if (chokeActive) {
                            // Choke is active but not last activated (show it anyway)
                            DisplayIO::showChoke();
                        } else {
                            // No effects active - show default
                            DisplayIO::showDefault();
                            lastActivatedEffect = EffectID::NONE;
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

        // ========== 2. HANDLE ENCODER 4 (QUANTIZATION MENU) ==========
        /**
         * ENCODER 4 QUANTIZATION MENU
         *
         * DESIGN:
         * - Encoder 4 controls global quantization setting
         * - Options: 1/32, 1/16 (default), 1/8, 1/4
         * - 2 detents = 1 "turn" (allows "peeking" without changing value)
         * - Display shows quantization bitmap while encoder is touched
         * - 2 second cooldown after release before returning to default display
         *
         * DETENT CALCULATION:
         * - Most encoders: 4 quadrature steps per physical detent
         * - 2 detents = 8 steps (allows slight touch without changing)
         * - Direction: CW = increase (1/32 → 1/16 → 1/8 → 1/4), CCW = decrease
         */

        // Process encoder events (update positions)
        EncoderIO::update();

        // Get current encoder 4 position (raw steps)
        int32_t currentEncoder4Position = EncoderIO::getPosition(3);  // Encoder 4 is index 3
        int32_t encoder4Delta = currentEncoder4Position - lastEncoder4Position;

        // Check if encoder was touched (position changed)
        if (encoder4Delta != 0) {
            // Encoder is being touched - show quantization bitmap immediately
            if (!encoder4WasTouched) {
                encoder4WasTouched = true;
                // Show current quantization bitmap
                DisplayIO::showBitmap(quantizationToBitmap(globalQuantization));
            }

            // Reset the release timer since encoder is still being touched
            encoder4ReleaseTime = 0;

            // Accumulate steps for turn detection
            encoder4Accumulator += encoder4Delta;

            // Calculate turns based on detents (2 detents = 1 turn)
            // Typical encoder: 4 steps per detent, so 8 steps = 2 detents = 1 turn
            int32_t turns = encoder4Accumulator / 8;  // 8 steps = 2 detents

            // Update quantization if we've crossed a turn boundary
            if (turns != 0) {
                // Map quantization to integer index (0-3)
                int8_t currentIndex = static_cast<int8_t>(globalQuantization);
                int8_t newIndex = currentIndex + turns;

                // Clamp to valid range (0-3)
                if (newIndex < 0) newIndex = 0;
                if (newIndex > 3) newIndex = 3;

                // Update quantization if changed
                if (newIndex != currentIndex) {
                    Quantization newQuant = static_cast<Quantization>(newIndex);
                    globalQuantization = newQuant;

                    // Update display to show new quantization
                    DisplayIO::showBitmap(quantizationToBitmap(newQuant));

                    // Serial output for debugging
                    Serial.print("Global Quantization: ");
                    Serial.println(quantizationName(newQuant));

                    // Reset accumulator to prevent "unwinding" at boundaries
                    encoder4Accumulator = 0;
                } else {
                    // Hit a boundary (clamped) - reset accumulator to prevent buildup
                    encoder4Accumulator = 0;
                }
            }

            // Always update last position so we can detect when encoder stops moving
            lastEncoder4Position = currentEncoder4Position;
        } else {
            // Encoder not being touched
            if (encoder4WasTouched) {
                // Encoder was just released - start cooldown timer
                encoder4WasTouched = false;
                encoder4ReleaseTime = millis();
            }
        }

        // Handle display cooldown (return to default after 2 seconds of inactivity)
        if (!encoder4WasTouched && encoder4ReleaseTime > 0) {
            uint32_t now = millis();
            if (now - encoder4ReleaseTime >= ENCODER_DISPLAY_COOLDOWN_MS) {
                // Cooldown expired - return to default display (unless effect is active)
                encoder4ReleaseTime = 0;  // Clear cooldown

                // Check if any effects are active (use same priority logic as effect system)
                AudioEffectBase* freezeEffect = EffectManager::getEffect(EffectID::FREEZE);
                AudioEffectBase* chokeEffect = EffectManager::getEffect(EffectID::CHOKE);

                bool freezeActive = freezeEffect && freezeEffect->isEnabled();
                bool chokeActive = chokeEffect && chokeEffect->isEnabled();

                if (lastActivatedEffect == EffectID::FREEZE && freezeActive) {
                    DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);
                } else if (lastActivatedEffect == EffectID::CHOKE && chokeActive) {
                    DisplayIO::showChoke();
                } else if (freezeActive) {
                    DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);
                } else if (chokeActive) {
                    DisplayIO::showChoke();
                } else {
                    DisplayIO::showDefault();
                }
            }
        }

        // ========== 2A. HANDLE ENCODER 3 (CHOKE LENGTH MENU) ==========
        /**
         * ENCODER 3 CHOKE LENGTH MENU
         *
         * DESIGN:
         * - Encoder 3 controls choke length parameter
         * - Options: FREE (default), QUANTIZED
         * - 2 detents = 1 "turn" (allows "peeking" without changing value)
         * - Display shows choke length bitmap while encoder is touched
         * - 2 second cooldown after release before returning to default display
         *
         * CHOKE LENGTH:
         * - FREE: Release immediately when button released
         * - QUANTIZED: Auto-release after global quantization duration
         *
         * DETENT CALCULATION:
         * - Most encoders: 4 quadrature steps per physical detent
         * - 2 detents = 8 steps (allows slight touch without changing)
         * - Direction: CW = QUANTIZED, CCW = FREE
         */

        // Get current encoder 3 position (raw steps)
        int32_t currentEncoder3Position = EncoderIO::getPosition(2);  // Encoder 3 is index 2
        int32_t encoder3Delta = currentEncoder3Position - lastEncoder3Position;

        // Check if encoder was touched (position changed)
        if (encoder3Delta != 0) {
            // Encoder is being touched - show choke length bitmap immediately
            if (!encoder3WasTouched) {
                encoder3WasTouched = true;
                // Show current choke length bitmap
                DisplayIO::showBitmap(chokeLengthToBitmap(choke.getLengthMode()));
            }

            // Reset the release timer since encoder is still being touched
            encoder3ReleaseTime = 0;

            // Accumulate steps for turn detection
            encoder3Accumulator += encoder3Delta;

            // Calculate turns based on detents (2 detents = 1 turn)
            // Typical encoder: 4 steps per detent, so 8 steps = 2 detents = 1 turn
            int32_t turns = encoder3Accumulator / 8;  // 8 steps = 2 detents

            // Update choke length if we've crossed a turn boundary
            if (turns != 0) {
                // Update LENGTH parameter
                int8_t currentIndex = static_cast<int8_t>(choke.getLengthMode());
                int8_t newIndex = currentIndex + turns;

                // Clamp to valid range (0-1)
                if (newIndex < 0) newIndex = 0;
                if (newIndex > 1) newIndex = 1;

                // Update choke length if changed
                if (newIndex != currentIndex) {
                    ChokeLength newLength = static_cast<ChokeLength>(newIndex);
                    choke.setLengthMode(newLength);

                    // Update display to show new value
                    DisplayIO::showBitmap(chokeLengthToBitmap(newLength));

                    // Serial output for debugging
                    Serial.print("Choke Length: ");
                    Serial.println(chokeLengthName(newLength));

                    // Reset accumulator to prevent "unwinding" at boundaries
                    encoder3Accumulator = 0;
                } else {
                    // Hit a boundary (clamped) - reset accumulator to prevent buildup
                    encoder3Accumulator = 0;
                }
            }

            // Always update last position so we can detect when encoder stops moving
            lastEncoder3Position = currentEncoder3Position;
        } else {
            // Encoder not being touched
            if (encoder3WasTouched) {
                // Encoder was just released - start cooldown timer
                encoder3WasTouched = false;
                encoder3ReleaseTime = millis();
            }
        }

        // Handle display cooldown (return to default after 2 seconds of inactivity)
        if (!encoder3WasTouched && encoder3ReleaseTime > 0) {
            uint32_t now = millis();
            if (now - encoder3ReleaseTime >= ENCODER_DISPLAY_COOLDOWN_MS) {
                // Cooldown expired - return to default display (unless effect is active)
                encoder3ReleaseTime = 0;  // Clear cooldown

                // Check if any effects are active (use same priority logic as effect system)
                AudioEffectBase* freezeEffect = EffectManager::getEffect(EffectID::FREEZE);
                AudioEffectBase* chokeEffect = EffectManager::getEffect(EffectID::CHOKE);

                bool freezeActive = freezeEffect && freezeEffect->isEnabled();
                bool chokeActive = chokeEffect && chokeEffect->isEnabled();

                if (lastActivatedEffect == EffectID::FREEZE && freezeActive) {
                    DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);
                } else if (lastActivatedEffect == EffectID::CHOKE && chokeActive) {
                    DisplayIO::showChoke();
                } else if (freezeActive) {
                    DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);
                } else if (chokeActive) {
                    DisplayIO::showChoke();
                } else {
                    DisplayIO::showDefault();
                }
            }
        }

        // ========== 2B. MONITOR CHOKE AUTO-RELEASE (QUANTIZED MODE) ==========
        /**
         * QUANTIZED CHOKE AUTO-RELEASE DISPLAY UPDATE
         *
         * PROBLEM:
         * - In QUANTIZED mode, choke auto-releases in audio ISR (not via button)
         * - Display shows choke bitmap until button press or encoder timeout
         * - Desired: Display should update immediately when choke auto-releases
         *
         * SOLUTION:
         * - Poll choke state every loop iteration
         * - If choke was active (lastActivatedEffect == CHOKE) but now disabled,
         *   update display to show default (or other active effects)
         *
         * PERFORMANCE:
         * - Single atomic read (isEnabled()) every 2ms
         * - Negligible overhead (~10 CPU cycles)
         */
        if (lastActivatedEffect == EffectID::CHOKE && choke.getLengthMode() == ChokeLength::QUANTIZED) {
            // In quantized mode with choke as last activated effect
            // Check if choke auto-released
            if (!choke.isEnabled()) {
                // Choke has auto-released - update display
                AudioEffectBase* freezeEffect = EffectManager::getEffect(EffectID::FREEZE);
                bool freezeActive = freezeEffect && freezeEffect->isEnabled();

                if (freezeActive) {
                    // Show freeze if active
                    DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);
                    lastActivatedEffect = EffectID::FREEZE;
                } else {
                    // No effects active - show default
                    DisplayIO::showDefault();
                    lastActivatedEffect = EffectID::NONE;
                }

                // Update LED to reflect disabled state
                InputIO::setLED(EffectID::CHOKE, false);

                // Debug output
                Serial.println("Choke auto-released (Quantized mode)");
            }
        }

        // ========== 3. DRAIN TRANSPORT EVENTS ==========
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

// =============================================================================
// GLOBAL QUANTIZATION API
// =============================================================================

Quantization AppLogic::getGlobalQuantization() {
    return globalQuantization;
}

void AppLogic::setGlobalQuantization(Quantization quant) {
    globalQuantization = quant;
}
