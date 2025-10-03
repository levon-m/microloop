# MicroLoop

**Real-time live-performance looper/sampler for Teensy 4.1**

MicroLoop is a MIDI-synchronized audio looper designed for live performance. It receives MIDI clock from external hardware (e.g., Elektron Digitakt), processes audio in real-time through an SGTL5000 codec, and will support loop recording, sample playback, and quantized operations.

**Current Status:** v0.1.0 - Basic infrastructure (MIDI clock, beat tracking, LED indicator, audio passthrough)

## Features

### ✅ Implemented
- MIDI DIN input (24 PPQN clock)
- Transport control (START/STOP/CONTINUE)
- LED beat indicator (blinks on every beat)
- Stereo audio passthrough (Line In → Line Out)
- Lock-free multi-threaded architecture
- Real-time safe design (no blocking, no allocation in audio path)

### 🚧 Planned
- Loop recording/playback with overdub
- Sample triggering via MIDI notes
- BPM display (7-segment or OLED)
- Quantized record/playback (beat/bar aligned)
- Multiple loop tracks
- Effects processing

## Hardware Requirements

- **Teensy 4.1** (ARM Cortex-M7 @ 600 MHz)
- **Audio Shield Rev D** (SGTL5000 codec)
- **MIDI DIN input** on Serial8 (RX=pin 34, TX=pin 35)
- **MIDI clock source** (e.g., Elektron Digitakt, Ableton Live + MIDI interface)

## Software Requirements

### Option 1: CMake Build (Recommended)

**Prerequisites:**
- CMake 3.16 or later
- arm-none-eabi-gcc toolchain (included with Teensyduino)
- Git (for fetching dependencies)
- teensy_loader_cli (for uploading firmware)

**Installation:**

1. **Install Teensyduino** (includes arm-none-eabi-gcc):
   - Download from https://www.pjrc.com/teensy/td_download.html
   - Install Arduino IDE + Teensyduino addon
   - Note the installation path (e.g., `C:/Program Files (x86)/Arduino`)

2. **Install CMake:**
   - Windows: Download from https://cmake.org/download/
   - macOS: `brew install cmake`
   - Linux: `sudo apt install cmake`

3. **Install teensy_loader_cli** (optional, for command-line uploads):
   ```bash
   # macOS
   brew install teensy_loader_cli

   # Linux
   git clone https://github.com/PaulStoffregen/teensy_loader_cli.git
   cd teensy_loader_cli
   make
   sudo cp teensy_loader_cli /usr/local/bin/

   # Windows: Use Teensy Loader GUI (comes with Teensyduino)
   ```

4. **Configure toolchain path:**

   Edit `cmake/teensy41.cmake` and set `COMPILER_PATH` to match your system:

   ```cmake
   # Example paths:
   # Windows: "C:/Program Files (x86)/Arduino/hardware/tools/arm/bin/"
   # macOS: "/Applications/Arduino.app/Contents/Java/hardware/tools/arm/bin/"
   # Linux: "/usr/share/arduino/hardware/tools/arm/bin/"
   ```

### Option 2: Arduino IDE (Legacy)

**Prerequisites:**
- Arduino IDE 1.8.x or 2.x
- Teensyduino addon
- Required libraries (install via Library Manager):
  - MIDI Library (FortySevenEffects)
  - Audio (included with Teensyduino)
  - TeensyThreads (Fernando Trias)

**Note:** The CMake build is preferred for new development. Arduino IDE support is for the legacy `PerformanceTool/` project only.

## Building and Uploading

### Using CMake

1. **Clone the repository:**
   ```bash
   git clone <your-repo-url>
   cd TeensyAudioTools
   ```

2. **Configure the build:**
   ```bash
   mkdir build
   cd build
   cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/teensy41.cmake ..
   ```

3. **Build the firmware:**
   ```bash
   make
   ```

   This generates `microloop.hex` in the `build/` directory.

4. **Upload to Teensy:**

   **Option A: Command line (teensy_loader_cli)**
   ```bash
   make upload
   ```

   **Option B: GUI (Teensy Loader)**
   - Open Teensy Loader application
   - Drag `build/microloop.hex` into the window
   - Press the button on Teensy to upload

### Using Arduino IDE (Legacy)

1. Open `PerformanceTool/PerformanceTool.ino` in Arduino IDE
2. Select **Tools → Board → Teensy 4.1**
3. Select **Tools → USB Type → Serial**
4. Click **Upload** (or Ctrl+U)

## Usage

### Hardware Setup

1. **Connect Audio Shield** to Teensy 4.1 (stacks on top)
2. **Wire MIDI DIN input** to Serial8:
   - MIDI Pin 4 (RX) → Teensy Pin 34 (RX8)
   - MIDI Pin 5 (TX) → Teensy Pin 35 (TX8) [optional for MIDI out]
   - Add 220Ω resistor + optocoupler for proper MIDI isolation (see MIDI spec)
3. **Connect audio source** to Line In (3.5mm jack)
4. **Connect headphones/speakers** to Line Out or Headphone jack
5. **Connect MIDI clock source** (e.g., Digitakt MIDI Out → MicroLoop MIDI In)

### Testing the LED Beat Indicator

1. **Upload firmware** (see "Building and Uploading" above)
2. **Open Serial Monitor:**
   ```bash
   # Linux/macOS
   screen /dev/ttyACM0 115200

   # Windows (PowerShell)
   # Use Arduino IDE Serial Monitor or PuTTY
   ```
3. **Start MIDI clock** on your external device (Digitakt, DAW, etc.)
4. **Observe:**
   - Built-in LED (pin 13) blinks on every beat
   - Serial output shows: `► START`, `Transport: RUNNING`, etc.
5. **Stop MIDI clock** → LED turns off, Serial shows `■ STOP`

### Expected Serial Output

```
=== MicroLoop Initializing ===
Audio: OK
MIDI: OK (DIN on Serial8, RX=pin34, TX=pin35)
App Logic: OK
Threads: Started
=== MicroLoop Running ===

► START
Transport: RUNNING | Beat tick: 0/24 | LED: ON
Transport: RUNNING | Beat tick: 12/24 | LED: OFF
Transport: RUNNING | Beat tick: 0/24 | LED: ON
■ STOP
Transport: STOPPED | Beat tick: 0/24 | LED: OFF
```

## Troubleshooting

### CMake Build Issues

**Problem:** `arm-none-eabi-gcc: command not found`
- **Solution:** Edit `cmake/teensy41.cmake` and set correct `COMPILER_PATH`

**Problem:** `teensy-cmake-macros` fetch fails
- **Solution:** Check internet connection, or manually clone:
  ```bash
  git clone https://github.com/newdigate/teensy-cmake-macros.git
  # Update CMakeLists.txt to use local path
  ```

**Problem:** CMake version too old
- **Solution:** Install CMake 3.16+ from https://cmake.org/

### Runtime Issues

**Problem:** No LED blink, no Serial output
- **Check:** Teensy connected? USB cable good? Try pressing reset button.
- **Check:** Correct board selected in Teensy Loader?

**Problem:** LED blinks, but not in time with MIDI
- **Check:** MIDI cable connected? MIDI clock enabled on source device?
- **Check:** MIDI wiring (RX8 = pin 34, ensure proper MIDI circuit)
- **Debug:** Add Serial.print in `onClock()` handler (will slow down MIDI)

**Problem:** Audio passthrough not working
- **Check:** Audio Shield properly seated on Teensy headers?
- **Check:** Audio source connected to Line In?
- **Check:** Volume on source and destination?
- **Debug:** Run Audio Shield test sketch from Teensy examples

**Problem:** Random crashes, hard faults
- **Likely:** Stack overflow (see CLAUDE.md → "Debugging Stack Overflow")
- **Solution:** Increase stack sizes in `src/main.cpp:128-129`
- **Solution:** Avoid `Serial.printf("%f", ...)` (use fixed-point int instead)

**Problem:** MIDI jitter, inconsistent beat timing
- **Normal:** Some jitter is expected (MIDI parser runs in thread, not ISR)
- **Future:** BPM calculation will smooth this out (per-beat averaging)

## Project Structure

```
TeensyAudioTools/
├── src/                    # Source files
│   ├── main.cpp           # Entry point, setup, threading
│   ├── midi_io.cpp        # MIDI I/O thread
│   ├── app_logic.cpp      # App thread, beat tracking
│   └── SGTL5000.cpp       # Custom codec driver
├── include/               # Public headers
│   ├── midi_io.h
│   ├── app_logic.h
│   └── SGTL5000.h
├── utils/                 # Reusable utilities
│   ├── spsc_queue.h      # Lock-free SPSC queue
│   └── span.h            # Buffer view (C++17 backport)
├── PerformanceTool/       # Legacy Arduino project (deprecated)
├── cmake/                 # Build configuration
│   └── teensy41.cmake    # Teensy 4.1 toolchain
├── CMakeLists.txt        # Main CMake config
├── CLAUDE.md             # Architecture documentation
└── README.md             # This file
```

## Development

See [CLAUDE.md](CLAUDE.md) for detailed architecture documentation, including:
- Thread model and real-time safety guidelines
- Lock-free queue design
- MIDI clock handling and jitter mitigation
- Adding new features
- Performance optimization tips

### Quick Development Cycle

1. **Edit code** in `src/`, `include/`, or `utils/`
2. **Rebuild:**
   ```bash
   cd build
   make
   ```
3. **Upload:**
   ```bash
   make upload  # or use Teensy Loader GUI
   ```
4. **Monitor:**
   ```bash
   screen /dev/ttyACM0 115200  # adjust port for your system
   ```

### Code Style

- **C++ Standard:** C++17 (no exceptions, no RTTI)
- **Naming:** `camelCase` for variables, `PascalCase` for types
- **Comments:** Explain "why", not "what" (code should be self-documenting)
- **Real-time safety:** No allocation, no blocking in audio/MIDI paths
- **Modularity:** Keep `utils/` generic and testable

## Contributing

This is a personal learning project, but suggestions and improvements are welcome!

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

**Please ensure:**
- Code follows real-time safety guidelines (see CLAUDE.md)
- Comments explain design decisions and tradeoffs
- New features are modular and testable

## License

[Specify your license here]

## Acknowledgments

- **PJRC** for Teensy and Audio Library
- **FortySevenEffects** for MIDI Library
- **Fernando Trias** for TeensyThreads
- **newdigate** for teensy-cmake-macros
- **Elektron** for making the Digitakt (MIDI clock source)

## Contact

[Your contact information]

---

**Happy looping!** 🎵🔁
