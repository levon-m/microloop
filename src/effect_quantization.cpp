#include "effect_quantization.h"
#include <AudioStream.h>  // For AUDIO_BLOCK_SAMPLES

namespace EffectQuantization {

// Global quantization state (default: 1/16 note)
static Quantization globalQuantization = Quantization::QUANT_16;

// ========== QUANTIZATION CALCULATIONS ==========

uint32_t calculateQuantizedDuration(Quantization quant) {
    /**
     * Calculate quantized duration in samples (EXACT)
     *
     * NO BLOCK ROUNDING:
     *   Returns exact subdivision duration calculated from samplesPerBeat.
     *   ISR will handle block-level scheduling automatically.
     *   This prevents cumulative drift when subdivisions are chained.
     *
     * EXAMPLE (120 BPM, samplesPerBeat = 22050):
     *   - 1/32 note: 22050 / 8 = 2756 samples (exact)
     *   - 1/16 note: 22050 / 4 = 5512 samples (exact)
     *   - 1/8 note:  22050 / 2 = 11025 samples (exact)
     *   - 1/4 note:  22050 / 1 = 22050 samples (exact)
     *
     * WHY NO ROUNDING?
     *   Block rounding causes cumulative drift. After 4 sixteenth notes,
     *   you'd be 34 samples off from the beat boundary. By using exact
     *   values, subdivisions stay anchored to the musical grid.
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

    // NO BLOCK ROUNDING - ISR will handle block-level granularity
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
