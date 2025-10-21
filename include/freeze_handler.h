#pragma once

#include "effect_manager.h"
#include "audio_freeze.h"
#include "effect_quantization.h"
#include "display_io.h"

/**
 * Freeze Handler Module
 *
 * Manages FREEZE effect onset/length quantization logic.
 * Encapsulates all FREEZE-specific state and behavior.
 *
 * FEATURES:
 * - Onset quantization (FREE/QUANTIZED)
 * - Length quantization (FREE/QUANTIZED)
 * - Scheduled onset monitoring
 * - Auto-release monitoring
 * - Visual feedback (LED, display)
 */

namespace FreezeHandler {

/**
 * Parameter selection for FREEZE menu
 */
enum class Parameter : uint8_t {
    LENGTH = 0,  // Freeze length (Free, Quantized)
    ONSET = 1    // Freeze onset timing (Free, Quantized)
};

/**
 * Initialize FREEZE handler
 * @param freezeEffect Reference to AudioEffectFreeze instance
 */
void initialize(AudioEffectFreeze& freezeEffect);

/**
 * Handle button press command (process onset/length modes)
 * @param cmd Input command (EFFECT_ENABLE or EFFECT_TOGGLE)
 * @return true if command was handled (skip default processing)
 */
bool handleButtonPress(const Command& cmd);

/**
 * Handle button release command (process onset/length modes)
 * @param cmd Input command (EFFECT_DISABLE)
 * @return true if command was handled (skip default processing)
 */
bool handleButtonRelease(const Command& cmd);

/**
 * Monitor visual feedback (call every loop iteration)
 * Updates LED and display when ISR fires onset or auto-releases.
 * Handles both QUANTIZED onset (rising edge) and QUANTIZED length (falling edge).
 */
void updateVisualFeedback();

/**
 * Get current parameter being edited
 */
Parameter getCurrentParameter();

/**
 * Set current parameter being edited
 */
void setCurrentParameter(Parameter param);

/**
 * Convert FreezeLength to BitmapID
 */
BitmapID lengthToBitmap(FreezeLength length);

/**
 * Convert FreezeOnset to BitmapID
 */
BitmapID onsetToBitmap(FreezeOnset onset);

/**
 * Get human-readable length name
 */
const char* lengthName(FreezeLength length);

/**
 * Get human-readable onset name
 */
const char* onsetName(FreezeOnset onset);

}  // namespace FreezeHandler
