/**
 * SdCardStorage.h - SD card HAL for preset storage
 *
 * PURPOSE:
 * Provides a hardware abstraction layer for reading and writing preset
 * loop buffers to the microSD card on Teensy 4.1.
 *
 * DESIGN:
 * - Uses Teensy's built-in SD library (SDIO interface for speed)
 * - Simple file format: [4 bytes length][left channel data][right channel data]
 * - File names: preset1.bin, preset2.bin, preset3.bin, preset4.bin
 * - All operations are blocking (called from App thread, not ISR)
 *
 * THREAD SAFETY:
 * - NOT thread-safe - all calls should be from App thread only
 * - SD operations can take 10-100ms, so never call from audio ISR
 */

#pragma once

#include <Arduino.h>

namespace SdCardStorage {

/**
 * Initialize SD card
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

/**
 * Check if a preset file exists
 *
 * @param slot Preset slot (1-4)
 * @return true if presetN.bin exists on SD card
 */
bool presetExists(uint8_t slot);

/**
 * Save loop buffer to preset file
 *
 * File format:
 * - Bytes 0-3: uint32_t captureLength (number of samples)
 * - Bytes 4 to 4+(length*2)-1: int16_t left channel samples
 * - Remaining bytes: int16_t right channel samples
 *
 * @param slot Preset slot (1-4)
 * @param bufferL Pointer to left channel buffer
 * @param bufferR Pointer to right channel buffer
 * @param length Number of samples to save
 * @return true if save successful, false on error
 */
bool savePreset(uint8_t slot, const int16_t* bufferL, const int16_t* bufferR, uint32_t length);

/**
 * Load loop buffer from preset file
 *
 * @param slot Preset slot (1-4)
 * @param bufferL Pointer to left channel buffer (must be large enough)
 * @param bufferR Pointer to right channel buffer (must be large enough)
 * @param length Output: number of samples loaded
 * @return true if load successful, false on error
 */
bool loadPreset(uint8_t slot, int16_t* bufferL, int16_t* bufferR, uint32_t& length);

/**
 * Delete a preset file
 *
 * @param slot Preset slot (1-4)
 * @return true if delete successful or file didn't exist, false on error
 */
bool deletePreset(uint8_t slot);

}
