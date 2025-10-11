# Effect System Migration Plan

## Document Purpose

This document outlines the migration from a single-effect system (Choke) to a modular, extensible multi-effect architecture. It serves as the design rationale and implementation roadmap for the refactoring effort.

**Last Updated:** 2025-10-11
**Status:** Planning Phase

---

## Executive Summary

### Current Architecture (Single Effect)

```
ChokeIO Thread → ChokeEvent enum → SPSC Queue → AppLogic switch → choke.engage()
                                                                  ↓
                                                            AudioEffectChoke (audio ISR)
```

**Limitations:**
- ❌ Tight coupling: ChokeIO → ChokeEvent → specific choke API
- ❌ Non-composable: Can't combine effects (Choke + Delay + Reverb)
- ❌ Hardcoded logic: `switch(chokeEvent)` in app_logic.cpp
- ❌ No abstraction: Each new effect needs new I/O handler, event enum, switch cases
- ❌ Static audio graph: Effects hardwired at compile time

### Target Architecture (Multi-Effect)

```
InputIO Thread → Command (POD) → SPSC Queue → AppLogic → EffectManager → AudioEffectBase
                                                                          ↓
                                                              [Choke, Delay, Reverb, ...]
```

**Benefits:**
- ✅ Loose coupling: Input → generic Command → polymorphic Effect
- ✅ Composable: Multiple effects via common interface
- ✅ Data-driven: Button mappings in table, no hardcoded switches
- ✅ Extensible: Add effects without touching input/app logic
- ✅ Testable: Commands are data, easy to simulate/mock

---

## Core Design Principles

### 1. **Real-Time Safety (Non-Negotiable)**

All existing real-time guarantees must be preserved:

- ✅ **Lock-free communication**: Continue using SPSC queues for inter-thread data
- ✅ **POD types in queues**: Command struct must be trivially copyable (no pointers to heap)
- ✅ **Atomic operations**: Effect state updates use `std::atomic<bool>`
- ✅ **No allocations**: Static registries, fixed-size arrays (no `new`/`malloc` in threads)
- ✅ **Bounded execution time**: All code paths in audio ISR and I/O threads have deterministic timing

**Why?** Audio glitches are unacceptable in live performance. Any regression in latency or reliability would make the system unusable.

### 2. **Zero Runtime Overhead in Audio Path**

The audio ISR is the most critical code path:

- ✅ **No virtual calls in `update()`**: Each effect's audio processing is direct (not through vtable)
- ✅ **Minimal branching**: Effect enable/disable uses atomic loads, not function calls
- ✅ **Cache-friendly**: Command struct is 8 bytes (fits in single cache line)

**Why?** At 44.1kHz with 128-sample blocks, the audio ISR runs every 2.9ms. We have ~1.7 million CPU cycles per block at 600MHz, but we want to leave plenty of headroom for future features (loop recording, sample playback). Virtual calls are acceptable in the App thread (~10-20 cycles) but forbidden in audio ISR.

### 3. **Phased Migration (No Big Bang)**

Each phase must:

- ✅ **Compile and run**: No broken intermediate states
- ✅ **Be testable**: Validate that existing functionality still works
- ✅ **Be reversible**: Can roll back if issues found
- ✅ **Add value incrementally**: Each phase delivers something useful

**Why?** A "rewrite everything" approach is risky on embedded systems. If the device stops working mid-refactor, we lose development momentum. Incremental changes allow testing on hardware after each step.

### 4. **Static Configuration (Not Dynamic)**

We will NOT implement:

- ❌ **Dynamic audio graph**: Effects cannot be reordered at runtime
- ❌ **Effect presets/scenes**: No saved configurations (yet)
- ❌ **Parameter automation**: No MIDI CC control (yet)
- ❌ **Undo/redo**: Not needed for live performance

**Why?** These features add complexity without immediate value. The Teensy Audio Library uses static `AudioConnection` objects created at compile time. Making this dynamic would require:
- Custom memory management (audio blocks are pooled)
- Complex graph algorithms (topological sort, cycle detection)
- Significant RAM overhead (connection metadata)

For 2-3 effects in a fixed chain, static configuration is sufficient and much simpler.

### 5. **Backward Compatibility During Migration**

During the migration:

- ✅ **Keep legacy APIs**: `choke.engage()` / `choke.releaseChoke()` remain callable
- ✅ **Dual I/O handlers**: Both `ChokeIO` and `InputIO` can coexist temporarily
- ✅ **Incremental cutover**: Move one effect at a time to new system

**Why?** If we break the choke feature mid-migration, we lose the ability to test on hardware. Keeping both systems running in parallel allows gradual validation.

---

## Architectural Components

### 1. Command System (Decouples Input from DSP)

#### Command Struct (POD)

```cpp
// utils/command.h

enum class CommandType : uint8_t {
    NONE = 0,
    EFFECT_TOGGLE = 1,    // Toggle effect on/off
    EFFECT_ENABLE = 2,    // Force enable
    EFFECT_DISABLE = 3,   // Force disable
    EFFECT_SET_PARAM = 4, // Set effect parameter (future)
    // Reserved for future: TRANSPORT_*, LOOP_*, SAMPLE_*
};

enum class EffectID : uint8_t {
    NONE = 0,
    CHOKE = 1,
    DELAY = 2,     // Future
    REVERB = 3,    // Future
    // Add more as needed (max 255)
};

struct Command {
    CommandType type;      // What action to perform
    EffectID targetEffect; // Which effect to control
    uint8_t param1;        // Generic parameter slot 1
    uint8_t param2;        // Generic parameter slot 2
    uint32_t value;        // Generic value (e.g., delay time in samples)

    // Size: 8 bytes (compact, cache-friendly)
    // POD: Safe for lock-free queues
};
```

**Design Rationale:**

- **Why enum class?** Type safety (can't accidentally mix CommandType and EffectID)
- **Why 8 bytes?** Fits in single cache line, powers-of-2 align well
- **Why generic param slots?** Flexible without dynamic allocation
  - Example: Delay time = `Command{EFFECT_SET_PARAM, DELAY, 0, 0, 22050}` (22050 samples = 0.5s @ 44.1kHz)
  - Example: Reverb mix = `Command{EFFECT_SET_PARAM, REVERB, 1, 0, 50}` (param1=1 means "mix", value=50%)

**Alternatives Considered:**
- ❌ **Union for type-specific fields**: More complex, harder to debug, not worth the space savings
- ❌ **Separate queue per command type**: More queues = more RAM, more complexity
- ❌ **Function pointers in command**: Not POD, can't use in lock-free queue

### 2. Effect Interface (Polymorphic Control API)

#### AudioEffectBase (Abstract Base Class)

```cpp
// include/audio_effect_base.h

class AudioEffectBase : public AudioStream {
public:
    // Control interface (called from App thread)
    virtual void enable() = 0;
    virtual void disable() = 0;
    virtual void toggle() = 0;
    virtual bool isEnabled() const = 0;
    virtual const char* getName() const = 0;

    // Parameter control (optional, default = no-op)
    virtual void setParameter(uint8_t paramIndex, float value) {}
    virtual float getParameter(uint8_t paramIndex) const { return 0.0f; }

    // Audio processing (NOT virtual - each effect implements directly)
    // virtual void update() override { ... }
};
```

**Design Rationale:**

- **Why virtual methods?** Allows EffectManager to control any effect via base pointer
- **Why not virtual `update()`?** Audio ISR performance is critical, no vtable overhead
- **Why abstract class (not interface)?** Inherits from `AudioStream`, can be in audio graph
- **Why `getName()`?** Debugging, serial output, future UI display

**Virtual Call Overhead:**
- Cost: ~10-20 CPU cycles (600MHz Cortex-M7)
- Context: Only called from App thread, not audio ISR
- Frequency: ~1-10 calls/second (button presses, state queries)
- Impact: Negligible (<0.0001% of CPU)

**Alternatives Considered:**
- ❌ **Function pointer table**: Slightly faster (~5 cycles), but more boilerplate and harder to extend
- ❌ **CRTP (Curiously Recurring Template Pattern)**: Zero overhead, but template explosion and harder to use in registry
- ❌ **Switch on effect type**: Back to hardcoded logic, defeats purpose of refactor

### 3. Effect Manager (Registry + Dispatcher)

#### EffectManager (Static Class)

```cpp
// include/effect_manager.h

class EffectManager {
public:
    // Register effects at startup
    static bool registerEffect(EffectID id, AudioEffectBase* effect);

    // Execute commands (routes to correct effect)
    static bool executeCommand(const Command& cmd);

    // Query interface (for UI, debugging)
    static AudioEffectBase* getEffect(EffectID id);
    static uint32_t getEnabledEffectsMask();
    static const char* getEffectName(EffectID id);

private:
    static constexpr uint8_t MAX_EFFECTS = 8;

    struct EffectEntry {
        EffectID id;
        AudioEffectBase* effect;  // Non-owning pointer
    };

    static EffectEntry s_effects[MAX_EFFECTS];  // Static array, no heap
    static uint8_t s_numEffects;
};
```

**Design Rationale:**

- **Why static class?** Single global registry, no need for instances
- **Why non-owning pointers?** Effects are created in `main.cpp` (global scope), live forever
- **Why fixed-size array?** No dynamic allocation, bounded memory usage
- **Why MAX_EFFECTS = 8?** Reasonable limit (8 × 4 bytes = 32 bytes RAM), unlikely to hit
- **Why linear search?** With 8 effects max, linear is faster than hash table (cache-friendly)

**Thread Safety:**
- Registry is populated in `setup()` (before threads start) → no race conditions
- Lookups are read-only after startup → no locking needed
- Effect state updates use atomics → thread-safe

**Alternatives Considered:**
- ❌ **Hash table**: Overkill for 8 effects, wastes RAM on empty buckets
- ❌ **Map/vector**: Requires heap allocation, not real-time safe
- ❌ **Singleton pattern**: Overly complex for static registry

### 4. Input Abstraction (Generic Command Emitter)

#### InputIO (Replaces ChokeIO)

```cpp
// include/input_io.h

namespace InputIO {
    bool begin();                          // Initialize hardware
    void threadLoop();                     // I/O thread (polls hardware, emits commands)
    bool popCommand(Command& outCmd);      // Consumer API (App thread)
    void setLED(EffectID effectID, bool enabled);  // Visual feedback
}
```

**Button Mapping Table:**

```cpp
// src/input_io.cpp

struct ButtonMapping {
    uint8_t keyIndex;          // Which physical key (0-3 on Neokey)
    Command pressCommand;      // Command to emit on press
    Command releaseCommand;    // Command to emit on release
};

static const ButtonMapping buttonMappings[] = {
    // Key 0: Choke (momentary - press to mute, release to unmute)
    {
        .keyIndex = 0,
        .pressCommand = Command{EFFECT_ENABLE, CHOKE},
        .releaseCommand = Command{EFFECT_DISABLE, CHOKE}
    },

    // Key 1: Delay (toggle - press to toggle, ignore release)
    {
        .keyIndex = 1,
        .pressCommand = Command{EFFECT_TOGGLE, DELAY},
        .releaseCommand = Command{NONE, NONE}  // No-op
    },

    // Key 2: Reverb (toggle)
    {
        .keyIndex = 2,
        .pressCommand = Command{EFFECT_TOGGLE, REVERB},
        .releaseCommand = Command{NONE, NONE}
    },

    // Key 3: Reserved (future: loop record, sample trigger, etc.)
    {
        .keyIndex = 3,
        .pressCommand = Command{NONE, NONE},
        .releaseCommand = Command{NONE, NONE}
    },
};
```

**Design Rationale:**

- **Why table-driven?** Change button mappings without recompiling (just edit table)
- **Why separate press/release commands?** Supports both momentary (choke) and toggle (delay) behaviors
- **Why NONE command?** Explicit no-op, clearer than null/empty
- **Why keyIndex?** Maps to physical hardware, independent of effect assignment

**Key Combinations (Future):**

The table structure can be extended to support multi-key combos:

```cpp
struct ButtonMapping {
    uint8_t keyMask;           // Bitmask of keys (bit 0 = key 0, etc.)
    uint8_t triggerKey;        // Which key triggers the command
    Command command;
};

// Example: Hold Key 0 + Press Key 1 = some action
{ .keyMask = 0b0011, .triggerKey = 1, .command = ... }
```

**Note:** We won't implement this initially, but the architecture supports it.

### 5. Refactored App Logic (Command Processor)

#### AppLogic Changes

**Before (Hardcoded Switch):**

```cpp
// src/app_logic.cpp (current)
void AppLogic::threadLoop() {
    for (;;) {
        ChokeEvent chokeEvent;
        while (ChokeIO::popEvent(chokeEvent)) {
            switch (chokeEvent) {
                case ChokeEvent::BUTTON_PRESS:
                    choke.engage();
                    ChokeIO::setLED(true);
                    DisplayIO::showChoke();
                    Serial.println("🔇 Choke ENGAGED");
                    break;

                case ChokeEvent::BUTTON_RELEASE:
                    choke.releaseChoke();
                    ChokeIO::setLED(false);
                    DisplayIO::showDefault();
                    Serial.println("🔊 Choke RELEASED");
                    break;
            }
        }
        // ... (MIDI events, etc.)
    }
}
```

**After (Command Dispatch):**

```cpp
// src/app_logic.cpp (refactored)
void AppLogic::threadLoop() {
    for (;;) {
        Command cmd;
        while (InputIO::popCommand(cmd)) {
            // Execute command via EffectManager
            if (EffectManager::executeCommand(cmd)) {
                // Update visual feedback (LED, display)
                AudioEffectBase* effect = EffectManager::getEffect(cmd.targetEffect);
                if (effect) {
                    bool enabled = effect->isEnabled();
                    InputIO::setLED(cmd.targetEffect, enabled);

                    // Update display based on effect type
                    if (cmd.targetEffect == EffectID::CHOKE) {
                        DisplayIO::showChoke(enabled);
                    }

                    // Debug output
                    Serial.print(effect->getName());
                    Serial.println(enabled ? " ENABLED" : " DISABLED");
                }
            } else {
                // Command failed (effect not found, invalid params, etc.)
                Serial.print("ERROR: Command failed (type=");
                Serial.print(static_cast<uint8_t>(cmd.type));
                Serial.print(", effect=");
                Serial.print(static_cast<uint8_t>(cmd.targetEffect));
                Serial.println(")");
            }
        }
        // ... (MIDI events, etc.)
    }
}
```

**Design Rationale:**

- **Why generic dispatch?** No hardcoded effect names, scales to N effects
- **Why error logging?** Helps debug misconfigured button mappings
- **Why check effect pointer?** Defensive programming (should never be null, but safe)

**Lines of Code:**
- Before: ~20 lines per effect (switch cases, LED, display, serial)
- After: ~15 lines total (handles all effects)
- Savings: 5 lines per effect × N effects

---

## Migration Phases

### Phase 1: Add Command Infrastructure (Foundation)

**Goal:** Introduce new types and interfaces without breaking existing code.

**Deliverables:**

1. **`utils/command.h`**
   - `CommandType` enum
   - `EffectID` enum
   - `Command` struct (POD, 8 bytes)
   - Static assertions for POD-ness

2. **`include/audio_effect_base.h`**
   - Abstract base class
   - Pure virtual methods (enable, disable, toggle, isEnabled, getName)
   - Virtual parameter methods (default no-op implementations)

3. **`include/effect_manager.h`** + **`src/effect_manager.cpp`**
   - Registry (static array)
   - `registerEffect()`, `executeCommand()`, `getEffect()` methods
   - `getEnabledEffectsMask()`, `getEffectName()` query methods

4. **Update `AudioEffectChoke`** (backward compatible)
   - Inherit from `AudioEffectBase` (was: direct `AudioStream`)
   - Implement pure virtual methods
   - Keep legacy API: `engage()`, `releaseChoke()`, `isChoked()` (call new methods internally)
   - Audio processing unchanged

5. **Update `main.cpp`** setup
   - Register choke effect: `EffectManager::registerEffect(EffectID::CHOKE, &choke);`
   - Verify registration succeeds (serial output)

**Testing:**
- ✅ Compiles successfully
- ✅ Choke still works via existing `ChokeIO` → `AppLogic` flow
- ✅ Can call `EffectManager::getEffect(EffectID::CHOKE)` in serial command handler (add `e` command for testing)
- ✅ Serial command `e` prints: "Choke: enabled=true/false"

**Time Estimate:** 2-3 hours

**Rollback Plan:** Delete new files, revert `AudioEffectChoke` changes (git revert)

---

### Phase 2: Refactor Input Handling (Parallel Path)

**Goal:** Create `InputIO` subsystem that emits Commands, run in parallel with existing `ChokeIO`.

**Deliverables:**

1. **`include/input_io.h`** + **`src/input_io.cpp`**
   - Copy `ChokeIO` code as starting point
   - Change event type: `ChokeEvent` → `Command`
   - Add `buttonMappings[]` table (initially just Key 0 for choke)
   - Update `threadLoop()` to emit Commands
   - Keep LED control (accept `EffectID` parameter)

2. **Update `main.cpp`** thread creation
   - Create InputIO thread: `threads.addThread(inputThreadEntry, 2048);`
   - Keep ChokeIO thread running (for now)
   - Both threads coexist, both push to separate queues

3. **Update `AppLogic`** (dual processing)
   - Keep existing `ChokeIO::popEvent()` loop
   - Add new `InputIO::popCommand()` loop
   - New loop uses `EffectManager::executeCommand()`
   - Print which system handled the event (for debugging)

**Testing:**
- ✅ Compiles successfully
- ✅ Choke works via old system (`ChokeIO`)
- ✅ Choke works via new system (`InputIO`)
- ✅ No conflicts (both queues independent)
- ✅ Serial shows: "Old system: Choke ENGAGED" vs "New system: Choke ENABLED"

**Time Estimate:** 2-3 hours

**Rollback Plan:** Comment out InputIO thread, remove command loop from AppLogic

---

### Phase 3: Cutover to New System (Switch Flip)

**Goal:** Remove old `ChokeIO` system, use only `InputIO` + Commands.

**Deliverables:**

1. **Remove ChokeIO thread** from `main.cpp`
   - Delete `threads.addThread(chokeThreadEntry, ...)`
   - Keep `ChokeIO::begin()` temporarily (hardware init) or merge into `InputIO::begin()`

2. **Remove old event handling** from `AppLogic`
   - Delete `ChokeIO::popEvent()` loop
   - Delete `switch(chokeEvent)` cases
   - Keep only `InputIO::popCommand()` loop

3. **Deprecate `choke_io.h`** (mark for deletion)
   - Add comment: "DEPRECATED: Use input_io.h instead"
   - Keep file temporarily for reference

4. **Update serial output** (remove debug dual-system prints)
   - Consistent format: "[EffectName] ENABLED/DISABLED"

**Testing:**
- ✅ Choke works via Commands only
- ✅ No references to `ChokeEvent` enum
- ✅ LED still updates correctly
- ✅ Display still updates correctly
- ✅ Trace events still logged

**Time Estimate:** 1 hour

**Rollback Plan:** Re-add ChokeIO thread, re-add event handling loop (git revert)

---

### Phase 4: Add Second Effect (Validation)

**Goal:** Prove architecture scales to multiple effects.

**Deliverables:**

1. **Implement `AudioEffectGain`** (simple gain/volume effect for testing)
   - Inherits from `AudioEffectBase`
   - `enable()` = reduce volume 50%, `disable()` = full volume
   - Simple audio processing (multiply samples by gain)

2. **Add to audio graph** in `main.cpp`
   - Insert after choke: `timekeeper → choke → gain → i2s_out`
   - Create `AudioConnection` objects

3. **Register in EffectManager**
   - `EffectManager::registerEffect(EffectID::GAIN, &gain);`
   - Update `EffectID` enum: add `GAIN = 2`

4. **Add Key 1 mapping** in `buttonMappings[]`
   - Key 1: `EFFECT_TOGGLE, GAIN` (toggle on press)
   - Update Neokey LED: Key 1 = blue when gain active

5. **Test combinations**
   - Key 0 only: Choke mutes audio
   - Key 1 only: Gain reduces volume
   - Both keys: Choke + Gain (audio muted and reduced)

**Testing:**
- ✅ Key 0 (choke) still works independently
- ✅ Key 1 (gain) toggles on/off
- ✅ Both effects can be active simultaneously
- ✅ Serial shows both effect states
- ✅ LEDs update correctly for both keys

**Success Criteria:**
- No changes to `AppLogic::threadLoop()` (command dispatch is generic)
- No changes to `InputIO::threadLoop()` (table-driven)
- Only changes: add effect class, register in main, update button table

**Time Estimate:** 2-3 hours (including testing)

**Rollback Plan:** Comment out gain effect, remove from audio graph

---

### Phase 5: Cleanup and Documentation (Polish)

**Goal:** Remove legacy code, update documentation, prepare for future effects.

**Deliverables:**

1. **Delete `ChokeIO`** (if not already done)
   - Remove `include/choke_io.h`
   - Remove `src/choke_io.cpp`
   - Remove `chokeThreadEntry()` from main.cpp

2. **Remove legacy API** from `AudioEffectChoke`
   - Delete `engage()`, `releaseChoke()`, `isChoked()` methods
   - Update CLAUDE.md references (replace old API with new)

3. **Update `CLAUDE.md`**
   - Document Command system
   - Document AudioEffectBase interface
   - Document EffectManager usage
   - Update "How to add a new effect" section
   - Update architecture diagrams

4. **Create `EFFECT_SYSTEM_USAGE.md`**
   - Quick reference for adding effects
   - Button mapping examples
   - Parameter control examples

5. **Add tests** (if test framework ready)
   - Test `EffectManager::registerEffect()` duplicate detection
   - Test `EffectManager::executeCommand()` routing
   - Test `Command` struct size and POD-ness

**Time Estimate:** 2 hours

**Success Criteria:**
- Codebase is clean (no deprecated code)
- Documentation is up-to-date
- Other developers can add effects without asking questions

---

## Phase Dependency Graph

```
Phase 1: Command Infrastructure (foundation)
    ↓
Phase 2: InputIO (parallel path) ←──┐
    ↓                               │
Phase 3: Cutover (switch flip)      │ (can test both systems)
    ↓                               │
Phase 4: Second Effect (validation) ─┘
    ↓
Phase 5: Cleanup (polish)
```

**Critical Path:** Phase 1 → Phase 2 → Phase 3 → Phase 4 → Phase 5

**Parallel Work:**
- Can implement `AudioEffectGain` during Phase 2 or 3 (doesn't block)
- Can update documentation during any phase

---

## Testing Strategy

### Per-Phase Testing

Each phase has explicit testing criteria (see phase descriptions above).

### Integration Testing

After Phase 4 (second effect added), perform full integration test:

1. **Hardware test:**
   - Connect Neokey, audio shield, MIDI input
   - Verify all buttons respond (<50ms latency)
   - Verify LEDs update correctly
   - Verify display updates correctly

2. **Audio quality test:**
   - Play sustained tone through audio input
   - Toggle effects, listen for clicks/pops (should be smooth)
   - Verify no audio glitches during heavy button mashing

3. **Performance test:**
   - Monitor CPU usage via `AudioProcessorUsageMax()`
   - Should remain <30% with all effects active
   - Monitor RAM usage (should have >700KB free)

4. **Stress test:**
   - Press all buttons rapidly for 60 seconds
   - Send MIDI clock continuously
   - Verify no crashes, no queue overruns
   - Check trace buffer for dropped events (should be zero)

5. **Regression test:**
   - Run existing unit tests (TimeKeeper, Trace, SPSC queue)
   - Verify all pass (no regressions)

### Automated Testing (Future)

Once effect system is stable, add unit tests:

- `test_command.cpp`: Command struct size, POD-ness
- `test_effect_manager.cpp`: Registration, duplicate detection, command routing
- `test_effect_base.cpp`: Mock effect, verify interface contract

---

## Risk Mitigation

### Risk: Audio Glitches During Refactor

**Likelihood:** Low
**Impact:** High (unusable for live performance)

**Mitigation:**
- Phases 1-3 don't touch audio processing (only control flow)
- Phase 4 uses simple gain effect (minimal DSP)
- Test on hardware after every phase
- Keep audio ISR profiling enabled (`AudioProcessorUsageMax()`)

**Rollback:** Git revert to last stable commit

---

### Risk: Queue Overrun (Commands Dropped)

**Likelihood:** Medium
**Impact:** Medium (missed button presses)

**Mitigation:**
- Command queue size: 32 slots (same as current ChokeEvent queue)
- Monitor queue depth via trace events (add `TRACE_COMMAND_QUEUE_DEPTH`)
- If overruns detected, increase queue size or reduce button mapping complexity

**Detection:**
- Add assertion in `InputIO::threadLoop()`: if push fails, log error

---

### Risk: EffectManager Registry Overflow

**Likelihood:** Low (MAX_EFFECTS = 8)
**Impact:** Low (compile-time detectable)

**Mitigation:**
- `registerEffect()` returns false if full → fatal error in setup()
- Blink LED rapidly + halt if registration fails
- Document max effects in CLAUDE.md

**Prevention:**
- If approaching 8 effects, increase `MAX_EFFECTS` to 16

---

### Risk: Virtual Method Overhead in Audio Path

**Likelihood:** Low (we're not using virtuals in `update()`)
**Impact:** High (if we accidentally do)

**Mitigation:**
- Code review: Verify no virtual calls in audio ISR
- Compiler optimization: `-O2` or `-O3` inlines non-virtual calls
- Profiling: Measure `AudioProcessorUsageMax()` before/after refactor

**Prevention:**
- Document in AUDIO_EFFECT_BASE.md: "NEVER make update() virtual"

---

## Success Metrics

### Phase 1-3 Success:
- ✅ Choke effect works identically to current system
- ✅ No increase in latency (measured with oscilloscope: button press → audio mute)
- ✅ No increase in CPU usage (<1% difference)
- ✅ No audio artifacts (smooth crossfades)

### Phase 4 Success:
- ✅ Two effects can be active simultaneously
- ✅ Effects can be toggled independently
- ✅ Adding second effect required <50 lines of code
- ✅ No changes to AppLogic command dispatch loop

### Phase 5 Success:
- ✅ Codebase is clean (no ChokeIO, no legacy API)
- ✅ Documentation is complete (CLAUDE.md, EFFECT_SYSTEM_USAGE.md)
- ✅ Other developers can add effects in <1 hour (documented process)

---

## Future Extensions (Post-Migration)

These features are NOT part of the migration, but the new architecture enables them:

### 1. Parameter Control (MIDI CC)

Once effect system is stable, add parameter control:

- Map MIDI CC messages → `EFFECT_SET_PARAM` commands
- Example: CC 20 → Delay time, CC 21 → Reverb decay
- Implement in existing MIDI I/O thread (already has MIDI parser)

**Enabled by:**
- `Command.value` field (already designed for parameters)
- `AudioEffectBase::setParameter()` interface (already exists)

---

### 2. Effect Presets (Scenes)

Allow saving/recalling effect combinations:

- Store enabled effects as bitmask (32 bits = 32 effects)
- Map buttons to scenes: "Key 0 + 1 + 2 held = save scene 1"
- Recall: "Key 3 double-tap = load scene 1"

**Enabled by:**
- `EffectManager::getEnabledEffectsMask()` (already exists)
- `EffectManager::executeCommand()` batch (loop over effects)

---

### 3. Visual Effect Chaining (Display)

Show effect chain on OLED display:

```
┌─────────────────────┐
│ Input               │
│   ↓                 │
│ [CHOKE] ON          │
│   ↓                 │
│ [DELAY] OFF         │
│   ↓                 │
│ [REVERB] ON         │
│   ↓                 │
│ Output              │
└─────────────────────┘
```

**Enabled by:**
- `EffectManager::getEnabledEffectsMask()` (query state)
- `EffectManager::getEffectName()` (display labels)

---

### 4. Effect Parameters from Encoder

Add rotary encoder for live parameter tweaking:

- Encoder turn → `EFFECT_SET_PARAM` command
- Which parameter? Last-touched effect (contextual)
- Display shows: "Delay Time: 350ms"

**Enabled by:**
- `AudioEffectBase::setParameter()` / `getParameter()` (already exists)
- Same command queue infrastructure (no new queues needed)

---

## References

### Related Documentation

- **`CLAUDE.md`**: Project overview, architecture, current system
- **`CHOKE_DESIGN.md`**: Detailed choke feature design (will update in Phase 5)
- **`TIMEKEEPER_USAGE.md`**: Timing system (unchanged by migration)
- **`TRACE_USAGE.md`**: Debugging system (add command trace events)

### External Resources

- **Teensy Audio Library**: https://www.pjrc.com/teensy/td_libs_Audio.html
- **Lock-Free Programming**: "Is Parallel Programming Hard" by Paul E. McKenney
- **Command Pattern**: "Design Patterns" by Gang of Four (GoF)

### Code References

Files to reference during implementation:

- **Current choke implementation**: `src/choke_io.cpp`, `include/audio_choke.h`
- **SPSC queue**: `utils/spsc_queue.h` (use for Command queue)
- **App logic structure**: `src/app_logic.cpp` (copy event processing pattern)

---

## Revision History

| Date       | Author | Changes |
|------------|--------|---------|
| 2025-10-11 | Claude | Initial migration plan created |

---

## Approvals

- [ ] **Technical Review**: Architecture approved (design is sound)
- [ ] **User Acceptance**: User confirms plan matches vision
- [ ] **Ready to Implement**: Proceed with Phase 1

---

**Next Steps:** Review this document, provide feedback, then begin Phase 1 implementation.
