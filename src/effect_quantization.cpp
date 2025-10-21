#include "effect_quantization.h"
#include <AudioStream.h>  // For AUDIO_BLOCK_SAMPLES

namespace EffectQuantization {

// Global quantization state (default: 1/16 note)
static Quantization globalQuantization = Quantization::QUANT_16;

// ========== QUANTIZATION CALCULATIONS ==========

uint32_t calculateQuantizedDuration(Quantization quant) {
    /**
     * Calculate quantized duration in samples (BLOCK-ROUNDED)
     *
     * BLOCK ROUNDING (NEW):
     *   Result is rounded to nearest AUDIO_BLOCK_SAMPLES (128) boundary.
     *   WHY? Audio ISR can only toggle effects at block boundaries anyway.
     *   Rounding makes durations consistent and prevents partial-block artifacts.
     *
     * EXAMPLE (120 BPM, samplesPerBeat = 22050):
     *   - 1/16 note: 22050 / 4 = 5512 samples → rounded to 5504 (43 blocks)
     *   - 1/8 note:  22050 / 2 = 11025 samples → rounded to 11008 (86 blocks)
     */
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

    // BLOCK ROUNDING: Round to nearest AUDIO_BLOCK_SAMPLES boundary
    uint32_t remainder = duration % AUDIO_BLOCK_SAMPLES;
    if (remainder >= (AUDIO_BLOCK_SAMPLES / 2)) {
        // Round up
        duration += (AUDIO_BLOCK_SAMPLES - remainder);
    } else {
        // Round down
        duration -= remainder;
    }

    return duration;
}

uint32_t samplesToNextQuantizedBoundary(Quantization quant) {
    /**
     * Calculate samples to next quantized boundary (FIXED)
     *
     * OLD BEHAVIOR:
     *   - Ignored quant parameter, always used beat boundaries
     *   - No subdivision support (1/32, 1/16, 1/8 all went to beat)
     *
     * NEW BEHAVIOR:
     *   - Uses TimeKeeper::samplesToNextSubdivision() with actual grid
     *   - Supports all subdivisions (1/32, 1/16, 1/8, 1/4)
     *   - Block-rounded to prevent app-thread jitter
     *   - Tolerance: fires immediately if within 128 samples of boundary
     */
    uint32_t subdivision = calculateQuantizedDuration(quant);
    return TimeKeeper::samplesToNextSubdivision(subdivision);
}

BitmapID quantizationToBitmap(Quantization quant) {
    switch (quant) {
        case Quantization::QUANT_32: return BitmapID::QUANT_32;
        case Quantization::QUANT_16: return BitmapID::QUANT_16;
        case Quantization::QUANT_8:  return BitmapID::QUANT_8;
        case Quantization::QUANT_4:  return BitmapID::QUANT_4;
        default: return BitmapID::QUANT_16;  // Default fallback
    }
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

// ========== GLOBAL QUANTIZATION API ==========

Quantization getGlobalQuantization() {
    return globalQuantization;
}

void setGlobalQuantization(Quantization quant) {
    globalQuantization = quant;
}

void initialize() {
    globalQuantization = Quantization::QUANT_16;
}

}  // namespace EffectQuantization
