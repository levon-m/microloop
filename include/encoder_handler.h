#pragma once

#include <stdint.h>
#include <functional>

namespace EncoderHandler {

using ValueChangeCallback = std::function<void(int8_t delta)>;

using ButtonPressCallback = std::function<void()>;

using DisplayUpdateCallback = std::function<void(bool isTouched)>;

class Handler {
public:
    explicit Handler(uint8_t encoderIndex);

    void update();

    void onValueChange(ValueChangeCallback callback);

    void onButtonPress(ButtonPressCallback callback);

    void onDisplayUpdate(DisplayUpdateCallback callback);

    bool isTouched() const;  // Returns true if actively touched OR within cooldown period

    void resetPosition();

private:
    // Encoder hardware index (0-3)
    uint8_t encoderIndex;

    // State tracking
    int32_t lastPosition;        // Last raw encoder position
    int32_t accumulator;         // Accumulated steps since last turn
    bool wasTouched;             // Encoder recently touched
    uint32_t releaseTime;        // Time when encoder was released
    bool lastButtonState;        // Previous button state (for edge detection)

    // Callbacks
    ValueChangeCallback valueChangeCallback;
    ButtonPressCallback buttonPressCallback;
    DisplayUpdateCallback displayUpdateCallback;

    // Constants
    static constexpr uint32_t DISPLAY_COOLDOWN_MS = 2000;  // 2s before returning to default
    static constexpr int32_t STEPS_PER_TURN = 8;           // 2 detents = 8 quadrature steps
};

}
