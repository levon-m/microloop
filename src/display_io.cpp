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

static constexpr uint32_t IDLE_DELAY_MS = 5;  // Delay when queue empty (more responsive)

static Adafruit_SSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire1, RESET_PIN);

static SPSCQueue<DisplayEvent, 32> commandQueue;  // Increased from 16 to handle spam

static volatile BitmapID currentBitmap = BitmapID::DEFAULT;
static volatile bool isShowingMenu = false;  // Track if menu is currently displayed

struct BitmapData {
    const uint8_t* data;  // Pointer to PROGMEM bitmap array
};

static const BitmapData bitmapRegistry[] = {
    { bitmap_default },            // BitmapID::DEFAULT
    { bitmap_freeze_active },      // BitmapID::FREEZE_ACTIVE
    { bitmap_choke_active },       // BitmapID::CHOKE_ACTIVE
    { bitmap_stutter_active }      // BitmapID::STUTTER_ACTIVE
};

static constexpr uint8_t NUM_BITMAPS = sizeof(bitmapRegistry) / sizeof(BitmapData);

// Section heights for menu layout
static constexpr uint8_t TOP_SECTION_HEIGHT = 16;
static constexpr uint8_t MIDDLE_SECTION_HEIGHT = 32;
static constexpr uint8_t BOTTOM_SECTION_HEIGHT = 16;

// Circle indicator settings
static constexpr uint8_t INDICATOR_RADIUS = 4;
static constexpr uint8_t INDICATOR_SPACING = 12;

static void drawMenu(const MenuDisplayData& menuData) {
    isShowingMenu = true;  // Mark that menu is being displayed

    // Clear display buffer
    display.clearDisplay();

    // --- TOP SECTION (16px): Effect->Parameter text ---
    display.setTextSize(1);  // Standard 5x8 font
    display.setTextColor(WHITE);
    display.setCursor(0, 4);  // Top-left aligned, 4px from top for vertical centering
    display.print(menuData.topText);

    // --- MIDDLE SECTION (32px): Current value text ---
    display.setTextSize(2);  // 2x scaled = 10x16 font
    // Calculate center position for text
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(menuData.middleText, 0, 0, &x1, &y1, &w, &h);
    uint8_t textX = (DISPLAY_WIDTH - w) / 2;
    uint8_t textY = TOP_SECTION_HEIGHT + (MIDDLE_SECTION_HEIGHT - h) / 2;
    display.setCursor(textX, textY);
    display.print(menuData.middleText);

    // --- BOTTOM SECTION (16px): Circle indicators ---
    uint8_t bottomSectionY = TOP_SECTION_HEIGHT + MIDDLE_SECTION_HEIGHT;
    uint8_t centerY = bottomSectionY + (BOTTOM_SECTION_HEIGHT / 2);

    // Calculate starting X position to center all circles
    uint8_t totalWidth = (menuData.numOptions - 1) * INDICATOR_SPACING;
    uint8_t startX = (DISPLAY_WIDTH - totalWidth) / 2;

    // Draw circles
    for (uint8_t i = 0; i < menuData.numOptions; i++) {
        uint8_t circleX = startX + (i * INDICATOR_SPACING);

        if (i == menuData.selectedIndex) {
            // Filled circle for selected option
            display.fillCircle(circleX, centerY, INDICATOR_RADIUS, WHITE);
        } else {
            // Outline circle for unselected options
            display.drawCircle(circleX, centerY, INDICATOR_RADIUS, WHITE);
        }
    }

    // Push to display
    display.display();
}

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
    isShowingMenu = false;  // No longer showing menu
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

                case DisplayCommand::SHOW_MENU:
                    drawMenu(event.menuData);
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

void DisplayIO::showMenu(const MenuDisplayData& menuData) {
    DisplayEvent event(DisplayCommand::SHOW_MENU, menuData);
    commandQueue.push(event);
}

BitmapID DisplayIO::getCurrentBitmap() {
    return currentBitmap;
}