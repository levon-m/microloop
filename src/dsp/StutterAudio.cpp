#include "StutterAudio.h"

// Define static EXTMEM buffers
EXTMEM int16_t StutterAudio::m_stutterBufferL[StutterAudio::STUTTER_BUFFER_SAMPLES];
EXTMEM int16_t StutterAudio::m_stutterBufferR[StutterAudio::STUTTER_BUFFER_SAMPLES];

StutterAudio::StutterAudio() : IEffectAudio(2) {  // Call base with 2 inputs (stereo)
    m_writePos = 0;
    m_readPos = 0;
    m_captureLength = 0;  // No captured loop yet
    m_state = StutterState::IDLE_NO_LOOP;
    m_lengthMode = StutterLength::FREE;  // Default: free mode
    m_onsetMode = StutterOnset::FREE;    // Default: free mode
    m_captureStartMode = StutterCaptureStart::FREE;    // Default: free mode
    m_captureEndMode = StutterCaptureEnd::FREE;    // Default: free mode
    m_captureStartAtSample = 0;   // No scheduled capture start
    m_captureEndAtSample = 0;     // No scheduled capture end
    m_playbackOnsetAtSample = 0;  // No scheduled playback onset
    m_playbackLengthAtSample = 0; // No scheduled playback length
    m_stutterHeld = false;        // Track if STUTTER button held (set by controller)

    // Initialize buffers to silence
    memset(m_stutterBufferL, 0, sizeof(m_stutterBufferL));
    memset(m_stutterBufferR, 0, sizeof(m_stutterBufferR));
}

void StutterAudio::enable() {
    // Start playback (used by controller for free onset)
    m_readPos = 0;  // Start from beginning of captured loop
    m_state = StutterState::PLAYING;
}

void StutterAudio::disable() {
    // Stop playback and clear loop
    m_state = StutterState::IDLE_NO_LOOP;
    m_captureLength = 0;
    m_writePos = 0;
    m_readPos = 0;
}

void StutterAudio::toggle() {
    if (isEnabled()) {
        disable();
    } else {
        enable();
    }
}

bool StutterAudio::isEnabled() const {
    // Effect is "enabled" if playing, capturing, or waiting
    return m_state != StutterState::IDLE_NO_LOOP &&
           m_state != StutterState::IDLE_WITH_LOOP;
}

const char* StutterAudio::getName() const {
    return "Stutter";
}

void StutterAudio::startCapture() {
    m_writePos = 0;  // Reset write position
    m_captureLength = 0;  // Clear previous capture
    m_state = StutterState::CAPTURING;
}

void StutterAudio::scheduleCaptureStart(uint64_t sample) {
    m_captureStartAtSample = sample;
    m_state = StutterState::WAIT_CAPTURE_START;
}

void StutterAudio::cancelCaptureStart() {
    m_captureStartAtSample = 0;
    m_state = StutterState::IDLE_NO_LOOP;
}

void StutterAudio::endCapture(bool stutterHeld) {
    if (m_writePos > 0) {  // Check we captured something
        m_captureLength = m_writePos;
        if (stutterHeld) {
            m_readPos = 0;
            m_state = StutterState::PLAYING;
        } else {
            m_state = StutterState::IDLE_WITH_LOOP;
        }
    } else {
        // No audio captured
        m_state = StutterState::IDLE_NO_LOOP;
    }
}

void StutterAudio::scheduleCaptureEnd(uint64_t sample, bool stutterHeld) {
    m_captureEndAtSample = sample;
    m_stutterHeld = stutterHeld;  // Remember button state for later transition
    // Only transition to WAIT_CAPTURE_END if we're currently CAPTURING
    // If we're in WAIT_CAPTURE_START, don't change state (end will fire after start)
    if (m_state == StutterState::CAPTURING) {
        m_state = StutterState::WAIT_CAPTURE_END;
    }
}

void StutterAudio::startPlayback() {
    m_readPos = 0;
    m_state = StutterState::PLAYING;
}

void StutterAudio::schedulePlaybackOnset(uint64_t sample) {
    m_playbackOnsetAtSample = sample;
    m_state = StutterState::WAIT_PLAYBACK_ONSET;
}

void StutterAudio::stopPlayback() {
    m_state = StutterState::IDLE_WITH_LOOP;
}

void StutterAudio::schedulePlaybackLength(uint64_t sample) {
    m_playbackLengthAtSample = sample;
    // Only transition to WAIT_PLAYBACK_LENGTH if we're currently PLAYING
    // If we're in WAIT_PLAYBACK_ONSET, don't change state (length will fire after onset)
    if (m_state == StutterState::PLAYING) {
        m_state = StutterState::WAIT_PLAYBACK_LENGTH;
    }
}

void StutterAudio::update() {
    uint64_t currentSample = Timebase::getSamplePosition();
    uint64_t blockEndSample = currentSample + AUDIO_BLOCK_SAMPLES;

    // ========== CHECK FOR SCHEDULED STATE TRANSITIONS (ISR) ==========

    // Check for scheduled capture start
    if (m_captureStartAtSample > 0 && currentSample >= m_captureStartAtSample && currentSample < blockEndSample) {
        m_writePos = 0;
        m_captureLength = 0;
        m_state = StutterState::CAPTURING;
        m_captureStartAtSample = 0;
    }

    // Check for scheduled capture end
    if (m_captureEndAtSample > 0 && currentSample >= m_captureEndAtSample && currentSample < blockEndSample) {
        if (m_writePos > 0) {
            m_captureLength = m_writePos;
            if (m_stutterHeld) {
                m_readPos = 0;
                m_state = StutterState::PLAYING;
            } else {
                m_state = StutterState::IDLE_WITH_LOOP;
            }
        } else {
            m_state = StutterState::IDLE_NO_LOOP;
        }
        m_captureEndAtSample = 0;
    }

    // Check for scheduled playback onset
    if (m_playbackOnsetAtSample > 0 && currentSample >= m_playbackOnsetAtSample && currentSample < blockEndSample) {
        m_readPos = 0;
        m_state = StutterState::PLAYING;
        m_playbackOnsetAtSample = 0;
    }

    // Check for scheduled playback length (stop)
    if (m_playbackLengthAtSample > 0 && currentSample >= m_playbackLengthAtSample && currentSample < blockEndSample) {
        m_state = StutterState::IDLE_WITH_LOOP;
        m_playbackLengthAtSample = 0;
    }

    // ========== STATE MACHINE AUDIO PROCESSING ==========

    switch (m_state) {
        case StutterState::IDLE_NO_LOOP:
        case StutterState::IDLE_WITH_LOOP:
        case StutterState::WAIT_CAPTURE_START:
        case StutterState::WAIT_PLAYBACK_ONSET: {
            // PASSTHROUGH: Just pass audio through unchanged
            audio_block_t* blockL = receiveWritable(0);
            audio_block_t* blockR = receiveWritable(1);

            if (blockL && blockR) {
                transmit(blockL, 0);
                transmit(blockR, 1);
            }

            if (blockL) release(blockL);
            if (blockR) release(blockR);
            break;
        }

        case StutterState::CAPTURING:
        case StutterState::WAIT_CAPTURE_END: {
            // CAPTURING: Write to buffer (non-circular) and pass through
            audio_block_t* blockL = receiveWritable(0);
            audio_block_t* blockR = receiveWritable(1);

            if (blockL && blockR) {
                // Write to buffer if space available
                for (size_t i = 0; i < AUDIO_BLOCK_SAMPLES && m_writePos < STUTTER_BUFFER_SAMPLES; i++) {
                    m_stutterBufferL[m_writePos] = blockL->data[i];
                    m_stutterBufferR[m_writePos] = blockR->data[i];
                    m_writePos++;
                }

                // Check if buffer is full (auto-transition, overrides quantization)
                if (m_writePos >= STUTTER_BUFFER_SAMPLES) {
                    m_captureLength = m_writePos;
                    if (m_stutterHeld) {
                        m_readPos = 0;
                        m_state = StutterState::PLAYING;
                    } else {
                        m_state = StutterState::IDLE_WITH_LOOP;
                    }
                    // Cancel any scheduled capture end
                    m_captureEndAtSample = 0;
                }

                // Pass through unmodified
                transmit(blockL, 0);
                transmit(blockR, 1);
            }

            if (blockL) release(blockL);
            if (blockR) release(blockR);
            break;
        }

        case StutterState::PLAYING:
        case StutterState::WAIT_PLAYBACK_LENGTH: {
            // PLAYING: Read from buffer and loop
            audio_block_t* outL = allocate();
            audio_block_t* outR = allocate();

            if (outL && outR) {
                // Read from captured buffer
                for (size_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
                    outL->data[i] = m_stutterBufferL[m_readPos];
                    outR->data[i] = m_stutterBufferR[m_readPos];

                    // Advance read position (loop when reaching end)
                    m_readPos++;
                    if (m_readPos >= m_captureLength) {
                        m_readPos = 0;  // Loop back to start
                    }
                }

                transmit(outL, 0);
                transmit(outR, 1);
            }

            if (outL) release(outL);
            if (outR) release(outR);

            // Consume and discard input blocks (not using live audio)
            audio_block_t* blockL = receiveReadOnly(0);
            audio_block_t* blockR = receiveReadOnly(1);
            if (blockL) release(blockL);
            if (blockR) release(blockR);
            break;
        }
    }
}
