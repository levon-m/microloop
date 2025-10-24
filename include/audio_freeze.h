#pragma once

#include "audio_effect_base.h"
#include "timekeeper.h"
#include <atomic>
#include <Arduino.h>

enum class FreezeLength : uint8_t {
    FREE = 0,       // Release immediately when button released (default)
    QUANTIZED = 1   // Auto-release after global quantization duration
};

enum class FreezeOnset : uint8_t {
    FREE = 0,       // Engage immediately when button pressed (default)
    QUANTIZED = 1   // Quantize onset to next beat/subdivision
};

class AudioEffectFreeze : public AudioEffectBase {
public:
    AudioEffectFreeze() : AudioEffectBase(2) {  // Call base with 2 inputs (stereo)
        m_writePos = 0;
        m_readPos = 0;
        m_isEnabled.store(false, std::memory_order_relaxed);  // Start disabled (passthrough)
        m_lengthMode = FreezeLength::FREE;  // Default: free mode
        m_onsetMode = FreezeOnset::FREE;    // Default: free mode
        m_releaseAtSample = 0;  // No scheduled release
        m_onsetAtSample = 0;    // No scheduled onset

        // Initialize buffers to silence
        memset(m_freezeBufferL, 0, sizeof(m_freezeBufferL));
        memset(m_freezeBufferR, 0, sizeof(m_freezeBufferR));
    }

    void enable() override {
        // Set read position to current write position
        // This captures the most recent audio in the buffer
        m_readPos = m_writePos;
        m_isEnabled.store(true, std::memory_order_release);
    }

    void disable() override {
        m_isEnabled.store(false, std::memory_order_release);
    }

    void toggle() override {
        if (isEnabled()) {
            disable();
        } else {
            enable();
        }
    }

    bool isEnabled() const override {
        return m_isEnabled.load(std::memory_order_acquire);
    }

    const char* getName() const override {
        return "Freeze";
    }

    void setLengthMode(FreezeLength mode) {
        m_lengthMode = mode;
    }

    FreezeLength getLengthMode() const {
        return m_lengthMode;
    }

    void scheduleRelease(uint64_t releaseSample) {
        m_releaseAtSample = releaseSample;
    }

    void cancelScheduledRelease() {
        m_releaseAtSample = 0;
    }

    void scheduleOnset(uint64_t onsetSample) {
        m_onsetAtSample = onsetSample;
    }

    void cancelScheduledOnset() {
        m_onsetAtSample = 0;
    }

    void setOnsetMode(FreezeOnset mode) {
        m_onsetMode = mode;
    }

    FreezeOnset getOnsetMode() const {
        return m_onsetMode;
    }

    virtual void update() override {
        uint64_t currentSample = TimeKeeper::getSamplePosition();
        uint64_t blockEndSample = currentSample + AUDIO_BLOCK_SAMPLES;

        // Check for scheduled onset (ISR-accurate quantized onset)
        // Fire if the scheduled sample falls within this audio block [currentSample, blockEndSample)
        if (m_onsetAtSample > 0 && m_onsetAtSample >= currentSample && m_onsetAtSample < blockEndSample) {
            // Time to engage freeze (block-accurate - best we can do in ISR)
            m_readPos = m_writePos;  // Capture current buffer position
            m_isEnabled.store(true, std::memory_order_release);
            m_onsetAtSample = 0;  // Clear scheduled onset
        }

        // Check for scheduled release (ISR-accurate quantized length)
        // Fire if the scheduled sample falls within this audio block [currentSample, blockEndSample)
        if (m_releaseAtSample > 0 && m_releaseAtSample >= currentSample && m_releaseAtSample < blockEndSample) {
            // Time to auto-release (block-accurate)
            m_isEnabled.store(false, std::memory_order_release);
            m_releaseAtSample = 0;  // Clear scheduled release
        }

        // Check freeze state
        bool frozen = m_isEnabled.load(std::memory_order_acquire);

        if (!frozen) {
            // PASSTHROUGH MODE: Record to buffer and pass through
            audio_block_t* blockL = receiveWritable(0);
            audio_block_t* blockR = receiveWritable(1);

            if (blockL && blockR) {
                // Write to circular buffer (continuously recording)
                for (size_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
                    m_freezeBufferL[m_writePos] = blockL->data[i];
                    m_freezeBufferR[m_writePos] = blockR->data[i];

                    // Advance write position (circular)
                    m_writePos++;
                    if (m_writePos >= FREEZE_BUFFER_SAMPLES) {
                        m_writePos = 0;
                    }
                }

                // Pass through unmodified
                transmit(blockL, 0);
                transmit(blockR, 1);
            }

            // Release blocks (if allocated)
            if (blockL) release(blockL);
            if (blockR) release(blockR);

        } else {
            // FROZEN MODE: Read from buffer and loop
            audio_block_t* outL = allocate();
            audio_block_t* outR = allocate();

            if (outL && outR) {
                // Read from circular buffer
                for (size_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
                    outL->data[i] = m_freezeBufferL[m_readPos];
                    outR->data[i] = m_freezeBufferR[m_readPos];

                    // Advance read position (circular)
                    m_readPos++;
                    if (m_readPos >= FREEZE_BUFFER_SAMPLES) {
                        m_readPos = 0;  // Loop back to start
                    }
                }

                // Transmit frozen audio
                transmit(outL, 0);
                transmit(outR, 1);
            }

            // Release output blocks
            if (outL) release(outL);
            if (outR) release(outR);

            // Consume and discard input blocks (we're not using them)
            audio_block_t* blockL = receiveReadOnly(0);
            audio_block_t* blockR = receiveReadOnly(1);
            if (blockL) release(blockL);
            if (blockR) release(blockR);
        }
    }

private:
    /**
     * - 3ms:   Very harsh buzz (333 Hz fundamental) - similar to single-block repeat
     * - 10ms:  Medium harshness (100 Hz fundamental)
     * - 25ms:  Balanced (40 Hz fundamental)
     * - 50ms:  Textured freeze (20 Hz fundamental)
     * - 100ms: Loop-like, more musical (10 Hz fundamental)
     * - 200ms: Clearly recognizable frozen phrase (5 Hz fundamental)
     */
    static constexpr uint32_t FREEZE_BUFFER_MS = 3;

    /**
     * Calculate buffer size in samples (compile-time constant)
     *
     * Formula: (milliseconds × 44100 samples/sec) / 1000
     * Example: 50ms = (50 × 44100) / 1000 = 2205 samples
     */
    static constexpr size_t FREEZE_BUFFER_SAMPLES =
        (FREEZE_BUFFER_MS * 44100) / 1000;

    int16_t m_freezeBufferL[FREEZE_BUFFER_SAMPLES];
    int16_t m_freezeBufferR[FREEZE_BUFFER_SAMPLES];

    size_t m_writePos;
    size_t m_readPos;

    std::atomic<bool> m_isEnabled;

    // Freeze length mode state
    FreezeLength m_lengthMode;        // FREE or QUANTIZED
    uint64_t m_releaseAtSample;       // Sample position when freeze should auto-release (0 = no scheduled release)

    // Freeze onset mode state
    FreezeOnset m_onsetMode;          // FREE or QUANTIZED
    uint64_t m_onsetAtSample;         // Sample position when freeze should engage (0 = no scheduled onset)
};
