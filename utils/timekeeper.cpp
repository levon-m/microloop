/**
 * timekeeper.cpp - Implementation of centralized timing authority
 */

#include "timekeeper.h"
#include "trace.h"

// AUDIO_BLOCK_SAMPLES is defined by Teensy Audio Library as 128
// We can't include <Audio.h> here due to SD card dependencies
#ifndef AUDIO_BLOCK_SAMPLES
#define AUDIO_BLOCK_SAMPLES 128
#endif

// ========== STATIC MEMBER INITIALIZATION ==========

// Audio timeline
volatile uint64_t TimeKeeper::s_samplePosition = 0;

// MIDI timeline
volatile uint32_t TimeKeeper::s_beatNumber = 0;
volatile uint32_t TimeKeeper::s_tickInBeat = 0;
//avoid division by 0, set sensible defaults
volatile uint32_t TimeKeeper::s_samplesPerBeat = TimeKeeper::DEFAULT_SAMPLES_PER_BEAT;

// Transport state
volatile TimeKeeper::TransportState TimeKeeper::s_transportState = TransportState::STOPPED;

// Beat notification
volatile bool TimeKeeper::s_beatFlag = false;

// ========== INITIALIZATION ==========

void TimeKeeper::begin() {
    reset();
}

void TimeKeeper::reset() {
    // Reset all state (with interrupt protection for 64-bit sample position)
    noInterrupts();
    s_samplePosition = 0;
    s_beatNumber = 0;
    s_tickInBeat = 0;
    s_samplesPerBeat = DEFAULT_SAMPLES_PER_BEAT;
    s_transportState = TransportState::STOPPED;
    interrupts();
}

// ========== AUDIO TIMELINE ==========

void TimeKeeper::incrementSamples(uint32_t numSamples) {
    /**
     * CRITICAL PATH: Called from audio ISR every ~2.9ms
     *
     * PERFORMANCE:
     * - Interrupt disable/enable: ~10 CPU cycles
     * - 64-bit add: ~5 CPU cycles
     * - Total: ~15 CPU cycles @ 600 MHz = ~25 nanoseconds
     * - Negligible overhead compared to audio processing
     *
     * THREAD SAFETY:
     * - ARM Cortex-M7 doesn't have native 64-bit atomic instructions
     * - We disable interrupts briefly (safe - we're already in ISR context)
     * - App thread reads are protected the same way
     */
    noInterrupts();
    s_samplePosition += numSamples;
    interrupts();
}

uint64_t TimeKeeper::getSamplePosition() {
    noInterrupts();
    uint64_t pos = s_samplePosition;
    interrupts();
    return pos;
}

// ========== MIDI TIMELINE ==========

void TimeKeeper::syncToMIDIClock(uint32_t tickPeriodUs) {
    /**
     * Convert MIDI tick period to samples per beat
     *
     * FORMULA:
     *   beatPeriodUs = tickPeriodUs * 24  (24 ticks per beat)
     *   samplesPerBeat = beatPeriodUs * (sampleRate / 1e6)
     *                  = tickPeriodUs * 24 * (44100 / 1000000)
     *                  = tickPeriodUs * 24 * 0.04410
     *                  = tickPeriodUs * 1.0584
     *
     * To avoid floating point, use integer math:
     *   samplesPerBeat = (tickPeriodUs * 24 * SAMPLE_RATE) / 1000000
     *
     * PRECISION:
     *   At 120 BPM: tickPeriodUs = 20833µs
     *   samplesPerBeat = (20833 * 24 * 44100) / 1000000
     *                  = 22049.9... ≈ 22050 samples
     *
     * OVERFLOW PROTECTION:
     *   tickPeriodUs: max ~50000 (60 BPM)
     *   Intermediate: 50000 * 24 * 44100 = 52,920,000,000
     *   Fits in uint64_t (max 18 quintillion)
     */
    uint64_t beatPeriodUs = (uint64_t)tickPeriodUs * MIDI_PPQN;
    uint32_t spb = (beatPeriodUs * SAMPLE_RATE) / 1000000ULL;

    // Sanity check: Reject absurd tempos (30-300 BPM range)
    // At 30 BPM: samplesPerBeat = 88200
    // At 300 BPM: samplesPerBeat = 8820
    if (spb >= 8000 && spb <= 100000) {
        __atomic_store_n(&s_samplesPerBeat, spb, __ATOMIC_RELAXED);

        // Trace sync event with BPM
        uint32_t bpm = (SAMPLE_RATE * 60) / spb;
        TRACE(TRACE_TIMEKEEPER_SYNC, bpm);
    }
}

//exists for testing, will only get calculated/called in syncToMIDIClock()
void TimeKeeper::setSamplesPerBeat(uint32_t samplesPerBeat) {
    __atomic_store_n(&s_samplesPerBeat, samplesPerBeat, __ATOMIC_RELAXED);
}

void TimeKeeper::incrementTick() {
    /**
     * Increment tick counter, advance beat when tick reaches 24
     *
     * THREAD SAFETY:
     * This is called from app thread only, so we don't need atomics
     * for the read-modify-write of tickInBeat. But we use atomics
     * for beatNumber since audio ISR may read it concurrently.
     *
     * BEAT FLAG:
     * When beat advances, we set s_beatFlag for external consumers
     * (e.g., beat LED). This provides perfect beat visualization.
     */
    uint32_t tick = __atomic_load_n(&s_tickInBeat, __ATOMIC_RELAXED);
    tick++;

    if (tick >= MIDI_PPQN) {
        // New beat started
        tick = 0;
        uint32_t newBeat = __atomic_fetch_add(&s_beatNumber, 1U, __ATOMIC_RELAXED) + 1;

        // Set beat flag for external beat indicators (LED, display, etc.)
        __atomic_store_n(&s_beatFlag, true, __ATOMIC_RELEASE);

        TRACE(TRACE_TIMEKEEPER_BEAT_ADVANCE, newBeat & 0xFFFF);
    }

    __atomic_store_n(&s_tickInBeat, tick, __ATOMIC_RELAXED);
}

//uncomment if you need CONTINUE handling or manual beat correction
//void TimeKeeper::advanceToBeat() {
//    __atomic_fetch_add(&s_beatNumber, 1U, __ATOMIC_RELAXED);
//    __atomic_store_n(&s_tickInBeat, 0U, __ATOMIC_RELAXED);
//}

// ========== TRANSPORT CONTROL ==========

void TimeKeeper::setTransportState(TransportState state) {
    __atomic_store_n(&s_transportState, state, __ATOMIC_RELAXED);
    TRACE(TRACE_TIMEKEEPER_TRANSPORT, (uint16_t)state);
}

TimeKeeper::TransportState TimeKeeper::getTransportState() {
    return __atomic_load_n(&s_transportState, __ATOMIC_RELAXED);
}

bool TimeKeeper::isRunning() {
    TransportState state = getTransportState();
    return (state == TransportState::PLAYING || state == TransportState::RECORDING);
}

// ========== QUERY API ==========

uint32_t TimeKeeper::getBeatNumber() {
    return __atomic_load_n(&s_beatNumber, __ATOMIC_RELAXED);
}

uint32_t TimeKeeper::getBarNumber() {
    uint32_t beat = getBeatNumber();
    return beat / BEATS_PER_BAR;
}

uint32_t TimeKeeper::getBeatInBar() {
    uint32_t beat = getBeatNumber();
    return beat % BEATS_PER_BAR;
}

uint32_t TimeKeeper::getTickInBeat() {
    return __atomic_load_n(&s_tickInBeat, __ATOMIC_RELAXED);
}

uint32_t TimeKeeper::getSamplesPerBeat() {
    return __atomic_load_n(&s_samplesPerBeat, __ATOMIC_RELAXED);
}

float TimeKeeper::getBPM() {
    uint32_t spb = getSamplesPerBeat();
    if (spb == 0) return 0.0f;

    // BPM = (sampleRate * 60) / samplesPerBeat
    return (float)(SAMPLE_RATE * 60) / (float)spb;
}

// ========== QUANTIZATION API ==========

uint32_t TimeKeeper::samplesToNextBeat() {
    /**
     * Calculate samples until next beat boundary
     *
     * RELATIVE ALGORITHM (drift-proof):
     *   Uses position within current beat to calculate relative offset to next beat.
     *   This avoids timing drift issues between MIDI beat tracking and audio samples.
     *
     * NEAR-BOUNDARY TOLERANCE (NEW):
     *   If we're very close to a beat boundary (within 128 samples = 1 audio block),
     *   return 0 to fire immediately. This prevents "just missed it" latency where
     *   pressing exactly on the beat adds a full beat of delay.
     *
     * EXAMPLE (120 BPM, spb = 22050):
     *   - At sample 100 within beat → 21950 samples until next beat
     *   - At sample 22000 within beat (50 samples before boundary) → 50 samples
     *   - At sample 22040 within beat (10 samples before boundary) → 0 (fire now!)
     *   - At sample 0 (exact boundary) → 0 (fire now!)
     */
    uint64_t currentSample = getSamplePosition();
    uint32_t spb = getSamplesPerBeat();

    // Calculate position within current beat (0 to spb-1)
    uint32_t sampleWithinBeat = (uint32_t)(currentSample % spb);

    // Samples remaining until next beat boundary
    uint32_t samplesToNext = spb - sampleWithinBeat;

    // TOLERANCE: Only fire immediately if we're AT or slightly PAST the boundary
    // Grace period: If we're within 16 samples (~0.36ms) PAST the boundary, treat as "on time"
    // This handles "just missed it by a few samples" without firing early
    if (sampleWithinBeat <= 16) {
        return 0;  // We're at or just past the boundary - fire now!
    }

    return samplesToNext;
}

uint32_t TimeKeeper::samplesToNextSubdivision(uint32_t subdivision) {
    /**
     * Calculate samples until next subdivision boundary
     *
     * BEAT-ANCHORED ALGORITHM (FIXED):
     *   Subdivisions are anchored to beat boundaries, not sample 0.
     *   This prevents drift when samplesPerBeat isn't perfectly divisible.
     *
     * APPROACH:
     *   1. Find position within current beat
     *   2. Use 64-bit fractional math to find next subdivision boundary within beat
     *   3. If we've passed all subdivisions in this beat, go to next beat
     *
     * TOLERANCE:
     *   16-sample grace period PAST boundary (like samplesToNextBeat)
     *
     * BLOCK ROUNDING:
     *   Round up to next AUDIO_BLOCK_SAMPLES boundary for ISR scheduling
     *
     * EXAMPLE (120 BPM, spb=22050, 1/8 note subdivision=11025):
     *   Beat 0: subdivisions at sample 0, 11025
     *   Beat 1: subdivisions at sample 22050, 33075
     *   Beat 2: subdivisions at sample 44100, 55125
     *   (Always anchored to beat boundaries, no drift!)
     */
    uint64_t currentSample = getSamplePosition();
    uint32_t spb = getSamplesPerBeat();

    // Find position within current beat
    uint32_t sampleWithinBeat = (uint32_t)(currentSample % spb);

    // How many subdivisions fit in one beat?
    // Use 64-bit to avoid overflow: subdivisions_per_beat = spb / subdivision
    // We need fractional precision to handle non-integer divisions
    uint32_t subdivisionsPerBeat = spb / subdivision;
    if (subdivisionsPerBeat == 0) subdivisionsPerBeat = 1;  // Safety: at least 1 per beat

    // Find which subdivision we're currently in (0-indexed)
    // currentSubdivision = floor(sampleWithinBeat / subdivision)
    uint32_t currentSubdivisionIndex = sampleWithinBeat / subdivision;

    // Find the sample position of the NEXT subdivision boundary within this beat
    uint32_t nextSubdivisionIndex = currentSubdivisionIndex + 1;

    // Check if we've passed all subdivisions in this beat
    if (nextSubdivisionIndex >= subdivisionsPerBeat) {
        // Go to first subdivision of next beat (which is the beat boundary itself)
        uint32_t samplesToNext = spb - sampleWithinBeat;

        // TOLERANCE: Grace period if just past beat boundary
        if (sampleWithinBeat <= 16) {
            return 0;  // At or just past boundary - fire now!
        }

        // BLOCK ROUNDING
        uint32_t remainder = samplesToNext % AUDIO_BLOCK_SAMPLES;
        if (remainder > 0) {
            samplesToNext += (AUDIO_BLOCK_SAMPLES - remainder);
        }

        return samplesToNext;
    }

    // Calculate next subdivision boundary sample (within current beat)
    uint32_t nextSubdivisionSample = nextSubdivisionIndex * subdivision;

    // Samples until that boundary
    uint32_t samplesToNext = nextSubdivisionSample - sampleWithinBeat;

    // TOLERANCE: Grace period if just past subdivision boundary
    uint32_t sampleWithinSubdivision = sampleWithinBeat % subdivision;
    if (sampleWithinSubdivision <= 16) {
        return 0;  // At or just past subdivision boundary - fire now!
    }

    // BLOCK ROUNDING
    uint32_t remainder = samplesToNext % AUDIO_BLOCK_SAMPLES;
    if (remainder > 0) {
        samplesToNext += (AUDIO_BLOCK_SAMPLES - remainder);
    }

    return samplesToNext;
}

uint32_t TimeKeeper::samplesToNextBar() {
    /**
     * Calculate samples until next bar boundary
     *
     * RELATIVE ALGORITHM (NEW - drift-proof):
     *   Uses position within current bar to calculate relative offset to next bar.
     *   Avoids relying on getBarNumber() which can lag/jump due to MIDI timing.
     *
     * WHY RELATIVE?
     *   - OLD: Used getBarNumber() (absolute) → can be stale if MIDI ticks lag
     *   - NEW: Uses position % samplesPerBar (relative) → always accurate
     *
     * TOLERANCE:
     *   Same as samplesToNextBeat() - fire immediately if within 128 samples
     */
    uint64_t currentSample = getSamplePosition();
    uint32_t spb = getSamplesPerBeat();
    uint32_t samplesPerBar = spb * BEATS_PER_BAR;

    // Calculate position within current bar (0 to samplesPerBar-1)
    uint32_t sampleWithinBar = (uint32_t)(currentSample % samplesPerBar);

    // Samples remaining until next bar boundary
    uint32_t samplesToNext = samplesPerBar - sampleWithinBar;

    // TOLERANCE: Only fire immediately if AT or slightly PAST boundary
    // Grace period: 16 samples (~0.36ms) past boundary
    if (sampleWithinBar <= 16) {
        return 0;  // At or just past boundary - fire now!
    }

    return samplesToNext;
}

uint64_t TimeKeeper::beatToSample(uint32_t beatNumber) {
    uint32_t spb = getSamplesPerBeat();
    return (uint64_t)beatNumber * spb;
}

uint64_t TimeKeeper::barToSample(uint32_t barNumber) {
    uint32_t spb = getSamplesPerBeat();
    return (uint64_t)barNumber * BEATS_PER_BAR * spb;
}

uint32_t TimeKeeper::sampleToBeat(uint64_t samplePos) {
    uint32_t spb = getSamplesPerBeat();
    if (spb == 0) return 0;

    return (uint32_t)(samplePos / spb);
}

bool TimeKeeper::isOnBeatBoundary() {
    /**
     * Check if current position is within one audio block of a beat boundary
     *
     * TOLERANCE: ±AUDIO_BLOCK_SAMPLES (±128 samples ≈ ±2.9ms)
     *
     * This accounts for:
     * - Audio block granularity (we can't start/stop mid-block)
     * - Small timing jitter from MIDI clock
     */
    uint64_t currentSample = getSamplePosition();
    uint32_t currentBeat = getBeatNumber();
    uint32_t spb = getSamplesPerBeat();

    uint64_t beatSample = (uint64_t)currentBeat * spb;

    // Check if within tolerance of beat boundary
    int64_t delta = (int64_t)currentSample - (int64_t)beatSample;
    return (delta >= 0 && delta <= (int64_t)AUDIO_BLOCK_SAMPLES);
}

bool TimeKeeper::isOnBarBoundary() {
    // Only downbeat (beat 0 in bar) is a bar boundary
    if (getBeatInBar() != 0) return false;

    return isOnBeatBoundary();
}

// ========== BEAT NOTIFICATION API ==========

bool TimeKeeper::pollBeatFlag() {
    /**
     * Test-and-clear beat flag
     *
     * OPERATION: Atomic exchange with false
     * - Returns current flag value
     * - Sets flag to false atomically
     * - Never misses a beat (flag stays set until consumed)
     *
     * PERFORMANCE:
     * - Single atomic instruction (~20-30 CPU cycles)
     * - Memory ordering: acquire-release (ensures visibility)
     *
     * USAGE:
     * Called from App thread every 2ms to check for beat boundaries.
     * If returns true, beat has occurred since last check.
     */
    return __atomic_exchange_n(&s_beatFlag, false, __ATOMIC_ACQ_REL);
}
