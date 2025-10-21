#include "choke_handler.h"
#include "input_io.h"
#include "display_manager.h"
#include "timekeeper.h"
#include <Arduino.h>

namespace ChokeHandler {

// ========== STATE ==========

static AudioEffectChoke* choke = nullptr;
static Parameter currentParameter = Parameter::LENGTH;

// ========== HELPER FUNCTIONS ==========

BitmapID lengthToBitmap(ChokeLength length) {
    switch (length) {
        case ChokeLength::FREE:      return BitmapID::CHOKE_LENGTH_FREE;
        case ChokeLength::QUANTIZED: return BitmapID::CHOKE_LENGTH_QUANT;
        default: return BitmapID::CHOKE_LENGTH_FREE;
    }
}

BitmapID onsetToBitmap(ChokeOnset onset) {
    switch (onset) {
        case ChokeOnset::FREE:      return BitmapID::CHOKE_ONSET_FREE;
        case ChokeOnset::QUANTIZED: return BitmapID::CHOKE_ONSET_QUANT;
        default: return BitmapID::CHOKE_ONSET_FREE;
    }
}

const char* lengthName(ChokeLength length) {
    switch (length) {
        case ChokeLength::FREE:      return "Free";
        case ChokeLength::QUANTIZED: return "Quantized";
        default: return "Free";
    }
}

const char* onsetName(ChokeOnset onset) {
    switch (onset) {
        case ChokeOnset::FREE:      return "Free";
        case ChokeOnset::QUANTIZED: return "Quantized";
        default: return "Free";
    }
}

// ========== PUBLIC API ==========

void initialize(AudioEffectChoke& chokeEffect) {
    choke = &chokeEffect;
    currentParameter = Parameter::LENGTH;
}

bool handleButtonPress(const Command& cmd) {
    if (!choke || cmd.targetEffect != EffectID::CHOKE) {
        return false;  // Not our effect
    }

    if (cmd.type != CommandType::EFFECT_ENABLE && cmd.type != CommandType::EFFECT_TOGGLE) {
        return false;  // Not a press command
    }

    ChokeLength lengthMode = choke->getLengthMode();
    ChokeOnset onsetMode = choke->getOnsetMode();

    if (onsetMode == ChokeOnset::FREE) {
        // FREE ONSET: Engage immediately
        choke->enable();

        if (lengthMode == ChokeLength::QUANTIZED) {
            // FREE ONSET + QUANTIZED LENGTH
            Quantization quant = EffectQuantization::getGlobalQuantization();
            uint32_t durationSamples = EffectQuantization::calculateQuantizedDuration(quant);
            uint64_t releaseSample = TimeKeeper::getSamplePosition() + durationSamples;
            choke->scheduleRelease(releaseSample);

            Serial.print("Choke ENGAGED (Free onset, Quantized length=");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.println(")");
        } else {
            // FREE ONSET + FREE LENGTH
            Serial.println("Choke ENGAGED (Free onset, Free length)");
        }

        // Update visual feedback
        InputIO::setLED(EffectID::CHOKE, true);
        DisplayManager::setLastActivatedEffect(EffectID::CHOKE);
        DisplayIO::showChoke();
        return true;  // Command handled
    } else {
        // QUANTIZED ONSET: Schedule for ISR-accurate timing
        Quantization quant = EffectQuantization::getGlobalQuantization();
        uint32_t samplesToNext = EffectQuantization::samplesToNextQuantizedBoundary(quant);

        // Check if we're at/past boundary (grace period)
        if (samplesToNext == 0) {
            // We're at or just past boundary - fire immediately!
            choke->enable();

            if (lengthMode == ChokeLength::QUANTIZED) {
                // Schedule auto-release
                uint32_t durationSamples = EffectQuantization::calculateQuantizedDuration(quant);
                uint64_t releaseSample = TimeKeeper::getSamplePosition() + durationSamples;
                choke->scheduleRelease(releaseSample);
            }

            // Update visual feedback
            InputIO::setLED(EffectID::CHOKE, true);
            DisplayManager::setLastActivatedEffect(EffectID::CHOKE);
            DisplayIO::showChoke();

            Serial.print("Choke ENGAGED immediately (on boundary, ");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.println(")");
        } else {
            // Schedule for next boundary
            uint64_t onsetSample = TimeKeeper::getSamplePosition() + samplesToNext;

            // Use ISR scheduling (sample-accurate!)
            choke->scheduleOnset(onsetSample);

            // Also schedule length if QUANTIZED
            if (lengthMode == ChokeLength::QUANTIZED) {
                uint32_t durationSamples = EffectQuantization::calculateQuantizedDuration(quant);
                uint64_t releaseSample = onsetSample + durationSamples;
                choke->scheduleRelease(releaseSample);
            }

            Serial.print("Choke ONSET scheduled (ISR, ");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.print(" boundary, ");
            Serial.print(samplesToNext);
            Serial.println(" samples)");
        }

        return true;  // Command handled
    }
}

bool handleButtonRelease(const Command& cmd) {
    if (!choke || cmd.targetEffect != EffectID::CHOKE) {
        return false;  // Not our effect
    }

    if (cmd.type != CommandType::EFFECT_DISABLE) {
        return false;  // Not a release command
    }

    ChokeLength lengthMode = choke->getLengthMode();

    if (lengthMode == ChokeLength::QUANTIZED) {
        // QUANTIZED LENGTH: Ignore release (auto-releases)
        Serial.println("Choke button released (ignored - quantized length)");
        return true;  // Command handled (skip default disable)
    }

    // FREE LENGTH: Check if we have scheduled onset via ISR API
    // QUANTIZED ONSET + FREE LENGTH: Cancel scheduled onset
    choke->cancelScheduledOnset();
    Serial.println("Choke scheduled onset CANCELLED (button released before beat)");

    // FREE ONSET + FREE LENGTH: Fall through to default disable
    return false;  // Let EffectManager handle disable
}

void updateVisualFeedback() {
    if (!choke) return;

    // Check for ISR-fired onset (QUANTIZED ONSET mode)
    // Detect rising edge: choke enabled but display not showing it yet
    if (choke->isEnabled() && DisplayManager::getLastActivatedEffect() != EffectID::CHOKE) {
        // ISR fired onset - update visual feedback
        InputIO::setLED(EffectID::CHOKE, true);
        DisplayManager::setLastActivatedEffect(EffectID::CHOKE);
        DisplayIO::showChoke();

        // Determine what happened based on onset/length modes
        ChokeOnset onsetMode = choke->getOnsetMode();
        ChokeLength lengthMode = choke->getLengthMode();

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
    if (!choke->isEnabled() && DisplayManager::getLastActivatedEffect() == EffectID::CHOKE) {
        // Only auto-release if in QUANTIZED length mode
        if (choke->getLengthMode() == ChokeLength::QUANTIZED) {
            // Choke auto-released - update display
            DisplayManager::setLastActivatedEffect(EffectID::NONE);
            DisplayManager::updateDisplay();

            // Update LED to reflect disabled state
            InputIO::setLED(EffectID::CHOKE, false);

            // Debug output
            Serial.println("Choke auto-released (Quantized mode)");
        }
    }
}

Parameter getCurrentParameter() {
    return currentParameter;
}

void setCurrentParameter(Parameter param) {
    currentParameter = param;
}

}  // namespace ChokeHandler
