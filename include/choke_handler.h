#pragma once

#include "effect_manager.h"
#include "audio_choke.h"
#include "effect_quantization.h"
#include "display_io.h"

namespace ChokeHandler {

enum class Parameter : uint8_t {
    LENGTH = 0,  // Choke length (Free, Quantized)
    ONSET = 1    // Choke onset timing (Free, Quantized)
};

void initialize(AudioEffectChoke& chokeEffect);

bool handleButtonPress(const Command& cmd);

bool handleButtonRelease(const Command& cmd);

void updateVisualFeedback();

Parameter getCurrentParameter();

void setCurrentParameter(Parameter param);

BitmapID lengthToBitmap(ChokeLength length);

BitmapID onsetToBitmap(ChokeOnset onset);

const char* lengthName(ChokeLength length);

const char* onsetName(ChokeOnset onset);

}