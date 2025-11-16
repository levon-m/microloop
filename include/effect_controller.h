/**
 * effect_controller.h - Abstract interface for effect controllers
 *
 * PURPOSE:
 * Defines the contract for all effect controllers. Controllers handle the
 * business logic for effects, including button presses, quantization modes,
 * and visual feedback. They decouple the effect logic from the audio DSP.
 *
 * DESIGN:
 * - Pure abstract interface (no implementation)
 * - Controllers own references to their audio effects
 * - Command pattern integration (handle button press/release)
 * - Visual feedback management (LEDs, display)
 *
 * PATTERN:
 * This follows the Controller pattern (similar to MVC, but for embedded):
 * - Model: AudioEffectBase (audio DSP)
 * - View: DisplayManager + InputIO (LEDs, display)
 * - Controller: IEffectController implementations (business logic)
 *
 * USAGE:
 *   class ChokeController : public IEffectController {
 *   public:
 *       ChokeController(AudioEffectChoke& effect) : m_effect(effect) {}
 *       bool handleButtonPress(const Command& cmd) override { ... }
 *       // ... implement other methods
 *   private:
 *       AudioEffectChoke& m_effect;
 *   };
 */

#pragma once

#include "command.h"  // For Command struct

/**
 * Abstract interface for effect controllers
 *
 * Controllers manage the lifecycle and behavior of audio effects,
 * including button handling, quantization logic, and visual feedback.
 */
class IEffectController {
public:
    /**
     * Virtual destructor (required for polymorphic deletion)
     */
    virtual ~IEffectController() = default;

    /**
     * Handle button press command
     *
     * Called when a button is pressed for this effect. The controller
     * can intercept the command and apply custom logic (e.g., quantization).
     *
     * @param cmd Command to handle (should target this effect)
     * @return true if command was handled (don't pass to EffectManager),
     *         false if command should be handled by default logic
     */
    virtual bool handleButtonPress(const Command& cmd) = 0;

    /**
     * Handle button release command
     *
     * Called when a button is released for this effect. The controller
     * can intercept the command and apply custom logic (e.g., cancel
     * scheduled onset).
     *
     * @param cmd Command to handle (should target this effect)
     * @return true if command was handled (don't pass to EffectManager),
     *         false if command should be handled by default logic
     */
    virtual bool handleButtonRelease(const Command& cmd) = 0;

    /**
     * Update visual feedback (LEDs, display)
     *
     * Called periodically from AppLogic thread to update LEDs and
     * display based on effect state. Handles edge detection for
     * quantized onset/release.
     */
    virtual void updateVisualFeedback() = 0;

    /**
     * Get the effect ID that this controller manages
     *
     * @return EffectID that this controller is responsible for
     */
    virtual EffectID getEffectID() const = 0;
};
