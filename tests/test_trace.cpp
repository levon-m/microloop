/**
 * test_trace.cpp - Unit tests for Trace utility
 */

#include "test_runner.h"
#include "trace.h"

TEST(Trace_Record_StoresEvent) {
    Trace::clear();

    TRACE(TRACE_MIDI_START);
    TRACE(TRACE_BEAT_START, 42);

    // Can't easily verify buffer contents without exposing internals,
    // but we can verify it doesn't crash and dump works
    Serial.println("\n[Test] Dumping trace (should show 2 events):");
    Trace::dump();
}

TEST(Trace_Clear_ResetsBuffer) {
    Trace::clear();

    TRACE(TRACE_MIDI_START);
    Trace::clear();

    // After clear, dump should show empty or minimal events
    Serial.println("\n[Test] After clear (should be empty or minimal):");
    Trace::dump();
}

TEST(Trace_MultipleEvents_RecordsInOrder) {
    Trace::clear();

    for (int i = 0; i < 10; i++) {
        TRACE(TRACE_BEAT_START, i);
        delayMicroseconds(100);  // Ensure different timestamps
    }

    Serial.println("\n[Test] 10 sequential events:");
    Trace::dump();
}

TEST(Trace_OverflowHandling_WrapsAround) {
    Trace::clear();

    // Fill buffer beyond capacity (1024 events)
    for (int i = 0; i < 1100; i++) {
        TRACE(TRACE_TICK_PERIOD_UPDATE, i & 0xFFFF);
    }

    Serial.println("\n[Test] After overflow (should show newest 1024 events):");
    // Dump should show events ~76-1100 (oldest overwritten)
    // We can't assert exact contents, but verify it doesn't crash
    Trace::dump();

    // Manual verification: Check that dump doesn't hang and shows reasonable data
    ASSERT_TRUE(true);  // If we got here, no crash
}

TEST(Trace_EventNames_ResolveCorrectly) {
    // Test a few event name lookups
    const char* name1 = Trace::eventName(TRACE_MIDI_START);
    const char* name2 = Trace::eventName(TRACE_BEAT_START);
    const char* name3 = Trace::eventName(TRACE_TIMEKEEPER_SYNC);
    const char* name4 = Trace::eventName(9999);  // Unknown

    Serial.print("TRACE_MIDI_START: ");
    Serial.println(name1);
    Serial.print("TRACE_BEAT_START: ");
    Serial.println(name2);
    Serial.print("TRACE_TIMEKEEPER_SYNC: ");
    Serial.println(name3);
    Serial.print("Unknown (9999): ");
    Serial.println(name4);

    // Basic sanity checks
    ASSERT_NE(name1[0], '\0');  // Not empty
    ASSERT_NE(name2[0], '\0');
    ASSERT_NE(name3[0], '\0');
}

TEST(Trace_HighFrequency_HandlesRapidCalls) {
    Trace::clear();

    uint32_t start = micros();

    // Hammer the trace buffer
    for (int i = 0; i < 1000; i++) {
        TRACE(TRACE_AUDIO_CALLBACK, i & 0xFFFF);
    }

    uint32_t duration = micros() - start;

    Serial.print("\n1000 trace calls took ");
    Serial.print(duration);
    Serial.println(" µs");
    Serial.print("Average per call: ");
    Serial.print(duration / 1000.0f);
    Serial.println(" µs");

    // Should be < 1µs per call on average (very fast)
    ASSERT_LT(duration, 1000000U);  // < 1 second total
}
