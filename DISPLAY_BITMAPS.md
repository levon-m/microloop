# Display Bitmap Guide

## Overview

The MicroLoop display system uses 128x64 monochrome bitmaps stored in PROGMEM to save RAM. This guide shows you how to create and add new bitmaps.

## Tools Required

**LCDAssistant** - Free bitmap converter for monochrome displays
- Download: http://en.radzio.dxp.pl/bitmap_converter/
- Windows only (works in Wine on Mac/Linux)
- **Note:** This is the ONLY tool you need - no Arduino IDE required!

## Creating Bitmaps

### 1. Design Your Image

Create a 128x64 pixel image in your favorite editor:
- **Recommended:** GIMP, Photoshop, Paint.NET, or online editors
- **Format:** Black and white (no grayscale)
- **Size:** Exactly 128 x 64 pixels
- **Colors:** White pixels = ON (lit), Black pixels = OFF (dark)

**Tips:**
- High contrast works best on OLED displays
- Avoid fine details (1-2 pixel lines may be hard to see)
- Bold text and simple shapes work great
- Test inverted versions (sometimes looks better)

### 2. Convert with LCDAssistant

1. Open LCDAssistant
2. **File → Load image** - Select your 128x64 image
3. Configure settings:
   - **Byte orientation:** Horizontal
   - **Width:** 128
   - **Height:** 64
   - **Include size:** Unchecked
   - **Size endianness:** Little
   - **Table name:** (any name, e.g., "bitmap_custom")
4. **File → Save output** - Copy the generated C array

### 3. Add to Code

Edit `src/display_io.cpp`:

**Step 1:** Add your bitmap array after the existing ones (around line 60):

```cpp
// Your custom bitmap
static const uint8_t PROGMEM bitmap_custom[1024] = {
    // Paste LCDAssistant output here
    0xFF, 0xFF, 0xFF, ...
};
```

**Step 2:** Add to `BitmapID` enum in `include/display_io.h` (around line 50):

```cpp
enum class BitmapID : uint8_t {
    DEFAULT = 0,
    CHOKE_ACTIVE = 1,
    CUSTOM_NAME = 2,  // Your new bitmap
};
```

**Step 3:** Register in bitmap registry in `src/display_io.cpp` (around line 250):

```cpp
static const BitmapData bitmapRegistry[] = {
    { bitmap_default, DISPLAY_WIDTH, DISPLAY_HEIGHT },
    { bitmap_choke_active, DISPLAY_WIDTH, DISPLAY_HEIGHT },
    { bitmap_custom, DISPLAY_WIDTH, DISPLAY_HEIGHT },  // Your new bitmap
};
```

**Step 4:** Display your bitmap from anywhere:

```cpp
DisplayIO::showBitmap(BitmapID::CUSTOM_NAME);
```

## Current Bitmaps

### DEFAULT (BitmapID::DEFAULT)
- **Purpose:** Shown on startup and when idle
- **Design:** Simple border frame (placeholder)
- **Triggers:** Startup, choke release

### CHOKE_ACTIVE (BitmapID::CHOKE_ACTIVE)
- **Purpose:** Visual feedback when audio is muted
- **Design:** Inverted colors (placeholder for bold "MUTE" text)
- **Triggers:** Choke button press

## Bitmap Ideas

Here are some suggested bitmaps to create:

1. **Logo/Branding**
   - MicroLoop logo or custom branding
   - Band name or artist logo

2. **Status Icons**
   - Recording indicator (red circle)
   - Playing/Paused icons
   - BPM display with large numbers

3. **Menu Screens**
   - Main menu with selectable options
   - Loop bank selection (A, B, C, D)
   - Settings screens

4. **Visual Feedback**
   - Level meters (VU style)
   - Beat indicator animations
   - Sample waveforms

5. **Transport**
   - Play symbol (triangle)
   - Stop symbol (square)
   - Record symbol (circle)

## Performance Notes

- **Memory:** Each 128x64 bitmap = 1024 bytes in PROGMEM (flash)
- **Display Update:** ~20-30ms for full screen refresh
- **RAM Usage:** Display buffer uses ~1KB RAM (managed by Adafruit_SSD1306)
- **Thread Safety:** All `DisplayIO::show*()` functions are thread-safe (use lock-free queue)

## Testing Your Bitmap

1. Build and upload firmware
2. Watch Serial Monitor for initialization:
   ```
   Display: OK (SSD1306 on I2C 0x3C / Wire1)
   ```
3. Use Serial command to show your bitmap (if you add debug command)
4. Check for visual clarity and alignment

## Troubleshooting

**Bitmap appears corrupted/scrambled:**
- Check LCDAssistant settings (must be Horizontal, 128x64)
- Verify array size is exactly 1024 bytes
- Ensure PROGMEM keyword is present

**Display shows nothing:**
- Check I2C wiring (SDA=pin 17, SCL=pin 16)
- Verify I2C address (0x3C is standard, some use 0x3D)
- Check Serial Monitor for initialization errors

**Bitmap looks inverted:**
- SSD1306: 1 = white pixel, 0 = black pixel
- Try inverting your source image before converting
- Or use `display.invertDisplay(true)` in code

## Advanced: Animated Bitmaps

For simple animations (e.g., beat indicator):

1. Create multiple frames as separate bitmaps
2. Call `DisplayIO::showBitmap()` to switch frames
3. Use timer or beat sync to drive animation
4. Keep frame count low (display updates are ~30ms each)

## Example: Large "MUTE" Text

Here's how to create a bold MUTE indicator:

1. Create 128x64 image in GIMP
2. Add text: "MUTE" in 48pt bold font, centered
3. Export as PNG (black background, white text)
4. Load in LCDAssistant with settings above
5. Add to display_io.cpp as `bitmap_choke_active`
6. Already integrated to show on choke press!

## Future Enhancements

Planned features for the display system:

- **Text Rendering:** Use Adafruit GFX built-in fonts for dynamic text
- **BPM Display:** Real-time BPM value from TimeKeeper
- **Menu System:** Navigate with Neokey buttons 1-3
- **Level Meters:** Real-time audio level visualization
- **Loop Status:** Show which loops are recording/playing

## Resources

- [LCDAssistant Download](http://en.radzio.dxp.pl/bitmap_converter/)
- [Adafruit SSD1306 Library](https://github.com/adafruit/Adafruit_SSD1306)
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)
- [OLED Display Tutorial](https://learn.adafruit.com/monochrome-oled-breakouts)
