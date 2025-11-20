#include "effect_quantization.h"
#include <AudioStream.h> 

namespace EffectQuantization {

// Global quantization state (default: 1/16 note)
static Quantization globalQuantization = Quantization::QUANT_16;

// Lookahead offset for quantized onset (default: 128 samples = ~3ms @ 44.1kHz)
// Fires onset slightly early to catch external audio transients (e.g., kick from Digitakt)
static uint32_t lookaheadOffset = 128;

uint32_t calculateQuantizedDuration(Quantization quant) {
    uint32_t samplesPerBeat = TimeKeeper::getSamplesPerBeat();
    uint32_t duration;

    switch (quant) {
        case Quantization::QUANT_32:
            duration = samplesPerBeat / 8;  // 1/32 note = 1/8 of a beat
            break;
        case Quantization::QUANT_16:
            duration = samplesPerBeat / 4;  // 1/16 note = 1/4 of a beat
            break;
        case Quantization::QUANT_8:
            duration = samplesPerBeat / 2;  // 1/8 note = 1/2 of a beat
            break;
        case Quantization::QUANT_4:
            duration = samplesPerBeat;      // 1/4 note = 1 full beat
            break;
        default:
            duration = samplesPerBeat / 4;  // Default: 1/16 note
            break;
    }

    // NO BLOCK ROUNDING - ISR will handle block-level granularity
    return duration;
}

uint32_t samplesToNextQuantizedBoundary(Quantization quant) {
    uint32_t subdivision = calculateQuantizedDuration(quant);
    return TimeKeeper::samplesToNextSubdivision(subdivision);
}

const char* quantizationName(Quantization quant) {
    switch (quant) {
        case Quantization::QUANT_32: return "1/32";
        case Quantization::QUANT_16: return "1/16";
        case Quantization::QUANT_8:  return "1/8";
        case Quantization::QUANT_4:  return "1/4";
        default: return "1/16";
    }
}

Quantization getGlobalQuantization() {
    return globalQuantization;
}

void setGlobalQuantization(Quantization quant) {
    globalQuantization = quant;
}

uint32_t getLookaheadOffset() {
    return lookaheadOffset;
}

// void setLookaheadOffset(uint32_t samples) {
//     lookaheadOffset = samples;
// }

void initialize() {
    globalQuantization = Quantization::QUANT_16;
    lookaheadOffset = 128;  // Default: 128 samples (~3ms)
}

}