#include <Arduino.h>
#include <Audio.h>
#include <TeensyThreads.h>
#include "midi_io.h"
#include "app_logic.h"
#include "neokey_io.h"
#include "oled_io.h"
#include "mcp_io.h"
#include "audio_freeze.h"
#include "audio_choke.h"
#include "audio_stutter.h"
#include "effect_manager.h"
#include "trace.h"
#include "timekeeper.h"
#include "audio_timekeeper.h"

AudioInputI2S i2s_in;
AudioTimeKeeper timekeeper;  // Tracks sample position
AudioEffectFreeze freeze;    // Circular buffer freeze effect
AudioEffectChoke choke;      // Smooth mute effect
AudioEffectStutter stutter;
AudioOutputI2S i2s_out;

// Audio connections (stereo L+R)
AudioConnection patchCord1(i2s_in, 0, timekeeper, 0);   // Left in → TimeKeeper
AudioConnection patchCord2(i2s_in, 1, timekeeper, 1);   // Right in → TimeKeeper
AudioConnection patchCord3(timekeeper, 0, stutter, 0);
AudioConnection patchCord4(timekeeper, 1, stutter, 1);
AudioConnection patchCord5(stutter, 0, freeze, 0);
AudioConnection patchCord6(stutter, 1, freeze, 1);
AudioConnection patchCord7(freeze, 1, choke, 0);
AudioConnection patchCord8(freeze, 1, choke, 1);
AudioConnection patchCord9(choke, 0, i2s_out, 0);       // Choke → Left out
AudioConnection patchCord10(choke, 1, i2s_out, 1);       // Choke → Right out

// Teensy Audio Library SGTL5000 control
AudioControlSGTL5000 codec;

void ioThreadEntry() {
    MidiIO::threadLoop();  // Never returns
}

void inputThreadEntry() {
    NeokeyIO::threadLoop();  // Never returns
}

void mcpThreadEntry() {
    McpIO::threadLoop();  // Never returns
}

void displayThreadEntry() {
    OledIO::threadLoop();  // Never returns
}

void appThreadEntry() {
    AppLogic::threadLoop();  // Never returns
}

void setup() {
    Serial.begin(115200);

    // Print crash report if available (from previous run)
    // DEBUGGING AID: If Teensy crashed, this tells us why
    if (CrashReport) {
        Serial.print(CrashReport);
    }

    Serial.println("=== MicroLoop Initializing ===");

    AudioMemory(12);

    if (!codec.enable()) {
        Serial.println("ERROR: Codec init failed!");
        while (1) {
            // Blink LED rapidly to indicate error
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            delay(100);
        }
    }

    // Configure for line-in and line-out operation
    // IMPORTANT: Use MOTU M4 **REAR LINE INPUTS 3-4** (not front combo jacks!)
    codec.inputSelect(AUDIO_INPUT_LINEIN);  // Use line-in (not mic)
    codec.lineInLevel(0);  // Line-in gain: 0-15 (0 = 3.12V p-p, lowest gain to prevent clipping)
    codec.lineOutLevel(13);  // Line-out level: 13-31 (13 = 1.31V p-p, standard line level)
    codec.unmuteLineout();  // Unmute line-out
    codec.volume(0.3);  // Headphone volume (0.0-1.0) - start low to avoid clipping
    codec.unmuteHeadphone();  // Unmute headphone (for testing)

    Serial.println("Audio: OK (using Teensy Audio Library SGTL5000)");

    TimeKeeper::begin();
    Serial.println("TimeKeeper: OK");

    MidiIO::begin();
    Serial.println("MIDI: OK (DIN on Serial8)");

    AppLogic::begin();
    Serial.println("App Logic: OK");

    if (!NeokeyIO::begin()) {
        Serial.println("ERROR: NeoKey I/O init failed!");
        while (1) {
            // Blink LED rapidly to indicate error
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            delay(100);
        }
    }
    Serial.println("NeoKey I/O: OK (Neokey on I2C 0x30 / Wire2)");

    if (!McpIO::begin()) {
        Serial.println("ERROR: MCP I/O init failed!");
        while (1) {
            // Blink LED rapidly to indicate error
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            delay(100);
        }
    }
    Serial.println("MCP I/O: OK (MCP23017 on I2C 0x20 / Wire, ISR capture mode)");

    if (!OledIO::begin()) {
        Serial.println("WARNING: Display init failed (will continue without display)");
        // Continue anyway - display is optional for basic functionality
    } else {
        Serial.println("Display: OK (SSD1306 on I2C 0x3C / Wire1)");
    }

    if (!EffectManager::registerEffect(EffectID::STUTTER, &stutter)) {
        Serial.println("FATAL: Failed to register stutter effect!");
        while (1) {
            // Blink LED rapidly to indicate error
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            delay(100);
        }
    }
    if (!EffectManager::registerEffect(EffectID::FREEZE, &freeze)) {
        Serial.println("FATAL: Failed to register freeze effect!");
        while (1) {
            // Blink LED rapidly to indicate error
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            delay(100);
        }
    }
    if (!EffectManager::registerEffect(EffectID::CHOKE, &choke)) {
        Serial.println("FATAL: Failed to register choke effect!");
        while (1) {
            // Blink LED rapidly to indicate error
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            delay(100);
        }
    }
    Serial.print("Effect Manager: Registered ");
    Serial.print(EffectManager::getNumEffects());
    Serial.println(" effect(s)");

    int ioThreadId = threads.addThread(ioThreadEntry, 2048);
    int inputThreadId = threads.addThread(inputThreadEntry, 2048);
    int mcpThreadId = threads.addThread(mcpThreadEntry, 2048);
    int displayThreadId = threads.addThread(displayThreadEntry, 2048);
    int appThreadId = threads.addThread(appThreadEntry, 3072);

    if (ioThreadId < 0 || inputThreadId < 0 || mcpThreadId < 0 || displayThreadId < 0 || appThreadId < 0) {
        Serial.println("ERROR: Thread creation failed!");
        while (1);  // Halt
    }

    // threads.setTimeSlice(ioThreadId, 2);   // 2ms - very responsive
    // threads.setTimeSlice(appThreadId, 5);  // 5ms - moderate

    Serial.println("Threads: Started");
    Serial.println("=== MicroLoop Running ===");
    Serial.println();
    Serial.println("Commands:");
    Serial.println("  't' - Dump trace buffer");
    Serial.println("  'c' - Clear trace buffer");
    Serial.println("  's' - Show TimeKeeper status");
    Serial.println();
}

void loop() {
    // Check for serial commands (non-blocking)
    if (Serial.available()) {
        char cmd = Serial.read();
        switch (cmd) {
            case 't':  // Dump trace buffer
                Serial.println("\n[Dumping trace buffer...]");
                Trace::dump();
                break;

            case 'c':  // Clear trace buffer
                Serial.println("\n[Clearing trace buffer...]");
                Trace::clear();
                Serial.println("Trace buffer cleared.");
                break;

            case 's':  // Show TimeKeeper status
                Serial.println("\n=== TimeKeeper Status ===");
                Serial.print("Sample Position: ");
                Serial.println((uint32_t)TimeKeeper::getSamplePosition());  // Print low 32 bits
                Serial.print("Beat: ");
                Serial.print(TimeKeeper::getBeatNumber());
                Serial.print(" (Bar ");
                Serial.print(TimeKeeper::getBarNumber());
                Serial.print(", Beat ");
                Serial.print(TimeKeeper::getBeatInBar());
                Serial.print(", Tick ");
                Serial.print(TimeKeeper::getTickInBeat());
                Serial.println(")");
                Serial.print("BPM: ");
                Serial.println(TimeKeeper::getBPM(), 2);
                Serial.print("Samples/Beat: ");
                Serial.println(TimeKeeper::getSamplesPerBeat());
                Serial.print("Transport: ");
                switch (TimeKeeper::getTransportState()) {
                    case TimeKeeper::TransportState::STOPPED: Serial.println("STOPPED"); break;
                    case TimeKeeper::TransportState::PLAYING: Serial.println("PLAYING"); break;
                    case TimeKeeper::TransportState::RECORDING: Serial.println("RECORDING"); break;
                }
                Serial.print("Samples to next beat: ");
                Serial.println(TimeKeeper::samplesToNextBeat());
                Serial.print("Samples to next bar: ");
                Serial.println(TimeKeeper::samplesToNextBar());
                Serial.println("=========================\n");
                break;

            case '\n':
            case '\r':
                // Ignore newlines
                break;

            default:
                Serial.print("Unknown command: ");
                Serial.println(cmd);
                Serial.println("Commands: 't' (dump trace), 'c' (clear trace), 's' (status)");
                break;
        }
    }

    delay(10);  // Don't hog CPU
}