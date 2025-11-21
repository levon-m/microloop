#pragma once

#include "IEffectAudio.h"
#include "Timebase.h"
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

class FreezeAudio : public IEffectAudio {
public:
    FreezeAudio();

    void enable() override;
    void disable() override;
    void toggle() override;
    bool isEnabled() const override;
    const char* getName() const override;

    void setLengthMode(FreezeLength mode) { m_lengthMode = mode; }
    FreezeLength getLengthMode() const { return m_lengthMode; }

    void scheduleRelease(uint64_t releaseSample) { m_releaseAtSample = releaseSample; }

    void scheduleOnset(uint64_t onsetSample) { m_onsetAtSample = onsetSample; }
    void cancelScheduledOnset() { m_onsetAtSample = 0; }

    void setOnsetMode(FreezeOnset mode) { m_onsetMode = mode; }
    FreezeOnset getOnsetMode() const { return m_onsetMode; }

    virtual void update() override;

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
    static constexpr size_t FREEZE_BUFFER_SAMPLES = (FREEZE_BUFFER_MS * Timebase::SAMPLE_RATE) / 1000;

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
