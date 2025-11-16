/**
 * test_timekeeper.cpp - Unit tests for TimeKeeper
 */

#include "test_runner.h"
#include "timekeeper.h"
#include "trace.h"

// ========== INITIALIZATION TESTS ==========

TEST(TimeKeeper_Begin_InitializesState) {
    TimeKeeper::begin();

    ASSERT_EQ(TimeKeeper::getSamplePosition(), 0ULL);
    ASSERT_EQ(TimeKeeper::getBeatNumber(), 0U);
    ASSERT_EQ(TimeKeeper::getTickInBeat(), 0U);
    ASSERT_EQ(TimeKeeper::getTransportState(), TimeKeeper::TransportState::STOPPED);
}

TEST(TimeKeeper_Reset_ClearsState) {
    // Set some state
    TimeKeeper::incrementSamples(1000);
    TimeKeeper::incrementTick();
    TimeKeeper::setTransportState(TimeKeeper::TransportState::PLAYING);

    // Reset
    TimeKeeper::reset();

    // Verify all cleared
    ASSERT_EQ(TimeKeeper::getSamplePosition(), 0ULL);
    ASSERT_EQ(TimeKeeper::getBeatNumber(), 0U);
    ASSERT_EQ(TimeKeeper::getTickInBeat(), 0U);
    ASSERT_EQ(TimeKeeper::getTransportState(), TimeKeeper::TransportState::STOPPED);
}

// ========== SAMPLE POSITION TESTS ==========

TEST(TimeKeeper_IncrementSamples_UpdatesPosition) {
    TimeKeeper::reset();

    TimeKeeper::incrementSamples(128);
    ASSERT_EQ(TimeKeeper::getSamplePosition(), 128ULL);

    TimeKeeper::incrementSamples(128);
    ASSERT_EQ(TimeKeeper::getSamplePosition(), 256ULL);
}

TEST(TimeKeeper_IncrementSamples_HandlesLargeValues) {
    TimeKeeper::reset();

    TimeKeeper::incrementSamples(1000000);
    ASSERT_EQ(TimeKeeper::getSamplePosition(), 1000000ULL);

    TimeKeeper::incrementSamples(1000000);
    ASSERT_EQ(TimeKeeper::getSamplePosition(), 2000000ULL);
}

TEST(TimeKeeper_IncrementSamples_NoOverflowAt32Bit) {
    TimeKeeper::reset();

    // Go past 32-bit limit (4,294,967,296)
    for (int i = 0; i < 40000; i++) {
        TimeKeeper::incrementSamples(128000);  // ~33 million samples per iteration
    }

    uint64_t pos = TimeKeeper::getSamplePosition();
    ASSERT_GT(pos, 4294967296ULL);  // Should be > 32-bit max
}

// ========== BEAT TRACKING TESTS ==========

TEST(TimeKeeper_IncrementTick_AdvancesBeat) {
    TimeKeeper::reset();

    ASSERT_EQ(TimeKeeper::getBeatNumber(), 0U);
    ASSERT_EQ(TimeKeeper::getTickInBeat(), 0U);

    // Increment 23 ticks (still in beat 0)
    for (int i = 0; i < 23; i++) {
        TimeKeeper::incrementTick();
    }
    ASSERT_EQ(TimeKeeper::getBeatNumber(), 0U);
    ASSERT_EQ(TimeKeeper::getTickInBeat(), 23U);

    // 24th tick advances to beat 1
    TimeKeeper::incrementTick();
    ASSERT_EQ(TimeKeeper::getBeatNumber(), 1U);
    ASSERT_EQ(TimeKeeper::getTickInBeat(), 0U);
}

TEST(TimeKeeper_IncrementTick_CyclesCorrectly) {
    TimeKeeper::reset();

    // Advance through multiple beats
    for (int beat = 0; beat < 10; beat++) {
        for (int tick = 0; tick < 24; tick++) {
            ASSERT_EQ(TimeKeeper::getBeatNumber(), beat);
            ASSERT_EQ(TimeKeeper::getTickInBeat(), tick);
            TimeKeeper::incrementTick();
        }
    }

    ASSERT_EQ(TimeKeeper::getBeatNumber(), 10U);
    ASSERT_EQ(TimeKeeper::getTickInBeat(), 0U);
}

TEST(TimeKeeper_GetBarNumber_CalculatesCorrectly) {
    TimeKeeper::reset();

    ASSERT_EQ(TimeKeeper::getBarNumber(), 0U);  // Beats 0-3 = bar 0

    // Advance to beat 4 (start of bar 1)
    for (int i = 0; i < 4 * 24; i++) {
        TimeKeeper::incrementTick();
    }
    ASSERT_EQ(TimeKeeper::getBarNumber(), 1U);
    ASSERT_EQ(TimeKeeper::getBeatInBar(), 0U);

    // Advance to beat 8 (start of bar 2)
    for (int i = 0; i < 4 * 24; i++) {
        TimeKeeper::incrementTick();
    }
    ASSERT_EQ(TimeKeeper::getBarNumber(), 2U);
    ASSERT_EQ(TimeKeeper::getBeatInBar(), 0U);
}

TEST(TimeKeeper_GetBeatInBar_CalculatesCorrectly) {
    TimeKeeper::reset();

    for (int beat = 0; beat < 16; beat++) {
        ASSERT_EQ(TimeKeeper::getBeatInBar(), beat % 4);

        for (int tick = 0; tick < 24; tick++) {
            TimeKeeper::incrementTick();
        }
    }
}

// ========== MIDI SYNC TESTS ==========

TEST(TimeKeeper_SyncToMIDIClock_CalculatesSamplesPerBeat) {
    TimeKeeper::reset();

    // 120 BPM: tick period = 20833µs
    uint32_t tickPeriodUs = 20833;
    TimeKeeper::syncToMIDIClock(tickPeriodUs);

    // Expected: (20833 * 24 * 44100) / 1000000 = 22050 samples/beat
    ASSERT_NEAR(TimeKeeper::getSamplesPerBeat(), 22050, 1);
}

TEST(TimeKeeper_SyncToMIDIClock_UpdatesBPM) {
    TimeKeeper::reset();

    // 120 BPM
    TimeKeeper::syncToMIDIClock(20833);
    ASSERT_NEAR(TimeKeeper::getBPM(), 120.0f, 0.1f);

    // 140 BPM: tick period = 17857µs
    TimeKeeper::syncToMIDIClock(17857);
    ASSERT_NEAR(TimeKeeper::getBPM(), 140.0f, 1.0f);

    // 90 BPM: tick period = 27778µs
    TimeKeeper::syncToMIDIClock(27778);
    ASSERT_NEAR(TimeKeeper::getBPM(), 90.0f, 1.0f);
}

TEST(TimeKeeper_SyncToMIDIClock_RejectsInvalidTempo) {
    TimeKeeper::reset();

    uint32_t originalSPB = TimeKeeper::getSamplesPerBeat();

    // Too fast (> 300 BPM)
    TimeKeeper::syncToMIDIClock(5000);  // ~694 BPM
    ASSERT_EQ(TimeKeeper::getSamplesPerBeat(), originalSPB);  // Should reject

    // Too slow (< 30 BPM)
    TimeKeeper::syncToMIDIClock(100000);  // ~13.9 BPM
    ASSERT_EQ(TimeKeeper::getSamplesPerBeat(), originalSPB);  // Should reject
}

TEST(TimeKeeper_SetSamplesPerBeat_UpdatesDirectly) {
    TimeKeeper::reset();

    TimeKeeper::setSamplesPerBeat(44100);  // 1 beat per second @ 44.1kHz
    ASSERT_EQ(TimeKeeper::getSamplesPerBeat(), 44100U);
    ASSERT_NEAR(TimeKeeper::getBPM(), 60.0f, 0.1f);
}

// ========== TRANSPORT TESTS ==========

TEST(TimeKeeper_SetTransportState_Updates) {
    TimeKeeper::reset();

    TimeKeeper::setTransportState(TimeKeeper::TransportState::PLAYING);
    ASSERT_EQ(TimeKeeper::getTransportState(), TimeKeeper::TransportState::PLAYING);
    ASSERT_TRUE(TimeKeeper::isRunning());

    TimeKeeper::setTransportState(TimeKeeper::TransportState::RECORDING);
    ASSERT_EQ(TimeKeeper::getTransportState(), TimeKeeper::TransportState::RECORDING);
    ASSERT_TRUE(TimeKeeper::isRunning());

    TimeKeeper::setTransportState(TimeKeeper::TransportState::STOPPED);
    ASSERT_EQ(TimeKeeper::getTransportState(), TimeKeeper::TransportState::STOPPED);
    ASSERT_FALSE(TimeKeeper::isRunning());
}

// ========== QUANTIZATION TESTS ==========

TEST(TimeKeeper_BeatToSample_CalculatesCorrectly) {
    TimeKeeper::reset();
    TimeKeeper::setSamplesPerBeat(22050);  // 120 BPM

    ASSERT_EQ(TimeKeeper::beatToSample(0), 0ULL);
    ASSERT_EQ(TimeKeeper::beatToSample(1), 22050ULL);
    ASSERT_EQ(TimeKeeper::beatToSample(2), 44100ULL);
    ASSERT_EQ(TimeKeeper::beatToSample(10), 220500ULL);
}

TEST(TimeKeeper_BarToSample_CalculatesCorrectly) {
    TimeKeeper::reset();
    TimeKeeper::setSamplesPerBeat(22050);  // 120 BPM

    // 1 bar = 4 beats = 4 * 22050 = 88200 samples
    ASSERT_EQ(TimeKeeper::barToSample(0), 0ULL);
    ASSERT_EQ(TimeKeeper::barToSample(1), 88200ULL);
    ASSERT_EQ(TimeKeeper::barToSample(2), 176400ULL);
}

TEST(TimeKeeper_SampleToBeat_CalculatesCorrectly) {
    TimeKeeper::reset();
    TimeKeeper::setSamplesPerBeat(22050);  // 120 BPM

    ASSERT_EQ(TimeKeeper::sampleToBeat(0), 0U);
    ASSERT_EQ(TimeKeeper::sampleToBeat(22049), 0U);  // Still in beat 0
    ASSERT_EQ(TimeKeeper::sampleToBeat(22050), 1U);  // Start of beat 1
    ASSERT_EQ(TimeKeeper::sampleToBeat(44100), 2U);
    ASSERT_EQ(TimeKeeper::sampleToBeat(220500), 10U);
}

TEST(TimeKeeper_SamplesToNextBeat_CalculatesFromCurrentPosition) {
    TimeKeeper::reset();
    TimeKeeper::setSamplesPerBeat(22050);  // 120 BPM

    // At sample 0, next beat is at 22050
    ASSERT_EQ(TimeKeeper::samplesToNextBeat(), 22050U);

    // Advance to sample 10000
    TimeKeeper::incrementSamples(10000);
    // Next beat at 22050, currently at 10000 → 12050 samples to go
    ASSERT_EQ(TimeKeeper::samplesToNextBeat(), 12050U);

    // Advance to sample 22000 (near beat 1)
    TimeKeeper::incrementSamples(12000);
    ASSERT_EQ(TimeKeeper::samplesToNextBeat(), 50U);
}

TEST(TimeKeeper_SamplesToNextBar_CalculatesFromCurrentPosition) {
    TimeKeeper::reset();
    TimeKeeper::setSamplesPerBeat(22050);  // 120 BPM
    TimeKeeper::incrementTick();  // Advance to beat 0, tick 1

    // At beat 0, next bar is at beat 4 = sample 88200
    uint32_t toNextBar = TimeKeeper::samplesToNextBar();
    ASSERT_NEAR(toNextBar, 88200U, 100);  // Allow small tolerance
}

TEST(TimeKeeper_IsOnBeatBoundary_DetectsBeatStart) {
    TimeKeeper::reset();
    TimeKeeper::setSamplesPerBeat(22050);

    // At sample 0 (beat 0 start)
    ASSERT_TRUE(TimeKeeper::isOnBeatBoundary());

    // Advance to sample 200 (outside 128-sample tolerance window)
    TimeKeeper::incrementSamples(200);
    ASSERT_FALSE(TimeKeeper::isOnBeatBoundary());

    // Advance to beat 1 start (sample 22050)
    TimeKeeper::incrementSamples(21850);
    for (int i = 0; i < 24; i++) {
        TimeKeeper::incrementTick();
    }
    ASSERT_TRUE(TimeKeeper::isOnBeatBoundary());
}

TEST(TimeKeeper_IsOnBarBoundary_DetectsBarStart) {
    TimeKeeper::reset();
    TimeKeeper::setSamplesPerBeat(22050);

    // At bar 0, beat 0
    ASSERT_TRUE(TimeKeeper::isOnBarBoundary());

    // Advance to beat 1 (not a bar boundary)
    TimeKeeper::incrementSamples(22050);
    for (int i = 0; i < 24; i++) {
        TimeKeeper::incrementTick();
    }
    ASSERT_FALSE(TimeKeeper::isOnBarBoundary());

    // Advance to bar 1 (beat 4)
    for (int beat = 1; beat < 4; beat++) {
        TimeKeeper::incrementSamples(22050);
        for (int i = 0; i < 24; i++) {
            TimeKeeper::incrementTick();
        }
    }
    ASSERT_TRUE(TimeKeeper::isOnBarBoundary());
}

// ========== EDGE CASE TESTS ==========

TEST(TimeKeeper_AdvanceToBeat_SkipsBeatAndResetsick) {
    TimeKeeper::reset();

    // Advance partway through beat 0
    for (int i = 0; i < 12; i++) {
        TimeKeeper::incrementTick();
    }
    ASSERT_EQ(TimeKeeper::getBeatNumber(), 0U);
    ASSERT_EQ(TimeKeeper::getTickInBeat(), 12U);

    // Manually advance to next beat
    TimeKeeper::advanceToBeat();
    ASSERT_EQ(TimeKeeper::getBeatNumber(), 1U);
    ASSERT_EQ(TimeKeeper::getTickInBeat(), 0U);
}

TEST(TimeKeeper_MultipleResets_AreIdempotent) {
    TimeKeeper::reset();
    uint64_t pos1 = TimeKeeper::getSamplePosition();

    TimeKeeper::reset();
    uint64_t pos2 = TimeKeeper::getSamplePosition();

    TimeKeeper::reset();
    uint64_t pos3 = TimeKeeper::getSamplePosition();

    ASSERT_EQ(pos1, pos2);
    ASSERT_EQ(pos2, pos3);
    ASSERT_EQ(pos3, 0ULL);
}

// ========== INTEGRATION TEST: SIMULATED AUDIO ISR ==========

TEST(TimeKeeper_Integration_SimulatedAudioCallback) {
    TimeKeeper::reset();
    TimeKeeper::setSamplesPerBeat(22050);  // 120 BPM
    TimeKeeper::setTransportState(TimeKeeper::TransportState::PLAYING);

    // Simulate audio ISR running at ~2.9ms intervals (128 samples @ 44.1kHz)
    // We'll run 172 iterations = 22016 samples ≈ 1 beat

    for (int i = 0; i < 172; i++) {
        TimeKeeper::incrementSamples(128);
    }

    uint64_t finalPos = TimeKeeper::getSamplePosition();
    ASSERT_EQ(finalPos, 172 * 128ULL);  // 22016 samples
    ASSERT_NEAR(finalPos, 22050ULL, 128);  // Within 1 audio block of beat 1
}

// ========== INTEGRATION TEST: SIMULATED MIDI CLOCK STREAM ==========

TEST(TimeKeeper_Integration_SimulatedMIDIClockStream) {
    TimeKeeper::reset();

    // Simulate MIDI clock at 120 BPM (tick every ~20.8ms)
    uint32_t tickPeriodUs = 20833;

    // Sync TimeKeeper
    TimeKeeper::syncToMIDIClock(tickPeriodUs);

    // Simulate 24 MIDI clock ticks (1 beat)
    for (int i = 0; i < 24; i++) {
        TimeKeeper::incrementTick();
    }

    ASSERT_EQ(TimeKeeper::getBeatNumber(), 1U);
    ASSERT_EQ(TimeKeeper::getTickInBeat(), 0U);

    // Simulate another 96 ticks (4 more beats = 1 bar total, now at bar 1)
    for (int i = 0; i < 96; i++) {
        TimeKeeper::incrementTick();
    }

    ASSERT_EQ(TimeKeeper::getBeatNumber(), 5U);
    ASSERT_EQ(TimeKeeper::getBarNumber(), 1U);
    ASSERT_EQ(TimeKeeper::getBeatInBar(), 1U);
}
