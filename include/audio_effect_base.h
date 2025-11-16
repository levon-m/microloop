#pragma once

#include <Audio.h>

class AudioEffectBase : public AudioStream {
public:
    AudioEffectBase(uint8_t numInputs)
        : AudioStream(numInputs, inputQueueArray) {}

    virtual ~AudioEffectBase() = default;

    virtual void enable() = 0;

    virtual void disable() = 0;

    virtual void toggle() = 0;

    virtual bool isEnabled() const = 0;

    virtual const char* getName() const = 0;

    virtual void setParameter(uint8_t paramIndex, float value) {
        // Default: no parameters
        (void)paramIndex;  // Suppress unused warning
        (void)value;
    }

    virtual float getParameter(uint8_t paramIndex) const {
        // Default: no parameters
        (void)paramIndex;  // Suppress unused warning
        return 0.0f;
    }

protected:
    audio_block_t* inputQueueArray[2];
};
