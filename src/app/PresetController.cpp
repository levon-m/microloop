#include "PresetController.h"
#include "SdCardStorage.h"
#include "Timebase.h"
#include <Arduino.h>
#include <TeensyThreads.h>

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

    Serial.print("PresetController: slotHasData=");
    Serial.print(slotHasData);
    Serial.print(", funcHeld=");
    Serial.println(funcHeld);

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
    // Debug point A: Entry
    static uint32_t s_debugCallCount = 0;
    s_debugCallCount++;

    // Only print debug for first few calls after a save
    static bool s_recentSave = false;
    static uint32_t s_saveDebugTime = 0;
    static uint8_t s_lastSelected = 0;

    // Detect save event
    if (m_selectedPreset != 0 && s_lastSelected == 0) {
        s_recentSave = true;
        s_saveDebugTime = millis();
        Serial.println("PresetController::updateLEDs [A] Entry - FIRST CALL AFTER SAVE");
    }

    // Print detailed debug for 3 seconds after save
    bool printDebug = s_recentSave && (millis() - s_saveDebugTime < 3000);

    if (printDebug) {
        Serial.print("PresetController::updateLEDs [B] call #");
        Serial.println(s_debugCallCount);
    }

    // Sync with the beat LED pin directly
    if (printDebug) Serial.println("PresetController::updateLEDs [C] About to digitalRead(38)");
    bool beatLedOn = (digitalRead(38) == HIGH);
    if (printDebug) {
        Serial.print("PresetController::updateLEDs [D] digitalRead(38) returned: ");
        Serial.println(beatLedOn);
    }

    // Periodic debug (every 5 seconds) to verify this is being called
    static uint32_t lastDebugTime = 0;
    uint32_t now = millis();
    if (now - lastDebugTime >= 5000) {
        lastDebugTime = now;
        Serial.print("PresetController::updateLEDs [PERIODIC] - selectedPreset=");
        Serial.print(m_selectedPreset);
        Serial.print(", exists=[");
        for (uint8_t i = 0; i < 4; i++) {
            Serial.print(m_presetExists[i] ? "1" : "0");
        }
        Serial.print("], beatLedOn=");
        Serial.println(beatLedOn);
    }

    if (printDebug) Serial.println("PresetController::updateLEDs [E] About to enter LED loop");

    for (uint8_t i = 0; i < 4; i++) {
        if (printDebug) {
            Serial.print("PresetController::updateLEDs [F] Processing LED ");
            Serial.println(i);
        }

        if (!m_presetExists[i]) {
            // Empty preset - LED off
            if (printDebug) Serial.println("PresetController::updateLEDs [F1] digitalWrite LOW (empty)");
            digitalWrite(PRESET_LED_PINS[i], LOW);
            if (printDebug) Serial.println("PresetController::updateLEDs [F1-done]");
        } else if (m_selectedPreset == (i + 1)) {
            // Selected preset - beat-synced blink
            if (printDebug) {
                Serial.print("PresetController::updateLEDs [F2] digitalWrite ");
                Serial.print(beatLedOn ? "HIGH" : "LOW");
                Serial.println(" (selected, beat-sync)");
            }
            digitalWrite(PRESET_LED_PINS[i], beatLedOn ? HIGH : LOW);
            if (printDebug) Serial.println("PresetController::updateLEDs [F2-done]");
        } else {
            // Written but not selected - LED on solid
            if (printDebug) Serial.println("PresetController::updateLEDs [F3] digitalWrite HIGH (written)");
            digitalWrite(PRESET_LED_PINS[i], HIGH);
            if (printDebug) Serial.println("PresetController::updateLEDs [F3-done]");
        }
    }

    if (printDebug) Serial.println("PresetController::updateLEDs [G] LED loop complete");

    s_lastSelected = m_selectedPreset;

    // Turn off debug after 3 seconds
    if (s_recentSave && (millis() - s_saveDebugTime >= 3000)) {
        s_recentSave = false;
        Serial.println("PresetController::updateLEDs [Z] Debug period complete");
    }

    if (printDebug) Serial.println("PresetController::updateLEDs [H] Exit");
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

    Serial.println("PresetController: [DEBUG] About to stop threading for SD save");

    // CRITICAL FIX: SD library is not thread-safe
    // Per TeensyThreads author recommendation: stop threading around SD I/O
    // This prevents context switches during SD operations which can corrupt state
    int prevState = threads.stop();

    Serial.print("PresetController: [DEBUG] Threading stopped, prevState=");
    Serial.print(prevState);
    Serial.println(", calling SD save");

    // Execute synchronous save (no thread switches will occur during this)
    SdCardStorage::SdResult result = SdCardStorage::saveSync(slot, bufferL, bufferR, length);

    Serial.print("PresetController: [DEBUG] SD save complete, restarting threading");
    Serial.print(" (prevState was ");
    Serial.print(prevState);
    Serial.println(")");

    // Restart threading system
    // NOTE: Don't pass prevState - just restart with default state
    int startResult = threads.start();

    Serial.print("PresetController: [DEBUG] threads.start() returned: ");
    Serial.println(startResult);

    if (result == SdCardStorage::SdResult::SUCCESS) {
        m_presetExists[index] = true;
        m_selectedPreset = slot;  // Auto-select after save
        Serial.print("PresetController: Save complete - preset ");
        Serial.println(slot);
        // LED will be updated in next updateLEDs() call
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
