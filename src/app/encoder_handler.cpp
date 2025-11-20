#include "encoder_handler.h"
#include "mcp_io.h"
#include <Arduino.h>

namespace EncoderHandler {

Handler::Handler(uint8_t encoderIndex)
    : encoderIndex(encoderIndex)
    , lastPosition(0)
    , accumulator(0)
    , wasTouched(false)
    , releaseTime(0)
    , valueChangeCallback(nullptr)
    , buttonPressCallback(nullptr)
    , displayUpdateCallback(nullptr)
{
    // Initialize last position from hardware
    lastPosition = McpIO::getPosition(encoderIndex);
}

void Handler::update() {
    // Check for button press (McpIO already handles edge detection and debouncing)
    if (buttonPressCallback && McpIO::getEncoderButton(encoderIndex)) {
        buttonPressCallback();
        // Mark as touched to reset cooldown
        if (!wasTouched) {
            wasTouched = true;
            if (displayUpdateCallback) {
                displayUpdateCallback(true);
            }
        }
        releaseTime = 0;  // Reset cooldown
    }

    // Get current encoder position
    int32_t currentPosition = McpIO::getPosition(encoderIndex);
    int32_t delta = currentPosition - lastPosition;

    // Check if encoder was touched (position changed)
    if (delta != 0) {
        // Encoder is being touched
        if (!wasTouched) {
            wasTouched = true;
            if (displayUpdateCallback) {
                displayUpdateCallback(true);
            }
        }

        // Reset the release timer since encoder is still being touched
        releaseTime = 0;

        // Accumulate steps for turn detection
        accumulator += delta;

        // Calculate turns based on detents (2 detents = 1 turn)
        // Typical encoder: 4 steps per detent, so 8 steps = 2 detents = 1 turn
        int32_t turns = accumulator / STEPS_PER_TURN;

        // Notify callback if we've crossed a turn boundary
        if (turns != 0 && valueChangeCallback) {
            valueChangeCallback(turns);
            // Reset accumulator after callback (callback may decide to keep or clear)
            accumulator = accumulator % STEPS_PER_TURN;  // Keep remainder
        }

        // Always update last position
        lastPosition = currentPosition;
    } else {
        // Encoder not being touched
        if (wasTouched) {
            // Encoder was just released - start cooldown timer
            wasTouched = false;
            releaseTime = millis();
        }
    }

    // Handle display cooldown (return to default after 2 seconds of inactivity)
    if (!wasTouched && releaseTime > 0) {
        uint32_t now = millis();
        if (now - releaseTime >= DISPLAY_COOLDOWN_MS) {
            // Cooldown expired
            releaseTime = 0;  // Clear cooldown
            if (displayUpdateCallback) {
                displayUpdateCallback(false);
            }
        }
    }
}

void Handler::onValueChange(ValueChangeCallback callback) {
    valueChangeCallback = callback;
}

void Handler::onButtonPress(ButtonPressCallback callback) {
    buttonPressCallback = callback;
}

void Handler::onDisplayUpdate(DisplayUpdateCallback callback) {
    displayUpdateCallback = callback;
}

void Handler::resetPosition() {
    lastPosition = McpIO::getPosition(encoderIndex);
    accumulator = 0;
}

bool Handler::isTouched() const {
    // If actively being touched, return true
    if (wasTouched) return true;

    // If within cooldown period, also return true
    if (releaseTime > 0) {
        uint32_t now = millis();
        return (now - releaseTime < DISPLAY_COOLDOWN_MS);
    }

    // Otherwise, not touched
    return false;
}

}
