#include "app_logic.h"
#include "midi_io.h"
#include "input_io.h"
#include "encoder_io.h"
#include "audio_choke.h"
#include "audio_freeze.h"
#include "effect_manager.h"
#include "trace.h"
#include "timekeeper.h"

// Modular subsystems
#include "effect_quantization.h"
#include "encoder_menu.h"
#include "display_manager.h"
#include "choke_handler.h"
#include "freeze_handler.h"

#include <TeensyThreads.h>

/**
 * Application Logic Implementation (REFACTORED)
 *
 * This file has been modularized to eliminate code duplication:
 * - EffectQuantization: Shared quantization logic
 * - EncoderMenu: Generic encoder handling
 * - DisplayManager: Display priority logic
 * - ChokeHandler: CHOKE-specific quantization
 * - FreezeHandler: FREEZE-specific quantization
 *
 * BEFORE: 1329 lines with massive duplication
 * AFTER:  ~350 lines of clean, focused code
 */

// External references to audio effects (defined in main.cpp)
extern AudioEffectChoke choke;
extern AudioEffectFreeze freeze;

// ========== LED BEAT INDICATOR ==========
static constexpr uint8_t LED_PIN = 37;
static uint64_t ledOffSample = 0;  // Sample position when LED should turn off (0 = LED off)

// ========== TRANSPORT STATE ==========
static bool transportActive = false;  // Is sequencer running?

// ========== MIDI CLOCK TIMING ==========
static uint32_t lastTickMicros = 0;
static uint32_t avgTickPeriodUs = 20833;  // ~20.8ms @ 120BPM

// ========== DEBUG OUTPUT ==========
static uint32_t lastPrint = 0;
static constexpr uint32_t PRINT_INTERVAL_MS = 1000;

// ========== ENCODER MENU INSTANCES ==========
static EncoderMenu::Handler* encoder1 = nullptr;  // FREEZE parameters
static EncoderMenu::Handler* encoder3 = nullptr;  // CHOKE parameters
static EncoderMenu::Handler* encoder4 = nullptr;  // Global quantization

// ========== ENCODER 1 (FREEZE PARAMETERS) ==========

static void setupEncoder1() {
    encoder1 = new EncoderMenu::Handler(0);  // Encoder 1 is index 0

    // Button press: Cycle between LENGTH and ONSET parameters
    encoder1->onButtonPress([]() {
        FreezeHandler::Parameter current = FreezeHandler::getCurrentParameter();
        if (current == FreezeHandler::Parameter::LENGTH) {
            FreezeHandler::setCurrentParameter(FreezeHandler::Parameter::ONSET);
            Serial.println("Freeze Parameter: ONSET");
            DisplayIO::showBitmap(FreezeHandler::onsetToBitmap(freeze.getOnsetMode()));
        } else {
            FreezeHandler::setCurrentParameter(FreezeHandler::Parameter::LENGTH);
            Serial.println("Freeze Parameter: LENGTH");
            DisplayIO::showBitmap(FreezeHandler::lengthToBitmap(freeze.getLengthMode()));
        }
    });

    // Value change: Adjust current parameter
    encoder1->onValueChange([](int8_t delta) {
        FreezeHandler::Parameter param = FreezeHandler::getCurrentParameter();

        if (param == FreezeHandler::Parameter::LENGTH) {
            // Update LENGTH parameter
            int8_t currentIndex = static_cast<int8_t>(freeze.getLengthMode());
            int8_t newIndex = currentIndex + delta;

            // Clamp to valid range (0-1)
            if (newIndex < 0) newIndex = 0;
            if (newIndex > 1) newIndex = 1;

            if (newIndex != currentIndex) {
                FreezeLength newLength = static_cast<FreezeLength>(newIndex);
                freeze.setLengthMode(newLength);
                DisplayIO::showBitmap(FreezeHandler::lengthToBitmap(newLength));
                Serial.print("Freeze Length: ");
                Serial.println(FreezeHandler::lengthName(newLength));
            }
        } else {  // ONSET parameter
            // Update ONSET parameter
            int8_t currentIndex = static_cast<int8_t>(freeze.getOnsetMode());
            int8_t newIndex = currentIndex + delta;

            // Clamp to valid range (0-1)
            if (newIndex < 0) newIndex = 0;
            if (newIndex > 1) newIndex = 1;

            if (newIndex != currentIndex) {
                FreezeOnset newOnset = static_cast<FreezeOnset>(newIndex);
                freeze.setOnsetMode(newOnset);
                DisplayIO::showBitmap(FreezeHandler::onsetToBitmap(newOnset));
                Serial.print("Freeze Onset: ");
                Serial.println(FreezeHandler::onsetName(newOnset));
            }
        }
    });

    // Display update: Show current parameter or return to effect display
    encoder1->onDisplayUpdate([](bool isTouched) {
        if (isTouched) {
            // Show current parameter
            FreezeHandler::Parameter param = FreezeHandler::getCurrentParameter();
            if (param == FreezeHandler::Parameter::LENGTH) {
                DisplayIO::showBitmap(FreezeHandler::lengthToBitmap(freeze.getLengthMode()));
            } else {
                DisplayIO::showBitmap(FreezeHandler::onsetToBitmap(freeze.getOnsetMode()));
            }
        } else {
            // Cooldown expired - return to effect display
            DisplayManager::updateDisplay();
        }
    });
}

// ========== ENCODER 3 (CHOKE PARAMETERS) ==========

static void setupEncoder3() {
    encoder3 = new EncoderMenu::Handler(2);  // Encoder 3 is index 2

    // Button press: Cycle between LENGTH and ONSET parameters
    encoder3->onButtonPress([]() {
        ChokeHandler::Parameter current = ChokeHandler::getCurrentParameter();
        if (current == ChokeHandler::Parameter::LENGTH) {
            ChokeHandler::setCurrentParameter(ChokeHandler::Parameter::ONSET);
            Serial.println("Choke Parameter: ONSET");
            DisplayIO::showBitmap(ChokeHandler::onsetToBitmap(choke.getOnsetMode()));
        } else {
            ChokeHandler::setCurrentParameter(ChokeHandler::Parameter::LENGTH);
            Serial.println("Choke Parameter: LENGTH");
            DisplayIO::showBitmap(ChokeHandler::lengthToBitmap(choke.getLengthMode()));
        }
    });

    // Value change: Adjust current parameter
    encoder3->onValueChange([](int8_t delta) {
        ChokeHandler::Parameter param = ChokeHandler::getCurrentParameter();

        if (param == ChokeHandler::Parameter::LENGTH) {
            // Update LENGTH parameter
            int8_t currentIndex = static_cast<int8_t>(choke.getLengthMode());
            int8_t newIndex = currentIndex + delta;

            // Clamp to valid range (0-1)
            if (newIndex < 0) newIndex = 0;
            if (newIndex > 1) newIndex = 1;

            if (newIndex != currentIndex) {
                ChokeLength newLength = static_cast<ChokeLength>(newIndex);
                choke.setLengthMode(newLength);
                DisplayIO::showBitmap(ChokeHandler::lengthToBitmap(newLength));
                Serial.print("Choke Length: ");
                Serial.println(ChokeHandler::lengthName(newLength));
            }
        } else {  // ONSET parameter
            // Update ONSET parameter
            int8_t currentIndex = static_cast<int8_t>(choke.getOnsetMode());
            int8_t newIndex = currentIndex + delta;

            // Clamp to valid range (0-1)
            if (newIndex < 0) newIndex = 0;
            if (newIndex > 1) newIndex = 1;

            if (newIndex != currentIndex) {
                ChokeOnset newOnset = static_cast<ChokeOnset>(newIndex);
                choke.setOnsetMode(newOnset);
                DisplayIO::showBitmap(ChokeHandler::onsetToBitmap(newOnset));
                Serial.print("Choke Onset: ");
                Serial.println(ChokeHandler::onsetName(newOnset));
            }
        }
    });

    // Display update: Show current parameter or return to effect display
    encoder3->onDisplayUpdate([](bool isTouched) {
        if (isTouched) {
            // Show current parameter
            ChokeHandler::Parameter param = ChokeHandler::getCurrentParameter();
            if (param == ChokeHandler::Parameter::LENGTH) {
                DisplayIO::showBitmap(ChokeHandler::lengthToBitmap(choke.getLengthMode()));
            } else {
                DisplayIO::showBitmap(ChokeHandler::onsetToBitmap(choke.getOnsetMode()));
            }
        } else {
            // Cooldown expired - return to effect display
            DisplayManager::updateDisplay();
        }
    });
}

// ========== ENCODER 4 (GLOBAL QUANTIZATION) ==========

static void setupEncoder4() {
    encoder4 = new EncoderMenu::Handler(3);  // Encoder 4 is index 3

    // Value change: Adjust global quantization
    encoder4->onValueChange([](int8_t delta) {
        int8_t currentIndex = static_cast<int8_t>(EffectQuantization::getGlobalQuantization());
        int8_t newIndex = currentIndex + delta;

        // Clamp to valid range (0-3)
        if (newIndex < 0) newIndex = 0;
        if (newIndex > 3) newIndex = 3;

        if (newIndex != currentIndex) {
            Quantization newQuant = static_cast<Quantization>(newIndex);
            EffectQuantization::setGlobalQuantization(newQuant);
            DisplayIO::showBitmap(EffectQuantization::quantizationToBitmap(newQuant));
            Serial.print("Global Quantization: ");
            Serial.println(EffectQuantization::quantizationName(newQuant));
        }
    });

    // Display update: Show quantization or return to effect display
    encoder4->onDisplayUpdate([](bool isTouched) {
        if (isTouched) {
            // Show current quantization
            Quantization quant = EffectQuantization::getGlobalQuantization();
            DisplayIO::showBitmap(EffectQuantization::quantizationToBitmap(quant));
        } else {
            // Cooldown expired - return to effect display
            DisplayManager::updateDisplay();
        }
    });
}

// ========== APP LOGIC INITIALIZATION ==========

void AppLogic::begin() {
    // Configure LED pin
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Initialize subsystems
    EffectQuantization::initialize();
    DisplayManager::initialize();
    ChokeHandler::initialize(choke);
    FreezeHandler::initialize(freeze);

    // Setup encoders
    setupEncoder1();  // FREEZE parameters
    setupEncoder3();  // CHOKE parameters
    setupEncoder4();  // Global quantization

    // Initialize state
    transportActive = false;
}

// ========== APP LOGIC MAIN LOOP ==========

void AppLogic::threadLoop() {
    for (;;) {
        // ========== 1. PROCESS INPUT COMMANDS ==========
        Command cmd;
        while (InputIO::popCommand(cmd)) {
            // Check if CHOKE/FREEZE handlers want to intercept
            bool handled = false;

            if (cmd.targetEffect == EffectID::CHOKE) {
                if (cmd.type == CommandType::EFFECT_ENABLE || cmd.type == CommandType::EFFECT_TOGGLE) {
                    handled = ChokeHandler::handleButtonPress(cmd);
                } else if (cmd.type == CommandType::EFFECT_DISABLE) {
                    handled = ChokeHandler::handleButtonRelease(cmd);
                }
            } else if (cmd.targetEffect == EffectID::FREEZE) {
                if (cmd.type == CommandType::EFFECT_ENABLE || cmd.type == CommandType::EFFECT_TOGGLE) {
                    handled = FreezeHandler::handleButtonPress(cmd);
                } else if (cmd.type == CommandType::EFFECT_DISABLE) {
                    handled = FreezeHandler::handleButtonRelease(cmd);
                }
            }

            // If handler didn't intercept, execute via EffectManager
            if (!handled && EffectManager::executeCommand(cmd)) {
                // Update visual feedback
                AudioEffectBase* effect = EffectManager::getEffect(cmd.targetEffect);
                if (effect) {
                    bool enabled = effect->isEnabled();
                    InputIO::setLED(cmd.targetEffect, enabled);

                    if (enabled) {
                        DisplayManager::setLastActivatedEffect(cmd.targetEffect);
                    } else {
                        DisplayManager::setLastActivatedEffect(EffectID::NONE);
                    }

                    DisplayManager::updateDisplay();
                    Serial.print(effect->getName());
                    Serial.println(enabled ? " ENABLED" : " DISABLED");
                }
            }
        }

        // ========== 2. UPDATE ENCODERS ==========
        EncoderIO::update();  // Process hardware events
        encoder1->update();   // FREEZE parameters
        encoder3->update();   // CHOKE parameters
        encoder4->update();   // Global quantization

        // ========== 3. UPDATE EFFECT HANDLERS ==========
        ChokeHandler::updateVisualFeedback();
        FreezeHandler::updateVisualFeedback();

        // ========== 4. PROCESS TRANSPORT EVENTS ==========
        MidiEvent event;
        while (MidiIO::popEvent(event)) {
            switch (event) {
                case MidiEvent::START: {
                    lastTickMicros = 0;
                    transportActive = true;
                    TimeKeeper::reset();
                    TimeKeeper::setTransportState(TimeKeeper::TransportState::PLAYING);

                    // Turn on LED for beat 0
                    digitalWrite(LED_PIN, HIGH);
                    uint32_t spb = TimeKeeper::getSamplesPerBeat();
                    uint32_t pulseSamples = (spb * 2) / 24;  // 2 ticks
                    ledOffSample = TimeKeeper::getSamplePosition() + pulseSamples;
                    TRACE(TRACE_BEAT_LED_ON);
                    TRACE(TRACE_MIDI_START);
                    Serial.println("▶ START");
                    break;
                }

                case MidiEvent::STOP:
                    transportActive = false;
                    TimeKeeper::setTransportState(TimeKeeper::TransportState::STOPPED);
                    digitalWrite(LED_PIN, LOW);
                    ledOffSample = 0;
                    TRACE(TRACE_MIDI_STOP);
                    Serial.println("■ STOP");
                    break;

                case MidiEvent::CONTINUE:
                    transportActive = true;
                    TimeKeeper::setTransportState(TimeKeeper::TransportState::PLAYING);
                    TRACE(TRACE_MIDI_CONTINUE);
                    Serial.println("▶ CONTINUE");
                    break;
            }
        }

        // ========== 5. PROCESS CLOCK TICKS ==========
        uint32_t clockMicros;
        while (MidiIO::popClock(clockMicros)) {
            if (!transportActive) continue;

            // Update tick period estimate (EMA)
            if (lastTickMicros > 0) {
                uint32_t tickPeriod = clockMicros - lastTickMicros;
                if (tickPeriod >= 10000 && tickPeriod <= 50000) {
                    avgTickPeriodUs = (avgTickPeriodUs * 9 + tickPeriod) / 10;
                    TimeKeeper::syncToMIDIClock(avgTickPeriodUs);
                    TRACE(TRACE_TICK_PERIOD_UPDATE, avgTickPeriodUs / 10);
                }
            }
            lastTickMicros = clockMicros;
            TimeKeeper::incrementTick();
        }

        // ========== 6. UPDATE BEAT LED ==========
        uint64_t currentSample = TimeKeeper::getSamplePosition();

        if (TimeKeeper::pollBeatFlag()) {
            digitalWrite(LED_PIN, HIGH);
            uint32_t spb = TimeKeeper::getSamplesPerBeat();
            uint32_t pulseSamples = (spb * 2) / 24;
            ledOffSample = currentSample + pulseSamples;
            TRACE(TRACE_BEAT_LED_ON);
        }

        if (ledOffSample > 0 && currentSample >= ledOffSample) {
            digitalWrite(LED_PIN, LOW);
            ledOffSample = 0;
            TRACE(TRACE_BEAT_LED_OFF);
        }

        // ========== 7. PERIODIC DEBUG OUTPUT ==========
        uint32_t now = millis();
        if (now - lastPrint >= PRINT_INTERVAL_MS) {
            lastPrint = now;
            // Optional: Print status here
        }

        // ========== 8. YIELD CPU ==========
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
