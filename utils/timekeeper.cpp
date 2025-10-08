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
     */
    uint32_t tick = __atomic_load_n(&s_tickInBeat, __ATOMIC_RELAXED);
    tick++;

    if (tick >= MIDI_PPQN) {
        // New beat started
        tick = 0;
        uint32_t newBeat = __atomic_fetch_add(&s_beatNumber, 1U, __ATOMIC_RELAXED) + 1;
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
     * ALGORITHM:
     *   1. Get current sample position
     *   2. Calculate sample position of next beat
     *   3. Return delta
     *
     * EDGE CASE: If we're exactly on a beat boundary, return full beat
     * (don't return 0, that would cause immediate trigger)
     */
    uint64_t currentSample = getSamplePosition();
    uint32_t currentBeat = getBeatNumber();
    uint32_t spb = getSamplesPerBeat();

    // Sample position of next beat
    uint64_t nextBeatSample = (uint64_t)(currentBeat + 1) * spb;

    // If we're past the next beat sample (can happen due to timing drift),
    // return samples to the beat after that
    if (currentSample >= nextBeatSample) {
        nextBeatSample = (uint64_t)(currentBeat + 2) * spb;
    }

    // Return delta (safe to cast to uint32_t since delta < samplesPerBeat < 100000)
    return (uint32_t)(nextBeatSample - currentSample);
}

uint32_t TimeKeeper::samplesToNextBar() {
    uint64_t currentSample = getSamplePosition();
    uint32_t currentBar = getBarNumber();
    uint32_t spb = getSamplesPerBeat();

    // Sample position of next bar (bars are 4 beats)
    uint64_t nextBarSample = (uint64_t)(currentBar + 1) * BEATS_PER_BAR * spb;

    // Handle past-bar case
    if (currentSample >= nextBarSample) {
        nextBarSample = (uint64_t)(currentBar + 2) * BEATS_PER_BAR * spb;
    }

    return (uint32_t)(nextBarSample - currentSample);
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
