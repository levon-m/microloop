#include "SdCardStorage.h"
#include <SD.h>
#include <SPI.h>

namespace SdCardStorage {

// SD card state
static bool s_cardInitialized = false;

// File name buffer (preset1.bin, preset2.bin, etc.)
static char s_fileNameBuffer[16];

// Build file name for preset slot
static const char* getFileName(uint8_t slot) {
    if (slot < 1 || slot > 4) {
        return nullptr;
    }
    snprintf(s_fileNameBuffer, sizeof(s_fileNameBuffer), "preset%d.bin", slot);
    return s_fileNameBuffer;
}

bool begin() {
    // Teensy 4.1 uses built-in SDIO interface (no chip select pin needed)
    // SD.begin() with BUILTIN_SDCARD uses the fast SDIO interface
    if (SD.begin(BUILTIN_SDCARD)) {
        s_cardInitialized = true;
        Serial.println("SdCardStorage: SD card initialized (SDIO)");
        return true;
    }

    s_cardInitialized = false;
    Serial.println("ERROR: SdCardStorage - SD card not detected!");
    return false;
}

bool isCardPresent() {
    return s_cardInitialized;
}

bool presetExists(uint8_t slot) {
    if (!s_cardInitialized) {
        return false;
    }

    const char* fileName = getFileName(slot);
    if (!fileName) {
        return false;
    }

    return SD.exists(fileName);
}

bool savePreset(uint8_t slot, const int16_t* bufferL, const int16_t* bufferR, uint32_t length) {
    if (!s_cardInitialized) {
        Serial.println("ERROR: SdCardStorage - SD card not initialized");
        return false;
    }

    if (slot < 1 || slot > 4) {
        Serial.println("ERROR: SdCardStorage - Invalid slot number");
        return false;
    }

    if (!bufferL || !bufferR || length == 0) {
        Serial.println("ERROR: SdCardStorage - Invalid buffer or length");
        return false;
    }

    const char* fileName = getFileName(slot);
    if (!fileName) {
        return false;
    }

    // Delete existing file first (if any)
    if (SD.exists(fileName)) {
        SD.remove(fileName);
    }

    // Open file for writing
    File file = SD.open(fileName, FILE_WRITE);
    if (!file) {
        Serial.print("ERROR: SdCardStorage - Failed to create file: ");
        Serial.println(fileName);
        return false;
    }

    // Write header: capture length (4 bytes)
    size_t written = file.write((const uint8_t*)&length, sizeof(uint32_t));
    if (written != sizeof(uint32_t)) {
        Serial.println("ERROR: SdCardStorage - Failed to write header");
        file.close();
        SD.remove(fileName);
        return false;
    }

    // Write left channel data
    size_t bytesToWrite = length * sizeof(int16_t);
    written = file.write((const uint8_t*)bufferL, bytesToWrite);
    if (written != bytesToWrite) {
        Serial.println("ERROR: SdCardStorage - Failed to write left channel");
        file.close();
        SD.remove(fileName);
        return false;
    }

    // Write right channel data
    written = file.write((const uint8_t*)bufferR, bytesToWrite);
    if (written != bytesToWrite) {
        Serial.println("ERROR: SdCardStorage - Failed to write right channel");
        file.close();
        SD.remove(fileName);
        return false;
    }

    file.close();

    Serial.print("SdCardStorage: Saved preset ");
    Serial.print(slot);
    Serial.print(" (");
    Serial.print(length);
    Serial.print(" samples, ");
    Serial.print((length * 4 + 4) / 1024);
    Serial.println(" KB)");

    return true;
}

bool loadPreset(uint8_t slot, int16_t* bufferL, int16_t* bufferR, uint32_t& length) {
    if (!s_cardInitialized) {
        Serial.println("ERROR: SdCardStorage - SD card not initialized");
        return false;
    }

    if (slot < 1 || slot > 4) {
        Serial.println("ERROR: SdCardStorage - Invalid slot number");
        return false;
    }

    if (!bufferL || !bufferR) {
        Serial.println("ERROR: SdCardStorage - Invalid buffer pointers");
        return false;
    }

    const char* fileName = getFileName(slot);
    if (!fileName) {
        return false;
    }

    // Open file for reading
    File file = SD.open(fileName, FILE_READ);
    if (!file) {
        Serial.print("ERROR: SdCardStorage - File not found: ");
        Serial.println(fileName);
        return false;
    }

    // Read header: capture length (4 bytes)
    uint32_t captureLength = 0;
    size_t bytesRead = file.read((uint8_t*)&captureLength, sizeof(uint32_t));
    if (bytesRead != sizeof(uint32_t)) {
        Serial.println("ERROR: SdCardStorage - Failed to read header");
        file.close();
        return false;
    }

    // Sanity check on length (max ~590KB buffer = ~150,000 samples per channel)
    if (captureLength == 0 || captureLength > 200000) {
        Serial.print("ERROR: SdCardStorage - Invalid capture length: ");
        Serial.println(captureLength);
        file.close();
        return false;
    }

    // Read left channel data
    size_t bytesToRead = captureLength * sizeof(int16_t);
    bytesRead = file.read((uint8_t*)bufferL, bytesToRead);
    if (bytesRead != bytesToRead) {
        Serial.println("ERROR: SdCardStorage - Failed to read left channel");
        file.close();
        return false;
    }

    // Read right channel data
    bytesRead = file.read((uint8_t*)bufferR, bytesToRead);
    if (bytesRead != bytesToRead) {
        Serial.println("ERROR: SdCardStorage - Failed to read right channel");
        file.close();
        return false;
    }

    file.close();

    length = captureLength;

    Serial.print("SdCardStorage: Loaded preset ");
    Serial.print(slot);
    Serial.print(" (");
    Serial.print(length);
    Serial.print(" samples, ");
    Serial.print((length * 4 + 4) / 1024);
    Serial.println(" KB)");

    return true;
}

bool deletePreset(uint8_t slot) {
    if (!s_cardInitialized) {
        Serial.println("ERROR: SdCardStorage - SD card not initialized");
        return false;
    }

    if (slot < 1 || slot > 4) {
        Serial.println("ERROR: SdCardStorage - Invalid slot number");
        return false;
    }

    const char* fileName = getFileName(slot);
    if (!fileName) {
        return false;
    }

    // Check if file exists
    if (!SD.exists(fileName)) {
        // File doesn't exist - that's fine, consider it deleted
        Serial.print("SdCardStorage: Preset ");
        Serial.print(slot);
        Serial.println(" already empty");
        return true;
    }

    // Delete the file
    if (SD.remove(fileName)) {
        Serial.print("SdCardStorage: Deleted preset ");
        Serial.println(slot);
        return true;
    }

    Serial.print("ERROR: SdCardStorage - Failed to delete: ");
    Serial.println(fileName);
    return false;
}

}
