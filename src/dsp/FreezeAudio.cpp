#include "FreezeAudio.h"

FreezeAudio::FreezeAudio() : IEffectAudio(2) {  // Call base with 2 inputs (stereo)
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

void FreezeAudio::enable() {
    // Set read position to current write position
    // This captures the most recent audio in the buffer
    m_readPos = m_writePos;
    m_isEnabled.store(true, std::memory_order_release);
}

void FreezeAudio::disable() {
    m_isEnabled.store(false, std::memory_order_release);
}

void FreezeAudio::toggle() {
    if (isEnabled()) {
        disable();
    } else {
        enable();
    }
}

bool FreezeAudio::isEnabled() const {
    return m_isEnabled.load(std::memory_order_acquire);
}

const char* FreezeAudio::getName() const {
    return "Freeze";
}

void FreezeAudio::update() {
    uint64_t currentSample = Timebase::getSamplePosition();
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
