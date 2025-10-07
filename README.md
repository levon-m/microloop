# μLoop: Minimal Live Looper & Sampler

Real-time MIDI-synchronized looper for Teensy 4.1 with professional-grade timing.

**Current Status:** v0.1.0 - MIDI clock tracking, beat indicator, audio passthrough

## Hardware

- Teensy 4.1
- Audio Shield Rev D (SGTL5000)
- MIDI DIN input on Serial8 (RX=pin 34)
- External LED on pin 31 (beat indicator)

## Quick Start

### Build

Requires CMake 3.16+ and arm-none-eabi-gcc (from Arduino IDE 2.x Teensyduino).

```bash
# Configure toolchain path first
# Edit cmake/teensy41.cmake, set COMPILER_PATH to your Arduino15 location

mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/teensy41.cmake ..
ninja
```

### Upload

Drag `build/microloop.hex` into Teensy Loader GUI, press button on Teensy.

### Monitor

Open serial monitor in VS Code or Arduino IDE (115200 baud).

## Features

**Implemented:**
- MIDI clock input (24 PPQN)
- Transport control (START/STOP/CONTINUE)
- Timestamp-based beat tracking with EMA jitter smoothing
- LED beat indicator (short pulse on downbeat)
- Stereo audio passthrough
- Lock-free multi-threaded architecture
- **Comprehensive on-device test suite (47+ tests)**

**Planned:**
- Loop recording/playback
- Sample triggering
- BPM display
- Quantized operations

## Testing

The project includes a comprehensive on-device test suite that runs directly on Teensy hardware.

**Quick test:**
1. Edit `CMakeLists.txt`: Comment out `src/main.cpp`, use `tests/run_tests.cpp`
2. Build: `cd build && ninja`
3. Upload `microloop.hex` to Teensy
4. Open serial monitor (115200 baud) - you'll see test results

**Test coverage:**
- ✅ TimeKeeper (30+ tests) - Beat tracking, MIDI sync, quantization
- ✅ Trace utility (7 tests) - Event logging, overflow handling
- ✅ SPSC queue (10 tests) - Push/pop, wraparound, performance

See [tests/TESTING.md](tests/TESTING.md) for detailed testing guide including manual integration tests.

## Documentation

- [CLAUDE.md](CLAUDE.md) - Architecture details, development workflow, real-time safety guidelines
- [tests/TESTING.md](tests/TESTING.md) - Comprehensive testing guide
- [utils/TIMEKEEPER_USAGE.md](utils/TIMEKEEPER_USAGE.md) - TimeKeeper API documentation
- [utils/TRACE_USAGE.md](utils/TRACE_USAGE.md) - Trace utility usage guide

## License

[Specify your license]
