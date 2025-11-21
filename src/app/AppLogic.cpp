#include "AppLogic.h"
#include "MidiIO.h"
#include "NeokeyIO.h"
#include "McpIO.h"
#include "AudioChoke.h"
#include "AudioFreeze.h"
#include "AudioStutter.h"
#include "EffectManager.h"
#include "Trace.h"
#include "TimeKeeper.h"
#include "EffectQuantization.h"
#include "EncoderHandler.h"
#include "DisplayManager.h"
#include "ChokeController.h"
#include "FreezeController.h"
#include "StutterController.h"
#include "GlobalController.h"
#include "AppState.h"

#include <TeensyThreads.h>

// External references to audio effects (defined in main.cpp)
extern AudioEffectChoke choke;
extern AudioEffectFreeze freeze;
extern AudioEffectStutter stutter;

// ========== APPLICATION STATE ==========
static AppState s_appState;  // Application mode and context

// ========== EFFECT CONTROLLERS ==========
static ChokeController* s_chokeController = nullptr;    // Choke effect controller
static FreezeController* s_freezeController = nullptr;  // Freeze effect controller
static StutterController* s_stutterController = nullptr;
static GlobalController* s_globalController = nullptr;  // Global parameters controller

// ========== LED BEAT INDICATOR STATE ==========
static constexpr uint8_t LED_PIN = 38;
static uint64_t s_ledOffSample = 0;  // Sample position when LED should turn off (0 = LED off)

// ========== TRANSPORT STATE ==========
static bool s_transportActive = false;  // Is sequencer running?

// ========== MIDI CLOCK TIMING ==========
static uint32_t s_lastTickMicros = 0;
static uint32_t s_avgTickPeriodUs = 20833;  // ~20.8ms @ 120BPM

// ========== DEBUG OUTPUT STATE ==========
static uint32_t s_lastPrint = 0;
static constexpr uint32_t PRINT_INTERVAL_MS = 1000;

// ========== ENCODER HANDLER INSTANCES ==========
static EncoderHandler::Handler* s_encoder1 = nullptr;  // STUTTER parameters
static EncoderHandler::Handler* s_encoder2 = nullptr;  // FREEZE parameters
static EncoderHandler::Handler* s_encoder3 = nullptr;  // CHOKE parameters
static EncoderHandler::Handler* s_encoder4 = nullptr;  // GLOBAL parameters

// ========== ENCODER HELPER FUNCTIONS ==========

/**
 * Check if any encoder (except one) is currently touched
 *
 * @param ignore Encoder to exclude from check (typically the caller's encoder)
 * @return true if any other encoder is touched, false otherwise
 */
static bool anyEncoderTouchedExcept(const EncoderHandler::Handler* ignore) {
    EncoderHandler::Handler* encoders[] = { s_encoder1, s_encoder2, s_encoder3, s_encoder4 };
    const size_t numEncoders = sizeof(encoders) / sizeof(encoders[0]);

    for (size_t i = 0; i < numEncoders; ++i) {
        EncoderHandler::Handler* e = encoders[i];
        if (e == NULL) {
            continue;
        }
        if (e == ignore) {
            continue;
        }
        if (e->isTouched()) {
            return true;
        }
    }
    return false;
}

/**
 * Clamp index to valid range
 *
 * @param value Value to clamp
 * @param minValue Minimum allowed value
 * @param maxValue Maximum allowed value
 * @return Clamped value
 */
static int8_t clampIndex(int8_t value, int8_t minValue, int8_t maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

// ========== HELPER FUNCTIONS (INTERNAL) ==========
// These functions break up the main thread loop into logical sections

/**
 * Process input commands from button queue
 * Handles effect toggle/enable/disable and visual feedback
 */
static void processInputCommands() {
    Command cmd;
    while (NeokeyIO::popCommand(cmd)) {
        // Check if CHOKE/FREEZE controllers want to intercept
        bool handled = false;

        if (cmd.targetEffect == EffectID::CHOKE && s_chokeController) {
            if (cmd.type == CommandType::EFFECT_ENABLE || cmd.type == CommandType::EFFECT_TOGGLE) {
                handled = s_chokeController->handleButtonPress(cmd);
            } else if (cmd.type == CommandType::EFFECT_DISABLE) {
                handled = s_chokeController->handleButtonRelease(cmd);
            }
        } else if (cmd.targetEffect == EffectID::FREEZE && s_freezeController) {
            if (cmd.type == CommandType::EFFECT_ENABLE || cmd.type == CommandType::EFFECT_TOGGLE) {
                handled = s_freezeController->handleButtonPress(cmd);
            } else if (cmd.type == CommandType::EFFECT_DISABLE) {
                handled = s_freezeController->handleButtonRelease(cmd);
            }
        } else if (cmd.targetEffect == EffectID::STUTTER && s_stutterController) {
            if (cmd.type == CommandType::EFFECT_ENABLE || cmd.type == CommandType::EFFECT_TOGGLE) {
                handled = s_stutterController->handleButtonPress(cmd);
            } else if (cmd.type == CommandType::EFFECT_DISABLE) {
                handled = s_stutterController->handleButtonRelease(cmd);
            }
        } else if (cmd.targetEffect == EffectID::FUNC && s_stutterController) {
            // FUNC is handled by stutter controller (modifier button)
            if (cmd.type == CommandType::EFFECT_ENABLE) {
                handled = s_stutterController->handleButtonPress(cmd);
            } else if (cmd.type == CommandType::EFFECT_DISABLE) {
                handled = s_stutterController->handleButtonRelease(cmd);
            }
        }

        // If handler didn't intercept, execute via EffectManager
        if (!handled && EffectManager::executeCommand(cmd)) {
            // Update visual feedback
            AudioEffectBase* effect = EffectManager::getEffect(cmd.targetEffect);
            if (effect) {
                bool enabled = effect->isEnabled();
                NeokeyIO::setLED(cmd.targetEffect, enabled);

                DisplayManager::instance().updateDisplay();
                Serial.print(effect->getName());
                Serial.println(enabled ? " ENABLED" : " DISABLED");
            }
        }
    }
}

/**
 * Update encoder handlers
 * Hardware event processing now handled by dedicated MCP thread (McpIO::threadLoop)
 * This function just updates the encoder handler logic (callbacks, display, etc.)
 */
static void updateEncoders() {
    // Note: McpIO::update() is no longer needed here - dedicated thread handles it
    s_encoder1->update();   // STUTTER parameters
    s_encoder2->update();   // FREEZE parameters
    s_encoder3->update();   // CHOKE parameters
    s_encoder4->update();   // GLOBAL parameters
}

/**
 * Update effect controller visual feedback
 * Handles LED blinking and display updates for active effects
 */
static void updateEffectHandlers() {
    if (s_chokeController) {
        s_chokeController->updateVisualFeedback();
    }
    if (s_freezeController) {
        s_freezeController->updateVisualFeedback();
    }
    if (s_stutterController) {
        s_stutterController->updateVisualFeedback();
    }
}

/**
 * Process MIDI transport events (START, STOP, CONTINUE)
 * Manages transport state and LED beat indicator
 */
static void processTransportEvents() {
    MidiEvent event;
    while (MidiIO::popEvent(event)) {
        switch (event) {
            case MidiEvent::START: {
                s_lastTickMicros = 0;
                s_transportActive = true;
                TimeKeeper::reset();
                TimeKeeper::setTransportState(TimeKeeper::TransportState::PLAYING);

                // Turn on LED for beat 0
                digitalWrite(LED_PIN, HIGH);
                uint32_t spb = TimeKeeper::getSamplesPerBeat();
                uint32_t pulseSamples = (spb * 2) / 24;  // 2 ticks
                s_ledOffSample = TimeKeeper::getSamplePosition() + pulseSamples;
                TRACE(TRACE_BEAT_LED_ON);
                TRACE(TRACE_MIDI_START);
                Serial.println("▶ START");
                break;
            }

            case MidiEvent::STOP:
                s_transportActive = false;
                TimeKeeper::setTransportState(TimeKeeper::TransportState::STOPPED);
                digitalWrite(LED_PIN, LOW);
                s_ledOffSample = 0;
                TRACE(TRACE_MIDI_STOP);
                Serial.println("■ STOP");
                break;

            case MidiEvent::CONTINUE:
                s_transportActive = true;
                TimeKeeper::setTransportState(TimeKeeper::TransportState::PLAYING);
                TRACE(TRACE_MIDI_CONTINUE);
                Serial.println("▶ CONTINUE");
                break;
        }
    }
}

/**
 * Process MIDI clock ticks
 * Updates tempo estimation and increments TimeKeeper tick counter
 */
static void processClockTicks() {
    uint32_t clockMicros;
    while (MidiIO::popClock(clockMicros)) {
        if (!s_transportActive) continue;

        // Update tick period estimate (EMA)
        if (s_lastTickMicros > 0) {
            uint32_t tickPeriod = clockMicros - s_lastTickMicros;
            if (tickPeriod >= 10000 && tickPeriod <= 50000) {
                s_avgTickPeriodUs = (s_avgTickPeriodUs * 9 + tickPeriod) / 10;
                TimeKeeper::syncToMIDIClock(s_avgTickPeriodUs);
                TRACE(TRACE_TICK_PERIOD_UPDATE, s_avgTickPeriodUs / 10);
            }
        }
        s_lastTickMicros = clockMicros;
        TimeKeeper::incrementTick();
    }
}

/**
 * Update beat indicator LED
 * Turns LED on at beat boundaries, off after short pulse
 */
static void updateBeatLed() {
    uint64_t currentSample = TimeKeeper::getSamplePosition();

    if (TimeKeeper::pollBeatFlag()) {
        digitalWrite(LED_PIN, HIGH);
        uint32_t spb = TimeKeeper::getSamplesPerBeat();
        uint32_t pulseSamples = (spb * 2) / 24;
        s_ledOffSample = currentSample + pulseSamples;
        TRACE(TRACE_BEAT_LED_ON);
    }

    if (s_ledOffSample > 0 && currentSample >= s_ledOffSample) {
        digitalWrite(LED_PIN, LOW);
        s_ledOffSample = 0;
        TRACE(TRACE_BEAT_LED_OFF);
    }
}

// ========== PUBLIC API ==========

void AppLogic::begin() {
    // Configure LED pin
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Configure RGB LED pins (for STUTTER effect visual feedback)
    pinMode(28, OUTPUT);  // R pin (PWM)
    pinMode(36, OUTPUT);  // G pin (PWM)
    pinMode(37, OUTPUT);  // B pin (PWM)
    analogWrite(28, 0);   // R off
    analogWrite(36, 0);   // G off
    analogWrite(37, 0);   // B off

    // Initialize subsystems
    EffectQuantization::initialize();
    DisplayManager::instance().initialize();

    // Create effect controllers
    s_chokeController = new ChokeController(choke);
    s_freezeController = new FreezeController(freeze);
    s_stutterController = new StutterController(stutter);
    s_globalController = new GlobalController();

    // Create encoder handlers
    s_encoder1 = new EncoderHandler::Handler(0);  // STUTTER parameters
    s_encoder2 = new EncoderHandler::Handler(1);  // FREEZE parameters
    s_encoder3 = new EncoderHandler::Handler(2);  // CHOKE parameters
    s_encoder4 = new EncoderHandler::Handler(3);  // GLOBAL parameters

    // Bind controllers to encoders
    s_stutterController->bindToEncoder(*s_encoder1, anyEncoderTouchedExcept);
    s_freezeController->bindToEncoder(*s_encoder2, anyEncoderTouchedExcept);
    s_chokeController->bindToEncoder(*s_encoder3, anyEncoderTouchedExcept);
    s_globalController->bindToEncoder(*s_encoder4, anyEncoderTouchedExcept);

    // Initialize state
    s_transportActive = false;
}

void AppLogic::threadLoop() {
    for (;;) {
        // Main application loop - organized into logical sections
        // Each section is implemented as a separate function for clarity

        // 1. Process button presses and effect commands
        processInputCommands();

        // 2. Update encoder menu handlers (parameter editing)
        updateEncoders();

        // 3. Update effect handler visual feedback
        updateEffectHandlers();

        // 4. Process MIDI transport events (START/STOP/CONTINUE)
        processTransportEvents();

        // 5. Process MIDI clock ticks (tempo tracking)
        processClockTicks();

        // 6. Update beat indicator LED
        updateBeatLed();

        // 7. Periodic debug output (optional)
        uint32_t now = millis();
        if (now - s_lastPrint >= PRINT_INTERVAL_MS) {
            s_lastPrint = now;
            // Optional: Print status here
        }

        // 8. Yield CPU to other threads
        threads.delay(2);
    }
}

// ========== GLOBAL QUANTIZATION API (delegated) ==========
Quantization AppLogic::getGlobalQuantization() {
    return EffectQuantization::getGlobalQuantization();
}

void AppLogic::setGlobalQuantization(Quantization quant) {
    EffectQuantization::setGlobalQuantization(quant);
}