#include "PresetController.h"
#include "SdCardStorage.h"
#include "Ssd1306Display.h"
#include "Timebase.h"
#include <Arduino.h>

// Static constexpr member definition
constexpr uint8_t PresetController::PRESET_LED_PINS[4];

PresetController::PresetController(StutterAudio& stutter)
    : m_stutter(stutter),
      m_sdCardPresent(false),
      m_selectedPreset(0),
      m_funcHeld(false),
      m_funcReleaseTime(0),
      m_lastBeatLedState(false) {
    // Initialize preset existence array
    for (int i = 0; i < 4; i++) {
        m_presetExists[i] = false;
    }
}

bool PresetController::begin() {
    // Configure LED pins as outputs
    for (int i = 0; i < 4; i++) {
        pinMode(PRESET_LED_PINS[i], OUTPUT);
        digitalWrite(PRESET_LED_PINS[i], LOW);
    }

    // Initialize SD card
    m_sdCardPresent = SdCardStorage::begin();

    if (!m_sdCardPresent) {
        // Show error on display
        showError("No SD Card");
        Serial.println("PresetController: SD card not present - preset feature disabled");
        return false;
    }

    // Scan for existing preset files
    for (uint8_t i = 0; i < 4; i++) {
        m_presetExists[i] = SdCardStorage::presetExists(i + 1);
        if (m_presetExists[i]) {
            // Turn on LED for existing preset (solid = written, not selected)
            digitalWrite(PRESET_LED_PINS[i], HIGH);
            Serial.print("PresetController: Found preset ");
            Serial.println(i + 1);
        }
    }

    // No preset selected at startup
    m_selectedPreset = 0;

    Serial.println("PresetController: Initialized");
    return true;
}

void PresetController::handleButtonPress(uint8_t slot) {
    if (slot < 1 || slot > 4) {
        return;
    }

    // Check if SD card is available
    if (!m_sdCardPresent) {
        showError("No SD Card");
        return;
    }

    // Check if stutter is in idle state (required for all preset actions)
    if (!isStutterIdle()) {
        Serial.println("PresetController: Action blocked - stutter not idle");
        return;
    }

    uint8_t index = slot - 1;
    bool slotHasData = m_presetExists[index];
    bool funcHeld = isFuncEffectivelyHeld();

    if (funcHeld) {
        // FUNC held - either save or delete
        if (slotHasData) {
            // FUNC + written preset = DELETE
            if (deletePresetSlot(slot)) {
                Serial.print("PresetController: Deleted preset ");
                Serial.println(slot);
            }
        } else {
            // FUNC + empty preset = SAVE (only if we have a loop)
            if (m_stutter.getState() == StutterState::IDLE_WITH_LOOP) {
                if (saveToPreset(slot)) {
                    Serial.print("PresetController: Saved to preset ");
                    Serial.println(slot);
                }
            } else {
                Serial.println("PresetController: Cannot save - no loop captured");
            }
        }
    } else {
        // No FUNC - select/load preset
        if (slotHasData) {
            // Click written preset = LOAD and SELECT
            if (loadFromPreset(slot)) {
                Serial.print("PresetController: Loaded preset ");
                Serial.println(slot);
            }
        } else {
            // Click empty preset - do nothing
            Serial.print("PresetController: Preset ");
            Serial.print(slot);
            Serial.println(" is empty");
        }
    }
}

void PresetController::handleButtonRelease(uint8_t slot) {
    // Currently no action on release
    (void)slot;
}

void PresetController::handleFuncPress() {
    m_funcHeld = true;
}

void PresetController::handleFuncRelease() {
    m_funcHeld = false;
    m_funcReleaseTime = millis();
}

void PresetController::onCaptureComplete() {
    // User captured a new loop - deselect any current preset
    // The new loop is now "scratch work" not associated with any preset
    if (m_selectedPreset != 0) {
        Serial.print("PresetController: Capture complete - deselecting preset ");
        Serial.println(m_selectedPreset);
        deselectPreset();
    }
}

void PresetController::updateLEDs() {
    // Get current beat state for sync (use same logic as beat LED in App.cpp)
    // We check if we're in the "on" portion of the beat pulse
    bool beatActive = Timebase::pollBeatFlag();  // This consumes the flag, so we need different approach

    // Alternative: Check if we're within the beat pulse window
    // For simplicity, we'll use a static flag that gets set by the beat indicator
    // and track it ourselves

    // Actually, let's sync with the beat LED pin directly
    bool beatLedOn = (digitalRead(38) == HIGH);

    for (uint8_t i = 0; i < 4; i++) {
        if (!m_presetExists[i]) {
            // Empty preset - LED off
            digitalWrite(PRESET_LED_PINS[i], LOW);
        } else if (m_selectedPreset == (i + 1)) {
            // Selected preset - beat-synced blink
            digitalWrite(PRESET_LED_PINS[i], beatLedOn ? HIGH : LOW);
        } else {
            // Written but not selected - LED on solid
            digitalWrite(PRESET_LED_PINS[i], HIGH);
        }
    }
}

bool PresetController::presetExists(uint8_t slot) const {
    if (slot < 1 || slot > 4) {
        return false;
    }
    return m_presetExists[slot - 1];
}

bool PresetController::isFuncEffectivelyHeld() const {
    if (m_funcHeld) {
        return true;
    }
    // Check grace period
    uint32_t elapsed = millis() - m_funcReleaseTime;
    return elapsed < FUNC_GRACE_MS;
}

bool PresetController::isStutterIdle() const {
    StutterState state = m_stutter.getState();
    return (state == StutterState::IDLE_NO_LOOP || state == StutterState::IDLE_WITH_LOOP);
}

bool PresetController::saveToPreset(uint8_t slot) {
    if (slot < 1 || slot > 4) {
        return false;
    }

    // Get buffer pointers and length from StutterAudio
    int16_t* bufferL = m_stutter.getBufferL();
    int16_t* bufferR = m_stutter.getBufferR();
    uint32_t length = m_stutter.getCaptureLength();

    if (!bufferL || !bufferR || length == 0) {
        showError("No loop data");
        return false;
    }

    // Save to SD card
    bool success = SdCardStorage::savePreset(slot, bufferL, bufferR, length);

    if (success) {
        uint8_t index = slot - 1;
        m_presetExists[index] = true;
        m_selectedPreset = slot;  // Auto-select after save
        // LED update will happen in updateLEDs()
    } else {
        showError("Write failed");
    }

    return success;
}

bool PresetController::loadFromPreset(uint8_t slot) {
    if (slot < 1 || slot > 4) {
        return false;
    }

    // Get buffer pointers from StutterAudio
    int16_t* bufferL = m_stutter.getBufferL();
    int16_t* bufferR = m_stutter.getBufferR();
    uint32_t length = 0;

    // Load from SD card
    bool success = SdCardStorage::loadPreset(slot, bufferL, bufferR, length);

    if (success) {
        // Update StutterAudio with loaded data
        m_stutter.setCaptureLength(length);
        m_stutter.setStateWithLoop();  // Transition to IDLE_WITH_LOOP

        // Select this preset
        m_selectedPreset = slot;
        // LED update will happen in updateLEDs()
    } else {
        showError("Read failed");
    }

    return success;
}

bool PresetController::deletePresetSlot(uint8_t slot) {
    if (slot < 1 || slot > 4) {
        return false;
    }

    // Delete from SD card
    bool success = SdCardStorage::deletePreset(slot);

    if (success) {
        uint8_t index = slot - 1;
        m_presetExists[index] = false;

        // If this was the selected preset, deselect it
        if (m_selectedPreset == slot) {
            m_selectedPreset = 0;
        }

        // Turn off LED
        digitalWrite(PRESET_LED_PINS[index], LOW);
    } else {
        showError("Delete failed");
    }

    return success;
}

void PresetController::deselectPreset() {
    if (m_selectedPreset != 0) {
        uint8_t index = m_selectedPreset - 1;
        m_selectedPreset = 0;

        // If the slot still has data, set LED to solid (not blinking)
        if (m_presetExists[index]) {
            digitalWrite(PRESET_LED_PINS[index], HIGH);
        }
    }
}

void PresetController::showError(const char* message) {
    // Use MenuDisplayData to show error on OLED
    MenuDisplayData errorData;
    errorData.topText = "ERROR";
    errorData.middleText = message;
    errorData.numOptions = 0;  // No selection circles for error
    errorData.selectedIndex = 0;

    Ssd1306Display::showMenu(errorData);

    Serial.print("PresetController ERROR: ");
    Serial.println(message);
}
