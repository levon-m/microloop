/**
 * timekeeper.h - Centralized timing authority for sample-accurate MIDI sync
 *
 * PURPOSE:
 * Single source of timing truth that bridges MIDI clock (24 PPQN) and audio
 * samples (44.1kHz). Essential for quantization, loop recording, and any
 * feature that needs to know "what time is it?" in the audio world.
 *
 * DESIGN:
 * - Audio timeline: Monotonic sample counter (incremented by audio ISR)
 * - MIDI timeline: Beat/bar position (synced from MIDI clock ticks)
 * - Lock-free: Atomic operations for cross-thread safety
 * - Sample-accurate: All timing in samples, not microseconds
 *
 * USAGE:
 *   // In audio ISR (every 128-sample block):
 *   TimeKeeper::incrementSamples(AUDIO_BLOCK_SAMPLES);
 *
 *   // In app thread (when MIDI clock ticks):
 *   TimeKeeper::syncToMIDIClock(avgTickPeriodUs);
 *
 *   // In any thread (query timing):
 *   uint64_t now = TimeKeeper::getSamplePosition();
 *   uint32_t toNextBeat = TimeKeeper::samplesToNextBeat();
 *   uint64_t beatSample = TimeKeeper::beatToSample(4);  // Sample position of beat 4
 *
 * KEY CONCEPTS:
 * - Sample position: Absolute sample count since audio start (monotonic)
 * - Beat position: Musical beat number (0, 1, 2, 3...), synced to MIDI clock
 * - Samples per beat: Calibrated from MIDI clock period (handles tempo changes)
 * - Bar: 4 beats (assumes 4/4 time signature)
 *
 * THREAD SAFETY:
 * - Audio ISR: incrementSamples() only
 * - App thread: syncToMIDIClock(), transport controls, queries
 * - All methods use atomic operations (lock-free, wait-free)
 */

#pragma once

#include <Arduino.h>

class TimeKeeper {
public:
    // Audio configuration
    static constexpr uint32_t SAMPLE_RATE = 44100;        // Hz
    // Note: AUDIO_BLOCK_SAMPLES is defined by Teensy Audio Library (128)
    static constexpr uint32_t BEATS_PER_BAR = 4;          // 4/4 time signature

    // MIDI configuration
    static constexpr uint32_t MIDI_PPQN = 24;  // Pulses Per Quarter Note

    /**
     * Initialize timing system
     * Call once during setup(), before starting audio/MIDI
     */
    static void begin();

    /**
     * Reset all timing state
     * Call on MIDI START to reset to beat 0, sample 0
     */
    static void reset();

    // ========== AUDIO TIMELINE (called from audio ISR) ==========

    /**
     * Increment sample counter (called by audio ISR every block)
     *
     * TIMING: ~20 CPU cycles (atomic increment)
     * THREAD SAFETY: Safe to call from audio ISR
     *
     * @param numSamples Number of samples processed (typically AUDIO_BLOCK_SAMPLES)
     */
    static void incrementSamples(uint32_t numSamples);

    /**
     * Get current sample position (monotonic counter)
     *
     * @return Absolute sample count since audio start (or last reset)
     */
    static uint64_t getSamplePosition();

    // ========== MIDI TIMELINE (called from app thread) ==========

    /**
     * Sync to MIDI clock tick (called every MIDI clock tick)
     *
     * Updates samples-per-beat based on MIDI clock period.
     * Uses exponential moving average for smooth tempo tracking.
     *
     * FORMULA:
     *   tickPeriodUs = time between MIDI clock ticks (microseconds)
     *   beatPeriodUs = tickPeriodUs * 24  (24 ticks per beat)
     *   samplesPerBeat = beatPeriodUs * (SAMPLE_RATE / 1e6)
     *
     * EXAMPLE:
     *   At 120 BPM: tickPeriodUs ≈ 20833µs
     *   beatPeriodUs = 20833 * 24 = 500000µs = 0.5s
     *   samplesPerBeat = 500000 * (44100 / 1e6) = 22050 samples
     *
     * @param tickPeriodUs Microseconds between MIDI clock ticks (from EMA)
     */
    static void syncToMIDIClock(uint32_t tickPeriodUs);

    /**
     * Manually set samples per beat (for testing or manual tempo)
     *
     * @param samplesPerBeat Number of samples in one beat
     */
    static void setSamplesPerBeat(uint32_t samplesPerBeat);

    /**
     * Increment tick counter (called every MIDI clock tick)
     *
     * Tracks ticks within beat (0-23), automatically advances beat counter
     * when tick reaches 24.
     */
    static void incrementTick();

    /**
     * Advance to next beat boundary
     *
     * Called on MIDI START or when manually snapping to beat grid.
     * Increments beat counter and resets tick counter to 0.
     */
    static void advanceToBeat();

    // ========== TRANSPORT CONTROL ==========

    /**
     * Transport states
     */
    enum class TransportState : uint8_t {
        STOPPED,    // Not playing, ignore clock ticks
        PLAYING,    // Playing, process clock ticks normally
        RECORDING   // Recording (same as playing, but signals intent)
    };

    /**
     * Set transport state
     *
     * @param state New transport state
     */
    static void setTransportState(TransportState state);

    /**
     * Get current transport state
     *
     * @return Current transport state
     */
    static TransportState getTransportState();

    /**
     * Check if transport is running (playing or recording)
     *
     * @return true if transport is active
     */
    static bool isRunning();

    // ========== QUERY API (thread-safe reads) ==========

    /**
     * Get current beat number (integer)
     *
     * Beat 0 = first beat after START
     * Beat 1 = second beat, etc.
     *
     * @return Current beat number (0-based)
     */
    static uint32_t getBeatNumber();

    /**
     * Get current bar number (integer)
     *
     * Bar 0 = beats 0-3
     * Bar 1 = beats 4-7, etc.
     *
     * @return Current bar number (0-based)
     */
    static uint32_t getBarNumber();

    /**
     * Get beat within current bar (0-3 for 4/4 time)
     *
     * @return Beat within bar (0 = downbeat, 1-3 = other beats)
     */
    static uint32_t getBeatInBar();

    /**
     * Get current tick within beat (0-23)
     *
     * @return Tick number within current beat
     */
    static uint32_t getTickInBeat();

    /**
     * Get samples per beat (current tempo)
     *
     * @return Number of samples in one beat at current tempo
     */
    static uint32_t getSamplesPerBeat();

    /**
     * Get current BPM (calculated from samples per beat)
     *
     * FORMULA: BPM = (SAMPLE_RATE * 60) / samplesPerBeat
     *
     * @return Beats per minute (floating point)
     */
    static float getBPM();

    // ========== QUANTIZATION API ==========

    /**
     * Get number of samples until next beat boundary
     *
     * CRITICAL for quantization: "How long should I wait before starting
     * recording to align with the next beat?"
     *
     * CALCULATION:
     *   nextBeatSample = (currentBeat + 1) * samplesPerBeat
     *   samplesToNextBeat = nextBeatSample - currentSamplePosition
     *
     * EXAMPLE:
     *   Current beat: 2 (samples 0-22049)
     *   Current sample: 15000
     *   Next beat sample: 22050 (start of beat 3)
     *   Return: 22050 - 15000 = 7050 samples (~160ms @ 44.1kHz)
     *
     * @return Samples remaining until next beat boundary
     */
    static uint32_t samplesToNextBeat();

    /**
     * Get number of samples until next bar boundary
     *
     * Similar to samplesToNextBeat(), but for bar boundaries (every 4 beats)
     *
     * @return Samples remaining until next bar boundary
     */
    static uint32_t samplesToNextBar();

    /**
     * Get sample position of a specific beat
     *
     * USAGE: Plan ahead - "Where will beat N occur?"
     *
     * @param beatNumber Beat number (0-based)
     * @return Sample position where that beat starts
     */
    static uint64_t beatToSample(uint32_t beatNumber);

    /**
     * Get sample position of a specific bar
     *
     * @param barNumber Bar number (0-based)
     * @return Sample position where that bar starts
     */
    static uint64_t barToSample(uint32_t barNumber);

    /**
     * Convert sample position to beat number
     *
     * @param samplePos Sample position
     * @return Beat number (integer, truncated)
     */
    static uint32_t sampleToBeat(uint64_t samplePos);

    /**
     * Check if currently on a beat boundary
     *
     * Useful for triggering events exactly on downbeat.
     * Considers a small tolerance window (1 audio block) to account for
     * timing jitter.
     *
     * @return true if current sample position is within tolerance of beat boundary
     */
    static bool isOnBeatBoundary();

    /**
     * Check if currently on a bar boundary
     *
     * @return true if current sample position is within tolerance of bar boundary
     */
    static bool isOnBarBoundary();

private:
    // ========== STATE (all volatile for cross-thread visibility) ==========

    // Audio timeline
    static volatile uint64_t s_samplePosition;  // Current sample count (incremented by audio ISR)

    // MIDI timeline
    static volatile uint32_t s_beatNumber;       // Current beat (0, 1, 2, 3...)
    static volatile uint32_t s_tickInBeat;       // Tick within beat (0-23)
    static volatile uint32_t s_samplesPerBeat;   // Samples in one beat (calibrated from MIDI)

    // Transport state
    static volatile TransportState s_transportState;

    //avoid division by 0, set sensible defaults
    static constexpr uint32_t DEFAULT_BPM = 120;
    static constexpr uint32_t DEFAULT_SAMPLES_PER_BEAT = (SAMPLE_RATE * 60) / DEFAULT_BPM;  // 22050 @ 120 BPM
};
