#include "SdCardStorage.h"
#include "SpscQueue.h"
#include <SD.h>
#include <SPI.h>
#include <TeensyThreads.h>

namespace SdCardStorage {

// ========== CONFIGURATION ==========

// Chunk size for SD card writes/reads (4KB is optimal for SD cards)
// Writing in chunks prevents blocking the scheduler and allows other threads to run
static constexpr size_t CHUNK_SIZE_BYTES = 4096;

// Idle delay when queue is empty (ms)
static constexpr uint32_t IDLE_DELAY_MS = 10;

// Note: No mutex needed - SD is only accessed from this thread after begin()
// The SD library is not thread-safe, but single-thread ownership is sufficient

// ========== COMMAND QUEUE ==========

struct SdCommand {
    SdOperation operation;
    uint8_t slot;
    int16_t* bufferL;       // Non-const for load, cast from const for save
    int16_t* bufferR;
    uint32_t length;        // Sample count (for save)
    CompletionCallback callback;
};

// Command queue (4 slots should be plenty - operations are slow)
static SpscQueue<SdCommand, 4> s_commandQueue;

// ========== STATE ==========

static bool s_cardInitialized = false;
static volatile bool s_busy = false;

// Preset existence state - updated only by SD thread after operations
// This allows presetExists() to be called safely from any thread without touching SD
static volatile bool s_slotHasPreset[5] = {false, false, false, false, false};  // indices 1-4 used

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
 * Write buffer to file in chunks
 * Note: No yields during SD operations - SD library is not thread-safe
 */
static bool writeChunked(File& file, const uint8_t* data, size_t totalBytes) {
    Serial.print("  writeChunked: ");
    Serial.print(totalBytes);
    Serial.println(" bytes");

    size_t bytesWritten = 0;

    while (bytesWritten < totalBytes) {
        size_t chunkSize = min(CHUNK_SIZE_BYTES, totalBytes - bytesWritten);

        size_t written = file.write(data + bytesWritten, chunkSize);
        if (written != chunkSize) {
            Serial.print("ERROR: SD write failed at offset ");
            Serial.print(bytesWritten);
            Serial.print(", wrote ");
            Serial.print(written);
            Serial.print(" of ");
            Serial.println(chunkSize);
            return false;
        }

        bytesWritten += written;
    }

    Serial.println("  writeChunked: done");
    return true;
}

/**
 * Read buffer from file in chunks
 * Note: No yields during SD operations - SD library is not thread-safe
 */
static bool readChunked(File& file, uint8_t* data, size_t totalBytes) {
    size_t bytesRead = 0;

    while (bytesRead < totalBytes) {
        size_t chunkSize = min(CHUNK_SIZE_BYTES, totalBytes - bytesRead);

        size_t readCount = file.read(data + bytesRead, chunkSize);
        if (readCount != chunkSize) {
            Serial.print("ERROR: SD read failed at offset ");
            Serial.println(bytesRead);
            return false;
        }

        bytesRead += readCount;
    }

    return true;
}

/**
 * Execute save operation (called from SD thread)
 * Only SD thread calls this, so no mutex needed
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
    const char* fileName = getFileName(slot);
    if (!fileName) {
        return SdResult::ERROR_INVALID_SLOT;
    }

    Serial.print("SdCardStorage: Saving preset ");
    Serial.print(slot);
    Serial.print(" (");
    Serial.print(length);
    Serial.println(" samples)...");

    // Delete existing file first (if any)
    if (SD.exists(fileName)) {
        SD.remove(fileName);
    }

    // Open file for writing
    File file = SD.open(fileName, FILE_WRITE);
    if (!file) {
        Serial.println("ERROR: Failed to create file");
        return SdResult::ERROR_FILE_CREATE;
    }

    // Write header: capture length (4 bytes)
    size_t written = file.write((const uint8_t*)&length, sizeof(uint32_t));
    if (written != sizeof(uint32_t)) {
        file.close();
        SD.remove(fileName);
        Serial.println("ERROR: Failed to write header");
        return SdResult::ERROR_WRITE_FAILED;
    }

    // Write left channel data
    size_t bytesToWrite = length * sizeof(int16_t);
    if (!writeChunked(file, (const uint8_t*)bufferL, bytesToWrite)) {
        file.close();
        SD.remove(fileName);
        Serial.println("ERROR: Failed to write left channel");
        return SdResult::ERROR_WRITE_FAILED;
    }

    // Write right channel data
    if (!writeChunked(file, (const uint8_t*)bufferR, bytesToWrite)) {
        file.close();
        SD.remove(fileName);
        Serial.println("ERROR: Failed to write right channel");
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
 * Execute load operation (called from SD thread)
 * Only SD thread calls this, so no mutex needed
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
        Serial.println("ERROR: File not found");
        return SdResult::ERROR_FILE_NOT_FOUND;
    }

    // Read header: capture length (4 bytes)
    size_t bytesRead = file.read((uint8_t*)&captureLength, sizeof(uint32_t));
    if (bytesRead != sizeof(uint32_t)) {
        file.close();
        Serial.println("ERROR: Failed to read header");
        return SdResult::ERROR_READ_FAILED;
    }

    // Sanity check on length (max ~590KB buffer = ~150,000 samples per channel)
    if (captureLength == 0 || captureLength > 200000) {
        file.close();
        Serial.print("ERROR: Invalid capture length: ");
        Serial.println(captureLength);
        return SdResult::ERROR_INVALID_LENGTH;
    }

    // Read left channel data
    size_t bytesToRead = captureLength * sizeof(int16_t);
    if (!readChunked(file, (uint8_t*)bufferL, bytesToRead)) {
        file.close();
        Serial.println("ERROR: Failed to read left channel");
        return SdResult::ERROR_READ_FAILED;
    }

    // Read right channel data
    if (!readChunked(file, (uint8_t*)bufferR, bytesToRead)) {
        file.close();
        Serial.println("ERROR: Failed to read right channel");
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
 * Execute delete operation (called from SD thread)
 * Only SD thread calls this, so no mutex needed
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
    bool fileExists = SD.exists(fileName);
    bool deleteSuccess = false;
    if (fileExists) {
        deleteSuccess = SD.remove(fileName);
    }

    if (!fileExists) {
        Serial.print("SdCardStorage: Preset ");
        Serial.print(slot);
        Serial.println(" already empty");
        return SdResult::SUCCESS;
    }

    if (deleteSuccess) {
        Serial.print("SdCardStorage: Deleted preset ");
        Serial.println(slot);
        return SdResult::SUCCESS;
    }

    Serial.println("ERROR: Failed to delete");
    return SdResult::ERROR_DELETE_FAILED;
}

// ========== PUBLIC API ==========

bool begin() {
    // Teensy 4.1 uses built-in SDIO interface (no chip select pin needed)
    if (SD.begin(BUILTIN_SDCARD)) {
        s_cardInitialized = true;
        Serial.println("SdCardStorage: SD card initialized (SDIO)");

        // One-time scan for existing presets at boot
        // This is the ONLY place SD.exists() is called outside the SD thread
        // (safe because thread hasn't started yet)
        for (uint8_t slot = 1; slot <= 4; ++slot) {
            char name[16];
            snprintf(name, sizeof(name), "preset%d.bin", slot);
            s_slotHasPreset[slot] = SD.exists(name);
            if (s_slotHasPreset[slot]) {
                Serial.print("  Found preset ");
                Serial.println(slot);
            }
        }

        return true;
    }

    s_cardInitialized = false;
    Serial.println("ERROR: SdCardStorage - SD card not detected!");
    return false;
}

bool isCardPresent() {
    return s_cardInitialized;
}

void threadLoop() {
    Serial.println("SdCardStorage: Thread started");

    for (;;) {
        SdCommand cmd;
        bool hadWork = false;

        // Process one command at a time
        if (s_commandQueue.pop(cmd)) {
            hadWork = true;
            s_busy = true;

            SdResult result = SdResult::SUCCESS;
            uint32_t loadedLength = 0;

            switch (cmd.operation) {
                case SdOperation::SAVE:
                    result = executeSave(cmd.slot, cmd.bufferL, cmd.bufferR, cmd.length);
                    // Update cached state on success
                    if (result == SdResult::SUCCESS) {
                        s_slotHasPreset[cmd.slot] = true;
                    }
                    break;

                case SdOperation::LOAD:
                    result = executeLoad(cmd.slot, cmd.bufferL, cmd.bufferR, loadedLength);
                    // Load doesn't change existence state
                    break;

                case SdOperation::DELETE:
                    result = executeDelete(cmd.slot);
                    // Update cached state on success
                    if (result == SdResult::SUCCESS) {
                        s_slotHasPreset[cmd.slot] = false;
                    }
                    break;

                default:
                    break;
            }

            s_busy = false;

            // Invoke completion callback (if provided)
            if (cmd.callback) {
                cmd.callback(cmd.operation, cmd.slot, result, loadedLength);
            }
        }

        // Sleep when idle
        if (!hadWork) {
            threads.delay(IDLE_DELAY_MS);
        }
    }
}

bool requestSave(uint8_t slot, const int16_t* bufferL, const int16_t* bufferR,
                 uint32_t length, CompletionCallback callback) {
    SdCommand cmd;
    cmd.operation = SdOperation::SAVE;
    cmd.slot = slot;
    cmd.bufferL = const_cast<int16_t*>(bufferL);  // Safe: save only reads
    cmd.bufferR = const_cast<int16_t*>(bufferR);
    cmd.length = length;
    cmd.callback = callback;

    return s_commandQueue.push(cmd);
}

bool requestLoad(uint8_t slot, int16_t* bufferL, int16_t* bufferR,
                 CompletionCallback callback) {
    SdCommand cmd;
    cmd.operation = SdOperation::LOAD;
    cmd.slot = slot;
    cmd.bufferL = bufferL;
    cmd.bufferR = bufferR;
    cmd.length = 0;  // Not used for load
    cmd.callback = callback;

    return s_commandQueue.push(cmd);
}

bool requestDelete(uint8_t slot, CompletionCallback callback) {
    SdCommand cmd;
    cmd.operation = SdOperation::DELETE;
    cmd.slot = slot;
    cmd.bufferL = nullptr;
    cmd.bufferR = nullptr;
    cmd.length = 0;
    cmd.callback = callback;

    return s_commandQueue.push(cmd);
}

bool presetExists(uint8_t slot) {
    if (!s_cardInitialized) {
        return false;
    }
    if (slot < 1 || slot > 4) {
        return false;
    }
    // Read from cached state - NEVER touch SD from outside the SD thread
    return s_slotHasPreset[slot];
}

bool isBusy() {
    return s_busy;
}

}
