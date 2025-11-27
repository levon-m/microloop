# μLoop

μLoop is a standalone **looper & sampler** quantized to external MIDI clock for real-time audio manipulation.\
\
Inspired by French House and Electro sounds, μLoop lets you create immediate **rhythmic glitches** and **sustained textures** with added performance effects.

![MicroLoop Hardware](media/microloop.jpg)

## Features

### Effects

- **STUTTER**: Loop buffer that captures and repeats audio slices for glitchy textures
- **FREEZE**: Granular hold effect that captures and sustains a 3ms moment of audio
- **CHOKE**: Instant mute with 3ms crossfades for dramatic cuts and rhythmic gating

### Control

- **4 Rotary Encoders**: Real-time parameter adjustment for each effect
- **4 Mechanical Switches**: Cherry MX Blue switches with RGB LED feedback
- **4 Preset Buttons**: Access to 4 slots for saving loops via microSD card

### Interface

- **OLED Display**: Shows effect state, parameter menu system, and settings
- **RGB LED**: Visual feedback for effect states and loop capture
- **Beat LED**: Visualizes incoming jitter-smoothed MIDI clock
- **Preset LEDs**: Visualizes selected preset as well as save/delete operations

### Timing

- **MIDI Sync**: External MIDI clock sync with <50µs jitter
- **Free & Quantized Modes**: Immediate triggering or synced onset/release for all effects
- **Quantization Grid**: Global beat divisions (1/4, 1/8, 1/16, 1/32 notes)

## Hardware

- Built around the [Teensy 4.1](https://www.pjrc.com/store/teensy41.html) (ARM Cortex-M7 @ 600 MHz)
- Stereo audio I/O via the [Teensy Audio Adapter](https://www.pjrc.com/store/teensy3_audio.html) (SGTL5000 codec @ 44.1 kHz, 16-bit, 128-sample block size)

### Components

- Teensy 4.1 + Audio Adapter
- Adafruit MIDI FeatherWing
- Adafruit NeoKey 1X4 switches
- SSD1306 128x64 OLED Display
- CYT1100 Rotary Encoders with switches
- MCP23017 I2C I/O expander
- MicroSD Card

### Interfaces

- **Audio**: Stereo line-in/out via 3.5mm jacks
- **MIDI**: DIN connector
- **I2C**: 3 independent buses for peripherals
- **SDIO**: High-speed 4-bit microSD interface

See [hardware/](hardware/) for full BOM and KiCAD schematics

## Software

### Signal Flow

- Input -> Timebase -> Stutter -> Freeze -> Choke -> Output

### Components

- **SGTL5000**: custom register-layer driver for I2C codec configuration
- **Timebase**: Centralized timing authority bridging MIDI clock and audio samples
- **Quantization API**: Sample-accurate for beat/bar-aligned recording and playback
- **Effect System**: Polymorphic command dispatch

### Architecture

- **Audio ISR**: 128-sample blocks, zero-allocation DSP
- **App Thread**: MIDI clock processing, command dispatch, preset I/O
- **MIDI Thread**: Serial clock reception from DIN connector
- **NeoKey Thread**: Button event handling ISR and RGB LED updates
- **MCP Thread**: 4 rotary encoders via I/O expander interrupts
- **Display Thread**: OLED runtime rendering

### Design Patterns

- **State Machines**: Deterministic effect transitions with atomic updates
- **Command Pattern**: Type-safe button -> effect communication
- **Registry Pattern**: Dynamic effect lookup and dispatch
- **Observer**: Display subscribes to effect state changes

See [libs/](libs/) for external libraries used

## Technical Highlights

- **Lock-free architecture**: Zero mutexes, all critical paths use atomics + SPSC queues
- **Zero-allocation DSP**: All buffers pre-allocated for deterministic performance
- **Quantization accuracy**: ±11µs (0.5 samples)
- **MIDI jitter**: <50µs (EMA-smoothed BPM)
- **Clean layering**: 4-tier dependency graph (Core -> HAL -> DSP -> App), no upward dependencies

## Build & Flash

### Prerequisites

- [ARM GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) (bare-metal `arm-none-eabi`) 10.3+ on your `PATH`  
- [CMake](https://cmake.org/download/) 3.16+
- [Ninja](https://ninja-build.org/) 1.10+
- Teensy Loader [GUI](https://www.pjrc.com/teensy/loader.html) or [CLI](https://www.pjrc.com/teensy/loader_cli.html)

### Build

```bash
git clone https://github.com/levon-m/microloop.git
cd microloop

# Configure and build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This produces the firmware file:

```text
build/microloop.hex
```

### Flash

GUI:

1. Open the Teensy Loader app

2. File -> Open HEX File -> select build/microloop.hex

3. Connect the Teensy and press Program (or the BOOT button if prompted)

CLI:

```bash
cd build
teensy_loader_cli --mcu=TEENSY41 -w microloop.hex
```

Press the Teensy BOOT button once if it doesn’t auto-detect




