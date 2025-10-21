#pragma once

#include <stdint.h>
#include "effect_manager.h"
#include "audio_choke.h"
#include "audio_freeze.h"
#include "display_io.h"
#include "input_io.h"
#include "timekeeper.h"

/**
 * Effect Quantization Module
 *
 * Provides generic onset/length quantization logic for audio effects.
 * Eliminates code duplication between CHOKE and FREEZE quantization handling.
 *
 * DESIGN:
 * - Template-based approach for effect-specific types (ChokeLength/FreezeLength, etc.)
 * - Unified logic for 4 quantization combinations:
 *   1. FREE onset + FREE length: Standard toggle behavior
 *   2. FREE onset + QUANTIZED length: Immediate onset, auto-release
 *   3. QUANTIZED onset + FREE length: Delayed onset, manual release
 *   4. QUANTIZED onset + QUANTIZED length: Delayed onset, auto-release
 */

// ========== QUANTIZATION ENUMS ==========

// Global quantization grid (shared across all effects)
enum class Quantization : uint8_t {
    QUANT_32 = 0,  // 1/32 note
    QUANT_16 = 1,  // 1/16 note (default)
    QUANT_8  = 2,  // 1/8 note
    QUANT_4  = 3   // 1/4 note
};

// ========== QUANTIZATION UTILITIES ==========

namespace EffectQuantization {

/**
 * Calculate quantized duration in samples
 * @param quant Quantization grid value (1/32, 1/16, 1/8, 1/4)
 * @return Duration in samples
 */
uint32_t calculateQuantizedDuration(Quantization quant);

/**
 * Calculate samples to next quantized boundary (for onset scheduling)
 * @param quant Quantization grid value
 * @return Samples until next beat boundary
 */
uint32_t samplesToNextQuantizedBoundary(Quantization quant);

/**
 * Convert Quantization enum to BitmapID for display
 */
BitmapID quantizationToBitmap(Quantization quant);

/**
 * Get human-readable quantization name
 */
const char* quantizationName(Quantization quant);

// ========== GLOBAL QUANTIZATION STATE ==========

/**
 * Get global quantization setting (shared across all effects)
 */
Quantization getGlobalQuantization();

/**
 * Set global quantization setting
 */
void setGlobalQuantization(Quantization quant);

/**
 * Initialize global quantization to default (1/16 note)
 */
void initialize();

}  // namespace EffectQuantization
