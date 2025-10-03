# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**MicroLoop** is a live-performance looper/sampler for Teensy 4.1, designed for real-time audio processing with sample-accurate timing. The system receives MIDI clock from an Elektron Digitakt, processes audio through an SGTL5000 codec (Audio Shield Rev D), and will eventually support loop recording, playback, and sample triggering.

**Current Status:** Basic infrastructure complete - MIDI clock tracking, beat indicator, audio passthrough

**Future Features:** Loop recording/playback, sample triggering, BPM display, quantized operations

## Build System

### CMake-based Build (Recommended)

The project uses CMake with `teensy-cmake-macros` for professional embedded workflow:

```bash
# Configure build
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/teensy41.cmake ..

# Build firmware
make

# Upload to Teensy (requires teensy_loader_cli)
make upload
```

**Required tools:**
- CMake 3.16+
- arm-none-eabi-gcc toolchain (from Teensyduino)
- teensy_loader_cli (for uploading)

**Toolchain configuration:** Adjust `cmake/teensy41.cmake` if your arm-none-eabi-gcc is in a non-standard location.

### Arduino IDE (Legacy)

The old `PerformanceTool/` can still be opened in Arduino IDE, but new development should use CMake.

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
   - Time slice: 10ms default (tunable)
   - **Never blocks:** Uses SPSC queues for handoff

3. **App Thread** (Normal Priority)
   - Application logic via `AppLogic::threadLoop()` (src/app_logic.cpp:42)
   - Drains queues, tracks beats, drives LED indicator
   - Stack: 3KB
   - Time slice: 10ms default
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

### MIDI Clock Handling

**MIDI Timing:** 24 PPQN (Pulses Per Quarter Note)
- 1 beat = 24 clock ticks
- At 120 BPM: Ticks arrive every ~20ms

**Jitter mitigation:**
- Timestamp ticks in `onClock()` handler with `micros()`
- Store timestamps (not just counts) for sub-tick precision
- Future: Calculate BPM per-beat (24 ticks) to average out jitter
- Why jitter exists: MIDI parser runs in thread (not ISR), subject to preemption

**Current feature:** LED beat indicator
- Blinks on every beat (24 ticks)
- 50% duty cycle (12 ticks ON, 12 ticks OFF)
- Pauses on MIDI STOP, resumes on START/CONTINUE

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

## Code Structure

```
TeensyAudioTools/
├── src/                    # Implementation files
│   ├── main.cpp           # Entry point, thread setup
│   ├── midi_io.cpp        # MIDI I/O thread
│   ├── app_logic.cpp      # App thread, beat tracking
│   └── SGTL5000.cpp       # Codec driver
├── include/               # Public headers
│   ├── midi_io.h
│   ├── app_logic.h
│   └── SGTL5000.h
├── utils/                 # Generic reusable utilities
│   ├── spsc_queue.h      # Lock-free SPSC queue (POD only)
│   └── span.h            # Buffer view (C++17 backport)
├── PerformanceTool/       # Legacy Arduino project (deprecated)
├── cmake/                 # Build configuration
└── CMakeLists.txt        # Main build file
```

**Module boundaries:**
- `utils/`: Generic, testable, no hardware dependencies
- `src/`: Application-specific, hardware-aware
- `include/`: Public APIs, thread interfaces

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

### Adding a New MIDI Event Type
1. Add enum to `include/midi_io.h:16`
2. Create handler in `src/midi_io.cpp` (push to event queue)
3. Register in `MidiIO::begin()` (src/midi_io.cpp:77)
4. Handle in `AppLogic::threadLoop()` (src/app_logic.cpp:52)

### Tuning Thread Responsiveness
Adjust time slices in `src/main.cpp:139`:
```cpp
threads.setTimeSlice(ioThreadId, 2);   // 2ms - very responsive
threads.setTimeSlice(appThreadId, 5);  // 5ms - moderate
```

**Tradeoff:**
- Smaller slice → More responsive, more context switch overhead
- Larger slice → Less overhead, chunkier execution

### Debugging Stack Overflow
Symptoms: Random crashes, hard faults, erratic behavior

Solutions:
1. Increase stack size in `src/main.cpp:128-129`
2. Avoid `Serial.printf("%f", ...)` (use fixed-point integer instead)
3. Minimize recursion and large local arrays
4. Future: Implement stack watermarking

### Measuring Audio Latency
```cpp
// In audio callback:
uint32_t start = ARM_DWT_CYCCNT;  // CPU cycle counter
// ... processing ...
uint32_t cycles = ARM_DWT_CYCCNT - start;
float micros = cycles / (F_CPU / 1000000.0f);
```

### Using Span for Buffer Safety
```cpp
#include "span.h"

void processAudio(Span<float> left, Span<float> right) {
    for (size_t i = 0; i < left.size(); i++) {
        left[i] *= 0.5f;  // Bounds-checked in debug builds
    }
}

// Usage:
float buffer[128];
processAudio(makeSpan(buffer), makeSpan(buffer));
```

## Hardware Configuration

### Teensy 4.1
- CPU: ARM Cortex-M7 @ 600 MHz
- RAM: 1 MB (plenty for audio buffers)
- Flash: 8 MB

### Audio Shield Rev D
- Codec: SGTL5000 (I2C addr 0x0A)
- Sample rates: 44.1kHz / 48kHz (we use 44.1kHz)
- Line input: Pins 18, 19 (I2S LRCLK, BCLK)
- Line output: Headphone jack + Line out

### MIDI DIN
- Serial8: RX=pin 34, TX=pin 35
- Baud: 31250 (MIDI standard)
- Source: Elektron Digitakt (clock master)

### LED Indicator
- Pin 13 (built-in LED)
- Blinks on every beat (24 MIDI clock ticks)

## Development Workflow

1. **Make changes** in `src/`, `include/`, or `utils/`
2. **Build:** `cd build && make`
3. **Upload:** `make upload` (or use Teensy Loader GUI)
4. **Monitor:** `screen /dev/ttyACM0 115200` (adjust port for your system)
5. **Debug:** Serial prints in app thread, logic analyzer for MIDI/I2S

## Future Architecture Notes

### Loop Recording (Planned)
- Circular buffer in SDRAM (Teensy 4.1 has expansion pins)
- Record trigger: MIDI note or footswitch
- Quantize to beat boundary (use clock timestamps)
- Overdub: Mix new audio with existing loop

### Sample Playback (Planned)
- Triggered via MIDI notes
- One-shot or looping modes
- Pitch shifting (resampling or granular)
- Envelopes (ADSR)

### BPM Display (Planned)
- Calculate from 24-tick intervals
- EMA smoothing (alpha ~0.3)
- Display on 7-segment or OLED

### Quantization (Planned)
- Align record/playback to beat/bar boundaries
- Use clock timestamps for sample-accurate timing
- Pre-roll/latency compensation

## Known Issues / TODOs

- [ ] CMake build not fully tested (may need teensy_loader_cli integration)
- [ ] Stack usage not monitored (add watermarking)
- [ ] No overrun detection on SPSC queues (add in debug builds)
- [ ] SGTL5000 driver lacks volume control (add later)
- [ ] No BPM calculation yet (removed from old code, will re-add)
