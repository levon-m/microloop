/**
 * SdCardStorage.h - SD card HAL for preset storage (async I/O thread)
 *
 * PURPOSE:
 * Provides a hardware abstraction layer for reading and writing preset
 * loop buffers to the microSD card on Teensy 4.1.
 *
 * DESIGN:
 * - Uses dedicated I/O thread to prevent blocking App thread
 * - SPSC queue for async command dispatch
 * - Chunked reads/writes with thread yields for scheduler fairness
 * - Callback-based completion notification
 * - Uses Teensy's built-in SD library (SDIO interface for speed)
 *
 * FILE FORMAT:
 * - [4 bytes length][left channel data][right channel data]
 * - File names: preset1.bin, preset2.bin, preset3.bin, preset4.bin
 *
 * THREAD SAFETY:
 * - Public API (requestSave, requestLoad, etc.) safe to call from any thread
 * - Actual SD operations happen only in sdThreadLoop()
 * - Callbacks execute in SD thread context
 */

#pragma once

#include <Arduino.h>

namespace SdCardStorage {

// ========== OPERATION TYPES ==========

enum class SdOperation : uint8_t {
    NONE = 0,
    SAVE = 1,
    LOAD = 2,
    DELETE = 3,
    CHECK_EXISTS = 4
};

enum class SdResult : uint8_t {
    SUCCESS = 0,
    ERROR_NO_CARD = 1,
    ERROR_INVALID_SLOT = 2,
    ERROR_INVALID_BUFFER = 3,
    ERROR_FILE_NOT_FOUND = 4,
    ERROR_FILE_CREATE = 5,
    ERROR_WRITE_FAILED = 6,
    ERROR_READ_FAILED = 7,
    ERROR_DELETE_FAILED = 8,
    ERROR_INVALID_LENGTH = 9
};

// Completion callback type
// Called from SD thread when operation completes
// Parameters: operation type, slot number, result code, loaded length (for LOAD only)
using CompletionCallback = void (*)(SdOperation op, uint8_t slot, SdResult result, uint32_t length);

// ========== INITIALIZATION ==========

/**
 * Initialize SD card (called from main setup, before thread starts)
 *
 * @return true if SD card is present and initialized, false otherwise
 */
bool begin();

/**
 * Check if SD card is present and initialized
 *
 * @return true if SD card is ready for use
 */
bool isCardPresent();

// ========== THREAD ENTRY POINT ==========

/**
 * SD card I/O thread main loop (never returns)
 * Processes queued operations with chunked I/O and yields
 */
void threadLoop();

// ========== ASYNC OPERATIONS ==========

/**
 * Request async save of loop buffer to preset file
 * Returns immediately; completion notified via callback
 *
 * @param slot Preset slot (1-4)
 * @param bufferL Pointer to left channel buffer
 * @param bufferR Pointer to right channel buffer
 * @param length Number of samples to save
 * @param callback Completion callback (called from SD thread)
 * @return true if request queued, false if queue full
 */
bool requestSave(uint8_t slot, const int16_t* bufferL, const int16_t* bufferR,
                 uint32_t length, CompletionCallback callback);

/**
 * Request async load of loop buffer from preset file
 * Returns immediately; completion notified via callback
 *
 * @param slot Preset slot (1-4)
 * @param bufferL Pointer to left channel buffer (output)
 * @param bufferR Pointer to right channel buffer (output)
 * @param callback Completion callback (called from SD thread, length in param)
 * @return true if request queued, false if queue full
 */
bool requestLoad(uint8_t slot, int16_t* bufferL, int16_t* bufferR,
                 CompletionCallback callback);

/**
 * Request async delete of preset file
 * Returns immediately; completion notified via callback
 *
 * @param slot Preset slot (1-4)
 * @param callback Completion callback (called from SD thread)
 * @return true if request queued, false if queue full
 */
bool requestDelete(uint8_t slot, CompletionCallback callback);

// ========== SYNCHRONOUS QUERIES (safe, fast) ==========

/**
 * Check if a preset file exists (synchronous, fast)
 * Safe to call from any thread - only reads filesystem metadata
 *
 * @param slot Preset slot (1-4)
 * @return true if presetN.bin exists on SD card
 */
bool presetExists(uint8_t slot);

/**
 * Check if an operation is currently in progress
 *
 * @return true if SD thread is busy with an operation
 */
bool isBusy();

}
