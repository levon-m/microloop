#pragma once

#include "AudioEffectBase.h"
#include "TimeKeeper.h"
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
    AudioEffectStutter();

    // AudioEffectBase interface implementation
    void enable() override;
    void disable() override;
    void toggle() override;
    bool isEnabled() const override;
    const char* getName() const override;

    // ========== STATE MACHINE CONTROL (called by controller) ==========

    /**
     * Get current state
     */
    StutterState getState() const { return m_state; }

    /**
     * Start capture immediately (CaptureStart=Free)
     */
    void startCapture();

    /**
     * Schedule capture start (CaptureStart=Quantized)
     */
    void scheduleCaptureStart(uint64_t sample);

    /**
     * Cancel scheduled capture start (STUTTER released during WAIT_CAPTURE_START)
     */
    void cancelCaptureStart();

    /**
     * End capture immediately (CaptureEnd=Free, button released)
     * Transitions to PLAYING if STUTTER held, else IDLE_WITH_LOOP
     */
    void endCapture(bool stutterHeld);

    /**
     * Schedule capture end (CaptureEnd=Quantized, button released)
     */
    void scheduleCaptureEnd(uint64_t sample, bool stutterHeld);

    /**
     * Start playback immediately (Onset=Free)
     */
    void startPlayback();

    /**
     * Schedule playback start (Onset=Quantized)
     */
    void schedulePlaybackOnset(uint64_t sample);

    /**
     * Stop playback immediately (Length=Free, STUTTER released)
     */
    void stopPlayback();

    /**
     * Schedule playback stop (Length=Quantized, STUTTER released)
     * Only changes state if currently PLAYING (not if waiting for onset)
     */
    void schedulePlaybackLength(uint64_t sample);

    // ========== PARAMETER CONTROL ==========

    void setStutterHeld(bool held) { m_stutterHeld = held; }

    void setLengthMode(StutterLength mode) { m_lengthMode = mode; }
    StutterLength getLengthMode() const { return m_lengthMode; }

    void setOnsetMode(StutterOnset mode) { m_onsetMode = mode; }
    StutterOnset getOnsetMode() const { return m_onsetMode; }

    void setCaptureStartMode(StutterCaptureStart mode) { m_captureStartMode = mode; }
    StutterCaptureStart getCaptureStartMode() const { return m_captureStartMode; }

    void setCaptureEndMode(StutterCaptureEnd mode) { m_captureEndMode = mode; }
    StutterCaptureEnd getCaptureEndMode() const { return m_captureEndMode; }

    virtual void update() override;

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
