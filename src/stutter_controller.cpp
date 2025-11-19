#include "stutter_controller.h"
#include "neokey_io.h"
#include "display_manager.h"
#include "timekeeper.h"
#include <Arduino.h>

// Define static EXTMEM buffers for AudioEffectStutter
EXTMEM int16_t AudioEffectStutter::m_stutterBufferL[AudioEffectStutter::STUTTER_BUFFER_SAMPLES];
EXTMEM int16_t AudioEffectStutter::m_stutterBufferR[AudioEffectStutter::STUTTER_BUFFER_SAMPLES];

// ========== RGB LED PIN DEFINITIONS ==========
static constexpr uint8_t RGB_LED_R_PIN = 28;  // Red (PWM capable)
static constexpr uint8_t RGB_LED_G_PIN = 36;  // Green (PWM capable)
static constexpr uint8_t RGB_LED_B_PIN = 37;  // Blue (PWM capable)

// ========== RGB LED BLINK STATE ==========
static bool s_rgbBlinkState = false;       // Current RGB LED blink state (on/off)
static uint32_t s_lastRgbBlinkTime = 0;    // Timestamp of last RGB LED blink toggle
static constexpr uint32_t RGB_BLINK_INTERVAL_MS = 100;  // 100ms on/off (10Hz rapid blink)

StutterController::StutterController(AudioEffectStutter& effect)
    : m_effect(effect),
      m_currentParameter(Parameter::ONSET),  // Default to ONSET (first in cycle)
      m_funcHeld(false),
      m_stutterHeld(false),
      m_lastBlinkTime(0),
      m_ledBlinkState(false),
      m_wasEnabled(false) {
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

        if (captureStartMode == StutterCaptureStart::FREE) {
            // FREE CAPTURE START: Start capturing immediately
            m_effect.startCapture();
            Serial.println("Stutter: CAPTURE started (Free)");
        } else {
            // QUANTIZED CAPTURE START: Schedule capture start
            Quantization quant = EffectQuantization::getGlobalQuantization();
            uint32_t samplesToNext = EffectQuantization::samplesToNextQuantizedBoundary(quant);
            uint64_t captureStartSample = TimeKeeper::getSamplePosition() + samplesToNext;
            m_effect.scheduleCaptureStart(captureStartSample);
            Serial.print("Stutter: CAPTURE START scheduled (");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.println(")");
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
            uint64_t playbackOnsetSample = TimeKeeper::getSamplePosition() + samplesToOnset;
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
                uint64_t captureEndSample = TimeKeeper::getSamplePosition() + samplesToNext;
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
            uint64_t captureEndSample = TimeKeeper::getSamplePosition() + samplesToNext;
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
            uint64_t playbackLengthSample = TimeKeeper::getSamplePosition() + samplesToNext;
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
    uint32_t now = millis();

    // ========== RGB LED STATE-SPECIFIC CONTROL ==========
    switch (currentState) {
        case StutterState::IDLE_NO_LOOP:
            // LED OFF
            analogWrite(RGB_LED_R_PIN, 0);
            analogWrite(RGB_LED_G_PIN, 0);
            analogWrite(RGB_LED_B_PIN, 0);
            NeokeyIO::setLED(EffectID::STUTTER, false);
            break;

        case StutterState::IDLE_WITH_LOOP:
            // LED SOLID WHITE
            analogWrite(RGB_LED_R_PIN, 255);
            analogWrite(RGB_LED_G_PIN, 255);
            analogWrite(RGB_LED_B_PIN, 255);
            NeokeyIO::setLED(EffectID::STUTTER, false);  // Off for now (RGB LED shows white)
            break;

        case StutterState::WAIT_CAPTURE_START:
            // LED BLINK RED VERY FAST
            if (now - s_lastRgbBlinkTime >= RGB_BLINK_INTERVAL_MS) {
                s_rgbBlinkState = !s_rgbBlinkState;
                s_lastRgbBlinkTime = now;

                // Write PWM values (red blinking)
                if (s_rgbBlinkState) {
                    analogWrite(RGB_LED_R_PIN, 255);
                    analogWrite(RGB_LED_G_PIN, 0);
                    analogWrite(RGB_LED_B_PIN, 0);
                } else {
                    analogWrite(RGB_LED_R_PIN, 0);
                    analogWrite(RGB_LED_G_PIN, 0);
                    analogWrite(RGB_LED_B_PIN, 0);
                }
            }
            // Neokey LED blinking
            if (now - m_lastBlinkTime >= BLINK_INTERVAL_MS) {
                m_ledBlinkState = !m_ledBlinkState;
                m_lastBlinkTime = now;
                NeokeyIO::setLED(EffectID::STUTTER, m_ledBlinkState);
            }
            break;

        case StutterState::CAPTURING:
        case StutterState::WAIT_CAPTURE_END:
            // LED SOLID RED
            analogWrite(RGB_LED_R_PIN, 255);
            analogWrite(RGB_LED_G_PIN, 0);
            analogWrite(RGB_LED_B_PIN, 0);
            NeokeyIO::setLED(EffectID::STUTTER, true);
            break;

        case StutterState::WAIT_PLAYBACK_ONSET:
            // LED BLINK BLUE VERY FAST
            if (now - s_lastRgbBlinkTime >= RGB_BLINK_INTERVAL_MS) {
                s_rgbBlinkState = !s_rgbBlinkState;
                s_lastRgbBlinkTime = now;

                // Write PWM values (blue blinking)
                if (s_rgbBlinkState) {
                    analogWrite(RGB_LED_R_PIN, 0);
                    analogWrite(RGB_LED_G_PIN, 0);
                    analogWrite(RGB_LED_B_PIN, 255);
                } else {
                    analogWrite(RGB_LED_R_PIN, 0);
                    analogWrite(RGB_LED_G_PIN, 0);
                    analogWrite(RGB_LED_B_PIN, 0);
                }
            }
            // Neokey LED blinking
            if (now - m_lastBlinkTime >= BLINK_INTERVAL_MS) {
                m_ledBlinkState = !m_ledBlinkState;
                m_lastBlinkTime = now;
                NeokeyIO::setLED(EffectID::STUTTER, m_ledBlinkState);
            }
            break;

        case StutterState::PLAYING:
        case StutterState::WAIT_PLAYBACK_LENGTH:
            // LED SOLID BLUE
            analogWrite(RGB_LED_R_PIN, 0);
            analogWrite(RGB_LED_G_PIN, 0);
            analogWrite(RGB_LED_B_PIN, 255);
            NeokeyIO::setLED(EffectID::STUTTER, true);
            break;

        default:
            // Fallback: LED OFF
            analogWrite(RGB_LED_R_PIN, 0);
            analogWrite(RGB_LED_G_PIN, 0);
            analogWrite(RGB_LED_B_PIN, 0);
            NeokeyIO::setLED(EffectID::STUTTER, false);
            break;
    }

    // ========== ISR STATE TRANSITION DETECTION ==========
    // Check for state changes that happened in ISR (scheduled events fired)
    static StutterState s_lastState = StutterState::IDLE_NO_LOOP;

    if (currentState != s_lastState) {
        // State changed - log it
        Serial.print("Stutter: State changed (");
        Serial.print(static_cast<int>(s_lastState));
        Serial.print(" → ");
        Serial.print(static_cast<int>(currentState));
        Serial.println(")");

        s_lastState = currentState;
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
