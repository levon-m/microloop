# External Libraries (`libs/`)

This folder contains **external libraries** used by MicroLoop. They are vendored here (or added as git submodules) so the project can be built without manually hunting down dependencies in the Arduino/Teensy environment.

> Note: Core Teensy libraries (core, Wire, SPI, SD, Audio, etc.) are provided by the Teensy toolchain and are **not** duplicated here. See `docs/build.md` for required Teensyduino versions.

## Included Libraries

- **TeensyThreads**  
  Path: `libs/TeensyThreads/`  
  Purpose: Lightweight threading on Teensy 4.1 for UI/control tasks.  
  Upstream: https://github.com/ftrias/TeensyThreads

- **Adafruit BusIO**  
  Path: `libs/Adafruit_BusIO/`  
  Purpose: I²C / SPI abstraction layer used by other Adafruit drivers.  
  Upstream: https://github.com/adafruit/Adafruit_BusIO

- **Adafruit seesaw Library**  
  Path: `libs/Adafruit_Seesaw/`  
  Purpose: Driver for the Adafruit NeoKey and other seesaw-based devices.  
  Upstream: https://github.com/adafruit/Adafruit_Seesaw

- **Adafruit NeoPixel**  
  Path: `libs/Adafruit_NeoPixel/`  
  Purpose: Driving the NeoKey RGB LEDs.  
  Upstream: https://github.com/adafruit/Adafruit_NeoPixel

- **Adafruit GFX Library**  
  Path: `libs/Adafruit_GFX/`  
  Purpose: Graphics primitives used for rendering UI on the OLED.  
  Upstream: https://github.com/adafruit/Adafruit-GFX-Library

- **Adafruit SSD1306**  
  Path: `libs/Adafruit_SSD1306/`  
  Purpose: SSD1306 OLED driver for the MicroLoop screen.  
  Upstream: https://github.com/adafruit/Adafruit_SSD1306

- **Adafruit MCP23017**  
  Path: `libs/Adafruit_MCP23017/`  
  Purpose: MCP23017 I/O expander driver used for the encoders + push buttons.  
  Upstream: https://github.com/adafruit/Adafruit-MCP23017-Arduino-Library

## Licensing

Each library keeps its **original license** file in its folder.

MicroLoop’s own code is licensed under the root [`LICENSE`](../LICENSE) file (e.g. MIT).  
Third-party code in `libs/` remains under the licenses specified by their authors.

For build instructions and exact version requirements, see [`docs/build.md`](../docs/build.md).
