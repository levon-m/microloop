/**
 * display_io.cpp - SSD1306 OLED Display I2C implementation
 */

#include "display_io.h"
#include "bitmaps.h"
#include "spsc_queue.h"
#include "trace.h"
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <TeensyThreads.h>
#include <Wire.h>

/**
 * HARDWARE CONFIGURATION
 */
static constexpr uint8_t DISPLAY_I2C_ADDR = 0x3C;  // Default SSD1306 address
static constexpr uint8_t DISPLAY_WIDTH = 128;
static constexpr uint8_t DISPLAY_HEIGHT = 64;
static constexpr int8_t RESET_PIN = -1;  // No reset pin (using I2C reset)

/**
 * THREAD TIMING
 */
static constexpr uint32_t IDLE_DELAY_MS = 50;  // Delay when queue empty (low CPU usage)

/**
 * SSD1306 Display object
 * Initialize with Wire1 bus (SDA1=pin 17, SCL1=pin 16)
 */
static Adafruit_SSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire1, RESET_PIN);

/**
 * Lock-free command queue (App thread → DisplayIO thread)
 */
static SPSCQueue<DisplayEvent, 16> commandQueue;

/**
 * Current display state
 */
static volatile BitmapID currentBitmap = BitmapID::DEFAULT;

// =============================================================================
// BITMAP STORAGE
// =============================================================================

/**
 * Bitmap structure for registry
 * Maps BitmapID enum to actual PROGMEM bitmap data
 */
struct BitmapData {
    const uint8_t* data;  // Pointer to PROGMEM bitmap array
    uint16_t width;
    uint16_t height;
};

/**
 * Bitmap registry - maps BitmapID to actual bitmap data
 *
 * IMPORTANT: Order must match BitmapID enum in display_io.h!
 *
 * To add new bitmaps:
 * 1. Add bitmap array to include/bitmaps.h
 * 2. Add BitmapID enum entry to include/display_io.h
 * 3. Add registry entry here (order matters!)
 */
static const BitmapData bitmapRegistry[] = {
    { bitmap_default, DISPLAY_WIDTH, DISPLAY_HEIGHT },            // BitmapID::DEFAULT
    { bitmap_freeze_active, DISPLAY_WIDTH, DISPLAY_HEIGHT },      // BitmapID::FREEZE_ACTIVE
    { bitmap_choke_active, DISPLAY_WIDTH, DISPLAY_HEIGHT },       // BitmapID::CHOKE_ACTIVE
    { bitmap_quant_32, DISPLAY_WIDTH, DISPLAY_HEIGHT },           // BitmapID::QUANT_32
    { bitmap_quant_16, DISPLAY_WIDTH, DISPLAY_HEIGHT },           // BitmapID::QUANT_16
    { bitmap_quant_8, DISPLAY_WIDTH, DISPLAY_HEIGHT },            // BitmapID::QUANT_8
    { bitmap_quant_4, DISPLAY_WIDTH, DISPLAY_HEIGHT },            // BitmapID::QUANT_4
    { bitmap_choke_length_free, DISPLAY_WIDTH, DISPLAY_HEIGHT },  // BitmapID::CHOKE_LENGTH_FREE
    { bitmap_choke_length_quant, DISPLAY_WIDTH, DISPLAY_HEIGHT }, // BitmapID::CHOKE_LENGTH_QUANT
    { bitmap_choke_onset_free, DISPLAY_WIDTH, DISPLAY_HEIGHT },   // BitmapID::CHOKE_ONSET_FREE
    { bitmap_choke_onset_quant, DISPLAY_WIDTH, DISPLAY_HEIGHT },  // BitmapID::CHOKE_ONSET_QUANT
    { bitmap_choke_length_free, DISPLAY_WIDTH, DISPLAY_HEIGHT },  // BitmapID::FREEZE_LENGTH_FREE (placeholder: reuse choke bitmap)
    { bitmap_choke_length_quant, DISPLAY_WIDTH, DISPLAY_HEIGHT }, // BitmapID::FREEZE_LENGTH_QUANT (placeholder: reuse choke bitmap)
    { bitmap_choke_onset_free, DISPLAY_WIDTH, DISPLAY_HEIGHT },   // BitmapID::FREEZE_ONSET_FREE (placeholder: reuse choke bitmap)
    { bitmap_choke_onset_quant, DISPLAY_WIDTH, DISPLAY_HEIGHT },  // BitmapID::FREEZE_ONSET_QUANT (placeholder: reuse choke bitmap)
    // Add more bitmaps here as needed
};

static constexpr uint8_t NUM_BITMAPS = sizeof(bitmapRegistry) / sizeof(BitmapData);

// =============================================================================
// INTERNAL HELPERS
// =============================================================================

/**
 * Draw bitmap to display
 *
 * @param id Bitmap ID to draw
 */
static void drawBitmap(BitmapID id) {
    uint8_t index = static_cast<uint8_t>(id);

    // Bounds check
    if (index >= NUM_BITMAPS) {
        Serial.print("ERROR: Invalid bitmap ID: ");
        Serial.println(index);
        return;
    }

    const BitmapData& bitmap = bitmapRegistry[index];

    // Clear display buffer
    display.clearDisplay();

    // Draw bitmap (full screen, top-left origin)
    display.drawBitmap(0, 0, bitmap.data, bitmap.width, bitmap.height, WHITE);

    // Push to display
    display.display();

    // Update state
    currentBitmap = id;
}

// =============================================================================
// PUBLIC API
// =============================================================================

bool DisplayIO::begin() {
    // Initialize Wire1 (I2C bus 1: SDA1=pin 17, SCL1=pin 16)
    Wire1.begin();
    Wire1.setClock(400000);  // 400kHz I2C speed (fast mode)

    // Initialize SSD1306 display
    if (!display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_I2C_ADDR)) {
        Serial.println("ERROR: SSD1306 display not detected on I2C!");
        return false;
    }

    // Clear display
    display.clearDisplay();
    display.display();

    // Show default bitmap
    drawBitmap(BitmapID::DEFAULT);

    Serial.println("DisplayIO: SSD1306 display initialized (I2C 0x3C on Wire1)");
    return true;
}

void DisplayIO::threadLoop() {
    /**
     * DISPLAY UPDATE STRATEGY:
     *
     * 1. Drain command queue
     * 2. Execute commands (bitmap switches)
     * 3. Sleep when idle (low CPU usage)
     *
     * PERFORMANCE:
     * - Full screen update: ~20-30ms (acceptable, not audio critical)
     * - Sleep 50ms when queue empty
     * - Thread priority: Lower than audio/MIDI
     */

    for (;;) {
        DisplayEvent event;
        bool hadWork = false;

        // Drain command queue
        while (commandQueue.pop(event)) {
            hadWork = true;

            switch (event.command) {
                case DisplayCommand::SHOW_DEFAULT:
                    drawBitmap(BitmapID::DEFAULT);
                    break;

                case DisplayCommand::SHOW_CHOKE:
                    drawBitmap(BitmapID::CHOKE_ACTIVE);
                    break;

                case DisplayCommand::SHOW_CUSTOM:
                    drawBitmap(event.bitmapID);
                    break;
            }
        }

        // Sleep when idle (reduce CPU usage)
        if (!hadWork) {
            threads.delay(IDLE_DELAY_MS);
        }
    }
}

void DisplayIO::showDefault() {
    DisplayEvent event(DisplayCommand::SHOW_DEFAULT);
    commandQueue.push(event);
}

void DisplayIO::showChoke() {
    DisplayEvent event(DisplayCommand::SHOW_CHOKE);
    commandQueue.push(event);
}

void DisplayIO::showBitmap(BitmapID id) {
    DisplayEvent event(DisplayCommand::SHOW_CUSTOM, id);
    commandQueue.push(event);
}

BitmapID DisplayIO::getCurrentBitmap() {
    return currentBitmap;
}
