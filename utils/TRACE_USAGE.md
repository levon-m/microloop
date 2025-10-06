# Trace Utility Usage Guide

## Overview

The trace utility provides **wait-free, lock-free event logging** for real-time debugging. It's safe to use in ISRs, I/O thread, and app thread without impacting timing.

## Quick Start

```cpp
#include "trace.h"

// Record an event
TRACE(TRACE_BEAT_START);

// Record an event with a value
TRACE(TRACE_MIDI_CLOCK_QUEUED, clockQueue.size());

// Dump trace buffer to Serial (app thread only!)
Trace::dump();

// Clear trace buffer
Trace::clear();
```

## Serial Commands

When running the firmware, use these commands in the serial monitor:

- `t` - Dump trace buffer (shows all recorded events with timestamps)
- `c` - Clear trace buffer (reset for new capture)

## Adding Custom Trace Events

1. **Add event ID** to `utils/trace.h`:
   ```cpp
   enum TraceEventId : uint16_t {
       // ... existing events ...
       TRACE_MY_FEATURE = 500,  // Your event ID
   };
   ```

2. **Add event name** to `Trace::eventName()` in `utils/trace.h`:
   ```cpp
   static const char* eventName(uint16_t eventId) {
       switch (eventId) {
           // ... existing cases ...
           case TRACE_MY_FEATURE: return "MY_FEATURE";
           default: return "UNKNOWN";
       }
   }
   ```

3. **Use in your code**:
   ```cpp
   TRACE(TRACE_MY_FEATURE, someValue);
   ```

## Example Output

```
=== TRACE DUMP ===
Timestamp(µs) | ID  | Value | Event
--------------|-----|-------|------
1234567       | 10  | 0     | MIDI_START
1234580       | 1   | 0     | MIDI_CLOCK_RECV
1234585       | 2   | 12    | MIDI_CLOCK_QUEUED
1255012       | 1   | 0     | MIDI_CLOCK_RECV
1255018       | 2   | 13    | MIDI_CLOCK_QUEUED
1275440       | 1   | 0     | MIDI_CLOCK_RECV
1275445       | 2   | 14    | MIDI_CLOCK_QUEUED
1500000       | 100 | 0     | BEAT_START
1500005       | 101 | 0     | BEAT_LED_ON
1540000       | 102 | 0     | BEAT_LED_OFF
=== END TRACE ===
```

## Analyzing Traces

### Measure Timing Between Events

Calculate delta between two timestamps to measure latency:

```
MIDI_CLOCK_RECV at 1234580µs
BEAT_START at 1500000µs
Delta: 1500000 - 1234580 = 265420µs = 265.4ms
```

### Check for Dropped Events

Look for `TRACE_MIDI_CLOCK_DROPPED` events. If you see these, the clock queue is full (app thread not draining fast enough).

### Verify Jitter Smoothing

Look at `TRACE_TICK_PERIOD_UPDATE` values over time. They should converge to a stable value (e.g., ~2083 for 120 BPM, since value is in units of 10µs).

### Monitor Queue Depth

`TRACE_MIDI_CLOCK_QUEUED` value shows queue size. If it grows unbounded, you have a backlog.

## Performance

- **Overhead per trace**: ~10-20 CPU cycles (~15-30ns @ 600MHz)
- **Memory**: 8KB (1024 events × 8 bytes each)
- **Buffer wraparound**: At 1000 traces/sec, ~1 second of history before overwrite

## Compile-Time Control

To disable tracing in release builds, define `TRACE_ENABLED=0` in your build system:

```cmake
add_compile_definitions(TRACE_ENABLED=0)
```

This compiles out all tracing with zero overhead.

## Common Use Cases

### Debug LED Timing

```cpp
// In app_logic.cpp
if (tickCount == 0) {
    TRACE(TRACE_BEAT_START);
    TRACE(TRACE_BEAT_LED_ON);
}
if (tickCount == 2) {
    TRACE(TRACE_BEAT_LED_OFF);
}
```

Dump trace to see exact timing of LED on/off relative to MIDI clock.

### Measure Thread Latency

```cpp
// In midi_io.cpp
static void onClock() {
    TRACE(TRACE_MIDI_CLOCK_RECV);
    // ... queue clock ...
}

// In app_logic.cpp
while (MidiIO::popClock(clockMicros)) {
    // TRACE_MIDI_CLOCK_RECV was already recorded in onClock()
    // Now we're processing it - timestamp delta = latency
}
```

### Verify Quantization (Future)

```cpp
// When implementing loop recording
if (userPressedRecord) {
    TRACE(TRACE_RECORD_REQUEST, micros() & 0xFFFF);
}
// Later, at quantized beat boundary
if (tickCount == 0 && recordPending) {
    TRACE(TRACE_RECORD_START, micros() & 0xFFFF);
}
```

Dump trace to verify record started exactly on beat boundary.

## Best Practices

1. **Use meaningful event IDs** - Group by feature (MIDI 1-99, Beat 100-199, etc.)
2. **Don't trace in tight loops** - Trace important state changes, not every iteration
3. **Use value field wisely** - Store queue depths, beat numbers, error codes, etc.
4. **Dump in app thread only** - Serial.print is slow, keep it out of real-time paths
5. **Clear before capture** - Start with clean buffer for focused debugging

## Limitations

- **Circular buffer**: Old events overwrite when buffer full (1024 events)
- **16-bit value**: Can only store 0-65535 (use scaling for larger values)
- **No filtering on record**: Everything gets logged (use `t` command to view)
- **Manual analysis**: No built-in statistics (calculate in post-processing if needed)

## Future Enhancements

- Statistics mode (min/max/avg timing between events)
- Conditional tracing (only record if condition met)
- Binary dump for offline analysis
- Integration with logic analyzer (export to VCD format)
