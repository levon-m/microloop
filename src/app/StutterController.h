/**
 * stutter_controller.h - Controller for stutter effect
 *
 * PURPOSE:
 * Manages stutter effect behavior, including capture mode, quantization modes,
 * button handling (FUNC+STUTTER combo detection), and visual feedback.
 * Decouples effect logic from DSP.
 *
 * DESIGN:
 * - Implements IEffectController interface
 * - Owns reference to StutterAudio
 * - Manages parameter editing state (ONSET, LENGTH, CAPTURE_START, CAPTURE_END)
 * - Handles FUNC+STUTTER button order detection
 * - Handles free/quantized onset, length, capture start, and capture end modes
 * - Manages LED blinking for armed states
 *
 * USAGE:
 *   StutterAudio stutter;
 *   StutterController controller(stutter);
 *
 *   // In AppLogic:
 *   if (controller.handleButtonPress(cmd)) {
 *       // Command handled by controller
 *   }
 */

#pragma once

#include "IEffectController.h"
#include "StutterAudio.h"
#include "EffectQuantization.h"
#include "Ssd1306Display.h"

// Forward declaration
namespace EncoderHandler {
    class Handler;
}

// Callback type for checking if any other encoder is touched
typedef bool (*AnyEncoderTouchedFn)(const EncoderHandler::Handler* ignore);

// Callback type for capture complete notification
typedef void (*CaptureCompleteCallback)();

/**
 * Stutter effect controller
 *
 * Handles button presses (including FUNC+STUTTER combo), quantization logic,
 * and visual feedback for the stutter effect.
 */
class StutterController : public IEffectController {
public:
    /**
     * Parameter selection for encoder editing
     * Cycle order: ONSET → LENGTH → CAPTURE_START → CAPTURE_END
     */
    enum class Parameter : uint8_t {
        ONSET = 0,          // Playback onset timing (Free, Quantized)
        LENGTH = 1,         // Playback length (Free, Quantized)
        CAPTURE_START = 2,  // Capture start timing (Free, Quantized)
        CAPTURE_END = 3     // Capture end timing (Free, Quantized)
    };

    /**
     * Constructor
     *
     * @param effect Reference to the stutter audio effect
     */
    explicit StutterController(StutterAudio& effect);

    // IEffectController interface implementation
    bool handleButtonPress(const Command& cmd) override;
    bool handleButtonRelease(const Command& cmd) override;
    void updateVisualFeedback() override;
    EffectID getEffectID() const override { return EffectID::STUTTER; }

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

    /**
     * Set callback for capture complete notification
     * Called when a new loop capture completes (transitions to IDLE_WITH_LOOP)
     * Used by PresetController to deselect preset when user captures new loop
     *
     * @param callback Function to call when capture completes
     */
    void setCaptureCompleteCallback(CaptureCompleteCallback callback) {
        m_captureCompleteCallback = callback;
    }

    // Utility functions for bitmap/name mapping
    // TODO: Re-enable when stutter parameter bitmaps are added
    // static BitmapID onsetToBitmap(StutterOnset onset);
    // static BitmapID lengthToBitmap(StutterLength length);
    // static BitmapID captureStartToBitmap(StutterCaptureStart captureStart);
    // static BitmapID captureEndToBitmap(StutterCaptureEnd captureEnd);
    static BitmapID stateToBitmap(StutterState state);

    static const char* onsetName(StutterOnset onset);
    static const char* lengthName(StutterLength length);
    static const char* captureStartName(StutterCaptureStart captureStart);
    static const char* captureEndName(StutterCaptureEnd captureEnd);

private:
    StutterAudio& m_effect;   // Reference to audio effect (DSP)
    Parameter m_currentParameter;   // Currently selected parameter for editing

    // Button state tracking for FUNC+STUTTER combo detection
    bool m_funcHeld;                // Is FUNC button currently held?
    bool m_stutterHeld;             // Is STUTTER button currently held?

    // Effect state tracking for edge detection
    bool m_wasEnabled;              // Previous enabled state (for edge detection)

    // Capture complete callback (for PresetController notification)
    CaptureCompleteCallback m_captureCompleteCallback;

    // Track previous state for capture complete detection
    StutterState m_lastState;

    // Track if we've been through a capture phase (for deferred callback)
    // Set true when entering CAPTURING, cleared when callback fires or on IDLE_NO_LOOP
    bool m_captureInProgress;
};
