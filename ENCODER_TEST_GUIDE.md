# Encoder Test Guide

## Overview

This guide walks you through testing 4 rotary encoders with push buttons connected via MCP23017 I2C expander.

## Hardware Setup

### MCP23017 Wiring (on Wire bus, shared with Audio Shield)
- **I2C Address:** 0x20
- **VDD** → Teensy 3.3V
- **VSS** → GND
- **0.1µF capacitor** between VDD and VSS (directly at MCP pins)
- **RESET** → 3.3V (tie high, chip enabled)
- **A0, A1, A2** → GND (sets address to 0x20)
- **SDA** → Teensy Pin 18 (Wire SDA)
- **SCL** → Teensy Pin 19 (Wire SCL)
- **INTA (or INTB)** → Teensy Pin 36 (interrupt on change)

**Note:** The MCP23017 shares the Wire bus with the Audio Shield (SGTL5000 codec at 0x0A). This is fine because:
- Audio Shield only uses I2C during initialization
- MCP23017 is at a different address (0x20)
- **Interrupt-driven:** Only reads I2C when encoders change (ultra-low traffic)

**Interrupt Mode Benefits:**
- **Fast response:** Hardware interrupt fires immediately on encoder change
- **No missed steps:** Can handle very fast encoder turns
- **Low CPU usage:** Teensy sleeps until encoder changes
- **Accurate tracking:** Every quadrature edge is captured

**INTA vs INTB:**
The MCP23017 has two interrupt pins:
- **INTA** - Fires on Port A changes (GPA0-GPA7)
- **INTB** - Fires on Port B changes (GPB0-GPB7)

**Mirror Mode (configured automatically):**
- Both INTA and INTB fire together on ANY pin change
- Only need to connect ONE wire (either INTA or INTB) to Teensy Pin 36
- This is simpler and uses fewer pins
- The code configures mirror mode during initialization

### Encoder Pin Assignments
Each encoder has 3 pins:
- **A** = Left pin (3-pin side)
- **B** = Right pin (3-pin side)
- **SW** = Switch pin (2-pin side)

**Encoder 1:**
- A = GPA4
- B = GPA3
- SW = GPA2

**Encoder 2:**
- A = GPB0
- B = GPB1
- SW = GPB2

**Encoder 3:**
- A = GPB3
- B = GPB4
- SW = GPB5

**Encoder 4:**
- A = GPA7
- B = GPA6
- SW = GPA5

## Library Installation

**REQUIRED:** Adafruit MCP23017 Library

### Option 1: Arduino IDE Library Manager
1. Open Arduino IDE 2.x
2. Tools → Manage Libraries
3. Search: "Adafruit MCP23017"
4. Install: "Adafruit MCP23017 Arduino Library" by Adafruit
5. Library will be installed to: `C:\Users\[username]\AppData\Local\Arduino15\packages\teensy\hardware\avr\1.59.0\libraries\Adafruit_MCP23X17`

### Option 2: Manual Download
1. Download from: https://github.com/adafruit/Adafruit-MCP23017-Arduino-Library/releases
2. Extract to: `C:\Users\[username]\AppData\Local\Arduino15\packages\teensy\hardware\avr\1.59.0\libraries\`
3. Rename folder to: `Adafruit_MCP23X17`

## Building the Test

1. **Edit CMakeLists.txt** (line 257):
   ```cmake
   # Comment out main
   #add_executable(microloop.elf src/main.cpp)

   # Enable encoder test
   add_executable(microloop.elf tests/test_encoders_main.cpp)
   ```

2. **Rebuild:**
   ```bash
   cd build
   cmake -G "Ninja" -DCMAKE_TOOLCHAIN_FILE=../cmake/teensy41.cmake ..
   ninja
   ```

3. **Upload:**
   - Open Teensy Loader application
   - File → Open HEX → select `build/microloop.hex`
   - Press physical button on Teensy to upload

## Running the Test

1. **Open Serial Monitor:**
   - Baud rate: 115200
   - You should see:
     ```
     ╔════════════════════════════════════════╗
     ║    MicroLoop Encoder Test             ║
     ╚════════════════════════════════════════╝

     MCP23017: Found on Wire @ 0x20
     Encoders: Initialized (4 encoders with buttons)
     Turn encoders or press buttons to test...
     Format: ENC[N] CW/CCW/PRESS (position)

     Ready! Turn encoders or press buttons to test.
     ```

2. **Test each encoder:**

   **Turn clockwise (a few detents):**
   ```
   ENC1 CW (pos=1, detents=0)
   ENC1 CW (pos=2, detents=0)
   ENC1 CW (pos=3, detents=0)
   ENC1 CW (pos=4, detents=1)  ← 1 detent complete
   ```

   **Turn counter-clockwise:**
   ```
   ENC1 CCW (pos=3, detents=0)
   ENC1 CCW (pos=2, detents=0)
   ENC1 CCW (pos=1, detents=0)
   ENC1 CCW (pos=0, detents=0)
   ```

   **Press button:**
   ```
   ENC1 PRESS (pos=0)
   ```

3. **Expected behavior:**
   - **Turning:** Direction (CW/CCW) should match physical rotation
   - **Detents:** Most encoders have 4 state changes per tactile click (detent)
   - **Button:** Pressing encoder shaft should register PRESS event
   - **Debouncing:** Button presses filtered to 20ms minimum

## Understanding the Output

### Position Tracking
- `pos=X` is the raw encoder position (increments/decrements by 1 per state change)
- `detents=Y` is the position divided by 4 (typical encoder has 4 steps per detent)
- Different encoder models may have different steps-per-detent ratios

### Quadrature Encoding
The code uses a state machine to decode the A/B phases:
- **State 00 → 01:** CW rotation
- **State 01 → 11:** CW rotation
- **State 11 → 10:** CW rotation
- **State 10 → 00:** CW rotation
- Reverse sequence: CCW rotation

## Troubleshooting

### "MCP23017 not found" error
- Check I2C wiring (SDA/SCL to correct pins)
- Verify VDD/VSS power
- Verify RESET tied to 3.3V (not floating)
- Verify A0/A1/A2 tied to GND (address 0x20)
- Add 0.1µF cap between VDD/VSS if not present

### Encoders not registering turns
- Verify encoder pin assignments (A/B pins correct?)
- Check encoder common/ground connection
- Try swapping A/B pins (will reverse direction)
- Encoders need pull-ups (enabled in code automatically)

### Wrong direction (CW shows as CCW)
- Swap A and B pin definitions in `encoder_test.cpp:10-13`
- OR reverse your physical wiring (swap A/B connections)

### Button not registering
- Verify SW pin wiring
- Check button is active-low (pressed = LOW, released = HIGH)
- Some encoders have separate button ground (verify wiring)

### Inconsistent/jittery readings
- Add/verify 0.1µF decoupling cap at MCP23017 VDD/VSS
- Shorten I2C wires if possible
- Reduce I2C speed (change `400000` to `100000` in encoder_test.cpp:40)
- Check for electrical noise near encoder wires

## Next Steps

Once all 4 encoders and buttons work correctly:

1. **Comment out encoder test** in CMakeLists.txt (line 260)
2. **Uncomment main** in CMakeLists.txt (line 257)
3. **Rebuild** with `ninja`
4. Encoders are now ready for integration into the main application

## Implementation Details

### Files Created
- `include/encoder_test.h` - Encoder test API
- `src/encoder_test.cpp` - Quadrature decoder + MCP23017 driver
- `tests/test_encoders_main.cpp` - Test program entry point

### Key Features
- Lock-free quadrature decoding (safe for real-time)
- 20ms button debouncing
- Supports different encoder types (configurable steps-per-detent)
- I2C @ 400kHz (fast mode)
- Pull-ups enabled automatically

### Performance
- Polling rate: 1ms (1000 Hz)
- I2C read latency: ~0.5ms per read
- CPU usage: <1% (very efficient)

## References

- [MCP23017 Datasheet](https://www.nxp.com/docs/en/data-sheet/MCP23017.pdf)
- [Adafruit MCP23017 Library](https://github.com/adafruit/Adafruit-MCP23017-Arduino-Library)
- [Quadrature Encoder Theory](https://en.wikipedia.org/wiki/Incremental_encoder)
