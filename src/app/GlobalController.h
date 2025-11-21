/**
 * GlobalController.h - Controller for global system parameters
 *
 * PURPOSE:
 * Manages global system parameters (quantization grid, etc.) with encoder
 * interaction. Follows the same encoder binding pattern as effect controllers
 * for consistency.
 *
 * DESIGN:
 * - Does NOT implement IEffectController (not tied to button commands)
 * - Manages parameter editing state (QUANTIZATION, future: MASTER_VOLUME, etc.)
 * - Binds to encoder for parameter cycling and adjustment
 * - Uses "GLOBAL->Parameter" display format
 *
 * USAGE:
 *   GlobalController controller;
 *   controller.bindToEncoder(*encoder4, anyEncoderTouchedExcept);
 */

#pragma once

#include "EffectQuantization.h"
#include "OledIO.h"

// Forward declaration
namespace EncoderHandler {
    class Handler;
}

// Callback type for checking if any other encoder is touched
typedef bool (*AnyEncoderTouchedFn)(const EncoderHandler::Handler* ignore);

/**
 * Global system parameters controller
 *
 * Handles encoder input for global parameters (quantization, etc.)
 */
class GlobalController {
public:
    /**
     * Parameter selection for encoder editing
     */
    enum class Parameter : uint8_t {
        QUANTIZATION = 0  // Global quantization grid (1/32, 1/16, 1/8, 1/4)
        // Future parameters can be added here:
        // MASTER_VOLUME = 1,
        // TEMPO_MULTIPLIER = 2,
        // SWING = 3,
        // etc.
    };

    /**
     * Constructor
     */
    GlobalController();

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

    // Utility function for parameter name mapping
    static const char* parameterName(Parameter param);

private:
    Parameter m_currentParameter;  // Currently selected parameter for editing
};
