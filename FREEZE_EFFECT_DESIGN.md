# FREEZE Effect Design Document

## Document Purpose

This document outlines different implementation approaches for the FREEZE effect, a live-performance audio effect that "freezes" incoming audio in place, creating a harsh, metallic sound similar to a Windows bluescreen freeze. This is part of Phase 4 of the effect system migration.

**Last Updated:** 2025-10-12
**Status:** Planning Phase

---

## Executive Summary

### Effect Description

**FREEZE** is a momentary audio effect that captures and loops a segment of incoming audio, creating the sensation that sound has been "frozen in place." Inspired by the harsh, repeating sound that occurs when a Windows desktop crashes, the effect should have a metallic, glitchy character.

### User Interaction

- **Hardware:** Neokey 1x4, Key 2 (third from left, between unused Key 1 and CHOKE on Key 3)
- **Button behavior:** Momentary (hold to freeze, release to resume)
- **LED feedback:** Cyan/Blue when active, Green when inactive
- **Display feedback:** Shows `bitmap_freeze_active` when engaged
- **Can stack with CHOKE:** Both effects can be active simultaneously

### Audio Path Position

```
I2S Input → TimeKeeper → [FREEZE] → [CHOKE] → I2S Output
```

**Rationale for ordering:**
- FREEZE comes before CHOKE (as specified)
- When both active: FREEZE captures/loops audio, CHOKE then mutes it
- When CHOKE released (FREEZE still active): Frozen audio becomes audible
- Independent operation: Either effect can be used alone

---

## Implementation Approaches

### Approach 1: Single Block Repetition

#### Description

Capture exactly one audio block (128 samples = 2.9ms @ 44.1kHz) when freeze is engaged, then repeat this block indefinitely until released.

#### Technical Details

**Memory footprint:**
- `int16_t frozenBlockL[128]` - 256 bytes
- `int16_t frozenBlockR[128]` - 256 bytes
- **Total: 512 bytes RAM**

**CPU usage:**
- Capture: One-time `memcpy` (2 blocks × 256 bytes) ≈ 300 cycles
- Playback: Per-block `memcpy` (2 × 256 bytes) ≈ 200 cycles per block
- **Total: ~200 cycles/block during freeze** (<0.02% CPU @ 600 MHz)

**Code complexity:**
- Simple state machine: `PASSTHROUGH`, `CAPTURING`, `FROZEN`
- ~60 lines of code in `update()` method
- No buffer management (fixed-size arrays)

#### Audio Characteristics

**Frequency content:**
- Loop period: 2.9ms = 344 Hz fundamental
- Harmonics at 688 Hz, 1032 Hz, 1376 Hz, etc.
- Creates a "buzzy" pitch depending on audio content at capture moment

**Sonic character:**
- Very harsh and aggressive
- Prominent pitch artifacts (short loop = obvious repetition)
- Sounds like a "stuck sample" or digital glitch
- Most similar to Windows bluescreen freeze

**Musical considerations:**
- Works best with percussive/transient material (drums, hits)
- Less musical with sustained tones (creates obvious buzz)
- Good for extreme, glitchy performance effects

#### Pros

- ✅ **Simplest implementation** - minimal code, easy to debug
- ✅ **Lowest CPU usage** - just memcpy operations
- ✅ **Minimal RAM** - only 512 bytes
- ✅ **Deterministic** - no buffer management complexity
- ✅ **Lowest latency** - captures and freezes in one block (2.9ms)
- ✅ **Most "harsh"** - closest to Windows bluescreen inspiration
- ✅ **Real-time safe** - no dynamic allocation, bounded execution

#### Cons

- ❌ **Very buzzy** - 2.9ms loop creates audible pitch artifacts
- ❌ **Least flexible** - cannot change freeze duration without code change
- ❌ **Limited musical use** - pitch artifacts limit applications
- ❌ **No fine control** - either works or doesn't, no tuning knobs

#### Best Use Cases

- Extreme glitch effects
- Percussive material (drum freeze)
- Short "stutter" bursts
- When you want maximum harshness

---

### Approach 2: Circular Buffer (RECOMMENDED)

#### Description

Maintain a configurable circular buffer (default 50-100ms) that continuously records incoming audio. When freeze is engaged, loop through this captured buffer. Buffer size is compile-time configurable for flexibility.

#### Technical Details

**Memory footprint (buffer size dependent):**
- **50ms:** 2205 samples × 2 channels × 2 bytes = **8,820 bytes (~8.6 KB)**
- **100ms:** 4410 samples × 2 channels × 2 bytes = **17,640 bytes (~17.2 KB)**
- **200ms:** 8820 samples × 2 channels × 2 bytes = **35,280 bytes (~34.5 KB)**

**For flexibility, can set to 3ms (like Approach 1):**
- **3ms:** 132 samples × 2 channels × 2 bytes = **528 bytes**
- This allows starting with short freeze, scaling up as desired

**CPU usage:**
- Passthrough: Buffer write (2 × memcpy) + index increment ≈ 300 cycles
- Frozen: Buffer read (2 × memcpy) + index increment ≈ 300 cycles
- **Total: ~300 cycles/block** (<0.03% CPU @ 600 MHz)
- Nearly identical to Approach 1, just different buffer management

**Code complexity:**
- Circular buffer with write/read pointers
- State machine: `PASSTHROUGH`, `CAPTURING`, `FROZEN`
- ~80-100 lines of code in `update()` method
- Simple modulo arithmetic for wraparound

#### Audio Characteristics

**Frequency content (50ms buffer example):**
- Loop period: 50ms = 20 Hz fundamental
- Harmonics at 40 Hz, 60 Hz, 80 Hz, etc.
- Much less obvious pitch artifacts (below perceivable pitch range)

**Frequency content (3ms buffer - matches Approach 1 sonically):**
- Loop period: 3ms = 333 Hz fundamental
- Similar buzz to Approach 1
- **Key benefit:** Can be changed to 50ms or 100ms by editing one constant

**Sonic character (50-100ms):**
- Harsh but more "textured" than Approach 1
- Retains some rhythmic/melodic content from captured audio
- Less "buzzy," more "metallic shimmer"
- Still aggressive, but more musically interesting

**Sonic character (3ms):**
- Nearly identical to Approach 1
- Provides "upgrade path" to longer buffers

**Musical considerations:**
- **3ms:** Same as Approach 1 - percussive, glitchy
- **50ms:** Works with wider range of material
- **100ms:** Recognizable frozen phrases, more "loop" than "freeze"
- Tunable for different performance contexts

#### Pros

- ✅ **Highly flexible** - one constant changes freeze duration (3ms to 200ms+)
- ✅ **Tuneable harshness** - short buffer = harsh, long buffer = textured
- ✅ **Still simple** - circular buffer is well-understood pattern
- ✅ **Low CPU usage** - similar to Approach 1
- ✅ **Real-time safe** - fixed allocation, no dynamic memory
- ✅ **More musical options** - longer buffers work with sustained material
- ✅ **"Upgrade path"** - start with 3ms, scale up if desired

#### Cons

- ❌ **More RAM** - scales with buffer size (but Teensy has 1 MB, plenty available)
- ❌ **Slightly more complex** - need to manage read/write pointers
- ❌ **Latency** - buffer must fill before freeze captures full duration (not noticeable in practice)

#### Best Use Cases

- **All use cases from Approach 1** (when set to 3ms)
- **Plus:** More textured freeze effects (50-100ms)
- **Plus:** Performance flexibility (tune buffer size for song/style)
- Recommended as "best of all worlds" solution

#### Configuration Example

```cpp
// include/audio_freeze.h

class AudioEffectFreeze : public AudioEffectBase {
private:
    // Configurable freeze buffer duration
    // Options:
    //   3ms   = 132 samples   (harsh buzz, like Approach 1)
    //   10ms  = 441 samples   (medium harshness)
    //   50ms  = 2205 samples  (textured freeze)
    //   100ms = 4410 samples  (loop-like freeze)
    static constexpr uint32_t FREEZE_BUFFER_MS = 3;  // <-- CHANGE THIS VALUE

    static constexpr size_t FREEZE_BUFFER_SAMPLES =
        (FREEZE_BUFFER_MS * 44100) / 1000;

    int16_t m_freezeBufferL[FREEZE_BUFFER_SAMPLES];
    int16_t m_freezeBufferR[FREEZE_BUFFER_SAMPLES];

    size_t m_writePos;  // Where to write next incoming sample
    size_t m_readPos;   // Where to read from during freeze
};
```

---

### Approach 3: Crossfaded Circular Buffer

#### Description

Similar to Approach 2, but add crossfading at the loop boundary to eliminate clicks/pops. This creates a smoother, more professional freeze effect at the cost of higher CPU usage.

#### Technical Details

**Memory footprint:**
- Same as Approach 2: 8.6 KB (50ms) to 34.5 KB (200ms)
- Additional crossfade buffer: 2 × 128 samples × 2 bytes = 512 bytes
- **Total: ~9.1 KB (50ms) to ~35 KB (200ms)**

**CPU usage:**
- Passthrough: Same as Approach 2 (~300 cycles)
- Frozen: Buffer read + crossfade multiply ≈ 1500 cycles per block
- Crossfade calculation: 128 samples × 2 channels × multiply/add
- **Total: ~1500 cycles/block** (~0.09% CPU @ 600 MHz)

**Code complexity:**
- Circular buffer (same as Approach 2)
- Crossfade window calculation (linear or cosine)
- Dual read pointers (current + lookahead for crossfade)
- ~150-200 lines of code

#### Audio Characteristics

**Frequency content:**
- Same fundamental frequency as Approach 2
- Smoother spectrum (no click harmonics)
- Less harsh high-frequency content

**Sonic character:**
- Smoothest freeze effect
- No audible clicks at loop point
- More "reverb/echo-like" than "frozen"
- Professional studio quality

**Musical considerations:**
- Works with all material (percussive and sustained)
- Less aggressive than Approach 1/2 (may not match "bluescreen" inspiration)
- Good for ambient/experimental music
- May be "too smooth" for harsh live performance

#### Pros

- ✅ **Best audio quality** - no clicks or pops
- ✅ **Most professional sounding** - studio-grade effect
- ✅ **Flexible buffer size** - inherits from Approach 2
- ✅ **Works with all material** - no artifacts on sustained tones

#### Cons

- ❌ **Higher CPU usage** - 5x more cycles than Approach 2
- ❌ **More complex code** - crossfade math, dual pointers
- ❌ **Less harsh** - may not match "Windows bluescreen" inspiration
- ❌ **More RAM** - extra crossfade buffers

#### Best Use Cases

- Studio/recording applications
- Ambient/experimental music
- When audio quality is paramount
- If you want "freeze as reverb" rather than "freeze as glitch"

**Recommendation:** Skip for Phase 4. This is overkill for the "harsh freeze" goal. Consider for future enhancement if needed.

---

### Approach 4: Stutter-Style Freeze (Rhythmic)

#### Description

Capture a very short buffer (1-16ms) and repeat it with variations: forward/backward (ping-pong), different speeds, or rhythmic patterns. Optionally sync to MIDI clock for musical repetitions (e.g., 1/16th note stutters).

#### Technical Details

**Memory footprint:**
- Short buffer: 16ms = 705 samples × 2 channels × 2 bytes = **2,820 bytes (~2.8 KB)**
- Pattern table (optional): ~512 bytes
- **Total: ~3.3 KB**

**CPU usage:**
- Variable depending on features:
  - Basic ping-pong: ~500 cycles/block
  - Speed variation: ~800 cycles/block
  - MIDI clock sync: +200 cycles/block
- **Total: ~500-1000 cycles/block** (~0.03-0.06% CPU)

**Code complexity:**
- Circular buffer (like Approach 2)
- Ping-pong or speed variation logic
- Optional TimeKeeper integration for sync
- Pattern sequencer (if rhythmic patterns desired)
- ~150-250 lines of code

#### Audio Characteristics

**Frequency content:**
- Variable depending on buffer size and speed
- 16ms = 62.5 Hz fundamental (ping-pong doubles to 125 Hz)
- Speed variations create pitch shifts

**Sonic character:**
- Very distinctive "stutter" effect
- Rhythmic and aggressive
- Can be musical or glitchy depending on settings
- Unique compared to other freeze approaches

**Musical considerations:**
- Great for rhythmic material (drums, percussion)
- Synced stutters work well with external sequencer (Digitakt)
- Requires more "tweaking" to sound good
- Less "set and forget" than Approaches 1/2

#### Pros

- ✅ **Unique sound** - stands out from typical freeze effects
- ✅ **Rhythmic** - can sync to MIDI clock
- ✅ **Performance-oriented** - good for live manipulation
- ✅ **Moderate CPU** - still real-time safe

#### Cons

- ❌ **Most complex** - many parameters to tune
- ❌ **Requires design decisions** - ping-pong vs speed vs patterns?
- ❌ **May not match inspiration** - less "Windows bluescreen," more "DJ scratch"
- ❌ **Needs TimeKeeper integration** - for sync features

#### Best Use Cases

- DJ-style performance effects
- Rhythmic stutter/glitch
- Synced repetitions (e.g., 1/16th note)
- When you want a unique signature sound

**Recommendation:** Skip for Phase 4. This is a different effect concept. Save for future "STUTTER" effect if desired.

---

## Comparison Matrix

| Feature | Approach 1<br>(Single Block) | Approach 2<br>(Circular Buffer) | Approach 3<br>(Crossfaded) | Approach 4<br>(Stutter) |
|---------|------------------------------|----------------------------------|----------------------------|-------------------------|
| **RAM Usage** | 512 bytes | 528 bytes - 35 KB<br>(configurable) | 9 KB - 35 KB | 3 KB |
| **CPU Usage** | 200 cycles<br>(<0.02%) | 300 cycles<br>(<0.03%) | 1500 cycles<br>(~0.09%) | 500-1000 cycles<br>(0.03-0.06%) |
| **Code Complexity** | Low (60 lines) | Medium (80 lines) | High (150 lines) | Very High (200+ lines) |
| **Harshness** | Very harsh (344 Hz buzz) | Configurable<br>(3ms = harsh, 100ms = smooth) | Smooth (no clicks) | Variable (depends on settings) |
| **Flexibility** | None (fixed 2.9ms) | High (3ms to 200ms+) | High (same as Approach 2) | Very High (many parameters) |
| **Matches Inspiration** | ✅ Yes (most similar) | ✅ Yes (at 3ms setting) | ❌ No (too smooth) | ❌ No (different concept) |
| **Musical Range** | Narrow (percussive only) | Wide (depends on buffer size) | Very Wide (all material) | Medium (rhythmic material) |
| **Real-Time Safe** | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes |
| **Future-Proof** | ❌ No (can't scale up) | ✅ Yes (edit one constant) | ⚠️ Maybe (complex to extend) | ⚠️ Maybe (different direction) |
| **Phase 4 Suitable** | ✅ Yes (simple, effective) | ✅✅ Yes (RECOMMENDED) | ❌ No (overkill) | ❌ No (scope creep) |

---

## Recommended Approach: Circular Buffer with 3ms Default

### Why This Wins

**Approach 2 (Circular Buffer) with `FREEZE_BUFFER_MS = 3` provides the best balance:**

1. **Matches Approach 1 sonically** (when set to 3ms)
   - 3ms ≈ 132 samples (vs Approach 1's 128 samples)
   - Nearly identical harsh buzz (333 Hz vs 344 Hz fundamental)
   - Same "Windows bluescreen" character

2. **Adds flexibility at zero cost**
   - Change one constant to experiment with 10ms, 50ms, 100ms, etc.
   - User can tune to taste without code rewrite
   - Provides "upgrade path" for future performances

3. **Minimal overhead vs Approach 1**
   - RAM: 528 bytes (3ms) vs 512 bytes (2.9ms) - negligible difference
   - CPU: 300 cycles vs 200 cycles - still <0.03%
   - Code complexity: Slightly higher, but well worth flexibility

4. **Real-world tuning benefits**
   - Can test 3ms, 5ms, 10ms, 25ms, 50ms on hardware
   - Find "sweet spot" for your specific musical style
   - No recompilation needed (just edit constant, rebuild)

5. **Future-proof design**
   - If you want longer freezes later, just change constant
   - No architectural changes needed
   - Same code handles 3ms and 200ms equally well

### Implementation Details

**Default configuration:**
```cpp
static constexpr uint32_t FREEZE_BUFFER_MS = 3;  // Start with harsh freeze
```

**Easy experimentation:**
```cpp
// Try these settings by uncommenting one:

// static constexpr uint32_t FREEZE_BUFFER_MS = 3;   // Very harsh (like Approach 1)
// static constexpr uint32_t FREEZE_BUFFER_MS = 10;  // Medium harsh
// static constexpr uint32_t FREEZE_BUFFER_MS = 25;  // Balanced
static constexpr uint32_t FREEZE_BUFFER_MS = 50;     // Textured freeze
// static constexpr uint32_t FREEZE_BUFFER_MS = 100; // Loop-like
```

**RAM usage guide:**
- 3ms: 528 bytes (0.05% of 1 MB RAM)
- 10ms: 1,764 bytes (0.17%)
- 50ms: 8,820 bytes (0.86%)
- 100ms: 17,640 bytes (1.72%)
- 200ms: 35,280 bytes (3.44%)

All values well within Teensy 4.1's 1 MB RAM budget.

---

## Integration with Effect System

### Audio Graph

```
I2S Input → TimeKeeper → [FREEZE] → [CHOKE] → I2S Output
```

**Freeze before Choke rationale:**
- Freeze captures/loops audio
- Choke then mutes the frozen output
- When Choke released (Freeze still active): Frozen audio audible
- Independent operation: Either effect works alone

### Button Mapping (Neokey 1x4)

```
[Key 0]   [Key 1]      [Key 2]    [Key 3]
 UNUSED    UNUSED      FREEZE     CHOKE
                     (momentary) (momentary)
```

**Note:** User specified Key 2 for FREEZE (third from left), which is next to CHOKE on Key 3.

### LED Colors

```cpp
// input_io.cpp LED mapping
case EffectID::FREEZE:
    if (enabled) {
        setPixelColor(2, 0x00, 0xFF, 0xFF);  // Cyan (freeze active)
    } else {
        setPixelColor(2, 0x00, 0xFF, 0x00);  // Green (inactive)
    }
    break;
```

### Display Priority System

The display should show the most recently activated effect:

```cpp
// app_logic.cpp display logic

// Track last activated effect
static EffectID lastActivatedEffect = EffectID::NONE;

// When command executed:
if (effect->isEnabled()) {
    lastActivatedEffect = cmd.targetEffect;
}

// Display update:
if (lastActivatedEffect == EffectID::FREEZE && freeze.isEnabled()) {
    DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);
} else if (lastActivatedEffect == EffectID::CHOKE && choke.isEnabled()) {
    DisplayIO::showChoke();
} else if (!freeze.isEnabled() && !choke.isEnabled()) {
    DisplayIO::showDefault();
}
```

**Behavior:**
- Press FREEZE → Display shows `bitmap_freeze_active`
- Press CHOKE (FREEZE still held) → Display shows `bitmap_choke_active`
- Release CHOKE (FREEZE still held) → Display shows `bitmap_freeze_active` again
- Release both → Display shows default bitmap

### EffectID Registration

```cpp
// include/effect_manager.h (add to enum)
enum class EffectID : uint8_t {
    NONE = 0,
    CHOKE = 1,
    FREEZE = 2,  // NEW
    // Future: DELAY = 3, REVERB = 4, etc.
};
```

### Button-to-Command Mapping

```cpp
// src/input_io.cpp (add to buttonMappings table)
{
    .keyIndex = 2,  // Key 2 (third from left)
    .pressCommand = Command{CommandType::EFFECT_ENABLE, EffectID::FREEZE},
    .releaseCommand = Command{CommandType::EFFECT_DISABLE, EffectID::FREEZE}
},
```

### Display Bitmap Registration

```cpp
// include/display_io.h (update enum)
enum class BitmapID : uint8_t {
    DEFAULT = 0,
    CHOKE_ACTIVE = 1,
    FREEZE_ACTIVE = 2,  // NEW - bitmap already exists in bitmaps.h
};
```

```cpp
// src/display_io.cpp (add to registry)
{BitmapID::FREEZE_ACTIVE, bitmap_freeze_active},
```

---

## Performance Considerations

### CPU Budget

**Current system (CHOKE only):**
- Audio passthrough: ~50 cycles/block
- Choke fading: ~2000 cycles/block (when active)
- TimeKeeper: ~20 cycles/block
- **Total: ~2070 cycles/block** (<0.13% of 1.7M cycles available)

**With FREEZE added (Approach 2, 50ms buffer):**
- Freeze passthrough: +300 cycles/block (always recording)
- Freeze playback: +300 cycles/block (when active)
- Choke: ~2000 cycles/block (when active)
- **Total: ~2370 cycles/block** (<0.14% of 1.7M cycles available)

**Headroom remaining:**
- ~1,697,630 cycles/block available for future effects
- Can add 5-10 more effects of similar complexity
- Safe for future loop recording, sample playback, etc.

### RAM Budget

**Current system:**
- Code + libraries: ~70 KB
- Audio block pool (Teensy Audio Library): ~50 KB
- SPSC queues: ~5 KB
- Thread stacks (5 threads × 2-3 KB): ~12 KB
- Choke + TimeKeeper + misc: ~5 KB
- **Total used: ~142 KB** (14% of 1 MB)

**With FREEZE added (50ms buffer):**
- Freeze buffers: +8.8 KB
- **Total used: ~151 KB** (15% of 1 MB)

**Even with 100ms buffer:**
- Freeze buffers: +17.6 KB
- **Total used: ~160 KB** (16% of 1 MB)

**Headroom remaining:**
- ~840 KB available for loop recording (future)
- Could store 19 seconds of stereo audio @ 44.1kHz
- Or multiple shorter loops (4 × 4-second loops, etc.)

---

## Implementation Checklist (Phase 4)

Following the phase structure from `EFFECT_SYSTEM_MIGRATION.md`:

### 1. Create AudioEffectFreeze class
- [ ] `include/audio_freeze.h` - Header file
- [ ] `src/audio_freeze.cpp` - Implementation (if needed, or header-only)
- [ ] Inherit from `AudioEffectBase`
- [ ] Implement pure virtual methods: `enable()`, `disable()`, `toggle()`, `isEnabled()`, `getName()`
- [ ] Implement `update()` - Audio ISR callback with circular buffer logic
- [ ] Set `FREEZE_BUFFER_MS = 3` (start with harsh freeze)

### 2. Register in EffectManager
- [ ] Add `FREEZE = 2` to `EffectID` enum in `include/effect_manager.h`
- [ ] Create global `AudioEffectFreeze freeze;` in `src/main.cpp`
- [ ] Call `EffectManager::registerEffect(EffectID::FREEZE, &freeze);` in `setup()`

### 3. Update audio graph
- [ ] Insert FREEZE between TimeKeeper and Choke:
  ```cpp
  AudioConnection c3(timekeeper, 0, freeze, 0);
  AudioConnection c4(timekeeper, 1, freeze, 1);
  AudioConnection c5(freeze, 0, choke, 0);
  AudioConnection c6(freeze, 1, choke, 1);
  ```
- [ ] Update existing `AudioConnection` objects to new topology

### 4. Add button mapping
- [ ] Update `buttonMappings[]` in `src/input_io.cpp`:
  ```cpp
  {
      .keyIndex = 2,
      .pressCommand = Command{CommandType::EFFECT_ENABLE, EffectID::FREEZE},
      .releaseCommand = Command{CommandType::EFFECT_DISABLE, EffectID::FREEZE}
  },
  ```

### 5. Add LED control
- [ ] Update `InputIO::setLED()` in `src/input_io.cpp`:
  ```cpp
  case EffectID::FREEZE:
      setPixelColor(2, enabled ? 0x00FFFF : 0x00FF00);  // Cyan or Green
      break;
  ```

### 6. Add display integration
- [ ] Add `FREEZE_ACTIVE = 2` to `BitmapID` enum in `include/display_io.h`
- [ ] Register `bitmap_freeze_active` in `src/display_io.cpp`
- [ ] Update `app_logic.cpp` display logic to handle FREEZE

### 7. Testing
- [ ] Compile successfully
- [ ] Key 2 (FREEZE) works independently
- [ ] Key 3 (CHOKE) still works independently
- [ ] Both effects work simultaneously (FREEZE → CHOKE order)
- [ ] Display shows correct bitmap for each effect
- [ ] LED colors correct (Key 2 = cyan/green, Key 3 = red/green)
- [ ] Serial output shows "Freeze ENABLED" / "Freeze DISABLED"
- [ ] Audio quality: harsh freeze at 3ms (buzzy, metallic)

### 8. Optional tuning
- [ ] Try 10ms, 25ms, 50ms buffer sizes
- [ ] Document preferred setting in CLAUDE.md

---

## Future Enhancements (Post-Phase 4)

These are NOT part of Phase 4 but are enabled by this architecture:

### 1. Variable Freeze Duration (Runtime Control)
- Add MIDI CC control for buffer size (e.g., CC 22 = freeze duration)
- Requires dynamic buffer allocation (or fixed max buffer with variable playback)
- Complexity: Medium

### 2. Freeze with Pitch Shift
- Vary playback speed (read position increment)
- 2× speed = +12 semitones, 0.5× speed = -12 semitones
- Requires interpolation for smooth pitch shifting
- Complexity: High

### 3. Freeze with Reverse Playback
- Decrement read pointer instead of increment
- Creates "reverse freeze" effect
- Simple state machine change
- Complexity: Low

### 4. Multi-Freeze (Layers)
- Capture multiple freeze buffers, blend/crossfade between them
- "Freeze stack" - press FREEZE multiple times to layer
- Requires multiple buffer sets
- Complexity: High

### 5. MIDI-Synced Stutter (Approach 4)
- Implement Approach 4 as separate "STUTTER" effect
- Use TimeKeeper for rhythmic sync (1/8th, 1/16th, 1/32nd notes)
- Different button (Key 1?)
- Complexity: High

---

## Design Rationale Q&A

### Q: Why not just use Approach 1 (simpler)?

**A:** Approach 2 with 3ms buffer is **nearly identical** in sound and performance, but adds flexibility at minimal cost. The slight increase in code complexity (~20 lines) is worth the ability to tune the freeze duration without architectural changes.

### Q: Why not crossfade (Approach 3)?

**A:** Crossfading is "too smooth" for the "Windows bluescreen" inspiration. The harshness and click artifacts are part of the desired aesthetic. Approach 3 would be better for a "reverb-like freeze" effect, which is a different creative goal.

### Q: Why not stutter-style (Approach 4)?

**A:** Approach 4 is a fundamentally different effect with different interaction paradigms (speed, patterns, sync). It's better suited as a separate "STUTTER" effect in the future. Mixing too many features into FREEZE would complicate the implementation and user experience.

### Q: What if 3ms is too harsh?

**A:** That's the beauty of Approach 2 - just change the constant to 10ms, 25ms, or 50ms. Test on hardware, find your preference, and commit the value. No code restructuring needed.

### Q: Why FREEZE before CHOKE in the audio graph?

**A:** This ordering allows FREEZE to capture and loop audio, then CHOKE to mute it. When you release CHOKE (while FREEZE is still held), you hear the frozen audio. Reversing the order would mean CHOKE blocks audio before FREEZE can capture it, making simultaneous use less useful.

### Q: Can I have different freeze durations for different songs/performances?

**A:** Not at runtime (that would require dynamic buffer allocation or parameter control). But you can easily recompile with different `FREEZE_BUFFER_MS` values for different sets/performances. Future enhancement could add runtime control via MIDI CC or encoder.

---

## Appendices

### Appendix A: Circular Buffer Pseudocode

```cpp
// Passthrough mode (recording to buffer)
void update_passthrough() {
    audio_block_t* inL = receiveReadOnly(0);
    audio_block_t* inR = receiveReadOnly(1);

    // Write to circular buffer
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        m_freezeBufferL[m_writePos] = inL->data[i];
        m_freezeBufferR[m_writePos] = inR->data[i];

        m_writePos = (m_writePos + 1) % FREEZE_BUFFER_SAMPLES;  // Wrap around
    }

    // Pass through unmodified
    transmit(inL, 0);
    transmit(inR, 1);
}

// Frozen mode (playback from buffer)
void update_frozen() {
    audio_block_t* outL = allocate();
    audio_block_t* outR = allocate();

    // Read from circular buffer
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        outL->data[i] = m_freezeBufferL[m_readPos];
        outR->data[i] = m_freezeBufferR[m_readPos];

        m_readPos = (m_readPos + 1) % FREEZE_BUFFER_SAMPLES;  // Wrap around
    }

    transmit(outL, 0);
    transmit(outR, 1);
    release(outL);
    release(outR);
}
```

### Appendix B: Memory Layout Diagram

```
Teensy 4.1 RAM Layout (1 MB total)

┌─────────────────────────────────────────────────────────────┐
│ Code + Libraries (~70 KB)                                    │
├─────────────────────────────────────────────────────────────┤
│ Audio Block Pool (~50 KB) - Teensy Audio Library            │
├─────────────────────────────────────────────────────────────┤
│ SPSC Queues (~5 KB)                                          │
│  - Clock queue: 256 slots × 4 bytes = 1 KB                  │
│  - Event queue: 32 slots × 4 bytes = 128 bytes              │
│  - Command queue: 32 slots × 8 bytes = 256 bytes            │
│  - Display queue: 16 slots × 4 bytes = 64 bytes             │
├─────────────────────────────────────────────────────────────┤
│ Thread Stacks (~12 KB)                                       │
│  - MIDI I/O: 2 KB                                            │
│  - Input I/O: 2 KB                                           │
│  - Display I/O: 2 KB                                         │
│  - App Logic: 3 KB                                           │
│  - Main thread: 3 KB                                         │
├─────────────────────────────────────────────────────────────┤
│ Effect State (~5 KB)                                         │
│  - AudioEffectChoke: ~500 bytes                              │
│  - TimeKeeper: ~100 bytes                                    │
│  - Trace buffer: 1024 × 8 bytes = 8 KB (optional)           │
├─────────────────────────────────────────────────────────────┤
│ AudioEffectFreeze (~8.8 KB @ 50ms buffer)                   │  <- NEW
│  - freezeBufferL: 2205 samples × 2 bytes = 4410 bytes       │
│  - freezeBufferR: 2205 samples × 2 bytes = 4410 bytes       │
│  - State variables: ~20 bytes                                │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│ Free RAM (~840 KB)                                           │
│ - Available for future loop recording, sample playback, etc.│
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

### Appendix C: CPU Cycle Breakdown

```
Audio ISR Budget (per block @ 44.1kHz, 128 samples):
- Block period: 128 samples ÷ 44100 Hz = 2.9ms
- CPU cycles available: 2.9ms × 600 MHz = 1,740,000 cycles

Current Usage with FREEZE:
┌────────────────────────────────────────────┬──────────────┬──────────┐
│ Component                                  │ Cycles       │ % of CPU │
├────────────────────────────────────────────┼──────────────┼──────────┤
│ AudioInputI2S (DMA, minimal ISR work)      │ ~100         │ 0.006%   │
│ AudioTimeKeeper (sample position tracking) │ ~20          │ 0.001%   │
│ AudioEffectFreeze (passthrough)            │ ~300         │ 0.017%   │
│ AudioEffectFreeze (frozen playback)        │ ~300         │ 0.017%   │
│ AudioEffectChoke (passthrough)             │ ~50          │ 0.003%   │
│ AudioEffectChoke (fading)                  │ ~2000        │ 0.115%   │
│ AudioOutputI2S (DMA, minimal ISR work)     │ ~100         │ 0.006%   │
├────────────────────────────────────────────┼──────────────┼──────────┤
│ Total (worst case: both effects active)    │ ~2870        │ 0.165%   │
├────────────────────────────────────────────┼──────────────┼──────────┤
│ Available headroom                         │ ~1,737,130   │ 99.835%  │
└────────────────────────────────────────────┴──────────────┴──────────┘

Plenty of headroom for future effects!
```

### Appendix D: Freeze Buffer Size Calculator

```cpp
// Helper macro for calculating buffer sizes
#define FREEZE_MS_TO_SAMPLES(ms) (((ms) * 44100) / 1000)
#define FREEZE_STEREO_RAM_KB(ms) ((FREEZE_MS_TO_SAMPLES(ms) * 2 * 2) / 1024.0)

// Examples:
// 3ms:   132 samples, 0.52 KB RAM
// 10ms:  441 samples, 1.72 KB RAM
// 25ms:  1102 samples, 4.30 KB RAM
// 50ms:  2205 samples, 8.61 KB RAM
// 100ms: 4410 samples, 17.23 KB RAM
// 200ms: 8820 samples, 34.45 KB RAM
```

---

## Conclusion

**Approach 2 (Circular Buffer) with a 3ms default buffer** is the recommended implementation for Phase 4. It provides:

- ✅ The harsh "Windows bluescreen" freeze sound (at 3ms)
- ✅ Flexibility to tune buffer size (one constant change)
- ✅ Minimal RAM overhead (528 bytes at 3ms, scales as needed)
- ✅ Low CPU usage (~300 cycles/block, <0.03%)
- ✅ Simple implementation (~80 lines, circular buffer is well-understood)
- ✅ Future-proof design (upgrade path to longer freezes)
- ✅ Integrates cleanly with existing effect system architecture

This design balances immediate simplicity (start at 3ms) with long-term flexibility (scale up as desired), making it the ideal choice for Phase 4 of the effect system migration.

---

## References

- **EFFECT_SYSTEM_MIGRATION.md**: Phase 1-3 architecture (completed)
- **CHOKE_DESIGN.md**: Similar effect implementation pattern
- **CLAUDE.md**: Project overview, audio graph, thread model
- **Teensy Audio Library**: https://www.pjrc.com/teensy/td_libs_Audio.html
- **Windows Bluescreen Sound**: https://www.youtube.com/watch?v=Gb2jGy76v0Y (inspiration)

---

**Document Status:** Ready for implementation approval
**Next Step:** User approval → Begin Phase 4 implementation
