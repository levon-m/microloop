#pragma once

#include <Arduino.h>

enum class DisplayCommand : uint8_t {
    SHOW_DEFAULT = 0,   // Show default/idle image
    SHOW_CHOKE = 1,     // Show choke active image
    SHOW_CUSTOM = 2     // Show custom bitmap (future: menu system)
};

enum class BitmapID : uint8_t {
    DEFAULT = 0,          // Default/idle screen
    FREEZE_ACTIVE = 1,    // Freeze engaged indicator
    CHOKE_ACTIVE = 2,     // Choke engaged indicator
    QUANT_32 = 3,         // Quantization: 1/32 note
    QUANT_16 = 4,         // Quantization: 1/16 note
    QUANT_8 = 5,          // Quantization: 1/8 note
    QUANT_4 = 6,          // Quantization: 1/4 note
    CHOKE_LENGTH_FREE = 7,   // Choke length: Free mode
    CHOKE_LENGTH_QUANT = 8,  // Choke length: Quantized mode
    CHOKE_ONSET_FREE = 9,    // Choke onset: Free mode
    CHOKE_ONSET_QUANT = 10,  // Choke onset: Quantized mode
    FREEZE_LENGTH_FREE = 11,  // Freeze length: Free mode
    FREEZE_LENGTH_QUANT = 12, // Freeze length: Quantized mode
    FREEZE_ONSET_FREE = 13,   // Freeze onset: Free mode
    FREEZE_ONSET_QUANT = 14,  // Freeze onset: Quantized mode
    STUTTER_IDLE_WITH_LOOP = 15,    // Stutter: Loop captured, ready for playback
    STUTTER_CAPTURING = 16,          // Stutter: Actively capturing
    STUTTER_PLAYING = 17,            // Stutter: Playing captured loop
    STUTTER_ONSET_FREE = 18,         // Stutter onset: Free mode
    STUTTER_ONSET_QUANT = 19,        // Stutter onset: Quantized mode
    STUTTER_LENGTH_FREE = 20,        // Stutter length: Free mode
    STUTTER_LENGTH_QUANT = 21,       // Stutter length: Quantized mode
    STUTTER_CAPTURE_START_FREE = 22, // Stutter capture start: Free mode
    STUTTER_CAPTURE_START_QUANT = 23,// Stutter capture start: Quantized mode
    STUTTER_CAPTURE_END_FREE = 24,   // Stutter capture end: Free mode
    STUTTER_CAPTURE_END_QUANT = 25   // Stutter capture end: Quantized mode
};

struct DisplayEvent {
    DisplayCommand command;
    BitmapID bitmapID;  // Used with SHOW_CUSTOM command

    DisplayEvent() : command(DisplayCommand::SHOW_DEFAULT), bitmapID(BitmapID::DEFAULT) {}
    DisplayEvent(DisplayCommand cmd) : command(cmd), bitmapID(BitmapID::DEFAULT) {}
    DisplayEvent(DisplayCommand cmd, BitmapID id) : command(cmd), bitmapID(id) {}
};

namespace DisplayIO {
    bool begin();

    void threadLoop();

    void showDefault();

    void showChoke();

    void showBitmap(BitmapID id);

    BitmapID getCurrentBitmap();
}
