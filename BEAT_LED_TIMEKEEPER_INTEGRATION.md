# Beat LED TimeKeeper Integration

**Purpose:** Connect the beat LED (pin 31) to TimeKeeper's sample-accurate beat tracking instead of using a separate `tickCount` variable in App thread.

**Status:** Planning document - implementation scheduled for AFTER Phase 3 (removal of old ChokeIO system)

**Rationale:**
- LED will show the exact beat grid used for quantization (single source of truth)
- Sample-accurate timing from audio ISR (never lags like App thread can)
- Simplifies code (removes duplicate beat tracking in `app_logic.cpp`)
- Future-proof: BPM changes, time signature changes automatically reflected

---

## Current Implementation (Pre-Integration)

**Location:** `src/app_logic.cpp:46-312`

**Mechanism:**
```cpp
static uint8_t tickCount = 0;  // 0-23 (MIDI clock ticks)
static uint32_t avgTickPeriodUs = 20833;  // EMA-smoothed tick period

// In MIDI clock processing loop:
tickCount++;
if (tickCount >= 24) {
    tickCount = 0;
    digitalWrite(LED_PIN, HIGH);  // Turn on at beat start
}
if (tickCount == 2) {
    digitalWrite(LED_PIN, LOW);  // Turn off after 2 ticks (~40ms @ 120 BPM)
}
```

**Characteristics:**
- ✅ Simple and direct
- ✅ Uses EMA-smoothed MIDI timing
- ❌ Runs in App thread (can lag if thread delayed)
- ❌ Separate from TimeKeeper (not the same beat grid used for quantization)
- ❌ Can drift if Serial.println() or I2C calls delay App thread

**Visual behavior:**
- Short pulse (2 ticks = ~40ms @ 120 BPM)
- Turns ON at tick 0 (beat start)
- Turns OFF at tick 2

---

## Option 1: Polling from App Thread (RECOMMENDED)

**Complexity:** Low
**Accuracy:** High (shows exact TimeKeeper beat grid)
**Real-time safety:** Safe (all reads from App thread)
**Latency:** 0-2ms (depends on App thread polling rate)

### Implementation

**Changes to `src/app_logic.cpp`:**

```cpp
// Remove old beat tracking state (lines 46-54):
// DELETE: static uint8_t tickCount = 0;
// DELETE: static uint32_t beatStartMicros = 0;
// DELETE: static uint32_t lastTickMicros = 0;
// DELETE: static uint32_t avgTickPeriodUs = 20833;

// Add new TimeKeeper-based state:
static uint32_t lastBeatNumber = 0;  // Track beat changes

// In threadLoop(), AFTER draining clock ticks (section 3):
void AppLogic::threadLoop() {
    for (;;) {
        // ... [existing choke/command/transport/clock processing] ...

        // ========== NEW: TimeKeeper-based Beat LED ==========
        /**
         * BEAT LED DRIVEN BY TIMEKEEPER
         *
         * APPROACH:
         * - Poll TimeKeeper::getBeatNumber() every loop iteration
         * - Detect beat boundary when beat number changes
         * - Turn LED on immediately, turn off after 2 ticks
         *
         * BENEFITS:
         * - Shows exact beat grid used for quantization
         * - Sample-accurate (TimeKeeper runs in audio ISR)
         * - No duplicate beat tracking logic
         *
         * TIMING:
         * - App thread polls every 2ms
         * - Beat changes detected within 0-2ms of actual beat
         * - LED latency: imperceptible (<3ms)
         */
        uint32_t currentBeat = TimeKeeper::getBeatNumber();

        // Detect beat boundary crossing
        if (currentBeat != lastBeatNumber) {
            // Beat advanced! Turn on LED
            digitalWrite(LED_PIN, HIGH);
            lastBeatNumber = currentBeat;
            TRACE(TRACE_BEAT_LED_ON);
        }

        // Turn off LED after 2 ticks (~40ms @ 120 BPM)
        // Uses TimeKeeper's tick counter (0-23)
        uint8_t tickInBeat = TimeKeeper::getTickInBeat();
        if (tickInBeat == 2) {
            digitalWrite(LED_PIN, LOW);
            TRACE(TRACE_BEAT_LED_OFF);
        }

        // ... [existing periodic debug output, yield] ...
    }
}
```

**Changes to MIDI clock processing (section 3):**

```cpp
// In clock tick drain loop (lines 248-313):
// SIMPLIFY: Remove LED control from here
// DELETE lines 298-303 (beat boundary LED on)
// DELETE lines 309-312 (tick 2 LED off)

// Keep only:
uint32_t clockMicros;
while (MidiIO::popClock(clockMicros)) {
    if (!transportActive) {
        continue;
    }

    // Update tick period estimate (EMA)
    if (lastTickMicros > 0) {
        uint32_t tickPeriod = clockMicros - lastTickMicros;
        if (tickPeriod >= 10000 && tickPeriod <= 50000) {
            avgTickPeriodUs = (avgTickPeriodUs * 9 + tickPeriod) / 10;
            TimeKeeper::syncToMIDIClock(avgTickPeriodUs);
        }
    }
    lastTickMicros = clockMicros;

    // Increment TimeKeeper (it tracks ticks internally)
    TimeKeeper::incrementTick();
}
```

**Changes to transport control:**

```cpp
case MidiEvent::START:
    transportActive = true;
    TimeKeeper::reset();
    TimeKeeper::setTransportState(TimeKeeper::TransportState::PLAYING);

    // LED will turn on automatically when beat advances
    // No need to digitalWrite(LED_PIN, HIGH) here

    TRACE(TRACE_MIDI_START);
    Serial.println("▶ START");
    break;

case MidiEvent::STOP:
    transportActive = false;
    TimeKeeper::setTransportState(TimeKeeper::TransportState::STOPPED);

    // Turn off LED immediately (visual feedback)
    digitalWrite(LED_PIN, LOW);

    TRACE(TRACE_MIDI_STOP);
    Serial.println("■ STOP");
    break;

case MidiEvent::CONTINUE:
    transportActive = true;
    TimeKeeper::setTransportState(TimeKeeper::TransportState::PLAYING);

    // LED state will be set by beat polling (based on current tick)
    // If we're in first half of beat (tick 0-1), it will turn on soon
    // If we're in second half (tick 2-23), it will stay off until next beat

    TRACE(TRACE_MIDI_CONTINUE);
    Serial.println("▶ CONTINUE");
    break;
```

### Pros:
- ✅ **Simple**: No new cross-thread primitives needed
- ✅ **Safe**: All TimeKeeper reads from App thread (no ISR conflicts)
- ✅ **Accurate**: Shows exact TimeKeeper beat grid
- ✅ **Low latency**: 0-2ms detection lag (imperceptible)
- ✅ **Clean code**: Removes duplicate beat tracking

### Cons:
- ⚠️ **Polling overhead**: Reads TimeKeeper every 2ms (negligible, but not zero)
- ⚠️ **Theoretical lag**: If App thread stalls >40ms, LED might miss a beat pulse entirely (very unlikely)

### When to use:
- **Recommended for Phase 4** (after old ChokeIO removed, before adding new effects)
- Simple, safe, and sufficient for 99% of use cases
- Only upgrade to Option 2 if you observe missed LED beats under heavy load

---

## Option 2: Atomic Beat Flag (MOST ACCURATE)

**Complexity:** Medium
**Accuracy:** Perfect (zero missed beats, even under heavy App thread load)
**Real-time safety:** Safe (atomic operations)
**Latency:** <100µs (set in audio ISR, cleared in App thread)

### Implementation

**Changes to `utils/timekeeper.h`:**

```cpp
// Add to public API (after line 150):
/**
 * Check and clear beat boundary flag (for external beat indicators)
 *
 * Thread-safe: Can be called from any thread
 *
 * @return true if beat boundary crossed since last check
 *
 * USAGE:
 * This flag is set by audio ISR when beat advances, and cleared
 * by consumer (e.g., App thread for LED control).
 *
 * Example:
 *   if (TimeKeeper::pollBeatFlag()) {
 *       digitalWrite(LED_PIN, HIGH);  // Turn on LED
 *   }
 */
static bool pollBeatFlag();

// Add to private members (after line 250):
static std::atomic<bool> s_beatFlag;  // Set by ISR, cleared by consumer
```

**Changes to `utils/timekeeper.cpp`:**

```cpp
// Add static member initialization (after line 11):
std::atomic<bool> TimeKeeper::s_beatFlag{false};

// Add function implementation (after incrementTick, line 90):
bool TimeKeeper::pollBeatFlag() {
    // Test-and-set: Return true if flag was set, then clear it
    return s_beatFlag.exchange(false, std::memory_order_acq_rel);
}

// Modify incrementTick() to set flag (line 73):
void TimeKeeper::incrementTick() {
    s_currentTick++;

    if (s_currentTick >= TICKS_PER_BEAT) {
        // Beat boundary crossed!
        s_currentTick = 0;
        s_currentBeat++;

        // Set beat flag for external consumers (e.g., beat LED)
        s_beatFlag.store(true, std::memory_order_release);

        TRACE(TRACE_BEAT_START, s_currentBeat);
    }
}
```

**Changes to `src/app_logic.cpp`:**

```cpp
// Remove old beat tracking state (lines 46-54):
// DELETE: static uint8_t tickCount = 0;
// DELETE: static uint32_t beatStartMicros = 0;
// DELETE: static uint32_t lastTickMicros = 0;
// DELETE: static uint32_t avgTickPeriodUs = 20833;

// In threadLoop(), AFTER draining clock ticks:
void AppLogic::threadLoop() {
    for (;;) {
        // ... [existing choke/command/transport/clock processing] ...

        // ========== NEW: TimeKeeper Beat Flag LED ==========
        /**
         * BEAT LED DRIVEN BY TIMEKEEPER BEAT FLAG
         *
         * APPROACH:
         * - Audio ISR sets atomic flag when beat advances
         * - App thread polls flag, clears if set
         * - Zero missed beats (flag stays set until consumed)
         *
         * BENEFITS:
         * - Perfect accuracy (never misses a beat)
         * - Shows exact beat grid used for quantization
         * - Robust to App thread stalls
         *
         * TIMING:
         * - Flag set in audio ISR (~20-30 CPU cycles)
         * - Detected by App thread within 0-2ms
         * - LED latency: <3ms
         */
        if (TimeKeeper::pollBeatFlag()) {
            // Beat boundary crossed! Turn on LED
            digitalWrite(LED_PIN, HIGH);
            TRACE(TRACE_BEAT_LED_ON);
        }

        // Turn off LED after 2 ticks (~40ms @ 120 BPM)
        uint8_t tickInBeat = TimeKeeper::getTickInBeat();
        if (tickInBeat == 2) {
            digitalWrite(LED_PIN, LOW);
            TRACE(TRACE_BEAT_LED_OFF);
        }

        // ... [existing periodic debug output, yield] ...
    }
}
```

**Changes to MIDI clock processing:**
- Same as Option 1 (remove LED control from clock drain loop)

**Changes to transport control:**
- Same as Option 1 (LED controlled by beat flag polling)

### Pros:
- ✅ **Perfect accuracy**: Never misses a beat, even if App thread stalls
- ✅ **Minimal ISR overhead**: Single atomic store (~20-30 cycles)
- ✅ **Lock-free**: No blocking, no priority inversion
- ✅ **Robust**: Works under heavy load (Serial.println spam, I2C delays, etc.)

### Cons:
- ⚠️ **Slightly more complex**: Adds atomic flag to TimeKeeper
- ⚠️ **Memory**: +1 byte for flag (negligible)

### When to use:
- If Option 1 shows missed LED beats under load
- If you want absolute guarantee of beat accuracy
- If you plan to add other beat-triggered features (display updates, etc.)

---

## Option 3: Callback (MOST FLEXIBLE)

**Complexity:** High
**Accuracy:** Perfect (immediate notification)
**Real-time safety:** Depends on callback implementation
**Latency:** ~0µs (called directly from audio ISR)

### Implementation

**Changes to `utils/timekeeper.h`:**

```cpp
// Add callback type (after includes):
/**
 * Beat callback function type
 *
 * Called from audio ISR when beat boundary crossed
 *
 * CONSTRAINTS:
 * - Must be ISR-safe (no Serial.print, malloc, etc.)
 * - Must be FAST (<100 CPU cycles recommended)
 * - Should only set flags or atomics
 *
 * @param beatNumber Current beat number (0, 1, 2, ...)
 */
using BeatCallback = void (*)(uint32_t beatNumber);

// Add to public API (after line 150):
/**
 * Register callback to be invoked on beat boundaries
 *
 * Thread-safe: Can be called from any thread
 *
 * @param callback Function to call on beat (nullptr to unregister)
 *
 * IMPORTANT:
 * - Callback invoked from audio ISR (must be ISR-safe!)
 * - Keep callback FAST (<100 cycles)
 * - Typically used to set flags for App thread
 *
 * Example:
 *   void onBeat(uint32_t beat) {
 *       s_beatFlag.store(true);  // ISR-safe
 *   }
 *   TimeKeeper::registerBeatCallback(onBeat);
 */
static void registerBeatCallback(BeatCallback callback);

// Add to private members (after line 250):
static BeatCallback s_beatCallback;  // Optional callback on beat
```

**Changes to `utils/timekeeper.cpp`:**

```cpp
// Add static member initialization (after line 11):
TimeKeeper::BeatCallback TimeKeeper::s_beatCallback = nullptr;

// Add function implementation (after incrementTick, line 90):
void TimeKeeper::registerBeatCallback(BeatCallback callback) {
    // Simple assignment (no locking needed, single writer)
    s_beatCallback = callback;
}

// Modify incrementTick() to invoke callback (line 73):
void TimeKeeper::incrementTick() {
    s_currentTick++;

    if (s_currentTick >= TICKS_PER_BEAT) {
        // Beat boundary crossed!
        s_currentTick = 0;
        s_currentBeat++;

        TRACE(TRACE_BEAT_START, s_currentBeat);

        // Invoke callback if registered
        if (s_beatCallback != nullptr) {
            s_beatCallback(s_currentBeat);
        }
    }
}
```

**Changes to `src/app_logic.cpp`:**

```cpp
// Add beat flag at file scope (after includes):
static std::atomic<bool> s_beatLEDFlag{false};

// Add beat callback function (before AppLogic::begin):
/**
 * Beat callback invoked from audio ISR
 *
 * ISR-SAFE: Only sets atomic flag (no Serial.print, no blocking)
 */
void onBeatBoundary(uint32_t beatNumber) {
    // Set flag for App thread to detect
    s_beatLEDFlag.store(true, std::memory_order_release);

    // Could add trace here (Trace is ISR-safe)
    TRACE(TRACE_BEAT_START, beatNumber);
}

// In AppLogic::begin() (after line 68):
void AppLogic::begin() {
    // ... [existing LED setup] ...

    // Register beat callback with TimeKeeper
    TimeKeeper::registerBeatCallback(onBeatBoundary);
}

// In threadLoop():
void AppLogic::threadLoop() {
    for (;;) {
        // ... [existing choke/command/transport/clock processing] ...

        // ========== NEW: Callback-driven Beat LED ==========
        /**
         * BEAT LED DRIVEN BY TIMEKEEPER CALLBACK
         *
         * APPROACH:
         * - TimeKeeper calls onBeatBoundary() from audio ISR
         * - Callback sets atomic flag
         * - App thread polls flag, turns on LED
         *
         * BENEFITS:
         * - Immediate notification (callback runs in ISR)
         * - Flexible (can trigger multiple actions)
         * - Extensible (other features can register callbacks)
         *
         * TIMING:
         * - Callback invoked in audio ISR (zero delay)
         * - LED turned on within 0-2ms (App thread polling)
         */
        if (s_beatLEDFlag.exchange(false)) {
            // Beat boundary crossed! Turn on LED
            digitalWrite(LED_PIN, HIGH);
            TRACE(TRACE_BEAT_LED_ON);
        }

        // Turn off LED after 2 ticks (~40ms @ 120 BPM)
        uint8_t tickInBeat = TimeKeeper::getTickInBeat();
        if (tickInBeat == 2) {
            digitalWrite(LED_PIN, LOW);
            TRACE(TRACE_BEAT_LED_OFF);
        }

        // ... [existing periodic debug output, yield] ...
    }
}
```

**Changes to MIDI clock processing:**
- Same as Option 1 (remove LED control from clock drain loop)

**Changes to transport control:**
- Same as Option 1 (LED controlled by callback flag)

### Pros:
- ✅ **Most flexible**: Easy to add multiple beat-triggered actions
- ✅ **Immediate notification**: Callback runs in ISR (zero delay)
- ✅ **Extensible**: Other modules can register their own callbacks (display, trace, etc.)
- ✅ **Perfect accuracy**: Never misses a beat

### Cons:
- ⚠️ **Complex**: Requires callback registration, ISR-safe discipline
- ⚠️ **ISR overhead**: Function call + flag write (~50-100 cycles)
- ⚠️ **Footgun potential**: Easy to write non-ISR-safe callbacks (Serial.print, etc.)
- ⚠️ **Single callback**: Current design supports one callback (would need array for multiple)

### When to use:
- If you want to trigger multiple actions on beat (LED + display + trace + ...)
- If you plan to build a full event system with subscribers
- If you want absolute minimum latency (callback runs immediately)

**Warning:** This is overkill for just the LED. Only use if you have multiple beat-synchronized features.

### When ARE Callbacks Useful?

Callbacks (Option 3) are useful when you have **multiple non-audio actions** that need to happen on beat boundaries.

**Good use cases for callbacks:**
- ✅ **Update OLED display** - Show current beat number, bar number, or beat animation
- ✅ **Flash multiple LEDs in sync** - Multiple visual indicators (beat LED, status LED, NeoPixels)
- ✅ **Send MIDI clock out** - Forward beat timing to other devices
- ✅ **Log beat events** - Add beat markers to trace buffer for debugging
- ✅ **Update BPM display** - Refresh tempo readout on beat boundaries
- ✅ **Trigger visual animations** - Beat-synchronized UI effects

**All of these share common characteristics:**
- Run **outside audio ISR** (App thread, Display thread, etc.)
- **Tolerate latency** (0-2ms is imperceptible for visual feedback)
- Need to be **notified** reactively, not predict proactively
- Are **non-critical** (missing one beat won't break functionality)

**BAD use cases for callbacks (DON'T use callbacks for these):**

- ❌ **Loop playback** - Needs sample-accurate **prediction**, not reactive notification
- ❌ **Loop recording** - Needs sample-accurate **prediction** of beat boundaries
- ❌ **Audio effects that respond to beats** - Must run in audio ISR with predictive timing
- ❌ **Quantization** - Requires knowing future beat positions, not past events

**Why callbacks DON'T work for audio quantization:**

The fundamental problem: **Callbacks are reactive (tell you about the past), but audio DSP needs to be predictive (know the future).**

```cpp
// WRONG APPROACH - Don't do this for loop playback!
void onBeatBoundary(uint32_t beatNumber) {
    // Called from audio ISR, but AFTER beat has already started
    // We're already 0-128 samples into the beat!
    startLoopPlayback();  // TOO LATE - missed the exact beat boundary
}
```

By the time `incrementTick()` calls your callback, the audio ISR is already processing a block that may be 0-128 samples past the beat boundary. You can't rewind time.

**CORRECT approach for audio quantization:**

Use TimeKeeper's **predictive quantization API** directly in your audio effect:

```cpp
// In your loop audio effect (AudioEffectLoop::update())
void AudioEffectLoop::update() {
    // 1. User pressed "play loop" button (detected in App thread, queued to audio thread)
    if (m_pendingPlayback) {
        // 2. Ask TimeKeeper: "When is the NEXT beat?" (predictive, not reactive)
        uint32_t samplesToNextBeat = TimeKeeper::samplesToNextBeat();

        // 3. Calculate exact future sample to start playback
        m_playbackStartSample = TimeKeeper::getSamplePosition() + samplesToNextBeat;

        m_pendingPlayback = false;
        m_state = State::PENDING_START;
    }

    // 4. Check if we've reached the start sample (sample-accurate check)
    uint64_t currentSample = TimeKeeper::getSamplePosition();

    if (m_state == State::PENDING_START &&
        currentSample >= m_playbackStartSample &&
        currentSample < m_playbackStartSample + AUDIO_BLOCK_SAMPLES) {
        // We're in the block containing the beat boundary!
        // Calculate exact offset within this block (0-127)
        uint32_t offsetInBlock = m_playbackStartSample - currentSample;

        // 5. Start mixing loop samples at exact offset (sample-accurate!)
        for (uint32_t i = offsetInBlock; i < AUDIO_BLOCK_SAMPLES; i++) {
            outputBlock->data[i] += loopBuffer[loopPosition++];
        }

        m_state = State::PLAYING;
    }
}
```

**Key APIs for audio quantization (already available in TimeKeeper):**

```cpp
// Query future beat positions (predictive)
static uint32_t samplesToNextBeat();      // "When should I start?" (beat quantize)
static uint32_t samplesToNextBar();       // "When should I start?" (bar quantize)
static uint64_t beatToSample(uint32_t beat);  // "What sample is beat N at?"

// Query current position (for sample-accurate triggering)
static uint64_t getSamplePosition();      // "Where am I now?"
static bool isOnBeatBoundary();           // "Am I within ±64 samples of beat?"
static uint32_t getBeatNumber();          // "What beat am I on?"
```

**Summary:**
- **Callbacks (Option 3)**: For non-audio visual/logging features that tolerate latency
- **Direct TimeKeeper queries**: For sample-accurate audio DSP that needs predictive timing
- **Rule of thumb**: If it runs in audio ISR and affects sound, query TimeKeeper directly. If it runs in App/Display thread and is just visual feedback, callbacks might help (but polling is often simpler).

---

## Comparison Matrix

| Feature | Option 1 (Polling) | Option 2 (Atomic Flag) | Option 3 (Callback) |
|---------|-------------------|----------------------|-------------------|
| **Complexity** | Low | Medium | High |
| **Lines of code** | ~30 | ~50 | ~80 |
| **ISR overhead** | 0 cycles | ~30 cycles | ~50-100 cycles |
| **App thread overhead** | 2 reads/2ms | 1 atomic/2ms | 1 atomic/2ms |
| **Missed beats** | Possible if stall >40ms | Never | Never |
| **Extensibility** | Low (LED only) | Medium (flag per feature) | High (callback per feature) |
| **ISR safety concerns** | None (no ISR code) | None (atomic only) | High (callback must be ISR-safe) |
| **Recommended for** | Simple LED control | Robust LED control | Multiple beat features |

---

## Recommendation

**Use Option 1 (Polling) for Phase 4 implementation.**

**Rationale:**
1. Simplest to implement and maintain
2. Sufficient accuracy for visual beat indicator (0-2ms lag is imperceptible)
3. No ISR modifications needed (reduces risk)
4. Easy to understand and debug
5. Can upgrade to Option 2 later if needed

**When to upgrade:**
- If you observe missed LED beats under heavy load (Serial spam, I2C delays)
- If you add other beat-synchronized features (display updates, BPM display, etc.)
- Option 2 is straightforward upgrade from Option 1

**Never use Option 3 unless:**
- You're building a full event system with multiple beat subscribers
- You need absolute minimum latency (<100µs)
- You have ISR-safety discipline in place

---

## Testing Checklist (Post-Implementation)

After implementing chosen option:

### Functional Tests:
- [ ] LED blinks on beat when MIDI clock running
- [ ] LED turns off after ~40ms (2 ticks)
- [ ] LED stops on MIDI STOP
- [ ] LED resumes on MIDI START/CONTINUE
- [ ] LED syncs with Digitakt metronome click (visual verification)

### Stress Tests:
- [ ] Spam Serial.println() from App thread (LED should not miss beats)
- [ ] Run I2C transactions (button presses) during beats
- [ ] Start/stop transport rapidly (LED should track cleanly)
- [ ] Change BPM on Digitakt (LED should adapt smoothly via EMA)

### Trace Verification:
- [ ] Type `c` to clear trace
- [ ] Run for 10 seconds
- [ ] Type `t` to dump trace
- [ ] Verify `TRACE_BEAT_LED_ON` and `TRACE_BEAT_LED_OFF` events are regular
- [ ] Check no missed beats or irregular timing

### Integration Tests:
- [ ] Type `s` in serial monitor - verify BPM matches Digitakt setting
- [ ] Count LED blinks for 10 seconds - should match expected beats (20 blinks @ 120 BPM)
- [ ] Visual sync: LED should align with Digitakt's metronome click

### Regression Tests:
- [ ] Choke button still works (no interference)
- [ ] Display updates still work (no I2C conflicts)
- [ ] Audio passthrough clean (no glitches or clicks)
- [ ] TimeKeeper status (`s` command) shows correct values

---

## Implementation Schedule

**Phase 3:** Remove old ChokeIO system (validates dual system, simplifies codebase)

**Phase 4:** Implement Option 1 (Beat LED TimeKeeper integration)
- Estimated time: 30 minutes
- Risk: Low (no ISR changes, simple polling)
- Testing: 15 minutes (functional + stress tests)

**Phase 5:** Remove old button mapping system (complete new system)

**Future (Optional):** Upgrade to Option 2 if needed
- Only if stress tests show missed beats
- Estimated time: 45 minutes
- Risk: Low (atomic operations are well-tested)

---

## References

**Related Files:**
- `src/app_logic.cpp:46-312` - Current beat LED implementation
- `utils/timekeeper.h` - TimeKeeper API
- `utils/timekeeper.cpp` - TimeKeeper implementation
- `include/audio_timekeeper.h` - Audio integration

**Related Documentation:**
- `EFFECT_SYSTEM_MIGRATION.md` - Overall refactoring plan
- `utils/TIMEKEEPER_USAGE.md` - TimeKeeper usage guide
- `CLAUDE.md` - Project architecture overview

**Design Principles:**
- Single source of truth (TimeKeeper for all timing)
- Real-time safety (lock-free, bounded execution)
- Simplicity first (Option 1), complexity only if needed
- Test-driven validation (comprehensive test suite)
