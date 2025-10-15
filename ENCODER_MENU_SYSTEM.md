# Encoder Menu System Architecture

**Purpose:** Unified menu system for parameter control via rotary encoders with visual feedback on OLED display.

**Status:** Production-ready (implemented for Global Quantization and Choke Length)

---

## Table of Contents

1. [Overview](#overview)
2. [Hardware Architecture](#hardware-architecture)
3. [Menu Design Philosophy](#menu-design-philosophy)
4. [Implementation Pattern](#implementation-pattern)
5. [Adding a New Menu Parameter](#adding-a-new-menu-parameter)
6. [Code Reference](#code-reference)
7. [Performance & Thread Safety](#performance--thread-safety)

---

## Overview

The encoder menu system provides a consistent, professional-feeling interface for adjusting parameters in real-time during live performance. Each encoder can control one parameter with visual feedback on the OLED display.

### Key Features

- **2 detents per turn**: Allows "peeking" at adjacent values without committing to the change
- **Immediate visual feedback**: Display shows parameter bitmap as soon as encoder is touched
- **2-second cooldown**: Display returns to default/effect state after 2 seconds of inactivity
- **Boundary handling**: Accumulator resets at min/max to prevent "unwinding" behavior
- **Thread-safe**: Lock-free communication between encoder ISR → App thread
- **Zero missed steps**: ISR state capture architecture (see `ENCODER_ARCHITECTURE.md`)

### Current Implementations

| Encoder | Parameter | Options | Default | File |
|---------|-----------|---------|---------|------|
| 4 | Global Quantization | 1/32, 1/16, 1/8, 1/4 | 1/16 | `app_logic.cpp:264-408` |
| 3 | Choke Length | Free, Quantized | Free | `app_logic.cpp:410-550` |

---

## Hardware Architecture

### Encoder Hardware (MCP23017 I2C GPIO Expander)

- **4× Rotary Encoders** with quadrature encoding and push buttons
- **I2C Address:** 0x20 (Wire bus, shared with Audio Shield)
- **Interrupt:** INTA/INTB → Teensy Pin 36
- **ISR State Capture:** MCP23017 INTCAP registers freeze GPIO state on interrupt
- **Event Queue:** 64-event circular buffer passes states to main loop

**Pin Assignments:**

| Encoder | A Pin | B Pin | Switch Pin | Current Use |
|---------|-------|-------|------------|-------------|
| 1       | GPA4  | GPA3  | GPA2       | *(Reserved)* |
| 2       | GPB0  | GPB1  | GPB2       | *(Reserved)* |
| 3       | GPB3  | GPB4  | GPB5       | **Choke Length** |
| 4       | GPA7  | GPA6  | GPA5       | **Global Quantization** |

### Display Hardware (SSD1306 OLED)

- **Resolution:** 128×64 pixels, monochrome
- **I2C Address:** 0x3C (Wire1 bus, separate from encoders)
- **Bitmap Format:** 1024 bytes per image (PROGMEM, flash storage)
- **Update Time:** ~20-30ms per full screen update

**See:** `ENCODER_ARCHITECTURE.md` for detailed hardware/ISR design

---

## Menu Design Philosophy

### Visual Ordering (Left to Right)

Parameters are arranged in a logical visual order from **smallest to largest** or **off to on**:

**Global Quantization:** `1/32 ← 1/16 ← 1/8 ← 1/4`
- Left (CCW): Smaller note divisions (faster)
- Right (CW): Larger note divisions (slower)

**Choke Length:** `Free ← Quantized`
- Left (CCW): Free mode (manual control)
- Right (CW): Quantized mode (auto-release)

### Encoder Mechanics

**Quadrature Encoding:**
- Most encoders: 4 quadrature steps per physical detent (tactile click)
- Direction: CW = +1 step, CCW = -1 step

**Turn Threshold:**
- **2 detents = 1 turn = 8 steps**
- Rationale: Allows slight touch without changing value (prevents accidental changes)
- Accumulator pattern tracks steps across multiple loop iterations

**Boundary Handling:**
- When reaching min/max value, accumulator **resets to 0**
- Prevents "unwinding" (needing extra turns to reverse direction)
- Ensures consistent 2-detent feel at all positions

### Display Behavior

**Immediate Feedback:**
- Display shows parameter bitmap **as soon as encoder moves** (first step)
- No delay or threshold before showing menu

**2-Second Cooldown:**
- After encoder stops moving, start 2-second timer
- When timer expires, return to default display or active effect bitmap
- Timer resets if encoder moves again before expiration

**Priority System:**
- Encoders have higher priority than effects while active
- After cooldown: Show last activated effect, or default if none active
- Order: **Encoder Menu** → **Freeze Effect** → **Choke Effect** → **Default**

---

## Implementation Pattern

### State Variables (per encoder)

Every encoder menu needs these state variables in `app_logic.cpp`:

```cpp
// Encoder N state tracking (for [parameter name] menu)
static int32_t lastEncoderNPosition = 0;      // Last raw position (for detecting movement)
static int32_t encoderNAccumulator = 0;       // Accumulated steps since last turn
static bool encoderNWasTouched = false;       // Tracks if encoder was recently touched
static uint32_t encoderNReleaseTime = 0;      // Time when encoder was released
```

**Constants:**
```cpp
static constexpr uint32_t ENCODER_DISPLAY_COOLDOWN_MS = 2000;  // Shared across all menus
```

### Helper Functions

Each parameter needs conversion and naming helpers:

```cpp
// Convert parameter enum to BitmapID
static BitmapID parameterToBitmap(ParameterEnum param) {
    switch (param) {
        case ParameterEnum::OPTION_1: return BitmapID::PARAM_OPTION_1;
        case ParameterEnum::OPTION_2: return BitmapID::PARAM_OPTION_2;
        // ... etc.
        default: return BitmapID::PARAM_DEFAULT;
    }
}

// Get parameter name string (for Serial debug output)
static const char* parameterName(ParameterEnum param) {
    switch (param) {
        case ParameterEnum::OPTION_1: return "Option 1";
        case ParameterEnum::OPTION_2: return "Option 2";
        // ... etc.
        default: return "Default";
    }
}
```

### Main Loop Logic

Insert this logic in `AppLogic::threadLoop()` after encoder update and before transport events:

```cpp
// ========== HANDLE ENCODER N ([PARAMETER NAME] MENU) ==========
/**
 * ENCODER N [PARAMETER NAME] MENU
 *
 * DESIGN:
 * - Encoder N controls [parameter description]
 * - Options: [list options in visual order, left to right]
 * - 2 detents = 1 "turn" (allows "peeking" without changing value)
 * - Display shows [parameter] bitmap while encoder is touched
 * - 2 second cooldown after release before returning to default display
 *
 * DETENT CALCULATION:
 * - Most encoders: 4 quadrature steps per physical detent
 * - 2 detents = 8 steps (allows slight touch without changing)
 * - Direction: CW = increase ([visual order]), CCW = decrease
 */

// Get current encoder N position (raw steps)
int32_t currentEncoderNPosition = EncoderIO::getPosition(N-1);  // Encoder N is index N-1
int32_t encoderNDelta = currentEncoderNPosition - lastEncoderNPosition;

// Check if encoder was touched (position changed)
if (encoderNDelta != 0) {
    // Encoder is being touched - show parameter bitmap immediately
    if (!encoderNWasTouched) {
        encoderNWasTouched = true;
        // Show current parameter bitmap
        DisplayIO::showBitmap(parameterToBitmap(currentParameter));
    }

    // Reset the release timer since encoder is still being touched
    encoderNReleaseTime = 0;

    // Accumulate steps for turn detection
    encoderNAccumulator += encoderNDelta;

    // Calculate turns based on detents (2 detents = 1 turn)
    // Typical encoder: 4 steps per detent, so 8 steps = 2 detents = 1 turn
    int32_t turns = encoderNAccumulator / 8;  // 8 steps = 2 detents

    // Update parameter if we've crossed a turn boundary
    if (turns != 0) {
        // Map parameter to integer index (0 to NUM_OPTIONS-1)
        int8_t currentIndex = static_cast<int8_t>(currentParameter);
        int8_t newIndex = currentIndex + turns;

        // Clamp to valid range (0 to NUM_OPTIONS-1)
        if (newIndex < 0) newIndex = 0;
        if (newIndex > NUM_OPTIONS-1) newIndex = NUM_OPTIONS-1;

        // Update parameter if changed
        if (newIndex != currentIndex) {
            ParameterEnum newParam = static_cast<ParameterEnum>(newIndex);
            currentParameter = newParam;

            // Update display to show new parameter
            DisplayIO::showBitmap(parameterToBitmap(newParam));

            // Serial output for debugging
            Serial.print("[Parameter Name]: ");
            Serial.println(parameterName(newParam));

            // Reset accumulator to prevent "unwinding" at boundaries
            encoderNAccumulator = 0;
        } else {
            // Hit a boundary (clamped) - reset accumulator to prevent buildup
            encoderNAccumulator = 0;
        }
    }

    // Always update last position so we can detect when encoder stops moving
    lastEncoderNPosition = currentEncoderNPosition;
} else {
    // Encoder not being touched
    if (encoderNWasTouched) {
        // Encoder was just released - start cooldown timer
        encoderNWasTouched = false;
        encoderNReleaseTime = millis();
    }
}

// Handle display cooldown (return to default after 2 seconds of inactivity)
if (!encoderNWasTouched && encoderNReleaseTime > 0) {
    uint32_t now = millis();
    if (now - encoderNReleaseTime >= ENCODER_DISPLAY_COOLDOWN_MS) {
        // Cooldown expired - return to default display (unless effect is active)
        encoderNReleaseTime = 0;  // Clear cooldown

        // Check if any effects are active (use same priority logic as effect system)
        AudioEffectBase* freezeEffect = EffectManager::getEffect(EffectID::FREEZE);
        AudioEffectBase* chokeEffect = EffectManager::getEffect(EffectID::CHOKE);

        bool freezeActive = freezeEffect && freezeEffect->isEnabled();
        bool chokeActive = chokeEffect && chokeEffect->isEnabled();

        if (lastActivatedEffect == EffectID::FREEZE && freezeActive) {
            DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);
        } else if (lastActivatedEffect == EffectID::CHOKE && chokeActive) {
            DisplayIO::showChoke();
        } else if (freezeActive) {
            DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);
        } else if (chokeActive) {
            DisplayIO::showChoke();
        } else {
            DisplayIO::showDefault();
        }
    }
}
```

### Initialization

In `AppLogic::begin()`, initialize encoder position:

```cpp
void AppLogic::begin() {
    // ... other initialization ...

    // Initialize encoder positions
    lastEncoderNPosition = EncoderIO::getPosition(N-1);  // Encoder N is index N-1
}
```

---

## Adding a New Menu Parameter

Follow this checklist to add a new encoder menu parameter:

### 1. Define Parameter Enum

**File:** `include/app_logic.h` (or appropriate header)

```cpp
/**
 * [Parameter Name] options
 * Order matches visual layout from left to right: [list options]
 */
enum class ParameterName : uint8_t {
    OPTION_1 = 0,  // [Description]
    OPTION_2 = 1,  // [Description]
    OPTION_3 = 2,  // [Description]
    // ... etc.
};
```

**Guidelines:**
- Enum values start at 0 and increment sequentially
- Order matches **visual layout left to right** (not necessarily logical order)
- Use descriptive names that make sense in context

### 2. Create Bitmaps

**File:** `include/bitmaps.h`

Create bitmap arrays for each option:

```cpp
// [Parameter Name] - [Option 1]
static const uint8_t bitmap_param_option1[] PROGMEM = {
    // ... 1024 bytes of bitmap data ...
};

// [Parameter Name] - [Option 2]
static const uint8_t bitmap_param_option2[] PROGMEM = {
    // ... 1024 bytes of bitmap data ...
};

// ... etc.
```

**Tools:**
- **LCDAssistant** (free Windows tool): Converts 128×64 images to C arrays
- Format: Horizontal bytes, LSB first
- Design: White = on, Black = off

**See:** `DISPLAY_BITMAPS.md` for bitmap creation guide

### 3. Register Bitmaps

**File:** `include/display_io.h`

Add BitmapID enum entries:

```cpp
enum class BitmapID : uint8_t {
    DEFAULT = 0,
    // ... existing entries ...
    PARAM_OPTION_1 = X,   // [Parameter Name]: [Option 1]
    PARAM_OPTION_2 = X+1, // [Parameter Name]: [Option 2]
    PARAM_OPTION_3 = X+2, // [Parameter Name]: [Option 3]
    // ... etc.
};
```

**File:** `src/display_io.cpp`

Add to bitmap registry:

```cpp
static const BitmapData bitmapRegistry[] = {
    // ... existing entries ...
    { bitmap_param_option1, DISPLAY_WIDTH, DISPLAY_HEIGHT },  // BitmapID::PARAM_OPTION_1
    { bitmap_param_option2, DISPLAY_WIDTH, DISPLAY_HEIGHT },  // BitmapID::PARAM_OPTION_2
    { bitmap_param_option3, DISPLAY_WIDTH, DISPLAY_HEIGHT },  // BitmapID::PARAM_OPTION_3
    // ... etc.
};
```

**IMPORTANT:** Order must match BitmapID enum!

### 4. Add State Variables

**File:** `src/app_logic.cpp`

After existing encoder state sections:

```cpp
// ========== [PARAMETER NAME] STATE ==========
// Encoder N state tracking (for [parameter name] menu)
static int32_t lastEncoderNPosition = 0;      // Last raw position (for detecting movement)
static int32_t encoderNAccumulator = 0;       // Accumulated steps since last turn
static bool encoderNWasTouched = false;       // Tracks if encoder was recently touched
static uint32_t encoderNReleaseTime = 0;      // Time when encoder was released

// Parameter state
static ParameterName currentParameter = ParameterName::DEFAULT_OPTION;
```

### 5. Add Helper Functions

**File:** `src/app_logic.cpp`

After existing helper functions:

```cpp
// Helper function to convert ParameterName to BitmapID
static BitmapID parameterToBitmap(ParameterName param) {
    switch (param) {
        case ParameterName::OPTION_1: return BitmapID::PARAM_OPTION_1;
        case ParameterName::OPTION_2: return BitmapID::PARAM_OPTION_2;
        case ParameterName::OPTION_3: return BitmapID::PARAM_OPTION_3;
        // ... etc.
        default: return BitmapID::PARAM_DEFAULT;
    }
}

// Helper function to get parameter name string
static const char* parameterName(ParameterName param) {
    switch (param) {
        case ParameterName::OPTION_1: return "Option 1";
        case ParameterName::OPTION_2: return "Option 2";
        case ParameterName::OPTION_3: return "Option 3";
        // ... etc.
        default: return "Default";
    }
}
```

### 6. Initialize Encoder Position

**File:** `src/app_logic.cpp`, in `AppLogic::begin()`

```cpp
void AppLogic::begin() {
    // ... existing initialization ...

    // Initialize encoder positions
    lastEncoderNPosition = EncoderIO::getPosition(N-1);  // Encoder N is index N-1
}
```

### 7. Add Main Loop Logic

**File:** `src/app_logic.cpp`, in `AppLogic::threadLoop()`

Insert the encoder menu logic (see [Implementation Pattern](#main-loop-logic) above) after `EncoderIO::update()` and before transport events section.

**Location:** Between sections "2. HANDLE ENCODER 4" and "3. DRAIN TRANSPORT EVENTS"

**Key changes to template:**
- Replace `N` with encoder number (1-4)
- Replace `ParameterEnum` with your enum type
- Replace `NUM_OPTIONS` with number of options (e.g., 3 for 3 options = indices 0-2)
- Update comments to reflect your parameter

### 8. Add Public API (Optional)

If other modules need to read/write this parameter:

**File:** `include/app_logic.h`

```cpp
namespace AppLogic {
    // ... existing functions ...

    /**
     * @brief Get current [parameter name] setting
     * @return Current parameter value
     */
    ParameterName getParameterName();

    /**
     * @brief Set [parameter name] setting
     * @param param New parameter value
     */
    void setParameterName(ParameterName param);
}
```

**File:** `src/app_logic.cpp`

```cpp
ParameterName AppLogic::getParameterName() {
    return currentParameter;
}

void AppLogic::setParameterName(ParameterName param) {
    currentParameter = param;
}
```

### 9. Build and Test

```bash
cd build
ninja
# Upload microloop.hex with Teensy Loader
```

**Testing checklist:**
- ✅ Encoder shows menu bitmap on first touch
- ✅ 2 detents = 1 step in either direction
- ✅ Display updates immediately when value changes
- ✅ Min/max boundaries don't require "unwinding"
- ✅ Display returns to default after 2 seconds idle
- ✅ Serial output shows parameter name on change
- ✅ Parameter persists across menu exits

---

## Code Reference

### Existing Implementations

**Global Quantization (Encoder 4):**
- Enum: `app_logic.h:26-31`
- State: `app_logic.cpp:67-76`
- Helpers: `app_logic.cpp:85-105`
- Main Loop: `app_logic.cpp:264-408`
- Initialization: `app_logic.cpp:153`

**Choke Length (Encoder 3):**
- Enum: `audio_choke.h:67-70`
- State: `app_logic.cpp:78-83`
- Helpers: `app_logic.cpp:107-141`
- Main Loop: `app_logic.cpp:410-550`
- Initialization: `app_logic.cpp:152`

### Key Files

| File | Purpose |
|------|---------|
| `include/app_logic.h` | Parameter enum definitions, public API |
| `src/app_logic.cpp` | Menu logic, state variables, helper functions |
| `include/display_io.h` | BitmapID enum |
| `src/display_io.cpp` | Bitmap registry |
| `include/bitmaps.h` | Bitmap data arrays (PROGMEM) |
| `include/encoder_io.h` | Encoder hardware API |

### Display Priority Logic

```cpp
// Priority order (highest to lowest):
1. Encoder menu (while encoder active)
2. Encoder cooldown (2 seconds after last touch)
3. Last activated effect (Freeze > Choke)
4. Any active effect (Freeze > Choke)
5. Default bitmap
```

**Implementation:** `app_logic.cpp:252-273` (encoder 4 cooldown) and `app_logic.cpp:432-453` (encoder 3 cooldown)

---

## Performance & Thread Safety

### CPU Usage

**Per encoder menu (idle):**
- State variables: 16 bytes RAM
- Logic execution: ~0% CPU (skipped when `encoderNDelta == 0`)

**Per encoder menu (active):**
- Delta calculation: ~10 CPU cycles
- Accumulator update: ~20 CPU cycles
- Boundary checks: ~30 CPU cycles
- Display update: ~200 CPU cycles (async queue push)
- **Total: <1% of 600MHz CPU**

### Memory Usage

**RAM per parameter:**
- State variables: 16 bytes
- Enum storage: 1 byte
- **Total: ~17 bytes**

**Flash per parameter:**
- Bitmaps: 1024 bytes × number of options
- Code: ~500 bytes (main loop logic)
- Helper functions: ~200 bytes
- **Total: ~(1024 × N) + 700 bytes**

### Thread Safety

**Lock-free communication:**
- `EncoderIO::getPosition()` reads volatile position (safe from ISR)
- `DisplayIO::showBitmap()` pushes to SPSC queue (wait-free)
- No mutexes, no blocking calls

**Execution context:**
- Encoder ISR: Updates positions in MCP23017 interrupt handler
- App Thread: Drains encoder events, updates menu state, pushes display commands
- Display Thread: Drains display queue, updates OLED

**Latency:**
- Encoder turn → Display update: ~5-10ms (typical)
- Components: ISR (0.026ms) + App thread (2ms max) + Display queue + I2C (20-30ms)
- Feels instant to human perception

---

## Design Patterns & Best Practices

### Accumulator Pattern

**Why:** Encoder steps arrive incrementally over multiple loop iterations (encoder ISR → app thread @ 2ms interval)

**How:**
1. Accumulate deltas: `accumulator += delta`
2. Calculate full turns: `turns = accumulator / 8`
3. Apply turns to parameter: `newIndex = currentIndex + turns`
4. Reset accumulator on change or boundary: `accumulator = 0`

**Benefit:** Reliable turn detection regardless of app thread timing jitter

### Boundary Reset Pattern

**Why:** Prevent accumulator buildup when hitting min/max (prevents "unwinding")

**How:**
```cpp
if (newIndex != currentIndex) {
    // Value changed - reset accumulator
    accumulator = 0;
} else {
    // Hit boundary (clamped) - also reset accumulator
    accumulator = 0;
}
```

**Result:** Consistent 2-detent feel at all positions, immediate direction reversal

### Display Cooldown Pattern

**Why:** Give user time to see final value before returning to default display

**How:**
1. On encoder release: `releaseTime = millis()`
2. Every loop: Check `millis() - releaseTime >= 2000`
3. On timeout: Return to default/effect display, reset `releaseTime = 0`
4. If encoder touched again: Reset `releaseTime = 0` (restart cooldown)

**Result:** Menu stays visible for 2 seconds after last interaction

### Visual Ordering Convention

**Left to Right (CCW to CW):**
- Smaller → Larger (e.g., 1/32 → 1/4)
- Off → On (e.g., Free → Quantized)
- Less → More (e.g., Low → High)

**Rationale:** Matches Western reading convention, feels intuitive

---

## Troubleshooting

### Problem: Encoder needs 3-4 detents to change value at boundaries

**Cause:** Accumulator buildup when hitting min/max

**Solution:** Reset accumulator on both value change AND boundary hit:

```cpp
if (newIndex != currentIndex) {
    accumulator = 0;  // Value changed
} else {
    accumulator = 0;  // Hit boundary (clamped)
}
```

**See:** `app_logic.cpp:358-363` (encoder 4 fix)

### Problem: Display doesn't return to default after encoder idle

**Cause:** Release timer not being reset while encoder is moving

**Solution:** Reset timer on any encoder movement:

```cpp
if (encoderDelta != 0) {
    encoderReleaseTime = 0;  // Reset timer
    // ... rest of logic
}
```

**See:** `app_logic.cpp:335` (encoder 4 fix)

### Problem: Menu updates too sensitive (changes on slight touch)

**Cause:** Turn threshold too low (1 detent instead of 2)

**Solution:** Use 8-step threshold for 2 detents:

```cpp
int32_t turns = accumulator / 8;  // 8 steps = 2 detents
```

**Do not use:** `turns = accumulator / 4` (too sensitive)

### Problem: Display shows wrong bitmap after effect auto-releases

**Cause:** Display priority not checking current effect state

**Solution:** Poll effect state and update display when state changes:

```cpp
if (lastActivatedEffect == EffectID::CHOKE && choke.isEnabled() == false) {
    // Effect auto-released - update display
    DisplayIO::showDefault();
    lastActivatedEffect = EffectID::NONE;
}
```

**See:** `app_logic.cpp:518-560` (choke auto-release monitor)

---

## Future Enhancements

### Potential Features

1. **Multi-page menus**: Long-press encoder button to enter sub-menu
2. **Numeric displays**: Show BPM, sample count, etc. with dynamic text rendering
3. **Encoder acceleration**: Faster turns = larger steps (for wide ranges)
4. **Visual indicators**: Scrollbar, option count (e.g., "2/4")
5. **Save/load presets**: Store parameter state to SD card or EEPROM

### Reserved Encoders

| Encoder | Status | Suggested Use |
|---------|--------|---------------|
| 1 | Available | Loop selection / Menu navigation |
| 2 | Available | Loop feedback / Tempo adjust |
| 3 | **In Use** | Choke length mode |
| 4 | **In Use** | Global quantization |

### Compatibility Notes

- Current system supports up to **4 encoders** (MCP23017 has 16 GPIO pins)
- Display supports **unlimited bitmaps** (limited by flash: 8MB total, ~1KB per bitmap)
- SPSC queue supports **16 display commands** (sufficient for menu system)
- App thread runs at **2ms interval** (fast enough for responsive menus)

---

## References

- **Encoder Hardware:** `ENCODER_ARCHITECTURE.md` (ISR design, quadrature decoding)
- **Display System:** `DISPLAY_BITMAPS.md` (bitmap creation, registry)
- **Effect System:** `EFFECT_SYSTEM_MIGRATION.md` (command dispatch, polymorphic effects)
- **Timing System:** `utils/TIMEKEEPER_USAGE.md` (sample-accurate timing for quantization)

---

**Document Version:** 1.0
**Last Updated:** 2025-01-14
**Authors:** Claude Code (with user guidance)
