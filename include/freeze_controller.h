/**
 * freeze_controller.h - Controller for freeze effect
 *
 * PURPOSE:
 * Manages freeze effect behavior, including quantization modes, button
 * handling, and visual feedback. Decouples effect logic from DSP.
 *
 * DESIGN:
 * - Implements IEffectController interface
 * - Owns reference to AudioEffectFreeze
 * - Manages parameter editing state (LENGTH, ONSET)
 * - Handles free/quantized onset and length modes
 *
 * USAGE:
 *   AudioEffectFreeze freeze;
 *   FreezeController controller(freeze);
 *
 *   // In AppLogic:
 *   if (controller.handleButtonPress(cmd)) {
 *       // Command handled by controller
 *   }
 */

#pragma once

#include "effect_controller.h"
#include "audio_freeze.h"
#include "effect_quantization.h"
#include "display_io.h"

/**
 * Freeze effect controller
 *
 * Handles button presses, quantization logic, and visual feedback
 * for the freeze effect.
 */
class FreezeController : public IEffectController {
public:
    /**
     * Parameter selection for encoder editing
     */
    enum class Parameter : uint8_t {
        LENGTH = 0,  // Freeze length (Free, Quantized)
        ONSET = 1    // Freeze onset timing (Free, Quantized)
    };

    /**
     * Constructor
     *
     * @param effect Reference to the freeze audio effect
     */
    explicit FreezeController(AudioEffectFreeze& effect);

    // IEffectController interface implementation
    bool handleButtonPress(const Command& cmd) override;
    bool handleButtonRelease(const Command& cmd) override;
    void updateVisualFeedback() override;
    EffectID getEffectID() const override { return EffectID::FREEZE; }

    /**
     * Get current parameter being edited
     */
    Parameter getCurrentParameter() const { return m_currentParameter; }

    /**
     * Set current parameter to edit
     */
    void setCurrentParameter(Parameter param) { m_currentParameter = param; }

    // Utility functions for name mapping
    static const char* lengthName(FreezeLength length);
    static const char* onsetName(FreezeOnset onset);

private:
    AudioEffectFreeze& m_effect;    // Reference to audio effect (DSP)
    Parameter m_currentParameter;   // Currently selected parameter for editing
};
