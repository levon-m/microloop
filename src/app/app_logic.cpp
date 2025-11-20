#include "app_logic.h"
#include "midi_io.h"
#include "neokey_io.h"
#include "mcp_io.h"
#include "audio_choke.h"
#include "audio_freeze.h"
#include "audio_stutter.h"
#include "effect_manager.h"
#include "trace.h"
#include "timekeeper.h"
#include "effect_quantization.h"
#include "encoder_handler.h"
#include "display_manager.h"
#include "choke_controller.h"
#include "freeze_controller.h"
#include "stutter_controller.h"
#include "app_state.h"

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
static EncoderHandler::Handler* s_encoder4 = nullptr;  // Global quantization

// ========== ENCODER SETUP FUNCTIONS ==========
// These functions configure the behavior of each encoder handler

static void setupEncoder1() {
    s_encoder1 = new EncoderHandler::Handler(0);  // Encoder 1 is index 0 (STUTTER parameters)

    // Button press: Cycle between ONSET → LENGTH → CAPTURE_START → CAPTURE_END
    s_encoder1->onButtonPress([]() {
        StutterController::Parameter current = s_stutterController->getCurrentParameter();

        // Cycle to next parameter
        if (current == StutterController::Parameter::ONSET) {
            s_stutterController->setCurrentParameter(StutterController::Parameter::LENGTH);
            Serial.println("Stutter Parameter: LENGTH");
        } else if (current == StutterController::Parameter::LENGTH) {
            s_stutterController->setCurrentParameter(StutterController::Parameter::CAPTURE_START);
            Serial.println("Stutter Parameter: CAPTURE_START");
        } else if (current == StutterController::Parameter::CAPTURE_START) {
            s_stutterController->setCurrentParameter(StutterController::Parameter::CAPTURE_END);
            Serial.println("Stutter Parameter: CAPTURE_END");
        } else {  // CAPTURE_END
            s_stutterController->setCurrentParameter(StutterController::Parameter::ONSET);
            Serial.println("Stutter Parameter: ONSET");
        }
        // Display update handled by onDisplayUpdate callback
    });

    // Value change: Adjust current parameter
    s_encoder1->onValueChange([](int8_t delta) {
        StutterController::Parameter param = s_stutterController->getCurrentParameter();

        if (param == StutterController::Parameter::ONSET) {
            int8_t currentIndex = static_cast<int8_t>(stutter.getOnsetMode());
            int8_t newIndex = currentIndex + delta;
            if (newIndex < 0) newIndex = 0;
            if (newIndex > 1) newIndex = 1;
            if (newIndex != currentIndex) {
                StutterOnset newOnset = static_cast<StutterOnset>(newIndex);
                stutter.setOnsetMode(newOnset);
                Serial.print("Stutter Onset: ");
                Serial.println(StutterController::onsetName(newOnset));

                // Update menu display immediately
                MenuDisplayData menuData("STUTTER->Onset", StutterController::onsetName(newOnset), 2, newIndex);
                DisplayManager::instance().showMenu(menuData);
                DisplayManager::instance().updateDisplay();
            }
        } else if (param == StutterController::Parameter::LENGTH) {
            int8_t currentIndex = static_cast<int8_t>(stutter.getLengthMode());
            int8_t newIndex = currentIndex + delta;
            if (newIndex < 0) newIndex = 0;
            if (newIndex > 1) newIndex = 1;
            if (newIndex != currentIndex) {
                StutterLength newLength = static_cast<StutterLength>(newIndex);
                stutter.setLengthMode(newLength);
                Serial.print("Stutter Length: ");
                Serial.println(StutterController::lengthName(newLength));

                // Update menu display immediately
                MenuDisplayData menuData("STUTTER->Length", StutterController::lengthName(newLength), 2, newIndex);
                DisplayManager::instance().showMenu(menuData);
                DisplayManager::instance().updateDisplay();
            }
        } else if (param == StutterController::Parameter::CAPTURE_START) {
            int8_t currentIndex = static_cast<int8_t>(stutter.getCaptureStartMode());
            int8_t newIndex = currentIndex + delta;
            if (newIndex < 0) newIndex = 0;
            if (newIndex > 1) newIndex = 1;
            if (newIndex != currentIndex) {
                StutterCaptureStart newCaptureStart = static_cast<StutterCaptureStart>(newIndex);
                stutter.setCaptureStartMode(newCaptureStart);
                Serial.print("Stutter Capture Start: ");
                Serial.println(StutterController::captureStartName(newCaptureStart));

                // Update menu display immediately
                MenuDisplayData menuData("STUTTER->Cap. Start", StutterController::captureStartName(newCaptureStart), 2, newIndex);
                DisplayManager::instance().showMenu(menuData);
                DisplayManager::instance().updateDisplay();
            }
        } else {  // CAPTURE_END
            int8_t currentIndex = static_cast<int8_t>(stutter.getCaptureEndMode());
            int8_t newIndex = currentIndex + delta;
            if (newIndex < 0) newIndex = 0;
            if (newIndex > 1) newIndex = 1;
            if (newIndex != currentIndex) {
                StutterCaptureEnd newCaptureEnd = static_cast<StutterCaptureEnd>(newIndex);
                stutter.setCaptureEndMode(newCaptureEnd);
                Serial.print("Stutter Capture End: ");
                Serial.println(StutterController::captureEndName(newCaptureEnd));

                // Update menu display immediately
                MenuDisplayData menuData("STUTTER->Cap. End", StutterController::captureEndName(newCaptureEnd), 2, newIndex);
                DisplayManager::instance().showMenu(menuData);
                DisplayManager::instance().updateDisplay();
            }
        }
    });

    // Display update: Show current parameter or return to effect display
    s_encoder1->onDisplayUpdate([](bool isTouched) {
        if (isTouched) {
            StutterController::Parameter param = s_stutterController->getCurrentParameter();

            // Build menu data
            MenuDisplayData menuData;
            if (param == StutterController::Parameter::ONSET) {
                menuData.topText = "STUTTER->Onset";
                menuData.middleText = StutterController::onsetName(stutter.getOnsetMode());
                menuData.numOptions = 2;
                menuData.selectedIndex = static_cast<uint8_t>(stutter.getOnsetMode());
            } else if (param == StutterController::Parameter::LENGTH) {
                menuData.topText = "STUTTER->Length";
                menuData.middleText = StutterController::lengthName(stutter.getLengthMode());
                menuData.numOptions = 2;
                menuData.selectedIndex = static_cast<uint8_t>(stutter.getLengthMode());
            } else if (param == StutterController::Parameter::CAPTURE_START) {
                menuData.topText = "STUTTER->Cap. Start";
                menuData.middleText = StutterController::captureStartName(stutter.getCaptureStartMode());
                menuData.numOptions = 2;
                menuData.selectedIndex = static_cast<uint8_t>(stutter.getCaptureStartMode());
            } else {  // CAPTURE_END
                menuData.topText = "STUTTER->Cap. End";
                menuData.middleText = StutterController::captureEndName(stutter.getCaptureEndMode());
                menuData.numOptions = 2;
                menuData.selectedIndex = static_cast<uint8_t>(stutter.getCaptureEndMode());
            }

            // Show menu via DisplayManager (takes priority over effects)
            DisplayManager::instance().showMenu(menuData);
            DisplayManager::instance().updateDisplay();
        } else {
            // Cooldown expired - only hide menu if NO other encoders are touched
            if (!s_encoder2->isTouched() && !s_encoder3->isTouched() && !s_encoder4->isTouched()) {
                DisplayManager::instance().hideMenu();
                DisplayManager::instance().updateDisplay();
            }
        }
    });
}

static void setupEncoder2() {
    s_encoder2 = new EncoderHandler::Handler(1);  // Encoder 2 is index 1 (FREEZE parameters)

    // Button press: Cycle between LENGTH and ONSET parameters
    s_encoder2->onButtonPress([]() {
        FreezeController::Parameter current = s_freezeController->getCurrentParameter();
        if (current == FreezeController::Parameter::LENGTH) {
            s_freezeController->setCurrentParameter(FreezeController::Parameter::ONSET);
            Serial.println("Freeze Parameter: ONSET");
        } else {
            s_freezeController->setCurrentParameter(FreezeController::Parameter::LENGTH);
            Serial.println("Freeze Parameter: LENGTH");
        }
        // Display update handled by onDisplayUpdate callback
    });

    // Value change: Adjust current parameter
    s_encoder2->onValueChange([](int8_t delta) {
        FreezeController::Parameter param = s_freezeController->getCurrentParameter();

        if (param == FreezeController::Parameter::LENGTH) {
            int8_t currentIndex = static_cast<int8_t>(freeze.getLengthMode());
            int8_t newIndex = currentIndex + delta;
            if (newIndex < 0) newIndex = 0;
            if (newIndex > 1) newIndex = 1;
            if (newIndex != currentIndex) {
                FreezeLength newLength = static_cast<FreezeLength>(newIndex);
                freeze.setLengthMode(newLength);
                Serial.print("Freeze Length: ");
                Serial.println(FreezeController::lengthName(newLength));

                // Update menu display immediately
                MenuDisplayData menuData("FREEZE->Length", FreezeController::lengthName(newLength), 2, newIndex);
                DisplayManager::instance().showMenu(menuData);
                DisplayManager::instance().updateDisplay();
            }
        } else {  // ONSET parameter
            int8_t currentIndex = static_cast<int8_t>(freeze.getOnsetMode());
            int8_t newIndex = currentIndex + delta;
            if (newIndex < 0) newIndex = 0;
            if (newIndex > 1) newIndex = 1;
            if (newIndex != currentIndex) {
                FreezeOnset newOnset = static_cast<FreezeOnset>(newIndex);
                freeze.setOnsetMode(newOnset);
                Serial.print("Freeze Onset: ");
                Serial.println(FreezeController::onsetName(newOnset));

                // Update menu display immediately
                MenuDisplayData menuData("FREEZE->Onset", FreezeController::onsetName(newOnset), 2, newIndex);
                DisplayManager::instance().showMenu(menuData);
                DisplayManager::instance().updateDisplay();
            }
        }
    });

    // Display update: Show current parameter or return to effect display
    s_encoder2->onDisplayUpdate([](bool isTouched) {
        if (isTouched) {
            FreezeController::Parameter param = s_freezeController->getCurrentParameter();

            // Build menu data
            MenuDisplayData menuData;
            if (param == FreezeController::Parameter::LENGTH) {
                menuData.topText = "FREEZE->Length";
                menuData.middleText = FreezeController::lengthName(freeze.getLengthMode());
                menuData.numOptions = 2;
                menuData.selectedIndex = static_cast<uint8_t>(freeze.getLengthMode());
            } else {  // ONSET
                menuData.topText = "FREEZE->Onset";
                menuData.middleText = FreezeController::onsetName(freeze.getOnsetMode());
                menuData.numOptions = 2;
                menuData.selectedIndex = static_cast<uint8_t>(freeze.getOnsetMode());
            }

            // Show menu via DisplayManager (takes priority over effects)
            DisplayManager::instance().showMenu(menuData);
            DisplayManager::instance().updateDisplay();
        } else {
            // Cooldown expired - only hide menu if NO other encoders are touched
            if (!s_encoder1->isTouched() && !s_encoder3->isTouched() && !s_encoder4->isTouched()) {
                DisplayManager::instance().hideMenu();
                DisplayManager::instance().updateDisplay();
            }
        }
    });
}

static void setupEncoder3() {
    s_encoder3 = new EncoderHandler::Handler(2);  // Encoder 3 is index 2 (CHOKE parameters)

    // Button press: Cycle between LENGTH and ONSET parameters
    s_encoder3->onButtonPress([]() {
        ChokeController::Parameter current = s_chokeController->getCurrentParameter();
        if (current == ChokeController::Parameter::LENGTH) {
            s_chokeController->setCurrentParameter(ChokeController::Parameter::ONSET);
            Serial.println("Choke Parameter: ONSET");
        } else {
            s_chokeController->setCurrentParameter(ChokeController::Parameter::LENGTH);
            Serial.println("Choke Parameter: LENGTH");
        }
        // Display update handled by onDisplayUpdate callback
    });

    // Value change: Adjust current parameter
    s_encoder3->onValueChange([](int8_t delta) {
        ChokeController::Parameter param = s_chokeController->getCurrentParameter();

        if (param == ChokeController::Parameter::LENGTH) {
            // Update LENGTH parameter
            int8_t currentIndex = static_cast<int8_t>(choke.getLengthMode());
            int8_t newIndex = currentIndex + delta;

            // Clamp to valid range (0-1)
            if (newIndex < 0) newIndex = 0;
            if (newIndex > 1) newIndex = 1;

            if (newIndex != currentIndex) {
                ChokeLength newLength = static_cast<ChokeLength>(newIndex);
                choke.setLengthMode(newLength);
                Serial.print("Choke Length: ");
                Serial.println(ChokeController::lengthName(newLength));

                // Update menu display immediately
                MenuDisplayData menuData("CHOKE->Length", ChokeController::lengthName(newLength), 2, newIndex);
                DisplayManager::instance().showMenu(menuData);
                DisplayManager::instance().updateDisplay();
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
                Serial.print("Choke Onset: ");
                Serial.println(ChokeController::onsetName(newOnset));

                // Update menu display immediately
                MenuDisplayData menuData("CHOKE->Onset", ChokeController::onsetName(newOnset), 2, newIndex);
                DisplayManager::instance().showMenu(menuData);
                DisplayManager::instance().updateDisplay();
            }
        }
    });

    // Display update: Show current parameter or return to effect display
    s_encoder3->onDisplayUpdate([](bool isTouched) {
        if (isTouched) {
            // Show current parameter
            ChokeController::Parameter param = s_chokeController->getCurrentParameter();

            // Build menu data
            MenuDisplayData menuData;
            if (param == ChokeController::Parameter::LENGTH) {
                menuData.topText = "CHOKE->Length";
                menuData.middleText = ChokeController::lengthName(choke.getLengthMode());
                menuData.numOptions = 2;
                menuData.selectedIndex = static_cast<uint8_t>(choke.getLengthMode());
            } else {  // ONSET
                menuData.topText = "CHOKE->Onset";
                menuData.middleText = ChokeController::onsetName(choke.getOnsetMode());
                menuData.numOptions = 2;
                menuData.selectedIndex = static_cast<uint8_t>(choke.getOnsetMode());
            }

            // Show menu via DisplayManager (takes priority over effects)
            DisplayManager::instance().showMenu(menuData);
            DisplayManager::instance().updateDisplay();
        } else {
            // Cooldown expired - only hide menu if NO other encoders are touched
            if (!s_encoder1->isTouched() && !s_encoder2->isTouched() && !s_encoder4->isTouched()) {
                DisplayManager::instance().hideMenu();
                DisplayManager::instance().updateDisplay();
            }
        }
    });
}

static void setupEncoder4() {
    s_encoder4 = new EncoderHandler::Handler(3);  // Encoder 4 is index 3

    // Value change: Adjust global quantization
    s_encoder4->onValueChange([](int8_t delta) {
        int8_t currentIndex = static_cast<int8_t>(EffectQuantization::getGlobalQuantization());
        int8_t newIndex = currentIndex + delta;

        // Clamp to valid range (0-3)
        if (newIndex < 0) newIndex = 0;
        if (newIndex > 3) newIndex = 3;

        if (newIndex != currentIndex) {
            Quantization newQuant = static_cast<Quantization>(newIndex);
            EffectQuantization::setGlobalQuantization(newQuant);
            Serial.print("Global Quantization: ");
            Serial.println(EffectQuantization::quantizationName(newQuant));

            // Update menu display immediately
            MenuDisplayData menuData("Global Quantization", EffectQuantization::quantizationName(newQuant), 4, newIndex);
            DisplayManager::instance().showMenu(menuData);
            DisplayManager::instance().updateDisplay();
        }
    });

    // Display update: Show quantization or return to effect display
    s_encoder4->onDisplayUpdate([](bool isTouched) {
        if (isTouched) {
            // Show current quantization
            Quantization quant = EffectQuantization::getGlobalQuantization();

            // Build menu data
            MenuDisplayData menuData;
            menuData.topText = "Global Quantization";
            menuData.middleText = EffectQuantization::quantizationName(quant);
            menuData.numOptions = 4;  // QUANT_32, QUANT_16, QUANT_8, QUANT_4
            menuData.selectedIndex = static_cast<uint8_t>(quant);

            // Show menu via DisplayManager (takes priority over effects)
            DisplayManager::instance().showMenu(menuData);
            DisplayManager::instance().updateDisplay();
        } else {
            // Cooldown expired - only hide menu if NO other encoders are touched
            if (!s_encoder1->isTouched() && !s_encoder2->isTouched() && !s_encoder3->isTouched()) {
                DisplayManager::instance().hideMenu();
                DisplayManager::instance().updateDisplay();
            }
        }
    });
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
    s_encoder4->update();   // Global quantization
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

    // Setup encoders
    setupEncoder1();  // STUTTER parameters
    setupEncoder2();  // FREEZE parameters
    setupEncoder3();  // CHOKE parameters
    setupEncoder4();  // Global quantization

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