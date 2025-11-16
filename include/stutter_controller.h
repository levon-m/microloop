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
 * - Owns reference to AudioEffectStutter
 * - Manages parameter editing state (ONSET, LENGTH, CAPTURE_START, CAPTURE_END)
 * - Handles FUNC+STUTTER button order detection
 * - Handles free/quantized onset, length, capture start, and capture end modes
 * - Manages LED blinking for armed states
 *
 * USAGE:
 *   AudioEffectStutter stutter;
 *   StutterController controller(stutter);
 *
 *   // In AppLogic:
 *   if (controller.handleButtonPress(cmd)) {
 *       // Command handled by controller
 *   }
 */

#pragma once

#include "effect_controller.h"
#include "audio_stutter.h"
#include "effect_quantization.h"
#include "display_io.h"

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
    explicit StutterController(AudioEffectStutter& effect);

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

    // Utility functions for bitmap/name mapping
    static BitmapID onsetToBitmap(StutterOnset onset);
    static BitmapID lengthToBitmap(StutterLength length);
    static BitmapID captureStartToBitmap(StutterCaptureStart captureStart);
    static BitmapID captureEndToBitmap(StutterCaptureEnd captureEnd);
    static BitmapID stateToBitmap(StutterState state);

    static const char* onsetName(StutterOnset onset);
    static const char* lengthName(StutterLength length);
    static const char* captureStartName(StutterCaptureStart captureStart);
    static const char* captureEndName(StutterCaptureEnd captureEnd);

private:
    AudioEffectStutter& m_effect;   // Reference to audio effect (DSP)
    Parameter m_currentParameter;   // Currently selected parameter for editing

    // Button state tracking for FUNC+STUTTER combo detection
    bool m_funcHeld;                // Is FUNC button currently held?
    bool m_stutterHeld;             // Is STUTTER button currently held?

    // LED blinking state for armed states
    uint32_t m_lastBlinkTime;       // Timestamp of last LED toggle
    bool m_ledBlinkState;           // Current LED blink state (on/off)
    static constexpr uint32_t BLINK_INTERVAL_MS = 250;  // 250ms on/off (4Hz blink)
};
