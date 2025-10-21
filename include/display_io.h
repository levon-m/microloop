/**
 * display_io.h - SSD1306 OLED Display I2C handler
 *
 * PURPOSE:
 * Manages 128x64 OLED display for visual feedback and future menu system.
 * Displays different bitmap images based on system state (choke, menu, etc.)
 *
 * HARDWARE:
 * - 128x64 OLED Display with SSD1306 driver
 * - Connected to Wire1 (SDA1=pin 17, SCL1=pin 16)
 * - I2C address: 0x3C (default for most SSD1306 displays)
 * - Separate bus from Audio Shield (Wire) and Neokey (Wire2)
 *
 * ARCHITECTURE:
 * - Dedicated I/O thread (lower priority than audio/MIDI)
 * - Command queue for display updates (lock-free SPSC)
 * - Bitmap storage system for multiple images
 * - Thread-safe display switching
 *
 * DISPLAY COMMANDS:
 * - SHOW_DEFAULT: Show default/idle image
 * - SHOW_CHOKE: Show choke active image
 * - SHOW_CUSTOM: Show specific bitmap by ID (future menu system)
 *
 * BITMAP MANAGEMENT:
 * - Bitmaps stored as progmem byte arrays (save RAM)
 * - 128x64 = 1024 bytes per image
 * - LCDAssistant format: horizontal bytes, LSB first
 * - Easy to add new bitmaps via bitmap registry
 *
 * THREAD MODEL:
 * - Producer: App thread (pushes display commands to queue)
 * - Consumer: DisplayIO thread (drains queue, updates display)
 *
 * PERFORMANCE:
 * - Full screen update: ~20-30ms (acceptable, not audio critical)
 * - I2C speed: 400kHz (fast mode)
 * - Thread priority: Lower than audio/MIDI (can tolerate latency)
 */

#pragma once

#include <Arduino.h>

/**
 * Display command types
 */
enum class DisplayCommand : uint8_t {
    SHOW_DEFAULT = 0,   // Show default/idle image
    SHOW_CHOKE = 1,     // Show choke active image
    SHOW_CUSTOM = 2     // Show custom bitmap (future: menu system)
};

/**
 * Bitmap ID enum for extensibility
 * Add new IDs here as you create more bitmaps
 */
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
    // Future bitmaps:
    // MENU_MAIN = 15,
    // MENU_LOOP = 12,
    // MENU_SAMPLE = 13,
    // BPM_DISPLAY = 14,
    // etc.
};

/**
 * Display event structure (for command queue)
 */
struct DisplayEvent {
    DisplayCommand command;
    BitmapID bitmapID;  // Used with SHOW_CUSTOM command

    DisplayEvent() : command(DisplayCommand::SHOW_DEFAULT), bitmapID(BitmapID::DEFAULT) {}
    DisplayEvent(DisplayCommand cmd) : command(cmd), bitmapID(BitmapID::DEFAULT) {}
    DisplayEvent(DisplayCommand cmd, BitmapID id) : command(cmd), bitmapID(id) {}
};

/**
 * DisplayIO subsystem - manages OLED display
 */
namespace DisplayIO {
    /**
     * Initialize SSD1306 OLED display
     *
     * WHAT IT DOES:
     * - Initializes Wire1 I2C bus (SDA1=pin 17, SCL1=pin 16)
     * - Configures SSD1306 display (128x64, I2C address 0x3C)
     * - Clears display
     * - Shows default bitmap
     *
     * @return true if display detected and configured, false on failure
     *
     * @note Must be called BEFORE starting I/O thread
     */
    bool begin();

    /**
     * I/O thread entry point (runs forever)
     *
     * WHAT IT DOES:
     * - Drains display command queue
     * - Updates display based on commands
     * - Switches bitmaps based on state
     * - Sleeps when idle (low CPU usage)
     *
     * PERFORMANCE:
     * - Full screen update: ~20-30ms
     * - I2C transaction: ~15-20ms @ 400kHz
     * - Idle: Sleeps 50ms between checks (low CPU)
     *
     * @note Never returns (infinite loop)
     */
    void threadLoop();

    /**
     * Show default/idle image (PRODUCER side)
     *
     * Thread-safe: Call from any thread
     * Real-time safe: O(1), no blocking
     */
    void showDefault();

    /**
     * Show choke active image (PRODUCER side)
     *
     * Thread-safe: Call from any thread
     * Real-time safe: O(1), no blocking
     */
    void showChoke();

    /**
     * Show custom bitmap by ID (PRODUCER side)
     *
     * Thread-safe: Call from any thread
     * Real-time safe: O(1), no blocking
     *
     * @param id Bitmap ID to display
     */
    void showBitmap(BitmapID id);

    /**
     * Get current display state (for debugging)
     *
     * @return Current bitmap ID being displayed
     */
    BitmapID getCurrentBitmap();
}
