#include "PresetController.h"
#include "SdCardStorage.h"
#include "Ssd1306Display.h"
#include "Timebase.h"
#include <Arduino.h>

// Static member definitions
constexpr uint8_t PresetController::PRESET_LED_PINS[4];
PresetController* PresetController::s_instance = nullptr;
SpscQueue<SdResultEvent, 4> PresetController::s_eventQueue;

PresetController::PresetController(StutterAudio& stutter)
    : m_stutter(stutter),
      m_sdCardPresent(false),
      m_selectedPreset(0),
      m_funcHeld(false),
      m_funcReleaseTime(0),
      m_lastBeatLedState(false),
      m_operationInProgress(false),
      m_pendingSlot(0) {
    // Initialize preset existence array
    for (int i = 0; i < 4; i++) {
        m_presetExists[i] = false;
    }

    // Set singleton instance for callbacks
    s_instance = this;
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

    // Check if SD card is available
    if (!m_sdCardPresent) {
        showError("No SD Card");
        return;
    }

    // Check if an operation is already in progress
    if (m_operationInProgress || SdCardStorage::isBusy()) {
        Serial.println("PresetController: Operation in progress, ignoring button");
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
            if (requestDelete(slot)) {
                showStatus("Deleting...");
                Serial.print("PresetController: Deleting preset ");
                Serial.println(slot);
            }
        } else {
            // FUNC + empty preset = SAVE (only if we have a loop)
            if (m_stutter.getState() == StutterState::IDLE_WITH_LOOP) {
                if (requestSave(slot)) {
                    showStatus("Saving...");
                    Serial.print("PresetController: Saving to preset ");
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
            if (requestLoad(slot)) {
                showStatus("Loading...");
                Serial.print("PresetController: Loading preset ");
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

bool PresetController::requestSave(uint8_t slot) {
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

    m_operationInProgress = true;
    m_pendingSlot = slot;

    // Request async save
    if (!SdCardStorage::requestSave(slot, bufferL, bufferR, length, onSaveComplete)) {
        m_operationInProgress = false;
        showError("Queue full");
        return false;
    }

    return true;
}

bool PresetController::requestLoad(uint8_t slot) {
    if (slot < 1 || slot > 4) {
        return false;
    }

    // Get buffer pointers from StutterAudio
    int16_t* bufferL = m_stutter.getBufferL();
    int16_t* bufferR = m_stutter.getBufferR();

    if (!bufferL || !bufferR) {
        showError("Buffer error");
        return false;
    }

    m_operationInProgress = true;
    m_pendingSlot = slot;

    // Request async load
    if (!SdCardStorage::requestLoad(slot, bufferL, bufferR, onLoadComplete)) {
        m_operationInProgress = false;
        showError("Queue full");
        return false;
    }

    return true;
}

bool PresetController::requestDelete(uint8_t slot) {
    if (slot < 1 || slot > 4) {
        return false;
    }

    m_operationInProgress = true;
    m_pendingSlot = slot;

    // Request async delete
    if (!SdCardStorage::requestDelete(slot, onDeleteComplete)) {
        m_operationInProgress = false;
        showError("Queue full");
        return false;
    }

    return true;
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

void PresetController::showStatus(const char* message) {
    // Use MenuDisplayData to show status on OLED
    MenuDisplayData statusData;
    statusData.topText = "PRESET";
    statusData.middleText = message;
    statusData.numOptions = 0;  // No selection circles for status
    statusData.selectedIndex = 0;

    Ssd1306Display::showMenu(statusData);
}

// ========== POLL SD EVENTS (App thread) ==========

void PresetController::pollSdEvents() {
    // Drain all pending events from the lock-free queue (usually just 1)
    SdResultEvent ev;
    while (s_eventQueue.pop(ev)) {
        // All operations clear the in-progress flag
        m_operationInProgress = false;

        uint8_t slot = ev.slot;

        // Validate slot before array access
        if (slot < 1 || slot > 4) {
            Serial.println("PresetController: Ignoring event with invalid slot");
            continue;
        }
        uint8_t index = slot - 1;

        switch (ev.op) {
            case SdCardStorage::SdOperation::SAVE:
                if (ev.result == SdCardStorage::SdResult::SUCCESS) {
                    m_presetExists[index] = true;
                    m_selectedPreset = slot;  // Auto-select after save

                    Serial.print("PresetController: Save complete - preset ");
                    Serial.println(slot);
                    showStatus("Saved!");
                } else {
                    Serial.print("PresetController: Save failed with error ");
                    Serial.println(static_cast<int>(ev.result));
                    showError("Write failed");
                }
                break;

            case SdCardStorage::SdOperation::LOAD:
                if (ev.result == SdCardStorage::SdResult::SUCCESS && ev.length > 0) {
                    // Update StutterAudio with loaded data
                    m_stutter.setCaptureLength(ev.length);
                    m_stutter.setStateWithLoop();  // Transition to IDLE_WITH_LOOP

                    // Select this preset
                    m_selectedPreset = slot;

                    Serial.print("PresetController: Load complete - preset ");
                    Serial.print(slot);
                    Serial.print(" (");
                    Serial.print(ev.length);
                    Serial.println(" samples)");
                    showStatus("Loaded!");
                } else {
                    Serial.print("PresetController: Load failed with error ");
                    Serial.println(static_cast<int>(ev.result));
                    showError("Read failed");
                }
                break;

            case SdCardStorage::SdOperation::DELETE:
                if (ev.result == SdCardStorage::SdResult::SUCCESS) {
                    m_presetExists[index] = false;

                    // If this was the selected preset, deselect it
                    if (m_selectedPreset == slot) {
                        m_selectedPreset = 0;
                    }

                    // Turn off LED
                    digitalWrite(PRESET_LED_PINS[index], LOW);

                    Serial.print("PresetController: Delete complete - preset ");
                    Serial.println(slot);
                    showStatus("Deleted!");
                } else {
                    Serial.print("PresetController: Delete failed with error ");
                    Serial.println(static_cast<int>(ev.result));
                    showError("Delete failed");
                }
                break;

            default:
                break;
        }
    }
}

// ========== STATIC CALLBACK HANDLERS ==========
// These run in the SD thread - push event to lock-free queue for App thread.
// Do NOT touch any other state, display, audio, or LEDs (those are App thread concerns)

void PresetController::onSaveComplete(SdCardStorage::SdOperation op, uint8_t slot,
                                      SdCardStorage::SdResult result, uint32_t length) {
    SdResultEvent ev{op, slot, result, length};
    // Push to lock-free queue - safe to call from SD thread
    // Queue size 4 should never overflow with one-at-a-time operations
    s_eventQueue.push(ev);
}

void PresetController::onLoadComplete(SdCardStorage::SdOperation op, uint8_t slot,
                                      SdCardStorage::SdResult result, uint32_t length) {
    SdResultEvent ev{op, slot, result, length};
    s_eventQueue.push(ev);
}

void PresetController::onDeleteComplete(SdCardStorage::SdOperation op, uint8_t slot,
                                        SdCardStorage::SdResult result, uint32_t length) {
    SdResultEvent ev{op, slot, result, length};
    s_eventQueue.push(ev);
}
