#include "PresetController.h"
#include "SdCardStorage.h"
#include "Timebase.h"
#include <Arduino.h>

// Static member definitions
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

    // Check if SD card is present (already initialized by SdCardStorage::begin())
    m_sdCardPresent = SdCardStorage::isCardPresent();

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

    Serial.print("PresetController: Button ");
    Serial.print(slot);
    Serial.print(" pressed, selected=");
    Serial.println(m_selectedPreset);

    // Check if SD card is available
    if (!m_sdCardPresent) {
        showError("No SD Card");
        return;
    }

    // Check if stutter is in idle state (required for all preset actions)
    if (!isStutterIdle()) {
        Serial.print("PresetController: Action blocked - stutter state=");
        Serial.println(static_cast<int>(m_stutter.getState()));
        return;
    }

    uint8_t index = slot - 1;
    bool slotHasData = m_presetExists[index];
    bool funcHeld = isFuncEffectivelyHeld();

    if (funcHeld) {
        // FUNC held - either save or delete
        if (slotHasData) {
            // FUNC + written preset = DELETE
            Serial.print("PresetController: Deleting preset ");
            Serial.println(slot);
            executeDelete(slot);
        } else {
            // FUNC + empty preset = SAVE (only if we have a loop)
            if (m_stutter.getState() == StutterState::IDLE_WITH_LOOP) {
                Serial.print("PresetController: Saving to preset ");
                Serial.println(slot);
                executeSave(slot);
            } else {
                Serial.println("PresetController: Cannot save - no loop captured");
            }
        }
    } else {
        // No FUNC - select/load preset
        if (slotHasData) {
            // Click written preset = LOAD and SELECT
            Serial.print("PresetController: Loading preset ");
            Serial.println(slot);
            executeLoad(slot);
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
    Serial.print("PresetController: onCaptureComplete called, m_selectedPreset=");
    Serial.println(m_selectedPreset);
    if (m_selectedPreset != 0) {
        Serial.print("PresetController: Capture complete - deselecting preset ");
        Serial.println(m_selectedPreset);
        deselectPreset();
    }
}

void PresetController::updateLEDs() {
    // Sync with the beat LED pin directly
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

void PresetController::executeSave(uint8_t slot) {
    if (slot < 1 || slot > 4) {
        return;
    }

    uint8_t index = slot - 1;

    // Get buffer pointers and length from StutterAudio
    int16_t* bufferL = m_stutter.getBufferL();
    int16_t* bufferR = m_stutter.getBufferR();
    uint32_t length = m_stutter.getCaptureLength();

    if (!bufferL || !bufferR || length == 0) {
        Serial.println("PresetController ERROR: No loop data");
        return;
    }

    Serial.println("PresetController: Saving...");

    // Execute synchronous save
    SdCardStorage::SdResult result = SdCardStorage::saveSync(slot, bufferL, bufferR, length);

    if (result == SdCardStorage::SdResult::SUCCESS) {
        m_presetExists[index] = true;
        m_selectedPreset = slot;  // Auto-select after save
        Serial.print("PresetController: Save complete - preset ");
        Serial.println(slot);
    } else {
        Serial.print("PresetController: Save failed with error ");
        Serial.println(static_cast<int>(result));
    }
}

void PresetController::executeLoad(uint8_t slot) {
    if (slot < 1 || slot > 4) {
        return;
    }

    // Get buffer pointers from StutterAudio
    int16_t* bufferL = m_stutter.getBufferL();
    int16_t* bufferR = m_stutter.getBufferR();

    if (!bufferL || !bufferR) {
        Serial.println("PresetController ERROR: Buffer error");
        return;
    }

    Serial.println("PresetController: Loading...");

    // Execute synchronous load
    uint32_t outLength = 0;
    SdCardStorage::SdResult result = SdCardStorage::loadSync(slot, bufferL, bufferR, outLength);

    if (result == SdCardStorage::SdResult::SUCCESS && outLength > 0) {
        // Update StutterAudio with loaded data
        m_stutter.setCaptureLength(outLength);
        m_stutter.setStateWithLoop();  // Transition to IDLE_WITH_LOOP

        // Select this preset
        m_selectedPreset = slot;

        Serial.print("PresetController: Load complete - preset ");
        Serial.print(slot);
        Serial.print(" (");
        Serial.print(outLength);
        Serial.println(" samples)");
    } else {
        Serial.print("PresetController: Load failed with error ");
        Serial.println(static_cast<int>(result));
    }
}

void PresetController::executeDelete(uint8_t slot) {
    if (slot < 1 || slot > 4) {
        return;
    }

    uint8_t index = slot - 1;

    Serial.println("PresetController: Deleting...");

    // Execute synchronous delete
    SdCardStorage::SdResult result = SdCardStorage::deleteSync(slot);

    if (result == SdCardStorage::SdResult::SUCCESS) {
        m_presetExists[index] = false;

        // If this was the selected preset, deselect it
        if (m_selectedPreset == slot) {
            m_selectedPreset = 0;
        }

        // Turn off LED
        digitalWrite(PRESET_LED_PINS[index], LOW);

        Serial.print("PresetController: Delete complete - preset ");
        Serial.println(slot);
    } else {
        Serial.print("PresetController: Delete failed with error ");
        Serial.println(static_cast<int>(result));
    }
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
    // Serial only - no OLED during preset operations to avoid stack issues
    Serial.print("PresetController ERROR: ");
    Serial.println(message);
}

void PresetController::showStatus(const char* message) {
    // Serial only - no OLED during preset operations to avoid stack issues
    Serial.print("PresetController: ");
    Serial.println(message);
}
