#include "display_io.h"
#include "bitmaps.h"
#include "spsc_queue.h"
#include "trace.h"
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <TeensyThreads.h>
#include <Wire.h>

static constexpr uint8_t DISPLAY_I2C_ADDR = 0x3C;  // Default SSD1306 address
static constexpr uint8_t DISPLAY_WIDTH = 128;
static constexpr uint8_t DISPLAY_HEIGHT = 64;
static constexpr int8_t RESET_PIN = -1;  // No reset pin (using I2C reset)

static constexpr uint32_t IDLE_DELAY_MS = 50;  // Delay when queue empty (low CPU usage)

static Adafruit_SSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire1, RESET_PIN);

static SPSCQueue<DisplayEvent, 16> commandQueue;

static volatile BitmapID currentBitmap = BitmapID::DEFAULT;

struct BitmapData {
    const uint8_t* data;  // Pointer to PROGMEM bitmap array
};

static const BitmapData bitmapRegistry[] = {
    { bitmap_default },            // BitmapID::DEFAULT
    { bitmap_freeze_active },      // BitmapID::FREEZE_ACTIVE
    { bitmap_choke_active },       // BitmapID::CHOKE_ACTIVE
    { bitmap_quant_32 },           // BitmapID::QUANT_32
    { bitmap_quant_16 },           // BitmapID::QUANT_16
    { bitmap_quant_8 },            // BitmapID::QUANT_8
    { bitmap_quant_4 },            // BitmapID::QUANT_4
    { bitmap_choke_length_free },  // BitmapID::CHOKE_LENGTH_FREE
    { bitmap_choke_length_quant }, // BitmapID::CHOKE_LENGTH_QUANT
    { bitmap_choke_onset_free },   // BitmapID::CHOKE_ONSET_FREE
    { bitmap_choke_onset_quant },  // BitmapID::CHOKE_ONSET_QUANT
    { bitmap_choke_length_free },  // BitmapID::FREEZE_LENGTH_FREE (placeholder: reuse choke bitmap)
    { bitmap_choke_length_quant }, // BitmapID::FREEZE_LENGTH_QUANT (placeholder: reuse choke bitmap)
    { bitmap_choke_onset_free },   // BitmapID::FREEZE_ONSET_FREE (placeholder: reuse choke bitmap)
    { bitmap_choke_onset_quant },  // BitmapID::FREEZE_ONSET_QUANT (placeholder: reuse choke bitmap)
    { bitmap_stutter_idle_with_loop },      // BitmapID::STUTTER_IDLE_WITH_LOOP
    { bitmap_stutter_capturing },            // BitmapID::STUTTER_CAPTURING
    { bitmap_stutter_playing },              // BitmapID::STUTTER_PLAYING
    { bitmap_stutter_onset_free },           // BitmapID::STUTTER_ONSET_FREE
    { bitmap_stutter_onset_quant },          // BitmapID::STUTTER_ONSET_QUANT
    { bitmap_stutter_length_free },          // BitmapID::STUTTER_LENGTH_FREE
    { bitmap_stutter_length_quant },         // BitmapID::STUTTER_LENGTH_QUANT
    { bitmap_stutter_capture_start_free },   // BitmapID::STUTTER_CAPTURE_START_FREE
    { bitmap_stutter_capture_start_quant },  // BitmapID::STUTTER_CAPTURE_START_QUANT
    { bitmap_stutter_capture_end_free },     // BitmapID::STUTTER_CAPTURE_END_FREE
    { bitmap_stutter_capture_end_quant },    // BitmapID::STUTTER_CAPTURE_END_QUANT
};

static constexpr uint8_t NUM_BITMAPS = sizeof(bitmapRegistry) / sizeof(BitmapData);

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
    display.drawBitmap(0, 0, bitmap.data, DISPLAY_WIDTH, DISPLAY_HEIGHT, WHITE);

    // Push to display
    display.display();

    // Update state
    currentBitmap = id;
}

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