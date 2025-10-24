#include "freeze_handler.h"
#include "input_io.h"
#include "display_manager.h"
#include "timekeeper.h"
#include <Arduino.h>

namespace FreezeHandler {

static AudioEffectFreeze* freeze = nullptr;
static Parameter currentParameter = Parameter::LENGTH;

BitmapID lengthToBitmap(FreezeLength length) {
    switch (length) {
        case FreezeLength::FREE:      return BitmapID::FREEZE_LENGTH_FREE;
        case FreezeLength::QUANTIZED: return BitmapID::FREEZE_LENGTH_QUANT;
        default: return BitmapID::FREEZE_LENGTH_FREE;
    }
}

BitmapID onsetToBitmap(FreezeOnset onset) {
    switch (onset) {
        case FreezeOnset::FREE:      return BitmapID::FREEZE_ONSET_FREE;
        case FreezeOnset::QUANTIZED: return BitmapID::FREEZE_ONSET_QUANT;
        default: return BitmapID::FREEZE_ONSET_FREE;
    }
}

const char* lengthName(FreezeLength length) {
    switch (length) {
        case FreezeLength::FREE:      return "Free";
        case FreezeLength::QUANTIZED: return "Quantized";
        default: return "Free";
    }
}

const char* onsetName(FreezeOnset onset) {
    switch (onset) {
        case FreezeOnset::FREE:      return "Free";
        case FreezeOnset::QUANTIZED: return "Quantized";
        default: return "Free";
    }
}

void initialize(AudioEffectFreeze& freezeEffect) {
    freeze = &freezeEffect;
    currentParameter = Parameter::LENGTH;
}

bool handleButtonPress(const Command& cmd) {
    if (!freeze || cmd.targetEffect != EffectID::FREEZE) {
        return false;  // Not our effect
    }

    if (cmd.type != CommandType::EFFECT_ENABLE && cmd.type != CommandType::EFFECT_TOGGLE) {
        return false;  // Not a press command
    }

    FreezeLength lengthMode = freeze->getLengthMode();
    FreezeOnset onsetMode = freeze->getOnsetMode();

    if (onsetMode == FreezeOnset::FREE) {
        // FREE ONSET: Engage immediately
        freeze->enable();

        if (lengthMode == FreezeLength::QUANTIZED) {
            // FREE ONSET + QUANTIZED LENGTH
            Quantization quant = EffectQuantization::getGlobalQuantization();
            uint32_t durationSamples = EffectQuantization::calculateQuantizedDuration(quant);
            uint64_t releaseSample = TimeKeeper::getSamplePosition() + durationSamples;
            freeze->scheduleRelease(releaseSample);

            Serial.print("Freeze ENGAGED (Free onset, Quantized length=");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.println(")");
        } else {
            // FREE ONSET + FREE LENGTH
            Serial.println("Freeze ENGAGED (Free onset, Free length)");
        }

        // Update visual feedback
        InputIO::setLED(EffectID::FREEZE, true);
        DisplayManager::setLastActivatedEffect(EffectID::FREEZE);
        DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);
        return true;  // Command handled
    } else {
        // QUANTIZED ONSET: Schedule for ISR-accurate timing
        Quantization quant = EffectQuantization::getGlobalQuantization();
        uint32_t samplesToNext = EffectQuantization::samplesToNextQuantizedBoundary(quant);

        // Check if we're at/past boundary (grace period)
        if (samplesToNext == 0) {
            // We're at or just past boundary - fire immediately!
            freeze->enable();

            if (lengthMode == FreezeLength::QUANTIZED) {
                // Schedule auto-release
                uint32_t durationSamples = EffectQuantization::calculateQuantizedDuration(quant);
                uint64_t releaseSample = TimeKeeper::getSamplePosition() + durationSamples;
                freeze->scheduleRelease(releaseSample);
            }

            // Update visual feedback
            InputIO::setLED(EffectID::FREEZE, true);
            DisplayManager::setLastActivatedEffect(EffectID::FREEZE);
            DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);

            Serial.print("Freeze ENGAGED immediately (on boundary, ");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.println(")");
        } else {
            // Schedule for next boundary
            uint64_t onsetSample = TimeKeeper::getSamplePosition() + samplesToNext;

            // Use ISR scheduling (sample-accurate!)
            freeze->scheduleOnset(onsetSample);

            // Also schedule length if QUANTIZED
            if (lengthMode == FreezeLength::QUANTIZED) {
                uint32_t durationSamples = EffectQuantization::calculateQuantizedDuration(quant);
                uint64_t releaseSample = onsetSample + durationSamples;
                freeze->scheduleRelease(releaseSample);
            }

            Serial.print("Freeze ONSET scheduled (ISR, ");
            Serial.print(EffectQuantization::quantizationName(quant));
            Serial.print(" boundary, ");
            Serial.print(samplesToNext);
            Serial.println(" samples)");
        }

        return true;  // Command handled
    }
}

bool handleButtonRelease(const Command& cmd) {
    if (!freeze || cmd.targetEffect != EffectID::FREEZE) {
        return false;  // Not our effect
    }

    if (cmd.type != CommandType::EFFECT_DISABLE) {
        return false;  // Not a release command
    }

    FreezeLength lengthMode = freeze->getLengthMode();

    if (lengthMode == FreezeLength::QUANTIZED) {
        // QUANTIZED LENGTH: Ignore release (auto-releases)
        Serial.println("Freeze button released (ignored - quantized length)");
        return true;  // Command handled (skip default disable)
    }

    // FREE LENGTH: Check if we have scheduled onset via ISR API
    // QUANTIZED ONSET + FREE LENGTH: Cancel scheduled onset
    freeze->cancelScheduledOnset();
    Serial.println("Freeze scheduled onset CANCELLED (button released before beat)");

    // FREE ONSET + FREE LENGTH: Fall through to default disable
    return false;  // Let EffectManager handle disable
}

void updateVisualFeedback() {
    if (!freeze) return;

    // Check for ISR-fired onset (QUANTIZED ONSET mode)
    // Detect rising edge: freeze enabled but display not showing it yet
    if (freeze->isEnabled() && DisplayManager::getLastActivatedEffect() != EffectID::FREEZE) {
        // ISR fired onset - update visual feedback
        InputIO::setLED(EffectID::FREEZE, true);
        DisplayManager::setLastActivatedEffect(EffectID::FREEZE);
        DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);

        // Determine what happened based on onset/length modes
        FreezeOnset onsetMode = freeze->getOnsetMode();
        FreezeLength lengthMode = freeze->getLengthMode();

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
    if (!freeze->isEnabled() && DisplayManager::getLastActivatedEffect() == EffectID::FREEZE) {
        // Only auto-release if in QUANTIZED length mode
        if (freeze->getLengthMode() == FreezeLength::QUANTIZED) {
            // Freeze auto-released - update display
            DisplayManager::setLastActivatedEffect(EffectID::NONE);
            DisplayManager::updateDisplay();

            // Update LED to reflect disabled state
            InputIO::setLED(EffectID::FREEZE, false);

            // Debug output
            Serial.println("Freeze auto-released (Quantized mode)");
        }
    }
}

Parameter getCurrentParameter() {
    return currentParameter;
}

void setCurrentParameter(Parameter param) {
    currentParameter = param;
}

}