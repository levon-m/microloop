# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**MicroLoop** is a live-performance looper/sampler for Teensy 4.1, designed for real-time audio processing with sample-accurate timing. The system receives MIDI clock from an Elektron Digitakt, processes audio through an SGTL5000 codec (Audio Shield Rev D), and will eventually support loop recording, playback, and sample triggering.

**Current Status:**
- ✅ MIDI clock tracking with professional-grade jitter smoothing (EMA)
- ✅ Timestamp-based beat indicator (LED on pin 31)
- ✅ **TimeKeeper: Single source of timing truth** (MIDI ↔ audio samples)
- ✅ **Sample-accurate quantization API** (samplesToNextBeat, beatToSample, etc.)
- ✅ **Trace utility for real-time debugging** (lock-free event logging)
- ✅ Audio passthrough with sample position tracking
- ✅ Custom minimal CMake build system (Ninja)
- ✅ Transport control (PLAYING/STOPPED states synced to MIDI)

**Future Features:** Loop recording/playback (quantized), sample triggering, BPM display, effects

## Build System

### CMake Build (Custom, No teensy-cmake-macros)

The project uses a **custom minimal CMake configuration** that directly compiles Teensy libraries without dependencies on teensy-cmake-macros:

```bash
# One-time setup
mkdir build && cd build
cmake -G "Ninja" -DCMAKE_TOOLCHAIN_FILE=../cmake/teensy41.cmake ..

# Build (incremental, fast)
ninja

# Upload (use Teensy Loader GUI)
# File → Open HEX → build/microloop.hex
```

**Required tools:**
- CMake 3.16+
- Ninja build system
- arm-none-eabi-gcc toolchain (from Arduino IDE 2.x or Teensyduino)
- Teensy Loader (GUI or CLI) for uploading

**Toolchain location (Arduino IDE 2.x on Windows):**
- Path: `C:/Users/[username]/AppData/Local/Arduino15/packages/teensy/tools/teensy-compile/11.3.1/arm/bin/`
- Already configured in `cmake/teensy41.cmake`

**Build output:**
- `build/microloop.elf` - Firmware binary with debug symbols
- `build/microloop.hex` - Intel HEX format for Teensy Loader

**Why custom CMake vs teensy-cmake-macros?**
- ✅ No external dependencies (more reliable)
- ✅ Full control over compiler flags and linking
- ✅ Faster incremental builds
- ✅ Better understanding of build process

## Architecture

### Thread Model

Three execution contexts with strict separation of concerns:

1. **Audio ISR** (Highest Priority)
   - Runs via Teensy Audio Library (timer-driven DMA)
   - Pre-empts all other code
   - Currently: Simple stereo passthrough
   - Future: Loop playback, effects processing
   - **Critical:** Keep ISR work minimal (<1ms per block)

2. **I/O Thread** (High Priority)
   - MIDI parsing via `MidiIO::threadLoop()` (src/midi_io.cpp:104)
   - Pumps DIN MIDI parser, pushes events to lock-free queues
   - Stack: 2KB
   - Time slice: 2ms (very responsive)
   - **Never blocks:** Uses SPSC queues for handoff

3. **App Thread** (Normal Priority)
   - Application logic via `AppLogic::threadLoop()` (src/app_logic.cpp:57)
   - Drains queues, tracks beats with timestamp-based smoothing, drives LED
   - Stack: 3KB
   - Time slice: 2ms
   - **Can afford latency:** Safe for Serial.print, UI updates

### Lock-Free Communication

All inter-thread data passing uses **Single Producer Single Consumer (SPSC)** queues:

**Implementation:** `utils/spsc_queue.h`
- Power-of-2 ring buffer with bitwise masking (no modulo)
- Volatile indices for cross-thread visibility
- Wait-free: O(1) push/pop, no blocking
- Compile-time size checking

**Queue sizing:**
- Clock queue: 256 slots (~5 seconds @ 120 BPM) - absorbs burst arrivals
- Event queue: 32 slots (~3+ seconds of transport events)

**Why SPSC over mutexes?**
- ✅ Real-time safe: Bounded execution time
- ✅ No priority inversion
- ✅ Cache-friendly (single writer per cache line)
- ❌ Limited to single producer/consumer (acceptable for our use case)

### MIDI Clock Handling with Professional Timing

**MIDI Timing:** 24 PPQN (Pulses Per Quarter Note)
- 1 beat = 24 clock ticks
- At 120 BPM: Ticks arrive every ~20.8ms

**Professional-grade jitter smoothing:**
- Timestamps captured with `micros()` in MIDI handler
- **Exponential Moving Average (EMA)** calculates tick period: `avgTickPeriodUs`
- Alpha = 0.1 (smooths jitter, converges in ~10 ticks)
- Sanity check: Rejects outliers outside 60-300 BPM range (10-50ms tick period)
- Foundation for sample-accurate quantization (future)

**LED beat indicator (pin 31):**
- Short pulse (2 ticks = ~40ms @ 120 BPM) at beat start
- Rock-solid timing despite MIDI jitter from hardware chain
- Pauses on MIDI STOP, resumes on START/CONTINUE
- Uses tick count for simplicity (LED ON at tick 0, OFF at tick 2)

**Why timestamp-based timing?**
- Immune to MIDI jitter from hardware THRU chains (Digitakt → K-2 → Edge → Teensy)
- LED timing stays smooth even when MIDI ticks arrive irregularly
- Provides accurate BPM calculation: `60,000,000 / (avgTickPeriodUs * 24)`
- Critical for sample-accurate loop quantization

### TimeKeeper: Single Source of Timing Truth

**Purpose:** Centralized timing authority that bridges MIDI clock (24 PPQN) and audio samples (44.1kHz)

**Implementation:** `utils/timekeeper.h`, `utils/timekeeper.cpp`, `include/audio_timekeeper.h`

**Design:**
- **Audio timeline:** Monotonic sample counter (incremented by audio ISR every 128 samples)
- **MIDI timeline:** Beat/bar position, synced from MIDI clock
- **Lock-free:** Atomic operations for cross-thread safety (audio ISR + app thread)
- **Sample-accurate:** All timing in samples, not microseconds

**Key Features:**
1. **Sample position tracking**
   - Absolute sample count since audio start (or MIDI START)
   - Incremented automatically via `AudioTimeKeeper` object in audio graph
   - Provides monotonic timeline for loop recording/playback

2. **MIDI-to-sample conversion**
   - `syncToMIDIClock(tickPeriodUs)` converts MIDI ticks → samples per beat
   - Formula: `samplesPerBeat = (tickPeriodUs * 24 * 44100) / 1e6`
   - Example: 120 BPM → 22050 samples/beat
   - Updated every MIDI tick via EMA smoothing

3. **Quantization API**
   - `samplesToNextBeat()` - Returns samples until next beat boundary
   - `samplesToNextBar()` - Returns samples until next bar (4 beats)
   - `beatToSample(beat)` - Converts beat number to sample position
   - `isOnBeatBoundary()` - Checks if within ±128 samples of beat

4. **Beat/bar tracking**
   - `getBeatNumber()` - Current beat (0, 1, 2, 3...)
   - `getBarNumber()` - Current bar (4 beats per bar in 4/4 time)
   - `getBeatInBar()` - Beat within bar (0-3)
   - `getTickInBeat()` - MIDI tick within beat (0-23)

5. **Transport control**
   - States: STOPPED, PLAYING, RECORDING
   - Synced to MIDI START/STOP/CONTINUE
   - `isRunning()` - Check if transport active

**Usage example (quantize record to next beat):**
```cpp
// User presses record at sample 15000
uint32_t samplesToWait = TimeKeeper::samplesToNextBeat();  // 7050 samples
uint64_t recordStart = TimeKeeper::getSamplePosition() + samplesToWait;  // 22050 (beat 1)

// In audio callback
if (TimeKeeper::getSamplePosition() >= recordStart) {
    startRecording();  // Sample-accurate!
}
```

**Why centralized timing?**
- ✅ Single source of truth (no drift between features)
- ✅ Sample-accurate quantization (know exact sample of next beat)
- ✅ Dynamic tempo tracking (handles BPM changes smoothly)
- ✅ Simplifies future features (loop, sampler, etc. all query TimeKeeper)

**Performance:**
- `incrementSamples()`: ~20 CPU cycles (~33ns @ 600MHz)
- `syncToMIDIClock()`: ~200 CPU cycles (~333ns)
- All methods lock-free and real-time safe

**Serial commands:**
- Type `s` in serial monitor to see TimeKeeper status (sample position, beat, BPM, etc.)

**See:** `utils/TIMEKEEPER_USAGE.md` for detailed usage guide

### Trace Utility: Wait-Free Event Logging

**Purpose:** Lock-free event tracing for real-time debugging (safe in ISR)

**Implementation:** `utils/trace.h`, `utils/trace.cpp`

**Design:**
- Circular buffer (1024 events = 8KB RAM)
- Each event: `{timestamp, eventId, value}` (8 bytes)
- Wait-free recording (~10-20 CPU cycles)
- Overwrites oldest events when full

**Usage:**
```cpp
TRACE(TRACE_MIDI_CLOCK_RECV);                    // Record event
TRACE(TRACE_TICK_PERIOD_UPDATE, avgTickPeriodUs / 10);  // Event with value
```

**Serial commands:**
- `t` - Dump trace buffer (chronological list of events with timestamps)
- `c` - Clear trace buffer

**Predefined events:**
- MIDI: Clock recv/queued/dropped, START/STOP/CONTINUE
- Beat tracking: Beat start, LED on/off, tick period updates
- TimeKeeper: MIDI sync, transport changes, beat advances
- Audio: Callback invoked, buffer underruns (future)

**Use cases:**
- Debug MIDI timing jitter
- Verify quantization accuracy
- Measure thread latency (MIDI recv → app thread processing)
- Analyze queue depths over time

**Compile-time control:**
- Define `TRACE_ENABLED=0` to compile out all tracing (zero overhead)

**See:** `utils/TRACE_USAGE.md` for detailed usage guide

### Audio Path

**Topology:** I2S Input → I2S Output (stereo passthrough)
- Sample rate: 44.1kHz
- Block size: 128 samples (~2.9ms per block)
- Latency: ~6ms (input + output buffering)

**SGTL5000 Codec:** Custom minimal driver (include/SGTL5000.h, src/SGTL5000.cpp)
- I2C configuration (400kHz)
- I2S slave mode (Teensy is master)
- Line input → Headphone/Line output
- 0dB gain on all paths

**Why custom codec driver?**
- Audio Library driver is ~10KB (bloated)
- We only need basic I/O
- Better understanding of hardware
- Easier to extend (volume control, EQ, etc.)

## Testing

### Test Philosophy

**On-Device Testing:** All tests run directly on Teensy hardware (no PC-side mocking). This ensures:
- Real hardware behavior (timing, interrupts, atomics)
- No mocking complexity
- Tests match production environment exactly

**Test Coverage:**
- ✅ **Unit Tests**: TimeKeeper (30+ tests), Trace (7 tests), SPSC Queue (10 tests)
- ✅ **Integration Tests**: Full system with MIDI input, audio passthrough
- ✅ **Performance Tests**: Timing measurements for real-time safety

### Running Tests

1. **Modify CMakeLists.txt** (temporarily):
   ```cmake
   # Comment out normal main
   # add_executable(microloop.elf src/main.cpp)

   # Use test main instead
   add_executable(microloop.elf tests/run_tests.cpp)
   ```

2. **Build and upload**:
   ```bash
   cd build
   ninja
   # Upload microloop.hex with Teensy Loader
   ```

3. **Open Serial Monitor** (115200 baud) - you'll see:
   ```
   ╔════════════════════════════════════════╗
   ║    MicroLoop On-Device Test Suite     ║
   ╚════════════════════════════════════════╝

   [ RUN      ] TimeKeeper_Begin_InitializesState
   [       OK ] TimeKeeper_Begin_InitializesState
   ...
   ========================================
   Tests run: 47
   Passed: 47
   Failed: 0
   Duration: 145 ms
   ========================================

   ✓ All tests passed!
   ```

### Test Structure

Tests are written using a simple macro-based framework in `tests/test_runner.h`:

```cpp
TEST(ModuleName_FunctionName_Behavior) {
    // Arrange: Set up test conditions
    TimeKeeper::reset();

    // Act: Perform the operation
    TimeKeeper::incrementSamples(128);

    // Assert: Verify expected outcome
    ASSERT_EQ(TimeKeeper::getSamplePosition(), 128ULL);
}
```

**Available assertions:**
- `ASSERT_TRUE(condition)` / `ASSERT_FALSE(condition)`
- `ASSERT_EQ(actual, expected)` - Equality
- `ASSERT_NE(actual, expected)` - Inequality
- `ASSERT_LT(actual, expected)` - Less than
- `ASSERT_GT(actual, expected)` - Greater than
- `ASSERT_NEAR(actual, expected, tolerance)` - Floating point equality

### Test Files

```
tests/
├── test_runner.h        # Test framework (macros, assertions)
├── test_timekeeper.cpp  # TimeKeeper unit tests (30+ tests)
├── test_trace.cpp       # Trace utility tests (7 tests)
├── test_spsc_queue.cpp  # SPSC queue tests (10 tests)
├── run_tests.cpp        # Main test entry point
└── TESTING.md          # Comprehensive testing documentation
```

### Writing New Tests

1. **Create test file**: `tests/test_your_module.cpp`
2. **Write tests using TEST() macro**
3. **Include in run_tests.cpp**: `#include "test_your_module.cpp"`
4. **Rebuild and run**

**Test naming convention**: `ModuleName_FunctionUnderTest_ExpectedBehavior`

Examples:
- `TimeKeeper_IncrementSamples_UpdatesPosition`
- `SPSCQueue_PushPop_MaintainsOrder`
- `Trace_OverflowHandling_WrapsAround`

### Integration Testing

Beyond unit tests, manual integration tests verify real-world operation:

**Test 1: MIDI Clock Reception**
- Connect MIDI clock source (Digitakt, DAW, etc.)
- Send MIDI START
- Verify: Serial shows `▶ START`, LED blinks on beat

**Test 2: TimeKeeper Accuracy**
- Start MIDI clock @ 120 BPM
- Type `s` in serial monitor after 10 seconds
- Verify: BPM shows ~120, sample position ~441000

**Test 3: Trace Verification**
- Type `c` to clear trace
- Wait 5 seconds
- Type `t` to dump trace
- Verify: Events in chronological order, no DROPPED events

See `tests/TESTING.md` for complete integration test procedures.

### Pre-Commit Testing Checklist

Before committing changes:

1. ✅ **Run unit tests** - All tests must pass
2. ✅ **Run integration test** - MIDI clock reception works
3. ✅ **Check serial output** - No assertion failures or warnings
4. ✅ **Verify trace** - Type `t`, check for healthy operation

### Regression Testing

When fixing a bug:
1. Write a failing test that reproduces the bug
2. Fix the bug
3. Verify test now passes
4. Keep the test to prevent regression

## Code Structure

```
TeensyAudioTools/
├── src/                    # Implementation files
│   ├── main.cpp           # Entry point, thread setup
│   ├── midi_io.cpp        # MIDI I/O thread
│   ├── app_logic.cpp      # App thread, timestamp-based beat tracking
│   └── SGTL5000.cpp       # Codec driver
├── include/               # Public headers
│   ├── midi_io.h
│   ├── app_logic.h
│   ├── SGTL5000.h
│   └── audio_timekeeper.h # Audio object for TimeKeeper integration
├── utils/                 # Generic reusable utilities
│   ├── spsc_queue.h       # Lock-free SPSC queue (POD only)
│   ├── span.h             # Buffer view (C++17 backport)
│   ├── timekeeper.h/cpp   # Centralized timing authority
│   ├── trace.h/cpp        # Lock-free event tracing
│   ├── TIMEKEEPER_USAGE.md  # TimeKeeper usage guide
│   └── TRACE_USAGE.md     # Trace utility usage guide
├── tests/                 # On-device test suite
│   ├── test_runner.h      # Test framework
│   ├── test_timekeeper.cpp  # TimeKeeper tests (30+)
│   ├── test_trace.cpp     # Trace tests (7)
│   ├── test_spsc_queue.cpp  # SPSC queue tests (10)
│   ├── run_tests.cpp      # Test entry point
│   └── TESTING.md         # Testing documentation
├── build/                 # CMake build output (git-ignored)
├── cmake/                 # Build configuration
│   └── teensy41.cmake    # Toolchain file for Teensy 4.1
└── CMakeLists.txt        # Main build file (custom, minimal)
```

**Module boundaries:**
- `utils/`: Generic, testable, no hardware dependencies
- `src/`: Application-specific, hardware-aware
- `include/`: Public APIs, thread interfaces
- `tests/`: On-device test suite

## Real-Time Safety Guidelines

### Forbidden in Real-Time Contexts (ISR, I/O Thread)
- ❌ Exceptions (`-fno-exceptions`)
- ❌ RTTI (`-fno-rtti`)
- ❌ `malloc`/`free`/`new`/`delete`
- ❌ Mutexes, semaphores, blocking calls
- ❌ `Serial.print` (100-500µs, can glitch audio)
- ❌ Unbounded loops (use fixed iteration counts)

### Allowed in App Thread (Lower Priority)
- ✅ `Serial.print` (rate-limited, e.g., 1Hz)
- ✅ Bounded allocation (but avoid if possible)
- ✅ `threads.delay()` (yields to other threads)

### Performance Tips
- Use `constexpr` for compile-time computation
- Prefer lookup tables over `sin()`/`sqrt()` in audio path
- Inline trivial functions (`inline`, `constexpr`)
- Profile with `micros()` timestamps, not assumptions

## Common Tasks

### Building and Uploading
```bash
# Rebuild after code changes
cd build
ninja

# Upload with Teensy Loader
# 1. Open Teensy Loader application
# 2. File → Open HEX File → select build/microloop.hex
# 3. Connect Teensy 4.1 via USB
# 4. Press physical button on Teensy to enter bootloader mode
# 5. Teensy Loader uploads automatically
```

### Viewing Serial Output (VS Code)
- Install "Serial Monitor" extension by Microsoft
- Click plug icon in status bar
- Select Teensy COM port
- Set baud rate: 115200

### Adding a New MIDI Event Type
1. Add enum to `include/midi_io.h:16`
2. Create handler in `src/midi_io.cpp` (push to event queue)
3. Register in `MidiIO::begin()` (src/midi_io.cpp:77)
4. Handle in `AppLogic::threadLoop()` (src/app_logic.cpp:78)

### Tuning LED Pulse Duration
Edit `src/app_logic.cpp:183`:
```cpp
if (tickCount == 2)  // Current: 2 ticks (~40ms @ 120 BPM)
if (tickCount == 1)  // Shorter: 1 tick (~20ms)
```

### Adjusting EMA Smoothing
Edit `src/app_logic.cpp:165`:
```cpp
avgTickPeriodUs = (avgTickPeriodUs * 9 + tickPeriod) / 10;  // Alpha = 0.1
avgTickPeriodUs = (avgTickPeriodUs * 19 + tickPeriod) / 20; // Alpha = 0.05 (smoother, slower convergence)
```

## Hardware Configuration

### Teensy 4.1
- CPU: ARM Cortex-M7 @ 600 MHz
- RAM: 1 MB (plenty for audio buffers)
- Flash: 8 MB (current usage: 45 KB / 0.57%)

### Audio Shield Rev D
- Codec: SGTL5000 (I2C addr 0x0A)
- Sample rates: 44.1kHz / 48kHz (we use 44.1kHz)
- Line input: Pins 18, 19 (I2S LRCLK, BCLK)
- Line output: Headphone jack + Line out

### MIDI DIN
- Serial8: RX=pin 34, TX=pin 35
- Baud: 31250 (MIDI standard)
- Source: Elektron Digitakt (clock master)
- Chain: Digitakt → Behringer K-2 THRU → Edge THRU → Teensy

### LED Beat Indicator
- **Pin 31** (external LED)
- Short pulse (2 ticks ≈ 40ms) at beat start
- Timestamp-based timing for rock-solid accuracy

## Future Architecture Notes

### Loop Recording (Planned)
- Circular buffer in SDRAM (Teensy 4.1 has expansion pins)
- Record trigger: MIDI note or footswitch
- **Quantize to beat boundary using `beatStartMicros` timestamps**
- Overdub: Mix new audio with existing loop

### Sample Playback (Planned)
- Triggered via MIDI notes
- One-shot or looping modes
- Pitch shifting (resampling or granular)
- Envelopes (ADSR)

### BPM Display (Planned)
- Calculate from `avgTickPeriodUs`: `60,000,000 / (avgTickPeriodUs * 24)`
- Already smoothed via EMA (no additional filtering needed)
- Display on 7-segment or OLED

### Quantization (Planned)
- Align record/playback to beat/bar boundaries
- Use `beatStartMicros` + `avgTickPeriodUs` for sample-accurate timing
- Pre-roll/latency compensation

## Known Status / Notes

### Working Features
- ✅ Custom CMake build system (Ninja, no teensy-cmake-macros dependency)
- ✅ MIDI clock reception with professional jitter smoothing (EMA)
- ✅ Timestamp-based beat tracking
- ✅ LED beat indicator on pin 31 (short pulse, rock-solid timing)
- ✅ Audio passthrough (44.1kHz, 128-sample blocks)
- ✅ Multi-threaded architecture (Audio ISR + I/O thread + App thread)
- ✅ Lock-free SPSC queues for real-time communication
- ✅ **Comprehensive on-device test suite (47+ tests)**
- ✅ **TimeKeeper, Trace, and SPSC queue fully tested**

### Build System Notes
- Toolchain path configured for Arduino IDE 2.x (Windows)
- Uses standard Teensy libraries from Arduino15 folder
- Excludes SD card Audio library files (not needed for current features)
- Binary size: ~45 KB (plenty of room for future features)

### Future Improvements
- [ ] Implement BPM calculation and display
- [ ] Add loop recording with beat-quantized start/stop
- [ ] Implement sample playback engine
- [ ] Add volume control to SGTL5000 driver
- [ ] Stack usage monitoring (watermarking)
- [ ] SPSC queue overrun detection (debug builds)
- [ ] Automated CI testing (GitHub Actions with PlatformIO)
