#include "EffectQuantization.h"
#include <AudioStream.h> 

namespace EffectQuantization {

// Global quantization state (default: 1/4 note)
static Quantization globalQuantization = Quantization::QUANT_4;

// Lookahead offset for quantized onset (default: 128 samples = ~3ms @ 44.1kHz)
// Fires onset slightly early to catch external audio transients (e.g., kick from Digitakt)
static uint32_t lookaheadOffset = 128;

uint32_t calculateQuantizedDuration(Quantization quant) {
    uint32_t samplesPerBeat = Timebase::getSamplesPerBeat();
    uint32_t duration;

    switch (quant) {
        case Quantization::QUANT_16:
            duration = samplesPerBeat / 4;      // 1/16 note = 1/4 of a beat
            break;
        case Quantization::QUANT_16T:
            duration = samplesPerBeat / 6;      // 1/16 triplet = 1/6 of a beat
            break;
        case Quantization::QUANT_8:
            duration = samplesPerBeat / 2;      // 1/8 note = 1/2 of a beat
            break;
        case Quantization::QUANT_8T:
            duration = samplesPerBeat / 3;      // 1/8 triplet = 1/3 of a beat
            break;
        case Quantization::QUANT_4:
            duration = samplesPerBeat;          // 1/4 note = 1 full beat
            break;
        case Quantization::QUANT_4T:
            duration = (samplesPerBeat * 2) / 3; // 1/4 triplet = 2/3 of a beat
            break;
        case Quantization::QUANT_32:
            duration = samplesPerBeat / 8;      // 1/32 note = 1/8 of a beat
            break;
        case Quantization::QUANT_32T:
            duration = samplesPerBeat / 12;     // 1/32 triplet = 1/12 of a beat
            break;
        default:
            duration = samplesPerBeat;          // Default: 1/4 note
            break;
    }

    // NO BLOCK ROUNDING - ISR will handle block-level granularity
    return duration;
}

uint32_t samplesToNextQuantizedBoundary(Quantization quant) {
    uint32_t subdivision = calculateQuantizedDuration(quant);
    return Timebase::samplesToNextSubdivision(subdivision);
}

const char* quantizationName(Quantization quant) {
    switch (quant) {
        case Quantization::QUANT_32:  return "1/32";
        case Quantization::QUANT_32T: return "1/32T";
        case Quantization::QUANT_16:  return "1/16";
        case Quantization::QUANT_16T: return "1/16T";
        case Quantization::QUANT_8:   return "1/8";
        case Quantization::QUANT_8T:  return "1/8T";
        case Quantization::QUANT_4:   return "1/4";
        case Quantization::QUANT_4T:  return "1/4T";
        default: return "1/4";
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
    globalQuantization = Quantization::QUANT_4;
    lookaheadOffset = 128;  // Default: 128 samples (~3ms)
}

}