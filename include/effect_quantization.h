#pragma once

#include <stdint.h>
#include "effect_manager.h"
#include "audio_choke.h"
#include "audio_freeze.h"
#include "display_io.h"
#include "input_io.h"
#include "timekeeper.h"

// Global quantization grid (shared across all effects)
enum class Quantization : uint8_t {
    QUANT_32 = 0,  // 1/32 note
    QUANT_16 = 1,  // 1/16 note (default)
    QUANT_8  = 2,  // 1/8 note
    QUANT_4  = 3   // 1/4 note
};

namespace EffectQuantization {

uint32_t calculateQuantizedDuration(Quantization quant);

uint32_t samplesToNextQuantizedBoundary(Quantization quant);

BitmapID quantizationToBitmap(Quantization quant);

const char* quantizationName(Quantization quant);

Quantization getGlobalQuantization();

void setGlobalQuantization(Quantization quant);

uint32_t getLookaheadOffset();

void setLookaheadOffset(uint32_t samples);

void initialize();

}