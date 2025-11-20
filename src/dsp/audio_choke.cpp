#include "audio_choke.h"

AudioEffectChoke::AudioEffectChoke() : AudioEffectBase(2) {  // Call base with 2 inputs (stereo)
    m_targetGain = 1.0f;      // Start unmuted
    m_currentGain = 1.0f;
    m_isEnabled.store(false, std::memory_order_relaxed);  // Start disabled (unmuted)
    m_lengthMode = ChokeLength::FREE;  // Default: free mode
    m_onsetMode = ChokeOnset::FREE;    // Default: free mode
    m_releaseAtSample = 0;  // No scheduled release
    m_onsetAtSample = 0;    // No scheduled onset
}

void AudioEffectChoke::enable() {
    m_targetGain = 0.0f;  // Mute
    m_isEnabled.store(true, std::memory_order_release);
}

void AudioEffectChoke::disable() {
    m_targetGain = 1.0f;  // Unmute
    m_isEnabled.store(false, std::memory_order_release);
}

void AudioEffectChoke::toggle() {
    if (isEnabled()) {
        disable();
    } else {
        enable();
    }
}

bool AudioEffectChoke::isEnabled() const {
    return m_isEnabled.load(std::memory_order_acquire);
}

const char* AudioEffectChoke::getName() const {
    return "Choke";
}

void AudioEffectChoke::update() {
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

void AudioEffectChoke::applyGainRamp(int16_t* data, size_t numSamples, float gainIncrement) {
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
