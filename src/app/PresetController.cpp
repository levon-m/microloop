#include "PresetController.h"
#include "SdCardStorage.h"
#include "Timebase.h"
#include <Arduino.h>
#include <TeensyThreads.h>

// Debug logging control - set to 0 for minimal output in production
#define PRESET_DEBUG 0

// Static member definitions
constexpr uint8_t PresetController::PRESET_LED_PINS[4];

PresetController::PresetController(StutterAudio& stutter)
    : m_stutter(stutter),
      m_sdCardPresent(false),
      m_selectedPreset(0),
      m_funcHeld(false),
      m_funcReleaseTime(0) {
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
        Serial.println("PresetController: SD card not present - preset feature disabled");
        return false;
    }

    // Scan for existing preset files
    for (uint8_t i = 0; i < 4; i++) {
        m_presetExists[i] = SdCardStorage::presetExists(i + 1);
        if (m_presetExists[i]) {
            // Turn on LED for existing preset (solid = written, not selected)
            digitalWrite(PRESET_LED_PINS[i], HIGH);
#if PRESET_DEBUG
            Serial.print("PresetController: Found preset ");
            Serial.println(i + 1);
#endif
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
        return;
    }

    // Check if stutter is in idle state (required for all preset actions)
    if (!isStutterIdle()) {
#if PRESET_DEBUG
        Serial.print("PresetController: Action blocked - stutter state=");
        Serial.println(static_cast<int>(m_stutter.getState()));
#endif
        return;
    }

    uint8_t index = slot - 1;
    bool slotHasData = m_presetExists[index];
    bool funcHeld = isFuncEffectivelyHeld();

    if (funcHeld) {
        // FUNC held - either save or delete
        if (slotHasData) {
            // FUNC + written preset = DELETE
            executeDelete(slot);
        } else {
            // FUNC + empty preset = SAVE (only if we have a loop)
            if (m_stutter.getState() == StutterState::IDLE_WITH_LOOP) {
                executeSave(slot);
            }
        }
    } else {
        // No FUNC - load preset if slot has data
        if (slotHasData) {
            // Click written preset = LOAD and SELECT
            executeLoad(slot);
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
#if PRESET_DEBUG
        Serial.print("PresetController: Capture complete - deselecting preset ");
        Serial.println(m_selectedPreset);
#endif
        deselectPreset();
    }
}

void PresetController::updateLEDs(bool beatLedState) {
    for (uint8_t i = 0; i < 4; i++) {
        if (!m_presetExists[i]) {
            // Empty preset - LED off
            digitalWrite(PRESET_LED_PINS[i], LOW);
        } else if (m_selectedPreset == (i + 1)) {
            // Selected preset - beat-synced blink
            digitalWrite(PRESET_LED_PINS[i], beatLedState ? HIGH : LOW);
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
        Serial.println("PresetController: Save failed - no loop data");
        return;
    }

    // Stop threading for SD operation (prevents context switches during SD I/O)
    int prevState = threads.stop();

    // Execute synchronous save (no thread switches will occur during this)
    SdCardStorage::SdResult result = SdCardStorage::saveSync(slot, bufferL, bufferR, length);

    // Restart threading system
    threads.start(prevState);

    if (result == SdCardStorage::SdResult::SUCCESS) {
        m_presetExists[index] = true;
        m_selectedPreset = slot;  // Auto-select after save
        Serial.print("PresetController: Saved preset ");
        Serial.println(slot);
    } else {
        Serial.print("PresetController: Save failed - error ");
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
        Serial.println("PresetController: Load failed - buffer error");
        return;
    }

    // Stop threading for SD operation
    int prevState = threads.stop();

    // Execute synchronous load
    uint32_t outLength = 0;
    SdCardStorage::SdResult result = SdCardStorage::loadSync(slot, bufferL, bufferR, outLength);

    // Restart threading
    threads.start(prevState);

    if (result == SdCardStorage::SdResult::SUCCESS && outLength > 0) {
        // Update StutterAudio with loaded data
        m_stutter.setCaptureLength(outLength);
        m_stutter.setStateWithLoop();  // Transition to IDLE_WITH_LOOP

        // Select this preset
        m_selectedPreset = slot;

        Serial.print("PresetController: Loaded preset ");
        Serial.print(slot);
        Serial.print(" (");
        Serial.print(outLength);
        Serial.println(" samples)");
    } else {
        Serial.print("PresetController: Load failed - error ");
        Serial.println(static_cast<int>(result));
    }
}

void PresetController::executeDelete(uint8_t slot) {
    if (slot < 1 || slot > 4) {
        return;
    }

    uint8_t index = slot - 1;

    // Stop threading for SD operation
    int prevState = threads.stop();

    // Execute synchronous delete
    SdCardStorage::SdResult result = SdCardStorage::deleteSync(slot);

    // Restart threading
    threads.start(prevState);

    if (result == SdCardStorage::SdResult::SUCCESS) {
        m_presetExists[index] = false;

        // If this was the selected preset, deselect it
        if (m_selectedPreset == slot) {
            m_selectedPreset = 0;
        }

        // Turn off LED
        digitalWrite(PRESET_LED_PINS[index], LOW);

        Serial.print("PresetController: Deleted preset ");
        Serial.println(slot);
    } else {
        Serial.print("PresetController: Delete failed - error ");
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
