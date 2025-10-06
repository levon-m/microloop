/**
 * audio_timekeeper.h - Audio object that tracks sample position for TimeKeeper
 *
 * PURPOSE:
 * Bridge between Teensy Audio Library and TimeKeeper. This audio object sits
 * in the audio graph and increments TimeKeeper's sample counter on every
 * audio block processed by the ISR.
 *
 * DESIGN:
 * - Inherits from AudioStream (Teensy Audio Library base class)
 * - Passes audio through unchanged (stereo passthrough)
 * - Increments TimeKeeper sample counter in update() callback
 * - update() is called by audio ISR every 128 samples (~2.9ms @ 44.1kHz)
 *
 * USAGE:
 *   AudioInputI2S i2s_in;
 *   AudioTimeKeeper timekeeper;
 *   AudioOutputI2S i2s_out;
 *
 *   AudioConnection c1(i2s_in, 0, timekeeper, 0);    // Left in
 *   AudioConnection c2(i2s_in, 1, timekeeper, 1);    // Right in
 *   AudioConnection c3(timekeeper, 0, i2s_out, 0);   // Left out
 *   AudioConnection c4(timekeeper, 1, i2s_out, 1);   // Right out
 *
 * PERFORMANCE:
 * - Overhead: ~50 CPU cycles (atomic increment + trace)
 * - Negligible compared to audio processing (thousands of cycles)
 */

#pragma once

#include <Audio.h>
#include "timekeeper.h"
#include "trace.h"

class AudioTimeKeeper : public AudioStream {
public:
    /**
     * Constructor
     *
     * Creates a stereo passthrough object (2 inputs, 2 outputs)
     */
    AudioTimeKeeper() : AudioStream(2, inputQueueArray) {}

    /**
     * Audio ISR callback (called every 128 samples)
     *
     * CRITICAL PATH:
     * - Keep processing minimal (just passthrough + sample counter)
     * - No Serial.print, no malloc, no blocking
     * - Total budget: <1ms (ideally <100µs)
     *
     * WHAT IT DOES:
     * 1. Increment TimeKeeper sample counter
     * 2. Pass audio through (copy input blocks to output)
     * 3. Release input blocks
     */
    virtual void update() override {
        // Increment sample counter (lock-free atomic operation)
        TimeKeeper::incrementSamples(AUDIO_BLOCK_SAMPLES);

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
