#include "App.h"
#include "MidiInput.h"
#include "NeokeyInput.h"
#include "Mcp23017Input.h"
#include "ChokeAudio.h"
#include "FreezeAudio.h"
#include "StutterAudio.h"
#include "EffectManager.h"
#include "Trace.h"
#include "Timebase.h"
#include "EffectQuantization.h"
#include "EncoderHandler.h"
#include "DisplayManager.h"
#include "ChokeController.h"
#include "FreezeController.h"
#include "StutterController.h"
#include "GlobalController.h"
#include "PresetController.h"
#include "AppState.h"

#include <TeensyThreads.h>

// External references to audio effects (defined in main.cpp)
extern ChokeAudio choke;
extern FreezeAudio freeze;
extern StutterAudio stutter;

// ========== APPLICATION STATE ==========
static AppState s_appState;  // Application mode and context

// ========== EFFECT CONTROLLERS ==========
static ChokeController* s_chokeController = nullptr;    // Choke effect controller
static FreezeController* s_freezeController = nullptr;  // Freeze effect controller
static StutterController* s_stutterController = nullptr;
static GlobalController* s_globalController = nullptr;  // Global parameters controller
static PresetController* s_presetController = nullptr;  // Preset save/load controller

// ========== LED BEAT INDICATOR STATE ==========
static constexpr uint8_t LED_PIN = 38;
static uint64_t s_ledOffSample = 0;  // Sample position when LED should turn off (0 = LED off)

// ========== PRESET BUTTON GPIO PINS ==========
static constexpr uint8_t PRESET_PINS[4] = { 40, 41, 27, 26 };  // Preset 1-4 buttons (active-low)
static bool s_presetLastState[4] = { true, true, true, true }; // true = released (HIGH)

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
    while (NeokeyInput::popCommand(cmd)) {
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
        } else if (cmd.targetEffect == EffectID::FUNC) {
            // FUNC is handled by stutter controller (modifier button)
            // Also notify preset controller for FUNC+preset combos
            if (cmd.type == CommandType::EFFECT_ENABLE) {
                if (s_stutterController) {
                    handled = s_stutterController->handleButtonPress(cmd);
                }
                if (s_presetController) {
                    s_presetController->handleFuncPress();
                }
            } else if (cmd.type == CommandType::EFFECT_DISABLE) {
                if (s_stutterController) {
                    handled = s_stutterController->handleButtonRelease(cmd);
                }
                if (s_presetController) {
                    s_presetController->handleFuncRelease();
                }
            }
        }

        // If handler didn't intercept, execute via EffectManager
        if (!handled && EffectManager::executeCommand(cmd)) {
            // Update visual feedback
            IEffectAudio* effect = EffectManager::getEffect(cmd.targetEffect);
            if (effect) {
                bool enabled = effect->isEnabled();
                NeokeyInput::setLED(cmd.targetEffect, enabled);

                DisplayManager::instance().updateDisplay();
                Serial.print(effect->getName());
                Serial.println(enabled ? " ENABLED" : " DISABLED");
            }
        }
    }
}

/**
 * Process preset button inputs from direct GPIO pins
 * Handles preset save/load/delete via PresetController
 * Buttons are active-low with INPUT_PULLUP, detect falling edge (HIGH→LOW)
 */
static void processPresetButtons() {
    // Debug: periodic state dump (every 2 seconds)
    // static uint32_t lastDebugTime = 0;
    // uint32_t now = millis();
    // if (now - lastDebugTime >= 2000) {
    //     lastDebugTime = now;
    //     Serial.print("Preset GPIO: ");
    //     for (uint8_t i = 0; i < 4; i++) {
    //         Serial.print(digitalRead(PRESET_PINS[i]) ? "1" : "0");
    //     }
    //     Serial.println();
    // }

    for (uint8_t i = 0; i < 4; i++) {
        bool currentState = digitalRead(PRESET_PINS[i]);  // HIGH = released, LOW = pressed

        // Detect falling edge (HIGH → LOW) = button press
        if (s_presetLastState[i] && !currentState) {
            // Serial.print("Preset button ");
            // Serial.print(i + 1);
            // Serial.println(" pressed");

            if (s_presetController && s_presetController->isEnabled()) {
                s_presetController->handleButtonPress(i + 1);  // Convert to 1-indexed slot
            }
        }

        s_presetLastState[i] = currentState;
    }
}

/**
 * Update encoder handlers
 * Hardware event processing now handled by dedicated MCP thread (Mcp23017Input::threadLoop)
 * This function just updates the encoder handler logic (callbacks, display, etc.)
 */
static void updateEncoders() {
    // Note: Mcp23017Input::update() is no longer needed here - dedicated thread handles it
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
    while (MidiInput::popEvent(event)) {
        switch (event) {
            case MidiEvent::START: {
                s_lastTickMicros = 0;
                s_transportActive = true;
                Timebase::reset();
                Timebase::setTransportState(Timebase::TransportState::PLAYING);

                // Turn on LED for beat 0
                digitalWrite(LED_PIN, HIGH);
                uint32_t spb = Timebase::getSamplesPerBeat();
                uint32_t pulseSamples = (spb * 2) / 24;  // 2 ticks
                s_ledOffSample = Timebase::getSamplePosition() + pulseSamples;
                TRACE(TRACE_BEAT_LED_ON);
                TRACE(TRACE_MIDI_START);
                Serial.println("▶ START");
                break;
            }

            case MidiEvent::STOP:
                s_transportActive = false;
                Timebase::setTransportState(Timebase::TransportState::STOPPED);
                digitalWrite(LED_PIN, LOW);
                s_ledOffSample = 0;
                TRACE(TRACE_MIDI_STOP);
                Serial.println("■ STOP");
                break;

            case MidiEvent::CONTINUE:
                s_transportActive = true;
                Timebase::setTransportState(Timebase::TransportState::PLAYING);
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
    while (MidiInput::popClock(clockMicros)) {
        if (!s_transportActive) continue;

        // Update tick period estimate (EMA)
        if (s_lastTickMicros > 0) {
            uint32_t tickPeriod = clockMicros - s_lastTickMicros;
            if (tickPeriod >= 10000 && tickPeriod <= 50000) {
                s_avgTickPeriodUs = (s_avgTickPeriodUs * 9 + tickPeriod) / 10;
                Timebase::syncToMIDIClock(s_avgTickPeriodUs);
                TRACE(TRACE_TICK_PERIOD_UPDATE, s_avgTickPeriodUs / 10);
            }
        }
        s_lastTickMicros = clockMicros;
        Timebase::incrementTick();
    }
}

/**
 * Update beat indicator LED
 * Turns LED on at beat boundaries, off after short pulse
 */
static void updateBeatLed() {
    uint64_t currentSample = Timebase::getSamplePosition();

    if (Timebase::pollBeatFlag()) {
        digitalWrite(LED_PIN, HIGH);
        uint32_t spb = Timebase::getSamplesPerBeat();
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

void App::begin() {
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

    // Configure preset button GPIO pins (active-low, internal pullup)
    for (uint8_t i = 0; i < 4; i++) {
        pinMode(PRESET_PINS[i], INPUT_PULLUP);
    }

    // Debug: Print initial state of preset buttons
    // Serial.println("Preset GPIO pins configured:");
    // for (uint8_t i = 0; i < 4; i++) {
    //     bool state = digitalRead(PRESET_PINS[i]);
    //     Serial.print("  Preset ");
    //     Serial.print(i + 1);
    //     Serial.print(" (pin ");
    //     Serial.print(PRESET_PINS[i]);
    //     Serial.print("): ");
    //     Serial.println(state ? "HIGH (released)" : "LOW (pressed)");
    // }

    // Initialize subsystems
    EffectQuantization::initialize();
    DisplayManager::instance().initialize();

    // Create effect controllers
    s_chokeController = new ChokeController(choke);
    s_freezeController = new FreezeController(freeze);
    s_stutterController = new StutterController(stutter);
    s_globalController = new GlobalController();
    s_presetController = new PresetController(stutter);

    // Initialize preset system (SD card)
    s_presetController->begin();

    // Set up capture complete callback to notify PresetController
    s_stutterController->setCaptureCompleteCallback([]() {
        if (s_presetController) {
            s_presetController->onCaptureComplete();
        }
    });

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

void App::threadLoop() {
    for (;;) {
        // Main application loop - organized into logical sections
        // Each section is implemented as a separate function for clarity

        // 1. Process button presses and effect commands
        processInputCommands();

        // 2. Process preset button inputs
        processPresetButtons();

        // 3. Update encoder menu handlers (parameter editing)
        updateEncoders();

        // 4. Update effect handler visual feedback
        updateEffectHandlers();

        // 5. Process MIDI transport events (START/STOP/CONTINUE)
        processTransportEvents();

        // 6. Process MIDI clock ticks (tempo tracking)
        processClockTicks();

        // 7. Update beat indicator LED
        updateBeatLed();

        // 8. Update preset LEDs (beat-synced for selected preset)
        if (s_presetController) {
            s_presetController->updateLEDs();
        }

        // 9. Periodic debug output (optional)
        uint32_t now = millis();
        if (now - s_lastPrint >= PRINT_INTERVAL_MS) {
            s_lastPrint = now;
            // Optional: Print status here
        }

        // 10. Yield CPU to other threads
        threads.delay(2);
    }
}

// ========== GLOBAL QUANTIZATION API (delegated) ==========
Quantization App::getGlobalQuantization() {
    return EffectQuantization::getGlobalQuantization();
}

void App::setGlobalQuantization(Quantization quant) {
    EffectQuantization::setGlobalQuantization(quant);
}