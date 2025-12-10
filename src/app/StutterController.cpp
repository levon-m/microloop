#include "StutterController.h"
#include "NeokeyInput.h"
#include "DisplayManager.h"
#include "Timebase.h"
#include "EncoderHandler.h"
#include <Arduino.h>

// ========== RGB LED PIN DEFINITIONS ==========
static constexpr uint8_t RGB_LED_R_PIN = 28;  // Red (PWM capable)
static constexpr uint8_t RGB_LED_G_PIN = 36;  // Green (PWM capable)
static constexpr uint8_t RGB_LED_B_PIN = 37;  // Blue (PWM capable)

// ========== GAMMA LOOKUP TABLE (γ=4.0) ==========
// Pre-computed gamma curve for dramatic LED brightness ramp.
// Input: linear progress 0-255, Output: gamma-corrected brightness 0-255
// Formula: output = 255 * (input/255)^4.0
// Characteristic: stays dim until ~80%, then rapid ramp in final ~20%
static constexpr uint8_t GAMMA_LUT[256] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,
      2,   2,   3,   3,   3,   3,   3,   3,   4,   4,   4,   4,   4,   5,   5,   5,
      5,   6,   6,   6,   6,   7,   7,   7,   8,   8,   8,   9,   9,   9,  10,  10,
     11,  11,  11,  12,  12,  13,  13,  14,  14,  15,  15,  16,  17,  17,  18,  18,
     19,  20,  20,  21,  22,  22,  23,  24,  25,  25,  26,  27,  28,  29,  30,  31,
     31,  32,  33,  34,  35,  36,  38,  39,  40,  41,  42,  43,  45,  46,  47,  48,
     50,  51,  52,  54,  55,  57,  58,  60,  61,  63,  64,  66,  68,  69,  71,  73,
     75,  76,  78,  80,  82,  84,  86,  88,  90,  92,  94,  96,  98, 100, 103, 105,
    107, 109, 112, 114, 117, 119, 122, 124, 127, 129, 132, 135, 137, 140, 143, 146,
    149, 151, 154, 157, 160, 163, 166, 170, 173, 176, 179, 182, 186, 189, 192, 196,
    199, 203, 206, 210, 214, 217, 221, 225, 228, 232, 236, 240, 244, 248, 252, 255
};

/**
 * Calculate LED brightness for wait states using gamma-corrected ramp
 *
 * @param currentSample Current audio sample position
 * @param startSample Sample position when wait began
 * @param targetSample Sample position when wait will end
 * @param rampUp true = 0→max (onset waits), false = max→0 (end waits)
 * @return Gamma-corrected brightness value 0-255
 */
static uint8_t calculateWaitBrightness(uint64_t currentSample, uint64_t startSample,
                                       uint64_t targetSample, bool rampUp) {
    // Safety checks
    if (targetSample <= startSample) return rampUp ? 255 : 0;
    if (currentSample <= startSample) return rampUp ? 0 : 255;
    if (currentSample >= targetSample) return rampUp ? 255 : 0;

    // Calculate linear progress (0-255 range for LUT indexing)
    uint64_t elapsed = currentSample - startSample;
    uint64_t total = targetSample - startSample;
    uint8_t linearProgress = static_cast<uint8_t>((elapsed * 255) / total);

    // Apply gamma curve
    uint8_t gammaBrightness = GAMMA_LUT[linearProgress];

    // Invert for ramp down
    return rampUp ? gammaBrightness : (255 - gammaBrightness);
}

StutterController::StutterController(StutterAudio& effect)
    : m_effect(effect),
      m_currentParameter(Parameter::ONSET),  // Default to ONSET (first in cycle)
      m_funcHeld(false),
      m_stutterHeld(false),
      m_wasEnabled(false),
      m_captureCompleteCallback(nullptr),
      m_lastState(StutterState::IDLE_NO_LOOP),
      m_captureInProgress(false) {
}

BitmapID StutterController::stateToBitmap(StutterState state) {
    // Simplified: Use STUTTER_ACTIVE for all non-idle states
    switch (state) {
        case StutterState::IDLE_NO_LOOP:
            return BitmapID::DEFAULT;
        default:
            return BitmapID::STUTTER_ACTIVE;  // All active states use same bitmap
    }
}

const char* StutterController::onsetName(StutterOnset onset) {
    switch (onset) {
        case StutterOnset::FREE:      return "Free";
        case StutterOnset::QUANTIZED: return "Quantized";
        default: return "Free";
    }
}

const char* StutterController::lengthName(StutterLength length) {
    switch (length) {
        case StutterLength::FREE:      return "Free";
        case StutterLength::QUANTIZED: return "Quantized";
        default: return "Free";
    }
}

const char* StutterController::captureStartName(StutterCaptureStart captureStart) {
    switch (captureStart) {
        case StutterCaptureStart::FREE:      return "Free";
        case StutterCaptureStart::QUANTIZED: return "Quantized";
        default: return "Free";
    }
}

const char* StutterController::captureEndName(StutterCaptureEnd captureEnd) {
    switch (captureEnd) {
        case StutterCaptureEnd::FREE:      return "Free";
        case StutterCaptureEnd::QUANTIZED: return "Quantized";
        default: return "Free";
    }
}

// ========== BUTTON PRESS HANDLER ==========

bool StutterController::handleButtonPress(const Command& cmd) {
    // Track FUNC button presses
    if (cmd.targetEffect == EffectID::FUNC) {
        m_funcHeld = true;
        return true;  // Command handled
    }

    // Handle STUTTER button press
    if (cmd.targetEffect != EffectID::STUTTER) {
        return false;  // Not our effect
    }

    if (cmd.type != CommandType::EFFECT_ENABLE && cmd.type != CommandType::EFFECT_TOGGLE) {
        return false;  // Not a press command
    }

    m_stutterHeld = true;  // Track that STUTTER is now held
    m_effect.setStutterHeld(true);  // Update audio effect's button state

    StutterState currentState = m_effect.getState();

    // ========== FUNC+STUTTER COMBO (CAPTURE MODE) ==========
    if (m_funcHeld) {
        // Valid FUNC+STUTTER combo (FUNC pressed first)
        // Start capture or delete existing loop

        if (currentState == StutterState::IDLE_WITH_LOOP) {
            // Delete existing loop and start new capture
            Serial.println("Stutter: Deleting existing loop, starting new capture");
        }

        StutterCaptureStart captureStartMode = m_effect.getCaptureStartMode();
        StutterCaptureEnd captureEndMode = m_effect.getCaptureEndMode();
        Quantization quant = EffectQuantization::getGlobalQuantization();

        if (captureStartMode == StutterCaptureStart::FREE) {
            // FREE CAPTURE START: Start capturing immediately
            m_effect.startCapture();
            Serial.println("Stutter: CAPTURE started (Free)");
            // Capture end will be scheduled when button is released (if quantized)
        } else {
            // QUANTIZED CAPTURE START: Schedule capture start
            uint32_t samplesToStart = EffectQuantization::samplesToNextQuantizedBoundary(quant);
            uint64_t captureStartSample = Timebase::getSamplePosition() + samplesToStart;
            m_effect.scheduleCaptureStart(captureStartSample);
            Serial.print("Stutter: CAPTURE START scheduled (");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.println(")");

            // If capture end is also QUANTIZED, schedule auto-end at next boundary after start
            if (captureEndMode == StutterCaptureEnd::QUANTIZED) {
                // Calculate one full quantization period to add after capture start
                uint32_t quantPeriod = EffectQuantization::calculateQuantizedDuration(quant);
                uint32_t samplesToEnd = samplesToStart + quantPeriod;
                uint64_t captureEndSample = Timebase::getSamplePosition() + samplesToEnd;
                m_effect.scheduleCaptureEnd(captureEndSample, m_stutterHeld);  // Pass current button state
                Serial.print("Stutter: CAPTURE END also scheduled (");
                Serial.print(EffectQuantization::quantizationName(quant));
                Serial.println(")");
            }
            // If capture end is FREE, it will be scheduled when button is released
        }

        // Update visual feedback
        DisplayManager::instance().updateDisplay();
        return true;  // Command handled
    }

    // ========== STUTTER ONLY (PLAYBACK MODE) ==========
    // Check if we have a captured loop
    if (currentState == StutterState::IDLE_NO_LOOP) {
        // No loop captured - can't play
        Serial.println("Stutter: No loop captured (press FUNC+STUTTER to capture)");
        return true;  // Command handled (don't let EffectManager try to enable)
    }

    // Valid states for playback: IDLE_WITH_LOOP
    if (currentState == StutterState::IDLE_WITH_LOOP) {
        StutterOnset onsetMode = m_effect.getOnsetMode();
        Quantization quant = EffectQuantization::getGlobalQuantization();

        if (onsetMode == StutterOnset::FREE) {
            // FREE ONSET: Start playback immediately
            m_effect.startPlayback();
            Serial.println("Stutter: PLAYBACK started (Free onset)");
            // Length will be scheduled when button is released (if quantized)
        } else {
            // QUANTIZED ONSET: Schedule playback start
            uint32_t samplesToOnset = EffectQuantization::samplesToNextQuantizedBoundary(quant);
            uint64_t playbackOnsetSample = Timebase::getSamplePosition() + samplesToOnset;
            m_effect.schedulePlaybackOnset(playbackOnsetSample);
            Serial.print("Stutter: PLAYBACK ONSET scheduled (");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.println(")");
            // Length will be scheduled when button is released (if quantized)
        }

        // Update visual feedback
        DisplayManager::instance().updateDisplay();
        return true;  // Command handled
    }

    // Ignore button press in other states (already capturing/playing/waiting)
    Serial.print("Stutter: Button press ignored (state=");
    Serial.print(static_cast<int>(currentState));
    Serial.println(")");
    return true;  // Command handled
}

// ========== BUTTON RELEASE HANDLER ==========

bool StutterController::handleButtonRelease(const Command& cmd) {
    // Track FUNC button releases
    if (cmd.targetEffect == EffectID::FUNC) {
        m_funcHeld = false;

        // Check if we're currently capturing and STUTTER is still held
        StutterState currentState = m_effect.getState();
        if ((currentState == StutterState::CAPTURING || currentState == StutterState::WAIT_CAPTURE_END) && m_stutterHeld) {
            // FUNC released during capture, STUTTER still held
            // End capture and determine next state based on CaptureEnd mode
            StutterCaptureEnd captureEndMode = m_effect.getCaptureEndMode();

            if (captureEndMode == StutterCaptureEnd::FREE) {
                // FREE CAPTURE END: End immediately, transition based on STUTTER held
                m_effect.endCapture(true);  // STUTTER held = true
                Serial.println("Stutter: CAPTURE ended (Free, FUNC released, STUTTER held → PLAYING)");
            } else {
                // QUANTIZED CAPTURE END: Schedule end
                Quantization quant = EffectQuantization::getGlobalQuantization();
                uint32_t samplesToNext = EffectQuantization::samplesToNextQuantizedBoundary(quant);
                uint64_t captureEndSample = Timebase::getSamplePosition() + samplesToNext;
                m_effect.scheduleCaptureEnd(captureEndSample, true);  // STUTTER held = true
                Serial.print("Stutter: CAPTURE END scheduled (");
                Serial.print(EffectQuantization::quantizationName(quant));
                Serial.println(", FUNC released, STUTTER held)");
            }

            // Update visual feedback
            DisplayManager::instance().updateDisplay();
        }

        return true;  // Command handled
    }

    // Handle STUTTER button release
    if (cmd.targetEffect != EffectID::STUTTER) {
        return false;  // Not our effect
    }

    if (cmd.type != CommandType::EFFECT_DISABLE) {
        return false;  // Not a release command
    }

    m_stutterHeld = false;  // Track that STUTTER is no longer held
    m_effect.setStutterHeld(false);  // Update audio effect's button state

    StutterState currentState = m_effect.getState();

    // ========== CAPTURE MODE RELEASES ==========

    if (currentState == StutterState::WAIT_CAPTURE_START) {
        // STUTTER released before capture started (waiting for quantized boundary)
        // DON'T cancel - let the scheduled capture start proceed
        // The capture will start at the quantized boundary regardless of button state
        Serial.println("Stutter: CAPTURE START still scheduled (button released, will capture at grid)");
        // Don't change state - let ISR transition to CAPTURING when scheduled sample arrives
        return true;  // Command handled
    }

    if (currentState == StutterState::CAPTURING || currentState == StutterState::WAIT_CAPTURE_END) {
        // STUTTER released during capture
        // End capture and determine next state based on CaptureEnd mode
        StutterCaptureEnd captureEndMode = m_effect.getCaptureEndMode();

        if (captureEndMode == StutterCaptureEnd::FREE) {
            // FREE CAPTURE END: End immediately
            m_effect.endCapture(false);  // STUTTER not held = false
            Serial.println("Stutter: CAPTURE ended (Free, STUTTER released → IDLE_WITH_LOOP)");
        } else {
            // QUANTIZED CAPTURE END: Schedule end
            Quantization quant = EffectQuantization::getGlobalQuantization();
            uint32_t samplesToNext = EffectQuantization::samplesToNextQuantizedBoundary(quant);
            uint64_t captureEndSample = Timebase::getSamplePosition() + samplesToNext;
            m_effect.scheduleCaptureEnd(captureEndSample, false);  // STUTTER not held = false
            Serial.print("Stutter: CAPTURE END scheduled (");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.println(", STUTTER released)");
        }

        // Update visual feedback (let edge detection handle it)
        return true;  // Command handled
    }

    // ========== PLAYBACK MODE RELEASES ==========

    if (currentState == StutterState::WAIT_PLAYBACK_ONSET) {
        // STUTTER released before playback started (waiting for quantized boundary)
        // DON'T cancel - let the scheduled onset proceed
        // The playback will start at the quantized boundary regardless of button state
        Serial.println("Stutter: PLAYBACK ONSET still scheduled (button released, will play at grid)");
        // Don't change state - let ISR transition to PLAYING when scheduled sample arrives
        return true;  // Command handled
    }

    if (currentState == StutterState::PLAYING) {
        // STUTTER released during playback
        StutterLength lengthMode = m_effect.getLengthMode();

        if (lengthMode == StutterLength::FREE) {
            // FREE LENGTH: Stop immediately
            m_effect.stopPlayback();
            Serial.println("Stutter: PLAYBACK stopped (Free length)");
        } else {
            // QUANTIZED LENGTH: Schedule stop at next grid boundary
            Quantization quant = EffectQuantization::getGlobalQuantization();
            uint32_t samplesToNext = EffectQuantization::samplesToNextQuantizedBoundary(quant);
            uint64_t playbackLengthSample = Timebase::getSamplePosition() + samplesToNext;
            m_effect.schedulePlaybackLength(playbackLengthSample);
            Serial.print("Stutter: PLAYBACK STOP scheduled (");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.println(")");
        }

        // Update visual feedback (let edge detection handle it)
        return true;  // Command handled
    }

    // Ignore release in other states
    return true;  // Command handled
}

// ========== VISUAL FEEDBACK UPDATE ==========

void StutterController::updateVisualFeedback() {
    StutterState currentState = m_effect.getState();

    // ========== RGB LED STATE-SPECIFIC CONTROL ==========
    switch (currentState) {
        case StutterState::IDLE_NO_LOOP:
            // LED OFF
            analogWrite(RGB_LED_R_PIN, 0);
            analogWrite(RGB_LED_G_PIN, 0);
            analogWrite(RGB_LED_B_PIN, 0);
            NeokeyInput::setLED(EffectID::STUTTER, false);
            break;

        case StutterState::IDLE_WITH_LOOP:
            // LED SOLID WHITE
            analogWrite(RGB_LED_R_PIN, 255);
            analogWrite(RGB_LED_G_PIN, 255);
            analogWrite(RGB_LED_B_PIN, 255);
            NeokeyInput::setLED(EffectID::STUTTER, false);  // Off for now (RGB LED shows white)
            break;

        case StutterState::WAIT_CAPTURE_START: {
            // LED RAMP UP RED (0→max brightness as boundary approaches)
            uint64_t currentSample = Timebase::getSamplePosition();
            uint64_t startSample = m_effect.getWaitStartSample();
            uint64_t targetSample = m_effect.getScheduledSample();
            uint8_t brightness = calculateWaitBrightness(currentSample, startSample, targetSample, true);

            analogWrite(RGB_LED_R_PIN, brightness);
            analogWrite(RGB_LED_G_PIN, 0);
            analogWrite(RGB_LED_B_PIN, 0);

            // Neokey LED follows brightness (on when >50%)
            NeokeyInput::setLED(EffectID::STUTTER, brightness > 127);
            break;
        }

        case StutterState::CAPTURING:
            // LED SOLID RED
            analogWrite(RGB_LED_R_PIN, 255);
            analogWrite(RGB_LED_G_PIN, 0);
            analogWrite(RGB_LED_B_PIN, 0);
            NeokeyInput::setLED(EffectID::STUTTER, true);
            break;

        case StutterState::WAIT_CAPTURE_END:
            // LED SOLID RED (same as CAPTURING)
            analogWrite(RGB_LED_R_PIN, 255);
            analogWrite(RGB_LED_G_PIN, 0);
            analogWrite(RGB_LED_B_PIN, 0);
            NeokeyInput::setLED(EffectID::STUTTER, true);
            break;

        case StutterState::WAIT_PLAYBACK_ONSET: {
            // LED RAMP UP BLUE (0→max brightness as boundary approaches)
            uint64_t currentSample = Timebase::getSamplePosition();
            uint64_t startSample = m_effect.getWaitStartSample();
            uint64_t targetSample = m_effect.getScheduledSample();
            uint8_t brightness = calculateWaitBrightness(currentSample, startSample, targetSample, true);

            analogWrite(RGB_LED_R_PIN, 0);
            analogWrite(RGB_LED_G_PIN, 0);
            analogWrite(RGB_LED_B_PIN, brightness);

            // Neokey LED follows brightness (on when >50%)
            NeokeyInput::setLED(EffectID::STUTTER, brightness > 127);
            break;
        }

        case StutterState::PLAYING:
            // LED SOLID BLUE
            analogWrite(RGB_LED_R_PIN, 0);
            analogWrite(RGB_LED_G_PIN, 0);
            analogWrite(RGB_LED_B_PIN, 255);
            NeokeyInput::setLED(EffectID::STUTTER, true);
            break;

        case StutterState::WAIT_PLAYBACK_LENGTH:
            // LED SOLID BLUE (same as PLAYING)
            analogWrite(RGB_LED_R_PIN, 0);
            analogWrite(RGB_LED_G_PIN, 0);
            analogWrite(RGB_LED_B_PIN, 255);
            NeokeyInput::setLED(EffectID::STUTTER, true);
            break;

        default:
            // Fallback: LED OFF
            analogWrite(RGB_LED_R_PIN, 0);
            analogWrite(RGB_LED_G_PIN, 0);
            analogWrite(RGB_LED_B_PIN, 0);
            NeokeyInput::setLED(EffectID::STUTTER, false);
            break;
    }

    // ========== ISR STATE TRANSITION DETECTION ==========
    // Check for state changes that happened in ISR (scheduled events fired)

    if (currentState != m_lastState) {
        // State changed - log it
        Serial.print("Stutter: State changed (");
        Serial.print(static_cast<int>(m_lastState));
        Serial.print(" → ");
        Serial.print(static_cast<int>(currentState));
        Serial.println(")");

        // Track if we've entered a capture state (set flag)
        // This handles the case where capture → play → idle (flag persists through play)
        bool enteringCapture = (currentState == StutterState::CAPTURING ||
                               currentState == StutterState::WAIT_CAPTURE_END ||
                               currentState == StutterState::WAIT_CAPTURE_START);
        if (enteringCapture) {
            m_captureInProgress = true;
        }

        // Check for capture complete: transition TO IDLE_WITH_LOOP while capture was in progress
        // This handles: CAPTURING → PLAYING → IDLE_WITH_LOOP (common stutter flow)
        bool nowIdleWithLoop = (currentState == StutterState::IDLE_WITH_LOOP);

        if (m_captureInProgress && nowIdleWithLoop && m_captureCompleteCallback) {
            // New capture completed - notify PresetController
            Serial.println("StutterController: Capture complete - notifying PresetController");
            m_captureCompleteCallback();
            m_captureInProgress = false;  // Clear flag after callback
        }

        // Clear capture flag if we go back to no loop (capture was cancelled/failed)
        if (currentState == StutterState::IDLE_NO_LOOP) {
            m_captureInProgress = false;
        }

        m_lastState = currentState;
    }

    // ========== EDGE DETECTION FOR DISPLAY UPDATES ==========
    // Only update display when enabled state changes (not on every internal state transition)
    bool isEnabled = m_effect.isEnabled();

    // Detect rising edge: effect just became enabled
    if (isEnabled && !m_wasEnabled) {
        // Effect became active - update display
        DisplayManager::instance().updateDisplay();
    }

    // Detect falling edge: effect just became disabled
    if (!isEnabled && m_wasEnabled) {
        // Effect became inactive - update display
        DisplayManager::instance().updateDisplay();
    }

    // Update state for next call
    m_wasEnabled = isEnabled;
}

// ========== HELPER FUNCTIONS ==========

/**
 * Clamp index to valid range
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

// ========== ENCODER BINDING ==========

void StutterController::bindToEncoder(EncoderHandler::Handler& encoder,
                                      AnyEncoderTouchedFn anyTouchedExcept) {
    // Button press: Cycle between ONSET → LENGTH → CAPTURE_START → CAPTURE_END
    encoder.onButtonPress([this]() {
        Parameter current = m_currentParameter;

        // Cycle to next parameter
        if (current == Parameter::ONSET) {
            m_currentParameter = Parameter::LENGTH;
            Serial.println("Stutter Parameter: LENGTH");
        } else if (current == Parameter::LENGTH) {
            m_currentParameter = Parameter::CAPTURE_START;
            Serial.println("Stutter Parameter: CAPTURE_START");
        } else if (current == Parameter::CAPTURE_START) {
            m_currentParameter = Parameter::CAPTURE_END;
            Serial.println("Stutter Parameter: CAPTURE_END");
        } else {  // CAPTURE_END
            m_currentParameter = Parameter::ONSET;
            Serial.println("Stutter Parameter: ONSET");
        }
        // Display update handled by onDisplayUpdate callback
    });

    // Value change: Adjust current parameter
    encoder.onValueChange([this](int8_t delta) {
        Parameter param = m_currentParameter;

        if (param == Parameter::ONSET) {
            int8_t currentIndex = static_cast<int8_t>(m_effect.getOnsetMode());
            int8_t newIndex = clampIndex(currentIndex + delta, 0, 1);
            if (newIndex != currentIndex) {
                StutterOnset newOnset = static_cast<StutterOnset>(newIndex);
                m_effect.setOnsetMode(newOnset);
                Serial.print("Stutter Onset: ");
                Serial.println(onsetName(newOnset));

                MenuDisplayData menuData;
                menuData.topText = "STUTTER->Onset";
                menuData.middleText = onsetName(newOnset);
                menuData.numOptions = 2;
                menuData.selectedIndex = newIndex;
                DisplayManager::instance().showMenu(menuData);
            }
        } else if (param == Parameter::LENGTH) {
            int8_t currentIndex = static_cast<int8_t>(m_effect.getLengthMode());
            int8_t newIndex = clampIndex(currentIndex + delta, 0, 1);
            if (newIndex != currentIndex) {
                StutterLength newLength = static_cast<StutterLength>(newIndex);
                m_effect.setLengthMode(newLength);
                Serial.print("Stutter Length: ");
                Serial.println(lengthName(newLength));

                MenuDisplayData menuData;
                menuData.topText = "STUTTER->Length";
                menuData.middleText = lengthName(newLength);
                menuData.numOptions = 2;
                menuData.selectedIndex = newIndex;
                DisplayManager::instance().showMenu(menuData);
            }
        } else if (param == Parameter::CAPTURE_START) {
            int8_t currentIndex = static_cast<int8_t>(m_effect.getCaptureStartMode());
            int8_t newIndex = clampIndex(currentIndex + delta, 0, 1);
            if (newIndex != currentIndex) {
                StutterCaptureStart newCaptureStart = static_cast<StutterCaptureStart>(newIndex);
                m_effect.setCaptureStartMode(newCaptureStart);
                Serial.print("Stutter Capture Start: ");
                Serial.println(captureStartName(newCaptureStart));

                MenuDisplayData menuData;
                menuData.topText = "STUTTER->Cap. Start";
                menuData.middleText = captureStartName(newCaptureStart);
                menuData.numOptions = 2;
                menuData.selectedIndex = newIndex;
                DisplayManager::instance().showMenu(menuData);
            }
        } else {  // CAPTURE_END
            int8_t currentIndex = static_cast<int8_t>(m_effect.getCaptureEndMode());
            int8_t newIndex = clampIndex(currentIndex + delta, 0, 1);
            if (newIndex != currentIndex) {
                StutterCaptureEnd newCaptureEnd = static_cast<StutterCaptureEnd>(newIndex);
                m_effect.setCaptureEndMode(newCaptureEnd);
                Serial.print("Stutter Capture End: ");
                Serial.println(captureEndName(newCaptureEnd));

                MenuDisplayData menuData;
                menuData.topText = "STUTTER->Cap. End";
                menuData.middleText = captureEndName(newCaptureEnd);
                menuData.numOptions = 2;
                menuData.selectedIndex = newIndex;
                DisplayManager::instance().showMenu(menuData);
            }
        }
    });

    // Display update: Show current parameter or return to effect display
    encoder.onDisplayUpdate([this, &encoder, anyTouchedExcept](bool isTouched) {
        if (isTouched) {
            Parameter param = m_currentParameter;

            MenuDisplayData menuData;
            menuData.numOptions = 2;
            if (param == Parameter::ONSET) {
                menuData.topText = "STUTTER->Onset";
                menuData.middleText = onsetName(m_effect.getOnsetMode());
                menuData.selectedIndex = static_cast<uint8_t>(m_effect.getOnsetMode());
            } else if (param == Parameter::LENGTH) {
                menuData.topText = "STUTTER->Length";
                menuData.middleText = lengthName(m_effect.getLengthMode());
                menuData.selectedIndex = static_cast<uint8_t>(m_effect.getLengthMode());
            } else if (param == Parameter::CAPTURE_START) {
                menuData.topText = "STUTTER->Cap. Start";
                menuData.middleText = captureStartName(m_effect.getCaptureStartMode());
                menuData.selectedIndex = static_cast<uint8_t>(m_effect.getCaptureStartMode());
            } else {  // CAPTURE_END
                menuData.topText = "STUTTER->Cap. End";
                menuData.middleText = captureEndName(m_effect.getCaptureEndMode());
                menuData.selectedIndex = static_cast<uint8_t>(m_effect.getCaptureEndMode());
            }
            DisplayManager::instance().showMenu(menuData);
        } else {
            // Cooldown expired - only hide menu if NO other encoders are touched
            if (!anyTouchedExcept(&encoder)) {
                DisplayManager::instance().hideMenu();
            }
        }
    });
}
