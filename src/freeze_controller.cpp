#include "freeze_controller.h"
#include "input_io.h"
#include "display_manager.h"
#include "timekeeper.h"
#include <Arduino.h>

FreezeController::FreezeController(AudioEffectFreeze& effect)
    : m_effect(effect),
      m_currentParameter(Parameter::LENGTH) {
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
            uint64_t releaseSample = TimeKeeper::getSamplePosition() + durationSamples;
            m_effect.scheduleRelease(releaseSample);

            Serial.print("Freeze ENGAGED (Free onset, Quantized length=");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.println(")");
        } else {
            // FREE ONSET + FREE LENGTH
            Serial.println("Freeze ENGAGED (Free onset, Free length)");
        }

        // Update visual feedback
        InputIO::setLED(EffectID::FREEZE, true);
        DisplayManager::instance().setLastActivatedEffect(EffectID::FREEZE);
        DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);
        return true;  // Command handled
    } else {
        // QUANTIZED ONSET: Schedule for next boundary with lookahead offset
        Quantization quant = EffectQuantization::getGlobalQuantization();
        uint32_t samplesToNext = EffectQuantization::samplesToNextQuantizedBoundary(quant);

        // Apply lookahead offset (fire early to catch external audio transients)
        uint32_t lookahead = EffectQuantization::getLookaheadOffset();
        uint32_t adjustedSamples = (samplesToNext > lookahead) ? (samplesToNext - lookahead) : 0;

        // Calculate absolute sample position for onset
        uint64_t onsetSample = TimeKeeper::getSamplePosition() + adjustedSamples;

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
    // Check for ISR-fired onset (QUANTIZED ONSET mode)
    // Detect rising edge: freeze enabled but display not showing it yet
    if (m_effect.isEnabled() && DisplayManager::instance().getLastActivatedEffect() != EffectID::FREEZE) {
        // ISR fired onset - update visual feedback
        InputIO::setLED(EffectID::FREEZE, true);
        DisplayManager::instance().setLastActivatedEffect(EffectID::FREEZE);
        DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);

        // Determine what happened based on onset/length modes
        FreezeOnset onsetMode = m_effect.getOnsetMode();
        FreezeLength lengthMode = m_effect.getLengthMode();

        if (onsetMode == FreezeOnset::QUANTIZED) {
            Quantization quant = EffectQuantization::getGlobalQuantization();
            Serial.print("Freeze ENGAGED at scheduled onset (");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.print(" boundary, ");
            Serial.print(lengthMode == FreezeLength::QUANTIZED ? "Quantized length)" : "Free length)");
            Serial.println();
        }
    }

    // Check for auto-release (QUANTIZED LENGTH mode)
    // Detect falling edge: freeze disabled but display still showing it
    if (!m_effect.isEnabled() && DisplayManager::instance().getLastActivatedEffect() == EffectID::FREEZE) {
        // Only auto-release if in QUANTIZED length mode
        if (m_effect.getLengthMode() == FreezeLength::QUANTIZED) {
            // Freeze auto-released - update display
            DisplayManager::instance().setLastActivatedEffect(EffectID::NONE);
            DisplayManager::instance().updateDisplay();

            // Update LED to reflect disabled state
            InputIO::setLED(EffectID::FREEZE, false);

            // Debug output
            Serial.println("Freeze auto-released (Quantized mode)");
        }
    }
}
