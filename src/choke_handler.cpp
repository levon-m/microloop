#include "choke_handler.h"
#include "input_io.h"
#include "display_manager.h"
#include "timekeeper.h"
#include <Arduino.h>

namespace ChokeHandler {

static AudioEffectChoke* choke = nullptr;
static Parameter currentParameter = Parameter::LENGTH;
static bool wasEnabled = false;  // Track previous enabled state for rising edge detection

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
        choke->scheduleOnset(onsetSample);

        // If length is also quantized, schedule release from onset position
        if (lengthMode == ChokeLength::QUANTIZED) {
            uint32_t durationSamples = EffectQuantization::calculateQuantizedDuration(quant);
            uint64_t releaseSample = onsetSample + durationSamples;
            choke->scheduleRelease(releaseSample);
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

    bool isEnabled = choke->isEnabled();

    // Check for ISR-fired onset (QUANTIZED ONSET mode)
    // Detect TRUE rising edge: transition from disabled to enabled
    if (isEnabled && !wasEnabled) {
        // ISR fired onset - update visual feedback
        InputIO::setLED(EffectID::CHOKE, true);
        DisplayManager::setLastActivatedEffect(EffectID::CHOKE);
        DisplayManager::updateDisplay();

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
    // Detect TRUE falling edge: transition from enabled to disabled
    if (!isEnabled && wasEnabled) {
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

    // Update previous state for next iteration
    wasEnabled = isEnabled;
}

Parameter getCurrentParameter() {
    return currentParameter;
}

void setCurrentParameter(Parameter param) {
    currentParameter = param;
}

}