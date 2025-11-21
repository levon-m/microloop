#pragma once

#include "IEffectAudio.h"
#include "Timebase.h"
#include <atomic>

enum class ChokeLength : uint8_t {
    FREE = 0,       // Release immediately when button released (default)
    QUANTIZED = 1   // Auto-release after global quantization duration
};

enum class ChokeOnset : uint8_t {
    FREE = 0,       // Engage immediately when button pressed (default)
    QUANTIZED = 1   // Quantize onset to next beat/subdivision
};

/**
 * Choke State Machine
 *
 * State transitions:
 * - IDLE: Not active, audio passes through unmuted
 * - ARMED: Button pressed with quantized onset, waiting for grid boundary (LED: YELLOW)
 * - ACTIVE: Choke engaged, audio muted (LED: WHITE)
 */
enum class ChokeState : uint8_t {
    IDLE = 0,    // Not active (LED: OFF)
    ARMED = 1,   // Waiting for quantized onset (LED: YELLOW blinking)
    ACTIVE = 2   // Choke engaged, audio muted (LED: WHITE solid)
};

class ChokeAudio : public IEffectAudio {
public:
    ChokeAudio();

    void enable() override;
    void disable() override;
    void toggle() override;
    bool isEnabled() const override;
    const char* getName() const override;

    // ========== STATE MACHINE ==========

    /**
     * Get current state
     */
    ChokeState getState() const { return m_state; }

    void setLengthMode(ChokeLength mode) { m_lengthMode = mode; }
    ChokeLength getLengthMode() const { return m_lengthMode; }

    void scheduleRelease(uint64_t releaseSample) { m_releaseAtSample = releaseSample; }
    void cancelScheduledRelease() { m_releaseAtSample = 0; }

    void scheduleOnset(uint64_t onsetSample) {
        m_onsetAtSample = onsetSample;
        m_state.store(ChokeState::ARMED, std::memory_order_release);  // Transition to ARMED
    }

    void cancelScheduledOnset() {
        m_onsetAtSample = 0;
        m_state.store(ChokeState::IDLE, std::memory_order_release);  // Transition back to IDLE
    }

    void setOnsetMode(ChokeOnset mode) { m_onsetMode = mode; }
    ChokeOnset getOnsetMode() const { return m_onsetMode; }

    // Legacy interface (for backwards compatibility)
    void engage() { enable(); }
    void releaseChoke() { disable(); }
    bool isChoked() const { return isEnabled(); }

    virtual void update() override;

private:
    void applyGainRamp(int16_t* data, size_t numSamples, float gainIncrement);

    // Fade parameters
    static constexpr float FADE_TIME_MS = 3.0f;  // 3ms crossfade (tighter feel for quantization)
    static constexpr float FADE_SAMPLES = (FADE_TIME_MS / 1000.0f) * 44100.0f;  // 132 samples

    // Gain state (modified in audio ISR)
    float m_currentGain;  // Current gain (ramped smoothly)
    float m_targetGain;   // Target gain (0.0 = mute, 1.0 = full volume)

    // ========== STATE MACHINE ==========
    // State is atomic for lock-free cross-thread access
    std::atomic<ChokeState> m_state;

    // Choke length mode state
    ChokeLength m_lengthMode;     // FREE or QUANTIZED
    uint64_t m_releaseAtSample;   // Sample position when choke should auto-release (0 = no scheduled release)

    // Choke onset mode state
    ChokeOnset m_onsetMode;       // FREE or QUANTIZED
    uint64_t m_onsetAtSample;     // Sample position when choke should engage (0 = no scheduled onset)
};
