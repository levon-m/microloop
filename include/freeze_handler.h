#pragma once

#include "effect_manager.h"
#include "audio_freeze.h"
#include "effect_quantization.h"
#include "display_io.h"

namespace FreezeHandler {

enum class Parameter : uint8_t {
    LENGTH = 0,  // Freeze length (Free, Quantized)
    ONSET = 1    // Freeze onset timing (Free, Quantized)
};

void initialize(AudioEffectFreeze& freezeEffect);

bool handleButtonPress(const Command& cmd);

bool handleButtonRelease(const Command& cmd);

void updateVisualFeedback();

Parameter getCurrentParameter();

void setCurrentParameter(Parameter param);

BitmapID lengthToBitmap(FreezeLength length);

BitmapID onsetToBitmap(FreezeOnset onset);

const char* lengthName(FreezeLength length);

const char* onsetName(FreezeOnset onset);

}