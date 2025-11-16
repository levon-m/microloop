#pragma once

#include <Arduino.h>

// Transport event types
enum class MidiEvent : uint8_t {
    START = 1,    // Sequencer started
    STOP = 2,     // Sequencer stopped
    CONTINUE = 3  // Sequencer continued from pause
};

namespace MidiIO {
    void begin();

    void threadLoop();

    bool popEvent(MidiEvent& outEvent);

    bool popClock(uint32_t& outMicros);

    bool running();
}