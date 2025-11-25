#include "SdCardStorage.h"
#include <SD.h>
#include <SPI.h>
#include "../dsp/StutterAudio.h"

// Debug logging control - set to 0 for minimal output in production
#define SD_DEBUG 0

namespace SdCardStorage {

// ========== CONFIGURATION ==========

// Chunk size for SD card writes/reads
// SD cards prefer 512-byte aligned transfers for optimal performance
static constexpr size_t CHUNK_SIZE_BYTES = 512;

// Maximum samples that can be stored in a preset - MUST match StutterAudio buffer size
// This prevents buffer overflows when loading presets with corrupt/invalid lengths
static constexpr size_t MAX_PRESET_SAMPLES = StutterAudio::getMaxBufferSize();

// ========== SCRATCH BUFFER ==========
// DMAMEM places this in internal RAM (not EXTMEM/PSRAM)
// SD library may have issues with direct EXTMEM pointers, so we stage all I/O
// through this scratch buffer and memcpy to/from the actual target buffers
DMAMEM static uint8_t s_sdScratch[CHUNK_SIZE_BYTES];

// ========== STATE ==========

static bool s_cardInitialized = false;

// Preset existence state - updated after SD operations
static bool s_slotHasPreset[5] = {false, false, false, false, false};  // indices 1-4 used

// File name buffer (preset1.bin, preset2.bin, etc.)
static char s_fileNameBuffer[16];

// ========== INTERNAL HELPERS ==========

static const char* getFileName(uint8_t slot) {
    if (slot < 1 || slot > 4) {
        return nullptr;
    }
    snprintf(s_fileNameBuffer, sizeof(s_fileNameBuffer), "preset%d.bin", slot);
    return s_fileNameBuffer;
}

/**
 * Write buffer to file in chunks via scratch buffer
 * Source data may be in EXTMEM - we copy to internal RAM scratch buffer first
 */
static bool writeChunked(File& file, const uint8_t* data, size_t totalBytes) {
    size_t bytesWritten = 0;

    while (bytesWritten < totalBytes) {
        size_t chunkSize = min(CHUNK_SIZE_BYTES, totalBytes - bytesWritten);

        // Copy from source (possibly EXTMEM) to internal RAM scratch buffer
        memcpy(s_sdScratch, data + bytesWritten, chunkSize);

        // Write from scratch buffer (internal RAM) to SD card
        size_t written = file.write(s_sdScratch, chunkSize);
        if (written != chunkSize) {
            return false;
        }

        bytesWritten += written;
    }

    return true;
}

/**
 * Read buffer from file in chunks via scratch buffer
 * Destination may be in EXTMEM - we read to internal RAM scratch buffer first
 */
static bool readChunked(File& file, uint8_t* data, size_t totalBytes) {
    size_t bytesRead = 0;

    while (bytesRead < totalBytes) {
        size_t chunkSize = min(CHUNK_SIZE_BYTES, totalBytes - bytesRead);

        // Read from SD card to scratch buffer (internal RAM)
        size_t readCount = file.read(s_sdScratch, chunkSize);
        if (readCount != chunkSize) {
            return false;
        }

        // Copy from scratch buffer to destination (possibly EXTMEM)
        memcpy(data + bytesRead, s_sdScratch, chunkSize);

        bytesRead += readCount;
    }

    return true;
}

/**
 * Execute save operation
 */
static SdResult executeSave(uint8_t slot, const int16_t* bufferL,
                            const int16_t* bufferR, uint32_t length) {
    // Validate parameters
    if (!s_cardInitialized) {
        return SdResult::ERROR_NO_CARD;
    }
    if (slot < 1 || slot > 4) {
        return SdResult::ERROR_INVALID_SLOT;
    }
    if (!bufferL || !bufferR || length == 0) {
        return SdResult::ERROR_INVALID_BUFFER;
    }

    // Bounds check - prevent saving more data than buffer can hold
    if (length > MAX_PRESET_SAMPLES) {
#if SD_DEBUG
        Serial.print("SdCardStorage: Save length too large: ");
        Serial.print(length);
        Serial.print(" (max: ");
        Serial.print(MAX_PRESET_SAMPLES);
        Serial.println(")");
#endif
        return SdResult::ERROR_INVALID_LENGTH;
    }

    const char* fileName = getFileName(slot);
    if (!fileName) {
        return SdResult::ERROR_INVALID_SLOT;
    }

    Serial.print("SdCardStorage: Saving preset ");
    Serial.print(slot);
    Serial.print(" (");
    Serial.print(length);
    Serial.println(" samples)");

    // Delete existing file first (if any)
    if (SD.exists(fileName)) {
        SD.remove(fileName);
    }

    // Open file for writing
    File file = SD.open(fileName, FILE_WRITE);
    if (!file) {
        Serial.println("SdCardStorage: Failed to create file");
        return SdResult::ERROR_FILE_CREATE;
    }

    // Write header: capture length (4 bytes) via scratch buffer
    memcpy(s_sdScratch, &length, sizeof(uint32_t));
    size_t written = file.write(s_sdScratch, sizeof(uint32_t));
    if (written != sizeof(uint32_t)) {
        file.close();
        SD.remove(fileName);
        Serial.println("SdCardStorage: Failed to write header");
        return SdResult::ERROR_WRITE_FAILED;
    }

    // Write left channel data
    size_t bytesToWrite = length * sizeof(int16_t);
    if (!writeChunked(file, (const uint8_t*)bufferL, bytesToWrite)) {
        file.close();
        SD.remove(fileName);
        Serial.println("SdCardStorage: Failed to write left channel");
        return SdResult::ERROR_WRITE_FAILED;
    }

    // Write right channel data
    if (!writeChunked(file, (const uint8_t*)bufferR, bytesToWrite)) {
        file.close();
        SD.remove(fileName);
        Serial.println("SdCardStorage: Failed to write right channel");
        return SdResult::ERROR_WRITE_FAILED;
    }

    file.close();

    Serial.print("SdCardStorage: Saved preset ");
    Serial.print(slot);
    Serial.print(" (");
    Serial.print((length * 4 + 4) / 1024);
    Serial.println(" KB)");

    return SdResult::SUCCESS;
}

/**
 * Execute load operation
 */
static SdResult executeLoad(uint8_t slot, int16_t* bufferL,
                            int16_t* bufferR, uint32_t& outLength) {
    outLength = 0;

    // Validate parameters
    if (!s_cardInitialized) {
        return SdResult::ERROR_NO_CARD;
    }
    if (slot < 1 || slot > 4) {
        return SdResult::ERROR_INVALID_SLOT;
    }
    if (!bufferL || !bufferR) {
        return SdResult::ERROR_INVALID_BUFFER;
    }
    const char* fileName = getFileName(slot);
    if (!fileName) {
        return SdResult::ERROR_INVALID_SLOT;
    }

    Serial.print("SdCardStorage: Loading preset ");
    Serial.print(slot);
    Serial.println("...");

    uint32_t captureLength = 0;

    // Open file for reading
    File file = SD.open(fileName, FILE_READ);
    if (!file) {
        Serial.println("SdCardStorage: File not found");
        return SdResult::ERROR_FILE_NOT_FOUND;
    }

    // Read header: capture length (4 bytes) via scratch buffer
    size_t bytesRead = file.read(s_sdScratch, sizeof(uint32_t));
    if (bytesRead != sizeof(uint32_t)) {
        file.close();
        Serial.println("SdCardStorage: Failed to read header");
        return SdResult::ERROR_READ_FAILED;
    }
    memcpy(&captureLength, s_sdScratch, sizeof(uint32_t));

    // Sanity check on length - MUST NOT exceed actual buffer capacity
    // This prevents buffer overflow into adjacent EXTMEM allocations
    if (captureLength == 0 || captureLength > MAX_PRESET_SAMPLES) {
        file.close();
#if SD_DEBUG
        Serial.print("SdCardStorage: Invalid capture length: ");
        Serial.print(captureLength);
        Serial.print(" (max: ");
        Serial.print(MAX_PRESET_SAMPLES);
        Serial.println(")");
#endif
        return SdResult::ERROR_INVALID_LENGTH;
    }

    // Read left channel data
    size_t bytesToRead = captureLength * sizeof(int16_t);
    if (!readChunked(file, (uint8_t*)bufferL, bytesToRead)) {
        file.close();
        Serial.println("SdCardStorage: Failed to read left channel");
        return SdResult::ERROR_READ_FAILED;
    }

    // Read right channel data
    if (!readChunked(file, (uint8_t*)bufferR, bytesToRead)) {
        file.close();
        Serial.println("SdCardStorage: Failed to read right channel");
        return SdResult::ERROR_READ_FAILED;
    }

    file.close();
    outLength = captureLength;

    Serial.print("SdCardStorage: Loaded preset ");
    Serial.print(slot);
    Serial.print(" (");
    Serial.print(captureLength);
    Serial.println(" samples)");

    return SdResult::SUCCESS;
}

/**
 * Execute delete operation
 */
static SdResult executeDelete(uint8_t slot) {
    // Validate parameters
    if (!s_cardInitialized) {
        return SdResult::ERROR_NO_CARD;
    }
    if (slot < 1 || slot > 4) {
        return SdResult::ERROR_INVALID_SLOT;
    }
    const char* fileName = getFileName(slot);
    if (!fileName) {
        return SdResult::ERROR_INVALID_SLOT;
    }

    // Check if file exists
    if (!SD.exists(fileName)) {
        // File doesn't exist - this is success (idempotent delete)
        return SdResult::SUCCESS;
    }

    // Attempt to delete file
    if (!SD.remove(fileName)) {
        Serial.println("SdCardStorage: Failed to delete file");
        return SdResult::ERROR_DELETE_FAILED;
    }

    Serial.print("SdCardStorage: Deleted preset ");
    Serial.println(slot);
    return SdResult::SUCCESS;
}

// ========== PUBLIC API ==========

bool begin() {
    // Teensy 4.1 uses built-in SDIO interface (no chip select pin needed)
    if (SD.begin(BUILTIN_SDCARD)) {
        s_cardInitialized = true;
        Serial.println("SdCardStorage: SD card initialized");

        // One-time scan for existing presets at boot
        for (uint8_t slot = 1; slot <= 4; ++slot) {
            const char* name = getFileName(slot);
            if (name) {
                s_slotHasPreset[slot] = SD.exists(name);
#if SD_DEBUG
                if (s_slotHasPreset[slot]) {
                    Serial.print("SdCardStorage: Found preset ");
                    Serial.println(slot);
                }
#endif
            }
        }

        return true;
    }

    s_cardInitialized = false;
    Serial.println("SdCardStorage: SD card not detected");
    return false;
}

bool isCardPresent() {
    return s_cardInitialized;
}

// ========== SYNCHRONOUS OPERATIONS ==========

SdResult saveSync(uint8_t slot, const int16_t* bufferL, const int16_t* bufferR,
                  uint32_t length) {
    SdResult result = executeSave(slot, bufferL, bufferR, length);

    // Update cached state on success
    if (result == SdResult::SUCCESS && slot >= 1 && slot <= 4) {
        s_slotHasPreset[slot] = true;
    }

    return result;
}

SdResult loadSync(uint8_t slot, int16_t* bufferL, int16_t* bufferR,
                  uint32_t& outLength) {
    return executeLoad(slot, bufferL, bufferR, outLength);
}

SdResult deleteSync(uint8_t slot) {
    SdResult result = executeDelete(slot);

    // Update cached state on success
    if (result == SdResult::SUCCESS && slot >= 1 && slot <= 4) {
        s_slotHasPreset[slot] = false;
    }

    return result;
}

bool presetExists(uint8_t slot) {
    if (!s_cardInitialized) {
        return false;
    }
    if (slot < 1 || slot > 4) {
        return false;
    }
    return s_slotHasPreset[slot];
}

}
