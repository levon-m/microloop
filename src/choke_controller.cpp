#include "choke_controller.h"
#include "input_io.h"
#include "display_manager.h"
#include "timekeeper.h"
#include <Arduino.h>

ChokeController::ChokeController(AudioEffectChoke& effect)
    : m_effect(effect),
      m_currentParameter(Parameter::LENGTH) {
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
        InputIO::setLED(EffectID::CHOKE, true);
        DisplayManager::instance().setLastActivatedEffect(EffectID::CHOKE);
        DisplayIO::showChoke();
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
    // Check for ISR-fired onset (QUANTIZED ONSET mode)
    // Detect rising edge: choke enabled but display not showing it yet
    if (m_effect.isEnabled() && DisplayManager::instance().getLastActivatedEffect() != EffectID::CHOKE) {
        // ISR fired onset - update visual feedback
        InputIO::setLED(EffectID::CHOKE, true);
        DisplayManager::instance().setLastActivatedEffect(EffectID::CHOKE);
        DisplayIO::showChoke();

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

    // Check for auto-release (QUANTIZED LENGTH mode)
    // Detect falling edge: choke disabled but display still showing it
    if (!m_effect.isEnabled() && DisplayManager::instance().getLastActivatedEffect() == EffectID::CHOKE) {
        // Only auto-release if in QUANTIZED length mode
        if (m_effect.getLengthMode() == ChokeLength::QUANTIZED) {
            // Choke auto-released - update display
            DisplayManager::instance().setLastActivatedEffect(EffectID::NONE);
            DisplayManager::instance().updateDisplay();

            // Update LED to reflect disabled state
            InputIO::setLED(EffectID::CHOKE, false);

            // Debug output
            Serial.println("Choke auto-released (Quantized mode)");
        }
    }
}
