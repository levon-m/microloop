#include "FreezeController.h"
#include "NeokeyInput.h"
#include "DisplayManager.h"
#include "Timebase.h"
#include "EncoderHandler.h"
#include <Arduino.h>

FreezeController::FreezeController(FreezeAudio& effect)
    : m_effect(effect),
      m_currentParameter(Parameter::LENGTH),
      m_wasEnabled(false) {
}

const char* FreezeController::lengthName(FreezeLength length) {
    switch (length) {
        case FreezeLength::FREE:      return "Free";
        case FreezeLength::QUANTIZED: return "Quantized";
        default: return "Free";
    }
}

const char* FreezeController::onsetName(FreezeOnset onset) {
    switch (onset) {
        case FreezeOnset::FREE:      return "Free";
        case FreezeOnset::QUANTIZED: return "Quantized";
        default: return "Free";
    }
}

bool FreezeController::handleButtonPress(const Command& cmd) {
    if (cmd.targetEffect != EffectID::FREEZE) {
        return false;  // Not our effect
    }

    if (cmd.type != CommandType::EFFECT_ENABLE && cmd.type != CommandType::EFFECT_TOGGLE) {
        return false;  // Not a press command
    }

    FreezeLength lengthMode = m_effect.getLengthMode();
    FreezeOnset onsetMode = m_effect.getOnsetMode();

    if (onsetMode == FreezeOnset::FREE) {
        // FREE ONSET: Engage immediately
        m_effect.enable();

        if (lengthMode == FreezeLength::QUANTIZED) {
            // FREE ONSET + QUANTIZED LENGTH
            Quantization quant = EffectQuantization::getGlobalQuantization();
            uint32_t durationSamples = EffectQuantization::calculateQuantizedDuration(quant);
            uint64_t releaseSample = Timebase::getSamplePosition() + durationSamples;
            m_effect.scheduleRelease(releaseSample);

            Serial.print("Freeze ENGAGED (Free onset, Quantized length=");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.println(")");
        } else {
            // FREE ONSET + FREE LENGTH
            Serial.println("Freeze ENGAGED (Free onset, Free length)");
        }

        // Update visual feedback
        NeokeyInput::setLED(EffectID::FREEZE, true);
        DisplayManager::instance().updateDisplay();
        return true;  // Command handled
    } else {
        // QUANTIZED ONSET: Schedule for next boundary with lookahead offset
        Quantization quant = EffectQuantization::getGlobalQuantization();
        uint32_t samplesToNext = EffectQuantization::samplesToNextQuantizedBoundary(quant);

        // Apply lookahead offset (fire early to catch external audio transients)
        uint32_t lookahead = EffectQuantization::getLookaheadOffset();
        uint32_t adjustedSamples = (samplesToNext > lookahead) ? (samplesToNext - lookahead) : 0;

        // Calculate absolute sample position for onset
        uint64_t onsetSample = Timebase::getSamplePosition() + adjustedSamples;

        // Schedule onset in ISR (same as how length scheduling works)
        m_effect.scheduleOnset(onsetSample);

        // If length is also quantized, schedule release from onset position
        if (lengthMode == FreezeLength::QUANTIZED) {
            uint32_t durationSamples = EffectQuantization::calculateQuantizedDuration(quant);
            uint64_t releaseSample = onsetSample + durationSamples;
            m_effect.scheduleRelease(releaseSample);
        }

        Serial.print("Freeze ONSET scheduled (");
        Serial.print(EffectQuantization::quantizationName(quant));
        Serial.print(" grid, ");
        Serial.print(adjustedSamples);
        Serial.print(" samples, lookahead=");
        Serial.print(lookahead);
        Serial.println(")");

        return true;  // Command handled
    }
}

bool FreezeController::handleButtonRelease(const Command& cmd) {
    if (cmd.targetEffect != EffectID::FREEZE) {
        return false;  // Not our effect
    }

    if (cmd.type != CommandType::EFFECT_DISABLE) {
        return false;  // Not a release command
    }

    FreezeLength lengthMode = m_effect.getLengthMode();

    if (lengthMode == FreezeLength::QUANTIZED) {
        // QUANTIZED LENGTH: Ignore release (auto-releases)
        Serial.println("Freeze button released (ignored - quantized length)");
        return true;  // Command handled (skip default disable)
    }

    // FREE LENGTH: Check if we have scheduled onset via ISR API
    // QUANTIZED ONSET + FREE LENGTH: Cancel scheduled onset
    m_effect.cancelScheduledOnset();
    Serial.println("Freeze scheduled onset CANCELLED (button released before beat)");

    // FREE ONSET + FREE LENGTH: Fall through to default disable
    return false;  // Let EffectManager handle disable
}

void FreezeController::updateVisualFeedback() {
    FreezeState currentState = m_effect.getState();
    bool isEnabled = m_effect.isEnabled();

    // Map state to LED: OFF for IDLE, ON for ARMED/ACTIVE
    // TODO: Add RGB LED support to differentiate ARMED (yellow) from ACTIVE (white)
    NeokeyInput::setLED(EffectID::FREEZE, currentState != FreezeState::IDLE);

    // Detect state transition to ARMED (quantized onset scheduled)
    static FreezeState s_prevState = FreezeState::IDLE;
    if (currentState == FreezeState::ARMED && s_prevState == FreezeState::IDLE) {
        Serial.println("Freeze ARMED (waiting for quantized onset)");
        DisplayManager::instance().updateDisplay();
    }

    // Detect state transition to ACTIVE
    if (currentState == FreezeState::ACTIVE && s_prevState != FreezeState::ACTIVE) {
        FreezeOnset onsetMode = m_effect.getOnsetMode();
        FreezeLength lengthMode = m_effect.getLengthMode();

        if (onsetMode == FreezeOnset::QUANTIZED) {
            Quantization quant = EffectQuantization::getGlobalQuantization();
            Serial.print("Freeze ACTIVE at scheduled onset (");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.print(" boundary, ");
            Serial.print(lengthMode == FreezeLength::QUANTIZED ? "Quantized length)" : "Free length)");
            Serial.println();
        } else {
            Serial.print("Freeze ACTIVE (Free onset, ");
            Serial.print(lengthMode == FreezeLength::QUANTIZED ? "Quantized length)" : "Free length)");
            Serial.println();
        }
        DisplayManager::instance().updateDisplay();
    }

    // Detect state transition back to IDLE
    if (currentState == FreezeState::IDLE && s_prevState != FreezeState::IDLE) {
        if (s_prevState == FreezeState::ARMED) {
            Serial.println("Freeze DISARMED (onset cancelled)");
        } else if (m_effect.getLengthMode() == FreezeLength::QUANTIZED) {
            Serial.println("Freeze IDLE (auto-released, Quantized mode)");
        } else {
            Serial.println("Freeze IDLE (released)");
        }
        DisplayManager::instance().updateDisplay();
    }

    // Update state for next call
    s_prevState = currentState;
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

void FreezeController::bindToEncoder(EncoderHandler::Handler& encoder,
                                     AnyEncoderTouchedFn anyTouchedExcept) {
    // Button press: Cycle between LENGTH and ONSET parameters
    encoder.onButtonPress([this]() {
        Parameter current = m_currentParameter;
        if (current == Parameter::LENGTH) {
            m_currentParameter = Parameter::ONSET;
            Serial.println("Freeze Parameter: ONSET");
        } else {
            m_currentParameter = Parameter::LENGTH;
            Serial.println("Freeze Parameter: LENGTH");
        }
        // Display update handled by onDisplayUpdate callback
    });

    // Value change: Adjust current parameter
    encoder.onValueChange([this](int8_t delta) {
        Parameter param = m_currentParameter;

        if (param == Parameter::LENGTH) {
            int8_t currentIndex = static_cast<int8_t>(m_effect.getLengthMode());
            int8_t newIndex = clampIndex(currentIndex + delta, 0, 1);
            if (newIndex != currentIndex) {
                FreezeLength newLength = static_cast<FreezeLength>(newIndex);
                m_effect.setLengthMode(newLength);
                Serial.print("Freeze Length: ");
                Serial.println(lengthName(newLength));

                MenuDisplayData menuData;
                menuData.topText = "FREEZE->Length";
                menuData.middleText = lengthName(newLength);
                menuData.numOptions = 2;
                menuData.selectedIndex = newIndex;
                DisplayManager::instance().showMenu(menuData);
            }
        } else {  // ONSET parameter
            int8_t currentIndex = static_cast<int8_t>(m_effect.getOnsetMode());
            int8_t newIndex = clampIndex(currentIndex + delta, 0, 1);
            if (newIndex != currentIndex) {
                FreezeOnset newOnset = static_cast<FreezeOnset>(newIndex);
                m_effect.setOnsetMode(newOnset);
                Serial.print("Freeze Onset: ");
                Serial.println(onsetName(newOnset));

                MenuDisplayData menuData;
                menuData.topText = "FREEZE->Onset";
                menuData.middleText = onsetName(newOnset);
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
            if (param == Parameter::LENGTH) {
                menuData.topText = "FREEZE->Length";
                menuData.middleText = lengthName(m_effect.getLengthMode());
                menuData.selectedIndex = static_cast<uint8_t>(m_effect.getLengthMode());
            } else {  // ONSET
                menuData.topText = "FREEZE->Onset";
                menuData.middleText = onsetName(m_effect.getOnsetMode());
                menuData.selectedIndex = static_cast<uint8_t>(m_effect.getOnsetMode());
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
