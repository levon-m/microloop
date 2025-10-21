#pragma once

#include "effect_manager.h"
#include "audio_choke.h"
#include "effect_quantization.h"
#include "display_io.h"

/**
 * Choke Handler Module
 *
 * Manages CHOKE effect onset/length quantization logic.
 * Encapsulates all CHOKE-specific state and behavior.
 *
 * FEATURES:
 * - Onset quantization (FREE/QUANTIZED)
 * - Length quantization (FREE/QUANTIZED)
 * - Scheduled onset monitoring
 * - Auto-release monitoring
 * - Visual feedback (LED, display)
 */

namespace ChokeHandler {

/**
 * Parameter selection for CHOKE menu
 */
enum class Parameter : uint8_t {
    LENGTH = 0,  // Choke length (Free, Quantized)
    ONSET = 1    // Choke onset timing (Free, Quantized)
};

/**
 * Initialize CHOKE handler
 * @param chokeEffect Reference to AudioEffectChoke instance
 */
void initialize(AudioEffectChoke& chokeEffect);

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
 * Convert ChokeLength to BitmapID
 */
BitmapID lengthToBitmap(ChokeLength length);

/**
 * Convert ChokeOnset to BitmapID
 */
BitmapID onsetToBitmap(ChokeOnset onset);

/**
 * Get human-readable length name
 */
const char* lengthName(ChokeLength length);

/**
 * Get human-readable onset name
 */
const char* onsetName(ChokeOnset onset);

}  // namespace ChokeHandler
