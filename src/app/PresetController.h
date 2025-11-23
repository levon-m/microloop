/**
 * PresetController.h - Controller for preset save/load functionality
 *
 * PURPOSE:
 * Manages preset storage and recall, including:
 * - Save current loop to SD card (FUNC + empty preset)
 * - Load preset from SD card (click written preset)
 * - Delete preset from SD card (FUNC + written preset)
 * - LED feedback for preset states
 * - Beat-synced LED blinking for selected preset
 *
 * DESIGN:
 * - Works with StutterAudio buffer via accessor methods
 * - Uses SdCardStorage HAL for async file operations
 * - Tracks FUNC button state with grace period for cross-bus timing
 * - LED states: OFF (empty), ON (written), beat-sync blink (selected)
 * - Operations are non-blocking; completion handled via callbacks
 *
 * CONSTRAINTS:
 * - All actions only allowed in IDLE states (IDLE_NO_LOOP or IDLE_WITH_LOOP)
 * - Cannot overwrite preset - must delete first then write
 * - New capture while preset selected deselects that preset
 * - Only one SD operation at a time (busy check)
 */

#pragma once

#include <Arduino.h>
#include "StutterAudio.h"
#include "SdCardStorage.h"

class PresetController {
public:
    /**
     * Constructor
     *
     * @param stutter Reference to the stutter audio effect
     */
    explicit PresetController(StutterAudio& stutter);

    /**
     * Initialize preset system
     * - Checks SD card presence
     * - Scans for existing preset files
     * - Configures LED pins
     *
     * @return true if SD card present, false otherwise (preset feature disabled)
     */
    bool begin();

    /**
     * Handle preset button press
     *
     * @param slot Preset slot (1-4)
     */
    void handleButtonPress(uint8_t slot);

    /**
     * Handle preset button release
     *
     * @param slot Preset slot (1-4)
     */
    void handleButtonRelease(uint8_t slot);

    /**
     * Handle FUNC button press (from Neokey)
     */
    void handleFuncPress();

    /**
     * Handle FUNC button release (from Neokey)
     */
    void handleFuncRelease();

    /**
     * Called when a new capture completes
     * Deselects current preset (user is now working with "scratch" loop)
     */
    void onCaptureComplete();

    /**
     * Update LED states (call from App::threadLoop)
     * Handles beat-synced blinking for selected preset
     */
    void updateLEDs();

    /**
     * Check if SD card is available for preset operations
     */
    bool isEnabled() const { return m_sdCardPresent; }

    /**
     * Get currently selected preset (0 = none, 1-4 = preset slot)
     */
    uint8_t getSelectedPreset() const { return m_selectedPreset; }

    /**
     * Check if a preset slot has data
     */
    bool presetExists(uint8_t slot) const;

    /**
     * Check if an SD operation is in progress
     */
    bool isBusy() const { return m_operationInProgress; }

private:
    StutterAudio& m_stutter;

    // SD card state
    bool m_sdCardPresent;

    // Preset existence tracking
    bool m_presetExists[4];

    // Currently selected preset (0 = none, 1-4 = selected slot)
    uint8_t m_selectedPreset;

    // FUNC button state with grace period
    bool m_funcHeld;
    uint32_t m_funcReleaseTime;
    static constexpr uint32_t FUNC_GRACE_MS = 100;

    // LED pins (directly on Teensy)
    static constexpr uint8_t PRESET_LED_PINS[4] = {29, 30, 31, 32};

    // Beat LED tracking for sync
    bool m_lastBeatLedState;

    // Async operation tracking
    volatile bool m_operationInProgress;
    uint8_t m_pendingSlot;  // Slot being operated on

    /**
     * Check if FUNC is effectively held (including grace period)
     */
    bool isFuncEffectivelyHeld() const;

    /**
     * Check if stutter is in an idle state (actions allowed)
     */
    bool isStutterIdle() const;

    /**
     * Request async save of current loop to preset slot
     */
    bool requestSave(uint8_t slot);

    /**
     * Request async load of preset into current loop buffer
     */
    bool requestLoad(uint8_t slot);

    /**
     * Request async delete of preset from SD card
     */
    bool requestDelete(uint8_t slot);

    /**
     * Deselect current preset (switch to "scratch" mode)
     */
    void deselectPreset();

    /**
     * Show error screen on display
     */
    void showError(const char* message);

    /**
     * Show status message on display
     */
    void showStatus(const char* message);

    // Static callback handlers (bridge to instance methods)
    static void onSaveComplete(SdCardStorage::SdOperation op, uint8_t slot,
                               SdCardStorage::SdResult result, uint32_t length);
    static void onLoadComplete(SdCardStorage::SdOperation op, uint8_t slot,
                               SdCardStorage::SdResult result, uint32_t length);
    static void onDeleteComplete(SdCardStorage::SdOperation op, uint8_t slot,
                                 SdCardStorage::SdResult result, uint32_t length);

    // Singleton instance pointer for static callbacks
    static PresetController* s_instance;
};
