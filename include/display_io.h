#pragma once

#include <Arduino.h>

enum class DisplayCommand : uint8_t {
    SHOW_DEFAULT = 0,   // Show default/idle image
    SHOW_CHOKE = 1,     // Show choke active image
    SHOW_CUSTOM = 2,    // Show custom bitmap
    SHOW_MENU = 3       // Show menu screen (runtime graphics)
};

enum class BitmapID : uint8_t {
    DEFAULT = 0,          // Default/idle screen
    FREEZE_ACTIVE = 1,    // Freeze engaged indicator
    CHOKE_ACTIVE = 2,     // Choke engaged indicator
    STUTTER_ACTIVE = 3    // Stutter engaged indicator (future feature)
};

// Menu display data for runtime graphics
struct MenuDisplayData {
    const char* topText;      // e.g., "CHOKE->Length" or "Global Quantization"
    const char* middleText;   // e.g., "Free", "Quantized", "1/32"
    uint8_t numOptions;       // Number of indicator circles (2 or 4)
    uint8_t selectedIndex;    // Currently selected option (0-3)

    MenuDisplayData() : topText(""), middleText(""), numOptions(2), selectedIndex(0) {}
    MenuDisplayData(const char* top, const char* middle, uint8_t num, uint8_t sel)
        : topText(top), middleText(middle), numOptions(num), selectedIndex(sel) {}
};

struct DisplayEvent {
    DisplayCommand command;

    // Union to save memory - only one is used at a time
    union {
        BitmapID bitmapID;      // Used with SHOW_CUSTOM command
        MenuDisplayData menuData; // Used with SHOW_MENU command
    };

    DisplayEvent() : command(DisplayCommand::SHOW_DEFAULT), bitmapID(BitmapID::DEFAULT) {}
    DisplayEvent(DisplayCommand cmd) : command(cmd), bitmapID(BitmapID::DEFAULT) {}
    DisplayEvent(DisplayCommand cmd, BitmapID id) : command(cmd), bitmapID(id) {}
    DisplayEvent(DisplayCommand cmd, const MenuDisplayData& menu) : command(cmd), menuData(menu) {}
};

namespace DisplayIO {
    bool begin();

    void threadLoop();

    void showDefault();

    void showChoke();

    void showBitmap(BitmapID id);

    void showMenu(const MenuDisplayData& menuData);

    BitmapID getCurrentBitmap();
}
