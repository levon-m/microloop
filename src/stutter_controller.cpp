#include "stutter_controller.h"
#include "input_io.h"
#include "display_manager.h"
#include "timekeeper.h"
#include <Arduino.h>

// Define static EXTMEM buffers for AudioEffectStutter
EXTMEM int16_t AudioEffectStutter::m_stutterBufferL[AudioEffectStutter::STUTTER_BUFFER_SAMPLES];
EXTMEM int16_t AudioEffectStutter::m_stutterBufferR[AudioEffectStutter::STUTTER_BUFFER_SAMPLES];

StutterController::StutterController(AudioEffectStutter& effect)
    : m_effect(effect),
      m_currentParameter(Parameter::ONSET),  // Default to ONSET (first in cycle)
      m_funcHeld(false),
      m_stutterHeld(false),
      m_lastBlinkTime(0),
      m_ledBlinkState(false) {
}

// ========== UTILITY FUNCTIONS FOR BITMAP/NAME MAPPING ==========

BitmapID StutterController::onsetToBitmap(StutterOnset onset) {
    switch (onset) {
        case StutterOnset::FREE:      return BitmapID::STUTTER_ONSET_FREE;
        case StutterOnset::QUANTIZED: return BitmapID::STUTTER_ONSET_QUANT;
        default: return BitmapID::STUTTER_ONSET_FREE;
    }
}

BitmapID StutterController::lengthToBitmap(StutterLength length) {
    switch (length) {
        case StutterLength::FREE:      return BitmapID::STUTTER_LENGTH_FREE;
        case StutterLength::QUANTIZED: return BitmapID::STUTTER_LENGTH_QUANT;
        default: return BitmapID::STUTTER_LENGTH_FREE;
    }
}

BitmapID StutterController::captureStartToBitmap(StutterCaptureStart captureStart) {
    switch (captureStart) {
        case StutterCaptureStart::FREE:      return BitmapID::STUTTER_CAPTURE_START_FREE;
        case StutterCaptureStart::QUANTIZED: return BitmapID::STUTTER_CAPTURE_START_QUANT;
        default: return BitmapID::STUTTER_CAPTURE_START_FREE;
    }
}

BitmapID StutterController::captureEndToBitmap(StutterCaptureEnd captureEnd) {
    switch (captureEnd) {
        case StutterCaptureEnd::FREE:      return BitmapID::STUTTER_CAPTURE_END_FREE;
        case StutterCaptureEnd::QUANTIZED: return BitmapID::STUTTER_CAPTURE_END_QUANT;
        default: return BitmapID::STUTTER_CAPTURE_END_FREE;
    }
}

BitmapID StutterController::stateToBitmap(StutterState state) {
    switch (state) {
        case StutterState::IDLE_NO_LOOP:        return BitmapID::DEFAULT;  // Show default screen
        case StutterState::IDLE_WITH_LOOP:      return BitmapID::STUTTER_IDLE_WITH_LOOP;
        case StutterState::WAIT_CAPTURE_START:  return BitmapID::STUTTER_CAPTURING;  // Use capturing bitmap for visual feedback
        case StutterState::CAPTURING:           return BitmapID::STUTTER_CAPTURING;
        case StutterState::WAIT_CAPTURE_END:    return BitmapID::STUTTER_CAPTURING;
        case StutterState::WAIT_PLAYBACK_ONSET: return BitmapID::STUTTER_PLAYING;  // Use playing bitmap for visual feedback
        case StutterState::PLAYING:             return BitmapID::STUTTER_PLAYING;
        case StutterState::WAIT_PLAYBACK_LENGTH: return BitmapID::STUTTER_PLAYING;
        default: return BitmapID::DEFAULT;
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
        DisplayManager::instance().setLastActivatedEffect(EffectID::STUTTER);
        DisplayIO::showBitmap(stateToBitmap(m_effect.getState()));
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

        if (onsetMode == StutterOnset::FREE) {
            // FREE ONSET: Start playback immediately
            m_effect.startPlayback();
            Serial.println("Stutter: PLAYBACK started (Free onset)");
        } else {
            // QUANTIZED ONSET: Schedule playback start
            Quantization quant = EffectQuantization::getGlobalQuantization();
            uint32_t samplesToNext = EffectQuantization::samplesToNextQuantizedBoundary(quant);
            uint64_t playbackOnsetSample = TimeKeeper::getSamplePosition() + samplesToNext;
            m_effect.schedulePlaybackOnset(playbackOnsetSample);
            Serial.print("Stutter: PLAYBACK ONSET scheduled (");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.println(")");
        }

        // Update visual feedback
        DisplayManager::instance().setLastActivatedEffect(EffectID::STUTTER);
        DisplayIO::showBitmap(stateToBitmap(m_effect.getState()));
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
            DisplayIO::showBitmap(stateToBitmap(m_effect.getState()));
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
        // Cancel capture and return to idle
        m_effect.cancelCaptureStart();
        Serial.println("Stutter: CAPTURE CANCELLED (released before start)");
        DisplayManager::instance().setLastActivatedEffect(EffectID::NONE);
        DisplayManager::instance().updateDisplay();
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

        // Update visual feedback
        DisplayIO::showBitmap(stateToBitmap(m_effect.getState()));
        return true;  // Command handled
    }

    // ========== PLAYBACK MODE RELEASES ==========

    if (currentState == StutterState::WAIT_PLAYBACK_ONSET) {
        // STUTTER released before playback started (waiting for quantized boundary)
        // Just return to IDLE_WITH_LOOP (don't cancel - let it time out naturally)
        // Actually, better to cancel so we don't have orphaned scheduled events
        m_effect.stopPlayback();  // Transition to IDLE_WITH_LOOP
        Serial.println("Stutter: PLAYBACK CANCELLED (released before onset)");
        DisplayIO::showBitmap(stateToBitmap(m_effect.getState()));
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

        // Update visual feedback
        DisplayIO::showBitmap(stateToBitmap(m_effect.getState()));
        return true;  // Command handled
    }

    // Ignore release in other states
    return true;  // Command handled
}

// ========== VISUAL FEEDBACK UPDATE ==========

void StutterController::updateVisualFeedback() {
    StutterState currentState = m_effect.getState();
    uint32_t now = millis();

    // ========== LED BLINKING FOR ARMED STATES ==========
    bool shouldBlink = (currentState == StutterState::WAIT_CAPTURE_START ||
                        currentState == StutterState::WAIT_PLAYBACK_ONSET);

    if (shouldBlink) {
        // Blink LED at 4Hz (250ms on/off)
        if (now - m_lastBlinkTime >= BLINK_INTERVAL_MS) {
            m_ledBlinkState = !m_ledBlinkState;
            m_lastBlinkTime = now;

            // Determine LED color based on state
            uint32_t ledColor;
            if (currentState == StutterState::WAIT_CAPTURE_START) {
                ledColor = m_ledBlinkState ? 0xFF0000 : 0x000000;  // RED blinking
            } else {  // WAIT_PLAYBACK_ONSET
                ledColor = m_ledBlinkState ? 0x0000FF : 0x000000;  // BLUE blinking
            }

            // Update Neokey LED directly (bypass InputIO::setLED which doesn't support colors)
            // Note: This would need to be implemented in InputIO or we use the existing setLED
            // For now, use InputIO::setLED with boolean
            InputIO::setLED(EffectID::STUTTER, m_ledBlinkState);
        }
    } else {
        // ========== SOLID LED FOR NON-BLINKING STATES ==========
        switch (currentState) {
            case StutterState::IDLE_NO_LOOP:
                // LED OFF
                InputIO::setLED(EffectID::STUTTER, false);
                break;

            case StutterState::IDLE_WITH_LOOP:
                // LED WHITE (would need InputIO support for colors)
                // For now, use GREEN as fallback
                InputIO::setLED(EffectID::STUTTER, false);  // Off for now
                break;

            case StutterState::CAPTURING:
            case StutterState::WAIT_CAPTURE_END:
                // LED RED (solid)
                InputIO::setLED(EffectID::STUTTER, true);  // RED (choke color)
                break;

            case StutterState::PLAYING:
            case StutterState::WAIT_PLAYBACK_LENGTH:
                // LED BLUE (solid)
                InputIO::setLED(EffectID::STUTTER, true);  // Will show as current effect color
                break;

            default:
                break;
        }
    }

    // ========== DISPLAY UPDATE ==========
    // Update display bitmap if effect is active and last activated
    if (DisplayManager::instance().getLastActivatedEffect() == EffectID::STUTTER) {
        DisplayIO::showBitmap(stateToBitmap(currentState));
    }

    // ========== ISR STATE TRANSITION DETECTION ==========
    // Check for state changes that happened in ISR (scheduled events fired)
    static StutterState s_lastState = StutterState::IDLE_NO_LOOP;

    if (currentState != s_lastState) {
        // State changed - update display
        Serial.print("Stutter: State changed (");
        Serial.print(static_cast<int>(s_lastState));
        Serial.print(" → ");
        Serial.print(static_cast<int>(currentState));
        Serial.println(")");

        // Update display if this effect is active
        if (currentState != StutterState::IDLE_NO_LOOP && currentState != StutterState::IDLE_WITH_LOOP) {
            DisplayManager::instance().setLastActivatedEffect(EffectID::STUTTER);
            DisplayIO::showBitmap(stateToBitmap(currentState));
        } else if (s_lastState != StutterState::IDLE_NO_LOOP && s_lastState != StutterState::IDLE_WITH_LOOP) {
            // Transitioned back to idle - clear display priority
            DisplayManager::instance().updateDisplay();
        }

        s_lastState = currentState;
    }
}
