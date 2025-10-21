#pragma once

#include <stdint.h>
#include <functional>

/**
 * Encoder Menu Module
 *
 * Generic encoder menu handling with parameter cycling and value adjustment.
 * Eliminates code duplication across encoder 1, 3, and 4 handling.
 *
 * FEATURES:
 * - Detent-based turn detection (2 detents = 1 turn)
 * - Touch detection (shows menu when encoder touched)
 * - Display cooldown (returns to default after 2s inactivity)
 * - Button-based parameter cycling (for multi-parameter menus)
 * - Value clamping (prevents overflow at boundaries)
 *
 * USAGE:
 * 1. Create EncoderMenu instance with encoder index
 * 2. Set value change callback
 * 3. Call update() every loop iteration
 * 4. Menu automatically handles display timing and value changes
 */

namespace EncoderMenu {

/**
 * Callback type for value changes
 * @param delta Change in value (+1 for CW, -1 for CCW)
 */
using ValueChangeCallback = std::function<void(int8_t delta)>;

/**
 * Callback type for button presses
 */
using ButtonPressCallback = std::function<void()>;

/**
 * Callback type for display updates (called when encoder touched/released)
 * @param isTouched True when encoder touched, false when cooldown expires
 */
using DisplayUpdateCallback = std::function<void(bool isTouched)>;

/**
 * Encoder Menu Handler
 *
 * Manages a single encoder's menu state, turn detection, and display timing.
 */
class Handler {
public:
    /**
     * Constructor
     * @param encoderIndex Encoder hardware index (0-3)
     */
    explicit Handler(uint8_t encoderIndex);

    /**
     * Update encoder state (call every loop iteration)
     * Processes rotation, button presses, and display cooldown.
     */
    void update();

    /**
     * Set callback for value changes (when encoder rotated)
     */
    void onValueChange(ValueChangeCallback callback);

    /**
     * Set callback for button presses (when encoder button pressed)
     */
    void onButtonPress(ButtonPressCallback callback);

    /**
     * Set callback for display updates (when encoder touched/released)
     */
    void onDisplayUpdate(DisplayUpdateCallback callback);

    /**
     * Check if encoder is currently being touched
     */
    bool isTouched() const { return wasTouched; }

    /**
     * Reset encoder position to zero
     */
    void resetPosition();

private:
    // Encoder hardware index (0-3)
    uint8_t encoderIndex;

    // State tracking
    int32_t lastPosition;        // Last raw encoder position
    int32_t accumulator;         // Accumulated steps since last turn
    bool wasTouched;             // Encoder recently touched
    uint32_t releaseTime;        // Time when encoder was released

    // Callbacks
    ValueChangeCallback valueChangeCallback;
    ButtonPressCallback buttonPressCallback;
    DisplayUpdateCallback displayUpdateCallback;

    // Constants
    static constexpr uint32_t DISPLAY_COOLDOWN_MS = 2000;  // 2s before returning to default
    static constexpr int32_t STEPS_PER_TURN = 8;           // 2 detents = 8 quadrature steps
};

}  // namespace EncoderMenu
