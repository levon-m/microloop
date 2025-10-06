## MicroLoop Testing Guide

This document explains how to test the MicroLoop firmware, both utilities and full system integration.

## Table of Contents
1. [Testing Philosophy](#testing-philosophy)
2. [Running Unit Tests](#running-unit-tests)
3. [Manual Integration Testing](#manual-integration-testing)
4. [Writing New Tests](#writing-new-tests)
5. [Continuous Verification](#continuous-verification)

---

## Testing Philosophy

**On-Device Testing:** All tests run directly on Teensy hardware (no PC-side test harness). This ensures:
- Real hardware behavior (timing, interrupts, atomics)
- No mocking complexity
- Tests match production environment exactly

**Test Coverage:**
- ✅ **Unit Tests**: TimeKeeper, Trace, SPSC Queue (isolated utilities)
- ✅ **Integration Tests**: Full system with MIDI input, audio passthrough
- ✅ **Performance Tests**: Timing measurements for real-time safety

---

## Running Unit Tests

### Quick Start

1. **Modify CMakeLists.txt** (temporarily):
   ```cmake
   # Comment out normal main
   # add_executable(microloop.elf src/main.cpp)

   # Use test main instead
   add_executable(microloop.elf tests/run_tests.cpp)
   ```

2. **Build**:
   ```bash
   cd build
   ninja
   ```

3. **Upload** `microloop.hex` to Teensy

4. **Open Serial Monitor** (115200 baud)

5. **Observe results**:
   ```
   ╔════════════════════════════════════════╗
   ║    MicroLoop On-Device Test Suite     ║
   ╚════════════════════════════════════════╝

   [ RUN      ] TimeKeeper_Begin_InitializesState
   [       OK ] TimeKeeper_Begin_InitializesState
   [ RUN      ] TimeKeeper_Reset_ClearsState
   [       OK ] TimeKeeper_Reset_ClearsState
   ...
   ========================================
   Tests run: 42
   Passed: 42
   Failed: 0
   Duration: 145 ms
   ========================================

   ✓ All tests passed!
   ```

### Expected Output

- **Green `[OK]`**: Test passed
- **Red `[FAILED]`**: Test failed (shows file:line and assertion)
- **Summary**: Total/passed/failed count

### Interpreting Failures

Example failure:
```
[ RUN      ] TimeKeeper_SyncToMIDIClock_CalculatesSamplesPerBeat
  FAIL: tests/test_timekeeper.cpp:123 - Expected 22050, got 22049
[  FAILED  ] TimeKeeper_SyncToMIDIClock_CalculatesSamplesPerBeat
```

**Action**: Check line 123 in test file, verify expected value is correct, fix implementation or test.

---

## Manual Integration Testing

**These tests verify real-world operation with actual MIDI hardware.**

### Prerequisites
- Teensy 4.1 with Audio Shield
- MIDI clock source (Digitakt, DAW, etc.)
- LED on pin 31
- Serial monitor

### Test 1: MIDI Clock Reception

**Goal**: Verify MIDI clock is received and processed

1. Upload normal firmware (not test firmware)
2. Connect MIDI clock source
3. Open serial monitor
4. Send MIDI START from clock source
5. **Expected**: Serial shows `▶ START`
6. **Verify**: LED blinks on beat (short pulse every 24 ticks)

**Pass criteria**:
- ✅ Serial shows START/STOP messages
- ✅ LED blinks at tempo
- ✅ LED timing is stable (no drift)

### Test 2: TimeKeeper Accuracy

**Goal**: Verify sample position tracks audio and MIDI sync works

1. Start MIDI clock @ 120 BPM
2. Wait 10 seconds
3. Type `s` in serial monitor
4. **Expected**:
   ```
   === TimeKeeper Status ===
   Sample Position: ~441000  (10s * 44100 Hz)
   Beat: ~20  (120 BPM = 2 beats/sec * 10s)
   BPM: 120.00
   Samples/Beat: 22050
   Transport: PLAYING
   ```

**Pass criteria**:
- ✅ Sample position increases continuously
- ✅ Beat number advances every ~0.5s @ 120 BPM
- ✅ BPM matches MIDI source (±1 BPM tolerance)
- ✅ Samples/Beat ~= 22050 @ 120 BPM

### Test 3: Transport Control

**Goal**: Verify START/STOP/CONTINUE work correctly

1. Send MIDI START → Serial shows `▶ START`, LED blinks
2. Send MIDI STOP → Serial shows `■ STOP`, LED off
3. Send MIDI CONTINUE → Serial shows `▶ CONTINUE`, LED resumes
4. Type `s` → Transport shows PLAYING/STOPPED

**Pass criteria**:
- ✅ Transport state updates immediately
- ✅ LED responds to transport
- ✅ Beat counter resets on START, preserves on CONTINUE

### Test 4: Tempo Change

**Goal**: Verify TimeKeeper adapts to tempo changes

1. Start @ 120 BPM
2. Type `s` → Note Samples/Beat and BPM
3. Change tempo to 140 BPM on MIDI source
4. Wait 5 seconds (allow EMA to converge)
5. Type `s` → **Expected**: BPM ~140, Samples/Beat ~18900

**Pass criteria**:
- ✅ BPM converges to new tempo within 10 seconds
- ✅ LED blink rate increases
- ✅ No beat counter glitches during transition

### Test 5: Trace Verification

**Goal**: Verify trace records events correctly

1. Start MIDI clock
2. Wait 5 seconds
3. Type `c` to clear trace
4. Wait 5 more seconds
5. Type `t` to dump trace
6. **Expected**: See MIDI_CLOCK_RECV, BEAT_START, TIMEKEEPER_SYNC events

**Pass criteria**:
- ✅ Events appear in chronological order
- ✅ Timestamps increase monotonically
- ✅ BEAT_START appears every ~22050 samples @ 120 BPM
- ✅ No MIDI_CLOCK_DROPPED events (queue healthy)

### Test 6: Long-Duration Stability

**Goal**: Verify system runs stably for extended periods

1. Start MIDI clock
2. Let run for 30 minutes
3. Periodically type `s` to check status
4. **Expected**: No crashes, no drift, no stuck states

**Pass criteria**:
- ✅ No serial output hangs
- ✅ LED continues blinking steadily
- ✅ Sample position continues increasing
- ✅ No memory corruption (use CrashReport on reset)

---

## Writing New Tests

### Test Structure

```cpp
#include "test_runner.h"
#include "your_module.h"

TEST(ModuleName_FunctionName_Behavior) {
    // Arrange: Set up test conditions
    YourModule::reset();

    // Act: Perform the operation
    YourModule::doSomething(42);

    // Assert: Verify expected outcome
    ASSERT_EQ(YourModule::getValue(), 42);
}
```

### Available Assertions

```cpp
ASSERT_TRUE(condition);
ASSERT_FALSE(condition);
ASSERT_EQ(actual, expected);  // Equality
ASSERT_NE(actual, expected);  // Inequality
ASSERT_LT(actual, expected);  // Less than
ASSERT_GT(actual, expected);  // Greater than
ASSERT_NEAR(actual, expected, tolerance);  // Floating point equality
```

### Test Naming Convention

**Pattern**: `ModuleName_FunctionUnderTest_ExpectedBehavior`

**Examples**:
- `TimeKeeper_IncrementSamples_UpdatesPosition`
- `SPSCQueue_PushPop_MaintainsOrder`
- `Trace_OverflowHandling_WrapsAround`

### Adding a New Test File

1. **Create** `tests/test_your_module.cpp`
2. **Include** in `tests/run_tests.cpp`:
   ```cpp
   #include "test_your_module.cpp"
   ```
3. **Rebuild** and run

### Testing Guidelines

**DO**:
- ✅ Test one thing per test
- ✅ Use descriptive names
- ✅ Reset state before each test (call `reset()`, `clear()`, etc.)
- ✅ Test edge cases (0, max, overflow)
- ✅ Test performance (use `micros()` for timing)

**DON'T**:
- ❌ Rely on test execution order
- ❌ Use global state without resetting
- ❌ Test implementation details (test behavior, not internals)
- ❌ Write overly complex tests (keep simple and focused)

---

## Continuous Verification

### Pre-Commit Checklist

Before committing code changes:

1. **Run unit tests** (`tests/run_tests.cpp`)
   - All tests should pass
   - No new failures introduced

2. **Run integration test** (manual, with MIDI hardware)
   - MIDI clock reception works
   - TimeKeeper shows correct BPM
   - LED blinks steadily

3. **Check serial output** for errors
   - No assertion failures
   - No unexpected warnings

4. **Verify trace** shows healthy operation
   - Type `t` - no DROPPED events
   - MIDI events arriving regularly

### Regression Testing

When fixing a bug:

1. **Write a failing test** that reproduces the bug
2. **Fix the bug**
3. **Verify test now passes**
4. **Keep the test** to prevent regression

Example:
```cpp
// Bug: TimeKeeper wraps at 32-bit limit
TEST(TimeKeeper_NoOverflowAt32Bit_Regression) {
    TimeKeeper::reset();

    // Go past 32-bit limit
    for (int i = 0; i < 40000; i++) {
        TimeKeeper::incrementSamples(128000);
    }

    uint64_t pos = TimeKeeper::getSamplePosition();
    ASSERT_GT(pos, 4294967296ULL);  // Should be > 32-bit max
}
```

### Performance Benchmarks

Monitor these metrics over time:

| Operation | Target | Measure |
|-----------|--------|---------|
| `TimeKeeper::incrementSamples()` | < 100 cycles | Trace timing |
| `SPSC queue push/pop` | < 50 cycles | Test output |
| `Trace::record()` | < 20 cycles | Test output |
| Audio callback | < 1ms | Logic analyzer |

**How to measure**:
```cpp
uint32_t start = ARM_DWT_CYCCNT;  // CPU cycle counter
// ... operation ...
uint32_t cycles = ARM_DWT_CYCCNT - start;
```

---

## Troubleshooting Tests

### Test Hangs / No Output

**Problem**: Serial monitor shows nothing after upload

**Solutions**:
1. Check baud rate (should be 115200)
2. Press reset button on Teensy
3. Wait up to 3 seconds for serial init
4. Verify USB cable is good

### All Tests Fail

**Problem**: Every test shows FAIL

**Solutions**:
1. Check TimeKeeper/Trace initialized in `setup()`
2. Verify includes in `run_tests.cpp`
3. Check for global state corruption
4. Try running tests individually (comment out others)

### Intermittent Failures

**Problem**: Test passes sometimes, fails others

**Solutions**:
1. Check for uninitialized variables
2. Verify test doesn't depend on previous test state
3. Look for race conditions (unlikely in single-threaded tests)
4. Add `delay(1)` if timing-sensitive

### Assertion Line Numbers Wrong

**Problem**: Failure shows wrong line number

**Solution**: Ensure `__LINE__` macro works - check compiler flags

---

## Test Maintenance

### When to Update Tests

- ✅ Adding new features → Write tests first (TDD)
- ✅ Fixing bugs → Add regression test
- ✅ Refactoring → Tests should still pass (no changes needed)
- ✅ Changing API → Update affected tests

### Deprecating Tests

If a feature is removed:
1. **Remove tests** for that feature
2. **Update documentation**
3. **Keep tests** if feature might return

---

## Summary

**Quick Testing Workflow**:

```
1. Write code
2. Write/update tests
3. Build with tests/run_tests.cpp
4. Upload to Teensy
5. Check serial output → All tests pass?
   - YES → Proceed to integration testing
   - NO → Fix and repeat

6. Integration test with real MIDI
   - LED blinks?
   - TimeKeeper shows correct BPM?
   - Trace shows healthy events?

7. Commit (tests pass + integration verified)
```

**Testing gives confidence that**:
- ✅ Utilities work correctly in isolation
- ✅ System works correctly with real hardware
- ✅ Changes don't break existing functionality
- ✅ Performance meets real-time requirements

Happy testing! 🧪
