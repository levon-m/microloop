#pragma once

#include "audio_effect_base.h"
#include "timekeeper.h"
#include <atomic>

enum class ChokeLength : uint8_t {
    FREE = 0,       // Release immediately when button released (default)
    QUANTIZED = 1   // Auto-release after global quantization duration
};

enum class ChokeOnset : uint8_t {
    FREE = 0,       // Engage immediately when button pressed (default)
    QUANTIZED = 1   // Quantize onset to next beat/subdivision
};

class AudioEffectChoke : public AudioEffectBase {
public:
    AudioEffectChoke() : AudioEffectBase(2) {  // Call base with 2 inputs (stereo)
        m_targetGain = 1.0f;      // Start unmuted
        m_currentGain = 1.0f;
        m_isEnabled.store(false, std::memory_order_relaxed);  // Start disabled (unmuted)
        m_lengthMode = ChokeLength::FREE;  // Default: free mode
        m_onsetMode = ChokeOnset::FREE;    // Default: free mode
        m_releaseAtSample = 0;  // No scheduled release
        m_onsetAtSample = 0;    // No scheduled onset
    }

    void enable() override {
        m_targetGain = 0.0f;  // Mute
        m_isEnabled.store(true, std::memory_order_release);
    }

    void disable() override {
        m_targetGain = 1.0f;  // Unmute
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
        return "Choke";
    }

    void setLengthMode(ChokeLength mode) {
        m_lengthMode = mode;
    }

    ChokeLength getLengthMode() const {
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

    void setOnsetMode(ChokeOnset mode) {
        m_onsetMode = mode;
    }

    ChokeOnset getOnsetMode() const {
        return m_onsetMode;
    }

    void engage() {
        enable();  // Forward to new interface
    }

    void releaseChoke() {
        disable();  // Forward to new interface
    }

    bool isChoked() const {
        return isEnabled();  // Forward to new interface
    }

    virtual void update() override {
        uint64_t currentSample = TimeKeeper::getSamplePosition();
        uint64_t blockEndSample = currentSample + AUDIO_BLOCK_SAMPLES;

        // Check for scheduled onset (ISR-accurate quantized onset)
        // Fire if the scheduled sample falls within this audio block [currentSample, blockEndSample)
        if (m_onsetAtSample > 0 && m_onsetAtSample >= currentSample && m_onsetAtSample < blockEndSample) {
            // Time to engage choke (block-accurate - best we can do in ISR)
            m_targetGain = 0.0f;  // Mute
            m_isEnabled.store(true, std::memory_order_release);
            m_onsetAtSample = 0;  // Clear scheduled onset
        }

        // Check for scheduled release (ISR-accurate quantized length)
        // Fire if the scheduled sample falls within this audio block [currentSample, blockEndSample)
        if (m_releaseAtSample > 0 && m_releaseAtSample >= currentSample && m_releaseAtSample < blockEndSample) {
            // Time to auto-release (block-accurate)
            m_targetGain = 1.0f;  // Unmute
            m_isEnabled.store(false, std::memory_order_release);
            m_releaseAtSample = 0;  // Clear scheduled release
        }

        // Receive input blocks (left and right channels)
        audio_block_t* blockL = receiveWritable(0);
        audio_block_t* blockR = receiveWritable(1);

        // Calculate gain increment per sample for smooth fade
        // Fade time: 10ms = 441 samples @ 44.1kHz
        // Over 128-sample block, we traverse: 128/441 of the fade
        const float gainIncrement = (m_targetGain - m_currentGain) / FADE_SAMPLES;

        // Process left channel
        if (blockL) {
            applyGainRamp(blockL->data, AUDIO_BLOCK_SAMPLES, gainIncrement);
            transmit(blockL, 0);
            release(blockL);
        }

        // Process right channel
        if (blockR) {
            applyGainRamp(blockR->data, AUDIO_BLOCK_SAMPLES, gainIncrement);
            transmit(blockR, 1);
            release(blockR);
        }
    }

private:
    inline void applyGainRamp(int16_t* data, size_t numSamples, float gainIncrement) {
        for (size_t i = 0; i < numSamples; i++) {
            // Update current gain (linear interpolation)
            m_currentGain += gainIncrement;

            // Clamp gain to [0.0, 1.0] to prevent overshoot
            if (m_currentGain < 0.0f) m_currentGain = 0.0f;
            if (m_currentGain > 1.0f) m_currentGain = 1.0f;

            // Apply gain to sample
            // Note: int16_t range is -32768 to 32767
            // We multiply by gain, then clamp to prevent overflow
            int32_t sample = static_cast<int32_t>(data[i]) * m_currentGain;

            // Clamp to int16_t range (shouldn't overflow with gain ≤ 1.0, but safe practice)
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;

            data[i] = static_cast<int16_t>(sample);
        }
    }

    // Fade parameters
    static constexpr float FADE_TIME_MS = 3.0f;  // 3ms crossfade (tighter feel for quantization)
    static constexpr float FADE_SAMPLES = (FADE_TIME_MS / 1000.0f) * 44100.0f;  // 132 samples

    // Gain state (modified in audio ISR)
    float m_currentGain;  // Current gain (ramped smoothly)
    float m_targetGain;   // Target gain (0.0 = mute, 1.0 = full volume)

    // Effect state (atomic for lock-free cross-thread access)
    // Note: For choke, enabled=true means muted, enabled=false means unmuted
    std::atomic<bool> m_isEnabled;

    // Choke length mode state
    ChokeLength m_lengthMode;     // FREE or QUANTIZED
    uint64_t m_releaseAtSample;   // Sample position when choke should auto-release (0 = no scheduled release)

    // Choke onset mode state
    ChokeOnset m_onsetMode;       // FREE or QUANTIZED
    uint64_t m_onsetAtSample;     // Sample position when choke should engage (0 = no scheduled onset)
};
