#pragma once

#include <Audio.h>
#include "Timebase.h"
#include "Trace.h"

class TimebaseAudio : public AudioStream {
public:
    TimebaseAudio() : AudioStream(2, inputQueueArray) {}

    virtual void update() override {
        // Increment sample counter (lock-free atomic operation)
        Timebase::incrementSamples(AUDIO_BLOCK_SAMPLES);

        // Optional: Trace audio callback (disabled by default - too noisy)
        // TRACE(TRACE_AUDIO_CALLBACK);

        // Receive input blocks (left and right channels)
        audio_block_t* blockL = receiveReadOnly(0);  // Left input
        audio_block_t* blockR = receiveReadOnly(1);  // Right input

        // Pass through to outputs (copy pointers, not data - zero-copy)
        if (blockL) {
            transmit(blockL, 0);  // Left output
            release(blockL);
        }

        if (blockR) {
            transmit(blockR, 1);  // Right output
            release(blockR);
        }
    }

private:
    audio_block_t* inputQueueArray[2];  // Input queue storage (required by AudioStream)
};
