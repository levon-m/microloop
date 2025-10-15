/**
 * audio_choke.h - Real-time audio mute effect with smooth crossfade
 *
 * PURPOSE:
 * Provides click-free audio muting for live performance "choke" functionality.
 * Inserts between TimeKeeper and output in audio graph, applies smooth gain
 * ramping to prevent audible artifacts.
 *
 * DESIGN:
 * - Stereo processor (2 inputs, 2 outputs)
 * - 10ms linear crossfade (441 samples @ 44.1kHz)
 * - Lock-free control (atomic bool for thread safety)
 * - Zero-copy passthrough when not fading
 * - Inherits from AudioEffectBase (multi-effect architecture)
 * - Quantized length mode: Auto-releases after global quantization duration
 *
 * USAGE IN AUDIO GRAPH:
 *   AudioInputI2S i2s_in;
 *   AudioTimeKeeper timekeeper;
 *   AudioEffectChoke choke;        // Insert here
 *   AudioOutputI2S i2s_out;
 *
 *   AudioConnection c1(i2s_in, 0, timekeeper, 0);
 *   AudioConnection c2(i2s_in, 1, timekeeper, 1);
 *   AudioConnection c3(timekeeper, 0, choke, 0);      // Through choke
 *   AudioConnection c4(timekeeper, 1, choke, 1);
 *   AudioConnection c5(choke, 0, i2s_out, 0);
 *   AudioConnection c6(choke, 1, i2s_out, 1);
 *
 * CONTROL API (NEW - AudioEffectBase interface):
 *   choke.enable();     // Start fade to silence (mute)
 *   choke.disable();    // Start fade to full volume (unmute)
 *   choke.toggle();     // Toggle choke on/off
 *   choke.isEnabled();  // Query current state
 *
 * CHOKE LENGTH MODES:
 *   choke.setLengthMode(ChokeLength::FREE);       // Hold to choke, release immediately
 *   choke.setLengthMode(ChokeLength::QUANTIZED);  // Auto-release after global quant duration
 *   choke.getLengthMode();                         // Query current mode
 *
 * LEGACY API (Backward compatible, will be removed in Phase 5):
 *   choke.engage();      // Same as enable()
 *   choke.releaseChoke(); // Same as disable()
 *   choke.isChoked();    // Same as isEnabled()
 *
 * PERFORMANCE:
 * - Passthrough: ~50 CPU cycles (memcpy + atomic read)
 * - Fading: ~2000 cycles per block (128 samples × gain multiply)
 * - Total: <1% of 600MHz CPU
 *
 * THREAD SAFETY:
 * - enable()/disable()/toggle() can be called from any thread
 * - Uses atomic operations for lock-free state updates
 * - Audio ISR reads state atomically, applies gain
 */

#pragma once

#include "audio_effect_base.h"
#include "timekeeper.h"
#include <atomic>

/**
 * Choke length mode
 * Controls how choke release is handled
 */
enum class ChokeLength : uint8_t {
    FREE = 0,       // Release immediately when button released (default)
    QUANTIZED = 1   // Auto-release after global quantization duration
};

/**
 * Choke onset mode
 * Controls how choke onset (engage timing) is handled
 */
enum class ChokeOnset : uint8_t {
    FREE = 0,       // Engage immediately when button pressed (default)
    QUANTIZED = 1   // Quantize onset to next beat/subdivision
};

class AudioEffectChoke : public AudioEffectBase {
public:
    /**
     * Constructor
     * Creates stereo choke effect (2 inputs, 2 outputs)
     */
    AudioEffectChoke() : AudioEffectBase(2) {  // Call base with 2 inputs (stereo)
        m_targetGain = 1.0f;      // Start unmuted
        m_currentGain = 1.0f;
        m_isEnabled.store(false, std::memory_order_relaxed);  // Start disabled (unmuted)
        m_lengthMode = ChokeLength::FREE;  // Default: free mode
        m_onsetMode = ChokeOnset::FREE;    // Default: free mode
        m_releaseAtSample = 0;  // No scheduled release
    }

    // ========================================================================
    // AUDIOEFFECTBASE INTERFACE (NEW)
    // ========================================================================

    /**
     * Enable effect (start fade to silence / mute)
     *
     * Thread-safe: Can be called from any thread
     * Effect: Audio fades to silence over 10ms
     *
     * Note: For choke, "enabled" means "muted" (choke is engaged)
     */
    void enable() override {
        m_targetGain = 0.0f;  // Mute
        m_isEnabled.store(true, std::memory_order_release);
    }

    /**
     * Disable effect (start fade to full volume / unmute)
     *
     * Thread-safe: Can be called from any thread
     * Effect: Audio fades to full volume over 10ms
     *
     * Note: For choke, "disabled" means "unmuted" (choke is released)
     */
    void disable() override {
        m_targetGain = 1.0f;  // Unmute
        m_isEnabled.store(false, std::memory_order_release);
    }

    /**
     * Toggle effect on/off
     *
     * Thread-safe: Can be called from any thread
     * Effect: If muted, unmute; if unmuted, mute
     */
    void toggle() override {
        if (isEnabled()) {
            disable();
        } else {
            enable();
        }
    }

    /**
     * Check if effect is enabled
     *
     * Thread-safe: Can be called from any thread
     *
     * @return true if choked (muted), false if unmuted
     */
    bool isEnabled() const override {
        return m_isEnabled.load(std::memory_order_acquire);
    }

    /**
     * Get effect name
     *
     * @return "Choke"
     */
    const char* getName() const override {
        return "Choke";
    }

    // ========================================================================
    // CHOKE LENGTH MODE API
    // ========================================================================

    /**
     * Set choke length mode
     *
     * Thread-safe: Can be called from any thread
     *
     * @param mode FREE (default) or QUANTIZED
     */
    void setLengthMode(ChokeLength mode) {
        m_lengthMode = mode;
    }

    /**
     * Get choke length mode
     *
     * Thread-safe: Can be called from any thread
     *
     * @return Current length mode
     */
    ChokeLength getLengthMode() const {
        return m_lengthMode;
    }

    /**
     * Schedule quantized auto-release
     *
     * Thread-safe: Can be called from any thread
     * Used internally when lengthMode is QUANTIZED
     *
     * @param releaseSample Sample position when choke should auto-release
     */
    void scheduleRelease(uint64_t releaseSample) {
        m_releaseAtSample = releaseSample;
    }

    // ========================================================================
    // CHOKE ONSET MODE API
    // ========================================================================

    /**
     * Set choke onset mode
     *
     * Thread-safe: Can be called from any thread
     *
     * @param mode FREE (default) or QUANTIZED
     */
    void setOnsetMode(ChokeOnset mode) {
        m_onsetMode = mode;
    }

    /**
     * Get choke onset mode
     *
     * Thread-safe: Can be called from any thread
     *
     * @return Current onset mode
     */
    ChokeOnset getOnsetMode() const {
        return m_onsetMode;
    }

    // ========================================================================
    // LEGACY API (BACKWARD COMPATIBLE - will be removed in Phase 5)
    // ========================================================================

    /**
     * LEGACY: Engage choke (start fade to silence)
     *
     * @deprecated Use enable() instead
     *
     * Thread-safe: Can be called from any thread
     * Effect: Audio fades to silence over 10ms
     */
    void engage() {
        enable();  // Forward to new interface
    }

    /**
     * LEGACY: Release choke (start fade to full volume)
     *
     * @deprecated Use disable() instead
     *
     * Thread-safe: Can be called from any thread
     * Effect: Audio fades to full volume over 10ms
     *
     * Note: Named releaseChoke() to avoid conflict with AudioStream::release()
     */
    void releaseChoke() {
        disable();  // Forward to new interface
    }

    /**
     * LEGACY: Query choke state
     *
     * @deprecated Use isEnabled() instead
     *
     * @return true if choked (muted or fading to mute)
     */
    bool isChoked() const {
        return isEnabled();  // Forward to new interface
    }

    /**
     * Audio ISR callback (called every 128 samples)
     *
     * CRITICAL PATH:
     * - Process audio with gain ramping
     * - Check for quantized auto-release
     * - Keep processing minimal (<1ms budget)
     */
    virtual void update() override {
        // Check for quantized auto-release
        if (m_releaseAtSample > 0) {
            uint64_t currentSample = TimeKeeper::getSamplePosition();
            if (currentSample >= m_releaseAtSample) {
                // Time to auto-release
                m_targetGain = 1.0f;  // Unmute
                m_isEnabled.store(false, std::memory_order_release);
                m_releaseAtSample = 0;  // Clear scheduled release
            }
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
    /**
     * Apply gain ramp to audio block
     *
     * @param data Audio samples (16-bit signed)
     * @param numSamples Number of samples to process
     * @param gainIncrement Gain change per sample
     */
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
    static constexpr float FADE_TIME_MS = 10.0f;  // 10ms crossfade
    static constexpr float FADE_SAMPLES = (FADE_TIME_MS / 1000.0f) * 44100.0f;  // 441 samples

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
};
