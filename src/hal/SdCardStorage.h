/**
 * SdCardStorage.h - SD card HAL for preset storage (synchronous I/O)
 *
 * PURPOSE:
 * Provides a hardware abstraction layer for reading and writing preset
 * loop buffers to the microSD card on Teensy 4.1.
 *
 * DESIGN:
 * - Synchronous (blocking) API - called from App thread
 * - Chunked reads/writes for large buffers
 * - Uses Teensy's built-in SD library (SDIO interface for speed)
 *
 * FILE FORMAT:
 * - [4 bytes length][left channel data][right channel data]
 * - File names: preset1.bin, preset2.bin, preset3.bin, preset4.bin
 *
 * THREAD SAFETY:
 * - All SD operations must be called from the same thread (App thread)
 * - Do NOT call SD functions from ISR or other threads
 */

#pragma once

#include <Arduino.h>

namespace SdCardStorage {

// ========== OPERATION TYPES ==========

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

// ========== SYNCHRONOUS OPERATIONS ==========

/**
 * Save loop buffer to preset file (blocking)
 * Call from App thread only - will block until complete
 *
 * @param slot Preset slot (1-4)
 * @param bufferL Pointer to left channel buffer
 * @param bufferR Pointer to right channel buffer
 * @param length Number of samples to save
 * @return Result code indicating success or failure
 */
SdResult saveSync(uint8_t slot, const int16_t* bufferL, const int16_t* bufferR,
                  uint32_t length);

/**
 * Load loop buffer from preset file (blocking)
 * Call from App thread only - will block until complete
 *
 * @param slot Preset slot (1-4)
 * @param bufferL Pointer to left channel buffer (output)
 * @param bufferR Pointer to right channel buffer (output)
 * @param outLength Output parameter: number of samples loaded
 * @return Result code indicating success or failure
 */
SdResult loadSync(uint8_t slot, int16_t* bufferL, int16_t* bufferR,
                  uint32_t& outLength);

/**
 * Delete preset file (blocking)
 * Call from App thread only - will block until complete
 *
 * @param slot Preset slot (1-4)
 * @return Result code indicating success or failure
 */
SdResult deleteSync(uint8_t slot);

// ========== SYNCHRONOUS QUERIES ==========

/**
 * Check if a preset file exists (synchronous, fast)
 * Uses cached state from boot scan and SD operations
 *
 * @param slot Preset slot (1-4)
 * @return true if presetN.bin exists on SD card
 */
bool presetExists(uint8_t slot);

}
