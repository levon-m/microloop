/**
 * choke_controller.h - Controller for choke effect
 *
 * PURPOSE:
 * Manages choke effect behavior, including quantization modes, button
 * handling, and visual feedback. Decouples effect logic from DSP.
 *
 * DESIGN:
 * - Implements IEffectController interface
 * - Owns reference to AudioEffectChoke
 * - Manages parameter editing state (LENGTH, ONSET)
 * - Handles free/quantized onset and length modes
 *
 * USAGE:
 *   AudioEffectChoke choke;
 *   ChokeController controller(choke);
 *
 *   // In AppLogic:
 *   if (controller.handleButtonPress(cmd)) {
 *       // Command handled by controller
 *   }
 */

#pragma once

#include "effect_controller.h"
#include "audio_choke.h"
#include "effect_quantization.h"
#include "oled_io.h"

// Forward declaration
namespace EncoderHandler {
    class Handler;
}

// Callback type for checking if any other encoder is touched
typedef bool (*AnyEncoderTouchedFn)(const EncoderHandler::Handler* ignore);

/**
 * Choke effect controller
 *
 * Handles button presses, quantization logic, and visual feedback
 * for the choke effect.
 */
class ChokeController : public IEffectController {
public:
    /**
     * Parameter selection for encoder editing
     */
    enum class Parameter : uint8_t {
        LENGTH = 0,  // Choke length (Free, Quantized)
        ONSET = 1    // Choke onset timing (Free, Quantized)
    };

    /**
     * Constructor
     *
     * @param effect Reference to the choke audio effect
     */
    explicit ChokeController(AudioEffectChoke& effect);

    // IEffectController interface implementation
    bool handleButtonPress(const Command& cmd) override;
    bool handleButtonRelease(const Command& cmd) override;
    void updateVisualFeedback() override;
    EffectID getEffectID() const override { return EffectID::CHOKE; }

    /**
     * Get current parameter being edited
     */
    Parameter getCurrentParameter() const { return m_currentParameter; }

    /**
     * Set current parameter to edit
     */
    void setCurrentParameter(Parameter param) { m_currentParameter = param; }

    /**
     * Bind controller to an encoder handler
     *
     * @param encoder Encoder handler to bind to
     * @param anyTouchedExcept Callback to check if any other encoder is touched
     */
    void bindToEncoder(EncoderHandler::Handler& encoder,
                       AnyEncoderTouchedFn anyTouchedExcept);

    // Utility functions for name mapping
    static const char* lengthName(ChokeLength length);
    static const char* onsetName(ChokeOnset onset);

private:
    AudioEffectChoke& m_effect;     // Reference to audio effect (DSP)
    Parameter m_currentParameter;   // Currently selected parameter for editing
    bool m_wasEnabled;              // Previous enabled state (for edge detection)
};
