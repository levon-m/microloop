#include "choke_controller.h"
#include "neokey_io.h"
#include "display_manager.h"
#include "timekeeper.h"
#include "encoder_handler.h"
#include <Arduino.h>

ChokeController::ChokeController(AudioEffectChoke& effect)
    : m_effect(effect),
      m_currentParameter(Parameter::LENGTH),
      m_wasEnabled(false) {
}

const char* ChokeController::lengthName(ChokeLength length) {
    switch (length) {
        case ChokeLength::FREE:      return "Free";
        case ChokeLength::QUANTIZED: return "Quantized";
        default: return "Free";
    }
}

const char* ChokeController::onsetName(ChokeOnset onset) {
    switch (onset) {
        case ChokeOnset::FREE:      return "Free";
        case ChokeOnset::QUANTIZED: return "Quantized";
        default: return "Free";
    }
}

bool ChokeController::handleButtonPress(const Command& cmd) {
    if (cmd.targetEffect != EffectID::CHOKE) {
        return false;  // Not our effect
    }

    if (cmd.type != CommandType::EFFECT_ENABLE && cmd.type != CommandType::EFFECT_TOGGLE) {
        return false;  // Not a press command
    }

    ChokeLength lengthMode = m_effect.getLengthMode();
    ChokeOnset onsetMode = m_effect.getOnsetMode();

    if (onsetMode == ChokeOnset::FREE) {
        // FREE ONSET: Engage immediately
        m_effect.enable();

        if (lengthMode == ChokeLength::QUANTIZED) {
            // FREE ONSET + QUANTIZED LENGTH
            Quantization quant = EffectQuantization::getGlobalQuantization();
            uint32_t durationSamples = EffectQuantization::calculateQuantizedDuration(quant);
            uint64_t releaseSample = TimeKeeper::getSamplePosition() + durationSamples;
            m_effect.scheduleRelease(releaseSample);

            Serial.print("Choke ENGAGED (Free onset, Quantized length=");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.println(")");
        } else {
            // FREE ONSET + FREE LENGTH
            Serial.println("Choke ENGAGED (Free onset, Free length)");
        }

        // Update visual feedback
        NeokeyIO::setLED(EffectID::CHOKE, true);
        DisplayManager::instance().updateDisplay();
        return true;  // Command handled
    } else {
        // QUANTIZED ONSET: Schedule for next boundary with lookahead offset
        Quantization quant = EffectQuantization::getGlobalQuantization();

        // DEBUG: Get all timing info
        uint64_t currentSample = TimeKeeper::getSamplePosition();
        uint32_t samplesPerBeat = TimeKeeper::getSamplesPerBeat();
        uint32_t beatNumber = TimeKeeper::getBeatNumber();
        uint32_t tickInBeat = TimeKeeper::getTickInBeat();

        uint32_t samplesToNext = EffectQuantization::samplesToNextQuantizedBoundary(quant);

        // Apply lookahead offset (fire early to catch external audio transients)
        uint32_t lookahead = EffectQuantization::getLookaheadOffset();
        uint32_t adjustedSamples = (samplesToNext > lookahead) ? (samplesToNext - lookahead) : 0;

        // Calculate absolute sample position for onset
        uint64_t onsetSample = currentSample + adjustedSamples;

        // Schedule onset in ISR (same as how length scheduling works)
        m_effect.scheduleOnset(onsetSample);

        // If length is also quantized, schedule release from onset position
        if (lengthMode == ChokeLength::QUANTIZED) {
            uint32_t durationSamples = EffectQuantization::calculateQuantizedDuration(quant);
            uint64_t releaseSample = onsetSample + durationSamples;
            m_effect.scheduleRelease(releaseSample);
        }

        Serial.print("ONSET DEBUG: currentSample=");
        Serial.print((uint32_t)currentSample);
        Serial.print(" beat=");
        Serial.print(beatNumber);
        Serial.print(" tick=");
        Serial.print(tickInBeat);
        Serial.print(" spb=");
        Serial.print(samplesPerBeat);
        Serial.print(" samplesToNext=");
        Serial.print(samplesToNext);
        Serial.print(" lookahead=");
        Serial.print(lookahead);
        Serial.print(" adjusted=");
        Serial.print(adjustedSamples);
        Serial.print(" onsetSample=");
        Serial.println((uint32_t)onsetSample);

        return true;  // Command handled
    }
}

bool ChokeController::handleButtonRelease(const Command& cmd) {
    if (cmd.targetEffect != EffectID::CHOKE) {
        return false;  // Not our effect
    }

    if (cmd.type != CommandType::EFFECT_DISABLE) {
        return false;  // Not a release command
    }

    ChokeLength lengthMode = m_effect.getLengthMode();

    if (lengthMode == ChokeLength::QUANTIZED) {
        // QUANTIZED LENGTH: Ignore release (auto-releases)
        Serial.println("Choke button released (ignored - quantized length)");
        return true;  // Command handled (skip default disable)
    }

    // FREE LENGTH: Check if we have scheduled onset via ISR API
    // QUANTIZED ONSET + FREE LENGTH: Cancel scheduled onset
    m_effect.cancelScheduledOnset();
    Serial.println("Choke scheduled onset CANCELLED (button released before beat)");

    // FREE ONSET + FREE LENGTH: Fall through to default disable
    return false;  // Let EffectManager handle disable
}

void ChokeController::updateVisualFeedback() {
    bool isEnabled = m_effect.isEnabled();

    // Detect rising edge: effect just became enabled
    if (isEnabled && !m_wasEnabled) {
        // ISR fired onset or immediate enable - update visual feedback
        NeokeyIO::setLED(EffectID::CHOKE, true);
        DisplayManager::instance().updateDisplay();

        // Determine what happened based on onset/length modes
        ChokeOnset onsetMode = m_effect.getOnsetMode();
        ChokeLength lengthMode = m_effect.getLengthMode();

        if (onsetMode == ChokeOnset::QUANTIZED) {
            Quantization quant = EffectQuantization::getGlobalQuantization();
            Serial.print("Choke ENGAGED at scheduled onset (");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.print(" boundary, ");
            Serial.print(lengthMode == ChokeLength::QUANTIZED ? "Quantized length)" : "Free length)");
            Serial.println();
        }
    }

    // Detect falling edge: effect just became disabled
    if (!isEnabled && m_wasEnabled) {
        // Update LED to reflect disabled state
        NeokeyIO::setLED(EffectID::CHOKE, false);
        DisplayManager::instance().updateDisplay();

        // Check if this was auto-release (quantized length mode)
        if (m_effect.getLengthMode() == ChokeLength::QUANTIZED) {
            Serial.println("Choke auto-released (Quantized mode)");
        }
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

void ChokeController::bindToEncoder(EncoderHandler::Handler& encoder,
                                    AnyEncoderTouchedFn anyTouchedExcept) {
    // Button press: Cycle between LENGTH and ONSET parameters
    encoder.onButtonPress([this]() {
        Parameter current = m_currentParameter;
        if (current == Parameter::LENGTH) {
            m_currentParameter = Parameter::ONSET;
            Serial.println("Choke Parameter: ONSET");
        } else {
            m_currentParameter = Parameter::LENGTH;
            Serial.println("Choke Parameter: LENGTH");
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
                ChokeLength newLength = static_cast<ChokeLength>(newIndex);
                m_effect.setLengthMode(newLength);
                Serial.print("Choke Length: ");
                Serial.println(lengthName(newLength));

                MenuDisplayData menuData;
                menuData.topText = "CHOKE->Length";
                menuData.middleText = lengthName(newLength);
                menuData.numOptions = 2;
                menuData.selectedIndex = newIndex;
                DisplayManager::instance().showMenu(menuData);
            }
        } else {  // ONSET parameter
            int8_t currentIndex = static_cast<int8_t>(m_effect.getOnsetMode());
            int8_t newIndex = clampIndex(currentIndex + delta, 0, 1);
            if (newIndex != currentIndex) {
                ChokeOnset newOnset = static_cast<ChokeOnset>(newIndex);
                m_effect.setOnsetMode(newOnset);
                Serial.print("Choke Onset: ");
                Serial.println(onsetName(newOnset));

                MenuDisplayData menuData;
                menuData.topText = "CHOKE->Onset";
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
                menuData.topText = "CHOKE->Length";
                menuData.middleText = lengthName(m_effect.getLengthMode());
                menuData.selectedIndex = static_cast<uint8_t>(m_effect.getLengthMode());
            } else {  // ONSET
                menuData.topText = "CHOKE->Onset";
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
