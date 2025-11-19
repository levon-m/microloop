#pragma once

#include "audio_effect_base.h"
#include "timekeeper.h"
#include <atomic>
#include <Arduino.h>

enum class StutterLength : uint8_t {
    FREE = 0,       // Stop immediately when button released (default)
    QUANTIZED = 1   // Stop at next grid boundary after release
};

enum class StutterOnset : uint8_t {
    FREE = 0,       // Start playback immediately when button pressed (default)
    QUANTIZED = 1   // Start playback at next grid boundary
};

enum class StutterCaptureStart : uint8_t {
    FREE = 0,       // Start capture immediately when FUNC+STUTTER pressed (default)
    QUANTIZED = 1   // Start capture at next grid boundary
};

enum class StutterCaptureEnd : uint8_t {
    FREE = 0,       // End capture immediately when button released (default)
    QUANTIZED = 1   // End capture at next grid boundary after release
};

/**
 * Stutter State Machine (8 states)
 *
 * State transitions:
 * - idleWithNoLoop: No loop captured, passthrough audio
 * - idleWithWrittenLoop: Loop captured, ready for playback
 * - waitForCaptureStart: Waiting for quantized capture start boundary
 * - Capturing: Actively recording into buffer
 * - waitForCaptureEnd: Waiting for quantized capture end boundary
 * - waitForPlaybackOnset: Waiting for quantized playback start boundary
 * - Playing: Actively playing captured loop
 * - waitForPlaybackLength: Waiting for quantized playback stop boundary
 */
enum class StutterState : uint8_t {
    IDLE_NO_LOOP = 0,           // No loop captured (LED: OFF)
    IDLE_WITH_LOOP = 1,         // Loop captured, not playing (LED: WHITE)
    WAIT_CAPTURE_START = 2,     // Waiting for capture start grid (LED: RED blinking)
    CAPTURING = 3,              // Recording into buffer (LED: RED solid)
    WAIT_CAPTURE_END = 4,       // Waiting for capture end grid (LED: RED solid)
    WAIT_PLAYBACK_ONSET = 5,    // Waiting for playback start grid (LED: BLUE blinking)
    PLAYING = 6,                // Playing captured loop (LED: BLUE solid)
    WAIT_PLAYBACK_LENGTH = 7    // Waiting for playback stop grid (LED: BLUE solid)
};

class AudioEffectStutter : public AudioEffectBase {
public:
    AudioEffectStutter() : AudioEffectBase(2) {  // Call base with 2 inputs (stereo)
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

    // AudioEffectBase interface implementation
    void enable() override {
        // Start playback (used by controller for free onset)
        m_readPos = 0;  // Start from beginning of captured loop
        m_state = StutterState::PLAYING;
    }

    void disable() override {
        // Stop playback and clear loop
        m_state = StutterState::IDLE_NO_LOOP;
        m_captureLength = 0;
        m_writePos = 0;
        m_readPos = 0;
    }

    void toggle() override {
        if (isEnabled()) {
            disable();
        } else {
            enable();
        }
    }

    bool isEnabled() const override {
        // Effect is "enabled" if playing, capturing, or waiting
        return m_state != StutterState::IDLE_NO_LOOP &&
               m_state != StutterState::IDLE_WITH_LOOP;
    }

    const char* getName() const override {
        return "Stutter";
    }

    // ========== STATE MACHINE CONTROL (called by controller) ==========

    /**
     * Get current state
     */
    StutterState getState() const {
        return m_state;
    }

    /**
     * Start capture immediately (CaptureStart=Free)
     */
    void startCapture() {
        m_writePos = 0;  // Reset write position
        m_captureLength = 0;  // Clear previous capture
        m_state = StutterState::CAPTURING;
    }

    /**
     * Schedule capture start (CaptureStart=Quantized)
     */
    void scheduleCaptureStart(uint64_t sample) {
        m_captureStartAtSample = sample;
        m_state = StutterState::WAIT_CAPTURE_START;
    }

    /**
     * Cancel scheduled capture start (STUTTER released during WAIT_CAPTURE_START)
     */
    void cancelCaptureStart() {
        m_captureStartAtSample = 0;
        m_state = StutterState::IDLE_NO_LOOP;
    }

    /**
     * End capture immediately (CaptureEnd=Free, button released)
     * Transitions to PLAYING if STUTTER held, else IDLE_WITH_LOOP
     */
    void endCapture(bool stutterHeld) {
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

    /**
     * Schedule capture end (CaptureEnd=Quantized, button released)
     */
    void scheduleCaptureEnd(uint64_t sample, bool stutterHeld) {
        m_captureEndAtSample = sample;
        m_stutterHeld = stutterHeld;  // Remember button state for later transition
        m_state = StutterState::WAIT_CAPTURE_END;
    }

    /**
     * Start playback immediately (Onset=Free)
     */
    void startPlayback() {
        m_readPos = 0;
        m_state = StutterState::PLAYING;
    }

    /**
     * Schedule playback start (Onset=Quantized)
     */
    void schedulePlaybackOnset(uint64_t sample) {
        m_playbackOnsetAtSample = sample;
        m_state = StutterState::WAIT_PLAYBACK_ONSET;
    }

    /**
     * Stop playback immediately (Length=Free, STUTTER released)
     */
    void stopPlayback() {
        m_state = StutterState::IDLE_WITH_LOOP;
    }

    /**
     * Schedule playback stop (Length=Quantized, STUTTER released)
     * Only changes state if currently PLAYING (not if waiting for onset)
     */
    void schedulePlaybackLength(uint64_t sample) {
        m_playbackLengthAtSample = sample;
        // Only transition to WAIT_PLAYBACK_LENGTH if we're currently PLAYING
        // If we're in WAIT_PLAYBACK_ONSET, don't change state (length will fire after onset)
        if (m_state == StutterState::PLAYING) {
            m_state = StutterState::WAIT_PLAYBACK_LENGTH;
        }
    }

    // ========== PARAMETER CONTROL ==========

    void setLengthMode(StutterLength mode) {
        m_lengthMode = mode;
    }

    StutterLength getLengthMode() const {
        return m_lengthMode;
    }

    void setOnsetMode(StutterOnset mode) {
        m_onsetMode = mode;
    }

    StutterOnset getOnsetMode() const {
        return m_onsetMode;
    }

    void setCaptureStartMode(StutterCaptureStart mode) {
        m_captureStartMode = mode;
    }

    StutterCaptureStart getCaptureStartMode() const {
        return m_captureStartMode;
    }

    void setCaptureEndMode(StutterCaptureEnd mode) {
        m_captureEndMode = mode;
    }

    StutterCaptureEnd getCaptureEndMode() const {
        return m_captureEndMode;
    }

    virtual void update() override {
        uint64_t currentSample = TimeKeeper::getSamplePosition();
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

private:
    // ========== BUFFER CONFIGURATION ==========
    // Buffer size: 1 bar @ 70 BPM (min tempo) = ~590KB total (295KB per channel)
    static constexpr uint8_t MIN_TEMPO = 70;
    static constexpr size_t STUTTER_BUFFER_SAMPLES = static_cast<size_t>((1 / (MIN_TEMPO / 60.0)) * TimeKeeper::SAMPLE_RATE) * 4;

    // Audio buffers (non-circular during capture)
    // EXTMEM places these in external PSRAM (16MB) instead of DTCM (512KB)
    // Static to allow EXTMEM usage (only one stutter instance exists)
    static EXTMEM int16_t m_stutterBufferL[STUTTER_BUFFER_SAMPLES];
    static EXTMEM int16_t m_stutterBufferR[STUTTER_BUFFER_SAMPLES];

    // ========== BUFFER POSITION STATE ==========
    size_t m_writePos;       // Current write position during capture
    size_t m_readPos;        // Current read position during playback
    size_t m_captureLength;  // Length of captured loop (0 = no loop)

    // ========== STATE MACHINE ==========
    StutterState m_state;

    // ========== QUANTIZATION MODES ==========
    StutterOnset m_onsetMode;                // Playback onset mode (FREE or QUANTIZED)
    StutterLength m_lengthMode;              // Playback length mode (FREE or QUANTIZED)
    StutterCaptureStart m_captureStartMode;  // Capture start mode (FREE or QUANTIZED)
    StutterCaptureEnd m_captureEndMode;      // Capture end mode (FREE or QUANTIZED)

    // ========== SCHEDULED SAMPLE POSITIONS ==========
    uint64_t m_captureStartAtSample;    // Scheduled capture start (0 = none)
    uint64_t m_captureEndAtSample;      // Scheduled capture end (0 = none)
    uint64_t m_playbackOnsetAtSample;   // Scheduled playback onset (0 = none)
    uint64_t m_playbackLengthAtSample;  // Scheduled playback stop (0 = none)

    // ========== BUTTON STATE TRACKING ==========
    bool m_stutterHeld;  // Is STUTTER button held? (set by controller)
};
