/**
 * audio_freeze.h - Real-time audio freeze effect with circular buffer
 *
 * PURPOSE:
 * Provides live-performance audio freeze functionality that captures and loops
 * a segment of incoming audio, creating a harsh, metallic "frozen in place" sound.
 * Inspired by Windows bluescreen freeze effect.
 *
 * DESIGN:
 * - Stereo processor (2 inputs, 2 outputs)
 * - Circular buffer captures audio continuously during passthrough
 * - When frozen, loops through captured buffer indefinitely
 * - Configurable buffer duration via compile-time constant
 * - Lock-free control (atomic bool for thread safety)
 * - Zero-copy passthrough when not frozen
 * - Inherits from AudioEffectBase (multi-effect architecture)
 *
 * USAGE IN AUDIO GRAPH:
 *   AudioInputI2S i2s_in;
 *   AudioTimeKeeper timekeeper;
 *   AudioEffectFreeze freeze;      // Insert here
 *   AudioEffectChoke choke;
 *   AudioOutputI2S i2s_out;
 *
 *   AudioConnection c1(i2s_in, 0, timekeeper, 0);
 *   AudioConnection c2(i2s_in, 1, timekeeper, 1);
 *   AudioConnection c3(timekeeper, 0, freeze, 0);   // Through freeze
 *   AudioConnection c4(timekeeper, 1, freeze, 1);
 *   AudioConnection c5(freeze, 0, choke, 0);        // Then choke
 *   AudioConnection c6(freeze, 1, choke, 1);
 *   AudioConnection c7(choke, 0, i2s_out, 0);
 *   AudioConnection c8(choke, 1, i2s_out, 1);
 *
 * CONTROL API (AudioEffectBase interface):
 *   freeze.enable();     // Start freeze (loop buffer)
 *   freeze.disable();    // Stop freeze (passthrough)
 *   freeze.toggle();     // Toggle freeze on/off
 *   freeze.isEnabled();  // Query current state
 *
 * BUFFER TUNING:
 * - Edit FREEZE_BUFFER_MS to change freeze duration
 * - 3ms:   Very harsh buzz (similar to single-block repeat)
 * - 10ms:  Medium harshness
 * - 50ms:  Textured freeze (default, good balance)
 * - 100ms: Loop-like, more musical
 * - 200ms: Clearly recognizable frozen phrase
 *
 * PERFORMANCE:
 * - Passthrough: ~300 CPU cycles (circular buffer write)
 * - Frozen: ~300 cycles per block (circular buffer read)
 * - Total: <0.03% of 600MHz CPU
 * - RAM: Scales with buffer size (50ms = ~8.8 KB)
 *
 * THREAD SAFETY:
 * - enable()/disable()/toggle() can be called from any thread
 * - Uses atomic operations for lock-free state updates
 * - Audio ISR reads state atomically, applies freeze logic
 */

#pragma once

#include "audio_effect_base.h"
#include "timekeeper.h"
#include <atomic>
#include <Arduino.h>

/**
 * Freeze length mode
 * Controls how freeze release is handled
 */
enum class FreezeLength : uint8_t {
    FREE = 0,       // Release immediately when button released (default)
    QUANTIZED = 1   // Auto-release after global quantization duration
};

/**
 * Freeze onset mode
 * Controls how freeze onset (engage timing) is handled
 */
enum class FreezeOnset : uint8_t {
    FREE = 0,       // Engage immediately when button pressed (default)
    QUANTIZED = 1   // Quantize onset to next beat/subdivision
};

class AudioEffectFreeze : public AudioEffectBase {
public:
    /**
     * Constructor
     * Creates stereo freeze effect (2 inputs, 2 outputs)
     */
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

    // ========================================================================
    // AUDIOEFFECTBASE INTERFACE
    // ========================================================================

    /**
     * Enable effect (start freeze - loop captured buffer)
     *
     * Thread-safe: Can be called from any thread
     * Effect: Stops passthrough, begins looping through captured buffer
     *
     * Note: For freeze, "enabled" means "frozen" (audio is looping)
     */
    void enable() override {
        // Set read position to current write position
        // This captures the most recent audio in the buffer
        m_readPos = m_writePos;
        m_isEnabled.store(true, std::memory_order_release);
    }

    /**
     * Disable effect (stop freeze - resume passthrough)
     *
     * Thread-safe: Can be called from any thread
     * Effect: Stops looping, resumes normal passthrough with buffer recording
     *
     * Note: For freeze, "disabled" means "passthrough" (normal audio flow)
     */
    void disable() override {
        m_isEnabled.store(false, std::memory_order_release);
    }

    /**
     * Toggle effect on/off
     *
     * Thread-safe: Can be called from any thread
     * Effect: If frozen, unfreeze; if passthrough, freeze
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
     * @return true if frozen (looping), false if passthrough
     */
    bool isEnabled() const override {
        return m_isEnabled.load(std::memory_order_acquire);
    }

    /**
     * Get effect name
     *
     * @return "Freeze"
     */
    const char* getName() const override {
        return "Freeze";
    }

    // ========================================================================
    // FREEZE LENGTH MODE API
    // ========================================================================

    /**
     * Set freeze length mode
     *
     * Thread-safe: Can be called from any thread
     *
     * @param mode FREE (default) or QUANTIZED
     */
    void setLengthMode(FreezeLength mode) {
        m_lengthMode = mode;
    }

    /**
     * Get freeze length mode
     *
     * Thread-safe: Can be called from any thread
     *
     * @return Current length mode
     */
    FreezeLength getLengthMode() const {
        return m_lengthMode;
    }

    /**
     * Schedule quantized auto-release
     *
     * Thread-safe: Can be called from any thread
     * Used internally when lengthMode is QUANTIZED
     *
     * @param releaseSample Sample position when freeze should auto-release
     */
    void scheduleRelease(uint64_t releaseSample) {
        m_releaseAtSample = releaseSample;
    }

    /**
     * Cancel scheduled auto-release
     *
     * Thread-safe: Can be called from any thread
     * Used when user releases button before scheduled release fires
     */
    void cancelScheduledRelease() {
        m_releaseAtSample = 0;
    }

    // ========================================================================
    // FREEZE ONSET MODE API
    // ========================================================================

    /**
     * Schedule quantized onset (ISR-accurate)
     *
     * Thread-safe: Can be called from any thread
     * Used internally when onsetMode is QUANTIZED
     *
     * DESIGN:
     *   - Mirrors scheduleRelease() for consistency
     *   - ISR checks m_onsetAtSample every update() call
     *   - When current sample >= onset sample, calls enable()
     *   - Sample-accurate (no app-thread polling jitter)
     *
     * @param onsetSample Sample position when freeze should engage
     */
    void scheduleOnset(uint64_t onsetSample) {
        m_onsetAtSample = onsetSample;
    }

    /**
     * Cancel scheduled onset
     *
     * Thread-safe: Can be called from any thread
     * Used when user releases button before scheduled onset fires
     */
    void cancelScheduledOnset() {
        m_onsetAtSample = 0;
    }

    /**
     * Set freeze onset mode
     *
     * Thread-safe: Can be called from any thread
     *
     * @param mode FREE (default) or QUANTIZED
     */
    void setOnsetMode(FreezeOnset mode) {
        m_onsetMode = mode;
    }

    /**
     * Get freeze onset mode
     *
     * Thread-safe: Can be called from any thread
     *
     * @return Current onset mode
     */
    FreezeOnset getOnsetMode() const {
        return m_onsetMode;
    }

    /**
     * Audio ISR callback (called every 128 samples)
     *
     * CRITICAL PATH:
     * - Process audio with circular buffer logic
     * - Check for scheduled onset (quantized onset mode)
     * - Check for scheduled release (quantized length mode)
     * - Keep processing minimal (<1ms budget)
     */
    virtual void update() override {
        uint64_t currentSample = TimeKeeper::getSamplePosition();

        // Check for scheduled onset (ISR-accurate quantized onset)
        if (m_onsetAtSample > 0 && currentSample >= m_onsetAtSample) {
            // Time to engage freeze (sample-accurate!)
            m_readPos = m_writePos;  // Capture current buffer position
            m_isEnabled.store(true, std::memory_order_release);
            m_onsetAtSample = 0;  // Clear scheduled onset
        }

        // Check for scheduled release (ISR-accurate quantized length)
        if (m_releaseAtSample > 0 && currentSample >= m_releaseAtSample) {
            // Time to auto-release
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
    // ========================================================================
    // BUFFER CONFIGURATION
    // ========================================================================

    /**
     * Freeze buffer duration in milliseconds
     *
     * TUNING GUIDE:
     * - 3ms:   Very harsh buzz (333 Hz fundamental) - similar to single-block repeat
     * - 10ms:  Medium harshness (100 Hz fundamental)
     * - 25ms:  Balanced (40 Hz fundamental)
     * - 50ms:  Textured freeze (20 Hz fundamental) - RECOMMENDED DEFAULT
     * - 100ms: Loop-like, more musical (10 Hz fundamental)
     * - 200ms: Clearly recognizable frozen phrase (5 Hz fundamental)
     *
     * RAM USAGE:
     * - 3ms:   528 bytes
     * - 10ms:  1,764 bytes (~1.7 KB)
     * - 50ms:  8,820 bytes (~8.6 KB)
     * - 100ms: 17,640 bytes (~17.2 KB)
     * - 200ms: 35,280 bytes (~34.5 KB)
     *
     * CHANGE THIS VALUE TO TUNE FREEZE CHARACTER
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

    // ========================================================================
    // CIRCULAR BUFFER STORAGE
    // ========================================================================

    /**
     * Circular buffers for stereo audio capture
     *
     * Size: FREEZE_BUFFER_SAMPLES samples per channel
     * Format: 16-bit signed integers (int16_t)
     * Total RAM: FREEZE_BUFFER_SAMPLES × 2 channels × 2 bytes
     */
    int16_t m_freezeBufferL[FREEZE_BUFFER_SAMPLES];
    int16_t m_freezeBufferR[FREEZE_BUFFER_SAMPLES];

    /**
     * Buffer position tracking
     *
     * m_writePos: Where to write next incoming sample (passthrough mode)
     * m_readPos:  Where to read from during freeze (frozen mode)
     *
     * Both positions wrap around circularly (0 to FREEZE_BUFFER_SAMPLES-1)
     */
    size_t m_writePos;
    size_t m_readPos;

    /**
     * Effect state (atomic for lock-free cross-thread access)
     *
     * true = frozen (looping buffer)
     * false = passthrough (recording to buffer)
     */
    std::atomic<bool> m_isEnabled;

    // Freeze length mode state
    FreezeLength m_lengthMode;        // FREE or QUANTIZED
    uint64_t m_releaseAtSample;       // Sample position when freeze should auto-release (0 = no scheduled release)

    // Freeze onset mode state
    FreezeOnset m_onsetMode;          // FREE or QUANTIZED
    uint64_t m_onsetAtSample;         // Sample position when freeze should engage (0 = no scheduled onset)
};
