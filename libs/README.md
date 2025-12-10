# External Libraries

The code in these folders is copied from the upstream projects listed below, with their original Git metadata (`.git/`) removed.

---

| Library                           | Local Folder                               | Upstream Repo                                                     | Version / Commit |
|-----------------------------------|--------------------------------------------|-------------------------------------------------------------------|------------------|
| Teensy Cores (teensy4)           | `TeensyCores/`                            | https://github.com/PaulStoffregen/cores                           | 1.59             |
| Teensy Audio             | `Audio/`                                   | https://github.com/PaulStoffregen/Audio                           | 1.03             |
| SD                               | `SD/`                                      | https://github.com/PaulStoffregen/SD                              | c535ae9             |
| SdFat                            | `SdFat/`                                   | https://github.com/PaulStoffregen/SdFat                           | d40d5c7             |
| SerialFlash                      | `SerialFlash/`                             | https://github.com/PaulStoffregen/SerialFlash                     | 0.5             |
| SPI                              | `SPI/`                                     | https://github.com/PaulStoffregen/SPI                             | 52f8402             |
| Wire                             | `Wire/`                                    | https://github.com/PaulStoffregen/Wire                            | 42adf2f             |
| MIDI                             | `MIDI/`                                    | https://github.com/PaulStoffregen/MIDI                            | 6a4b79e             |
| TeensyThreads                    | `TeensyThreads/`                           | https://github.com/ftrias/TeensyThreads                           | ec83f0c             |
| Adafruit BusIO                   | `Adafruit_BusIO/`                          | https://github.com/adafruit/Adafruit_BusIO                        | 1.17.4             |
| Adafruit Seesaw                  | `Adafruit_Seesaw/`                         | https://github.com/adafruit/Adafruit_Seesaw                       | 1.7.9             |
| Adafruit NeoPixel                | `Adafruit_NeoPixel/`                       | https://github.com/adafruit/Adafruit_NeoPixel                     | 1.15.2             |
| Adafruit GFX             | `Adafruit_GFX/`                    | https://github.com/adafruit/Adafruit-GFX-Library                  | 1.12.4             |
| Adafruit SSD1306                 | `Adafruit_SSD1306/`                        | https://github.com/adafruit/Adafruit_SSD1306                      | 2.5.15             |
| Adafruit MCP23017 | `Adafruit_MCP23017/`     | https://github.com/adafruit/Adafruit-MCP23017-Arduino-Library     | 2.3.2             |

---

## Updating Libraries

To update a library to a newer upstream version:

1. Temporarily clone the upstream repo somewhere outside this tree:

   ```bash
   git clone https://github.com/PaulStoffregen/Audio.git /tmp/Audio
   # or the relevant repo
   ```

2. Optionally check out a specific tag or commit:

   ```bash
   cd /tmp/Audio
   git checkout <tag-or-commit>
   ```

3. Replace the existing library folder in `libs/`:

   ```bash
   rm -rf /path/to/microloop/libs/Audio
   cp -R /tmp/Audio/path/to/microloop/libs/Audio
   rm -rf /path/to/microloop/libs/Audio/.git
   ```

4. Update the “Version / Commit” field in the table above.
5. Rebuild MicroLoop to ensure everything still compiles and works.

---

## Licensing

Each library remains under its original license:

- Check the `LICENSE`, or equivalent file in each subfolder.
- MicroLoop’s top-level license applies to the project’s own source code, third-party libraries remain under their respective licenses.
