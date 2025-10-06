# TimeKeeper Usage Guide

## Overview

**TimeKeeper** is the centralized timing authority that provides a **single source of truth** for all timing in MicroLoop. It bridges the MIDI world (24 PPQN ticks) and the audio world (44.1kHz samples), enabling sample-accurate quantization.

## Key Concepts

### Audio Timeline (Sample-Based)

- **Sample Position**: Monotonically increasing counter of audio samples processed
- Incremented by audio ISR every 128-sample block (~2.9ms)
- Never resets (until MIDI START) - provides absolute timing reference
- Used for: Loop record/playback, sample triggers, quantization

### MIDI Timeline (Beat-Based)

- **Beat Number**: Current beat (0, 1, 2, 3...)
- **Tick In Beat**: MIDI clock tick within current beat (0-23)
- **Bar Number**: Current bar (4 beats per bar in 4/4 time)
- **Samples Per Beat**: Calibrated from MIDI clock period (dynamic tempo tracking)
- Used for: Beat/bar boundaries, BPM display, transport control

### The Bridge: `syncToMIDIClock()`

Converts MIDI tick period (microseconds) to samples per beat:

```
tickPeriodUs = 20833µs  (@ 120 BPM)
beatPeriodUs = tickPeriodUs × 24 = 500000µs = 0.5s
samplesPerBeat = beatPeriodUs × (44100 / 1e6) = 22050 samples
```

This allows TimeKeeper to predict future beat boundaries in sample-accurate coordinates.

## Usage Examples

### Query Current Timing

```cpp
#include "timekeeper.h"

// Where are we in the audio timeline?
uint64_t currentSample = TimeKeeper::getSamplePosition();
// -> 123456 samples (~2.8 seconds @ 44.1kHz)

// Where are we in the MIDI timeline?
uint32_t beat = TimeKeeper::getBeatNumber();      // -> 5 (sixth beat)
uint32_t bar = TimeKeeper::getBarNumber();        // -> 1 (second bar)
uint32_t beatInBar = TimeKeeper::getBeatInBar();  // -> 1 (second beat of bar)
uint32_t tick = TimeKeeper::getTickInBeat();      // -> 12 (halfway through beat)

// What's the current tempo?
float bpm = TimeKeeper::getBPM();                 // -> 120.5 BPM
uint32_t spb = TimeKeeper::getSamplesPerBeat();   // -> 22050 samples/beat
```

### Quantize to Next Beat

```cpp
// User presses "record" button at sample 15000
// Don't start recording immediately - wait for next beat boundary

uint32_t samplesToWait = TimeKeeper::samplesToNextBeat();
// -> 7050 samples (~160ms @ 44.1kHz)

uint64_t recordStartSample = TimeKeeper::getSamplePosition() + samplesToWait;
// -> 22050 (exactly on beat 1)

// In audio callback: check if we've reached the quantized start point
void audioCallback() {
    if (TimeKeeper::getSamplePosition() >= recordStartSample) {
        startRecording();  // Perfectly aligned to beat!
    }
}
```

### Calculate Beat Boundaries Ahead of Time

```cpp
// Schedule events at specific beats (e.g., trigger samples on beats 0, 4, 8, 12)
for (uint32_t beat = 0; beat < 16; beat += 4) {
    uint64_t triggerSample = TimeKeeper::beatToSample(beat);
    scheduleEvent(triggerSample, KICK_DRUM);
}
```

### Check if On Beat/Bar Boundary

```cpp
// In audio callback - trigger event exactly on downbeat
void audioCallback() {
    if (TimeKeeper::isOnBeatBoundary()) {
        // We're within ±128 samples of a beat boundary
        blinkLED();
    }

    if (TimeKeeper::isOnBarBoundary()) {
        // We're on the downbeat (first beat of bar)
        playAccent();
    }
}
```

### Transport Control

```cpp
// MIDI START received
TimeKeeper::reset();  // Reset to sample 0, beat 0
TimeKeeper::setTransportState(TimeKeeper::TransportState::PLAYING);

// MIDI STOP received
TimeKeeper::setTransportState(TimeKeeper::TransportState::STOPPED);

// Check if running
if (TimeKeeper::isRunning()) {
    // Process audio normally
}
```

## Integration Points

### Audio ISR (Automatically Handled)

The `AudioTimeKeeper` object sits in the audio graph and automatically increments the sample counter:

```cpp
// In src/main.cpp
AudioInputI2S i2s_in;
AudioTimeKeeper timekeeper;  // Tracks sample position
AudioOutputI2S i2s_out;

AudioConnection c1(i2s_in, 0, timekeeper, 0);
AudioConnection c2(timekeeper, 0, i2s_out, 0);
```

**You don't need to call `incrementSamples()` manually** - it's done in the audio ISR automatically.

### MIDI Clock Sync (App Thread)

In `app_logic.cpp`, every MIDI clock tick syncs TimeKeeper:

```cpp
// Already implemented in app_logic.cpp
void AppLogic::threadLoop() {
    while (MidiIO::popClock(clockMicros)) {
        // Update EMA tick period
        avgTickPeriodUs = (avgTickPeriodUs * 9 + tickPeriod) / 10;

        // Sync TimeKeeper (converts ticks → samples)
        TimeKeeper::syncToMIDIClock(avgTickPeriodUs);

        // Increment tick/beat counters
        TimeKeeper::incrementTick();  // Auto-advances beat at tick 24
    }
}
```

## Debugging

### Serial Commands

Type in serial monitor:
- `s` - Show TimeKeeper status (sample position, beat, BPM, etc.)
- `t` - Dump trace buffer (see MIDI sync events, beat advances)

### Example Status Output

```
=== TimeKeeper Status ===
Sample Position: 1323000
Beat: 30 (Bar 7, Beat 2, Tick 15)
BPM: 120.25
Samples/Beat: 22042
Transport: PLAYING
Samples to next beat: 6300
Samples to next bar: 50400
=========================
```

### Trace Events

TimeKeeper generates these trace events:
- `TRACE_TIMEKEEPER_SYNC` - MIDI clock synced (value = BPM)
- `TRACE_TIMEKEEPER_TRANSPORT` - Transport state changed
- `TRACE_TIMEKEEPER_BEAT_ADVANCE` - Beat counter advanced

## Future Features Using TimeKeeper

### Loop Recording (Quantized)

```cpp
// User presses record - quantize to next bar
uint32_t samplesToBar = TimeKeeper::samplesToNextBar();
uint64_t recordStart = TimeKeeper::getSamplePosition() + samplesToBar;

// In audio callback
if (getSamplePosition() >= recordStart && getSamplePosition() < recordStart + loopLength) {
    recordBuffer[getSamplePosition() - recordStart] = inputSample;
}
```

### Sample Triggering (MIDI Note → Sample Playback)

```cpp
// MIDI note received - trigger sample on next beat
void onMidiNote(uint8_t note) {
    uint32_t samplesToNext = TimeKeeper::samplesToNextBeat();
    uint64_t triggerSample = TimeKeeper::getSamplePosition() + samplesToNext;

    scheduleSample(note, triggerSample);
}
```

### BPM Display (Already Implemented!)

```cpp
float bpm = TimeKeeper::getBPM();
display.print(bpm, 1);  // "120.2"
```

### Tempo Automation (Future)

```cpp
// Gradually change tempo over 4 bars
for (uint32_t bar = 0; bar < 4; bar++) {
    float bpm = 120.0f + (bar * 5.0f);  // 120 → 135 BPM
    uint32_t spb = (44100 * 60) / bpm;

    uint64_t barSample = TimeKeeper::barToSample(bar);
    scheduleTempoChange(barSample, spb);
}
```

## Thread Safety

All TimeKeeper methods are **lock-free and wait-free**:

- ✅ Safe to call from audio ISR (incrementSamples)
- ✅ Safe to call from app thread (sync, queries)
- ✅ Concurrent reads/writes use atomic operations
- ✅ No blocking, no mutexes, no priority inversion

## Performance

- **incrementSamples()**: ~20 CPU cycles (~33ns @ 600MHz)
- **syncToMIDIClock()**: ~200 CPU cycles (~333ns)
- **Query methods**: ~10 CPU cycles (atomic load)

All methods are **real-time safe** and suitable for use in time-critical paths.

## Common Pitfalls

### ❌ Don't Reset Sample Counter Arbitrarily

```cpp
// BAD: Breaks absolute timing reference
TimeKeeper::reset();  // Only call on MIDI START!
```

Sample counter should only reset on MIDI START. It provides a monotonic timeline for loop recording.

### ❌ Don't Assume Fixed Tempo

```cpp
// BAD: Assumes tempo never changes
const uint32_t samplesPerBeat = 22050;  // Wrong if tempo != 120 BPM
```

Always query `getSamplesPerBeat()` dynamically - tempo can change at any time.

### ❌ Don't Use Microseconds for Sample-Accurate Timing

```cpp
// BAD: Microseconds drift, samples don't
uint32_t nextBeatUs = micros() + 500000;  // Inaccurate
```

Use sample positions for all audio timing. Microseconds are only used for MIDI → sample conversion.

### ✅ Do Use Sample Positions for All Audio Events

```cpp
// GOOD: Sample-accurate timing
uint64_t loopStartSample = TimeKeeper::beatToSample(0);
uint64_t loopEndSample = TimeKeeper::beatToSample(4);
```

## Summary

TimeKeeper provides:
1. **Single source of timing truth** (audio samples + MIDI beats)
2. **Sample-accurate quantization** (know exactly when next beat occurs)
3. **Dynamic tempo tracking** (synced to MIDI clock via EMA)
4. **Lock-free real-time safety** (safe in ISR and threads)
5. **Transport control** (PLAYING/STOPPED/RECORDING states)

All future features (looper, sampler, effects) should query TimeKeeper for timing - never maintain separate timing state!
