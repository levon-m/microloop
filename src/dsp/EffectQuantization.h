#pragma once

#include <stdint.h>
#include "EffectManager.h"
#include "ChokeAudio.h"
#include "FreezeAudio.h"
#include "Ssd1306Display.h"
#include "NeokeyInput.h"
#include "Timebase.h"

// Global quantization grid (shared across all effects)
// Ordered from smallest to largest subdivision
enum class Quantization : uint8_t {
    QUANT_32  = 0,  // 1/32 note
    QUANT_32T = 1,  // 1/32 triplet
    QUANT_16  = 2,  // 1/16 note
    QUANT_16T = 3,  // 1/16 triplet
    QUANT_8   = 4,  // 1/8 note
    QUANT_8T  = 5,  // 1/8 triplet
    QUANT_4   = 6,  // 1/4 note (default)
    QUANT_4T  = 7   // 1/4 triplet
};

constexpr uint8_t QUANT_COUNT = 8;

namespace EffectQuantization {

uint32_t calculateQuantizedDuration(Quantization quant);

uint32_t samplesToNextQuantizedBoundary(Quantization quant);

const char* quantizationName(Quantization quant);

Quantization getGlobalQuantization();

void setGlobalQuantization(Quantization quant);

uint32_t getLookaheadOffset();

//void setLookaheadOffset(uint32_t samples);

void initialize();

}