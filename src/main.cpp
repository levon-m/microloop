/**
 * @file main.cpp
 * @brief MicroLoop - Live Performance Looper/Sampler for Teensy 4.1
 *
 * HARDWARE:
 * - Teensy 4.1 (ARM Cortex-M7 @ 600 MHz)
 * - Audio Shield Rev D (SGTL5000 codec)
 * - MIDI DIN input from Elektron Digitakt (clock source)
 *
 * ARCHITECTURE:
 * - Audio: Runs in ISR via Teensy Audio Library (highest priority)
 * - I/O Thread: MIDI parsing, real-time event handling
 * - App Thread: Beat tracking, LED indicator, UI (future)
 *
 * CURRENT FEATURES:
 * - MIDI clock reception (24 PPQN)
 * - Transport control (START/STOP/CONTINUE)
 * - LED beat indicator (blinks on every beat)
 * - Audio passthrough (Line In → Line Out)
 *
 * FUTURE FEATURES:
 * - Loop recording/playback
 * - Sample triggering
 * - BPM display
 * - Quantized operations
 */

#include <Arduino.h>
#include <Audio.h>
#include <TeensyThreads.h>
#include "SGTL5000.h"
#include "midi_io.h"
#include "app_logic.h"
#include "choke_io.h"
#include "display_io.h"
#include "audio_choke.h"
#include "trace.h"
#include "timekeeper.h"
#include "audio_timekeeper.h"

// ========== AUDIO GRAPH ==========
/**
 * Stereo audio chain with TimeKeeper and Choke effect
 *
 * TOPOLOGY:
 *   I2S Input → TimeKeeper → Choke → I2S Output
 *
 * SIGNAL FLOW:
 * 1. I2S Input: Line-in from audio shield (stereo)
 * 2. TimeKeeper: Sample position tracking (zero-latency passthrough)
 * 3. Choke: Smooth mute effect with 10ms crossfade
 * 4. I2S Output: Line-out/headphone (stereo)
 *
 * LATENCY:
 * - Audio Library block size: 128 samples
 * - At 44.1kHz: 128/44100 ≈ 2.9ms per block
 * - Total latency: ~6ms (input buffer + output buffer)
 * - TimeKeeper overhead: <1µs (negligible)
 * - Choke overhead: ~50 cycles passthrough, ~2000 cycles fading
 */
AudioInputI2S i2s_in;
AudioTimeKeeper timekeeper;  // Tracks sample position
AudioEffectChoke choke;      // Smooth mute effect
AudioOutputI2S i2s_out;

// Audio connections (stereo L+R)
AudioConnection patchCord1(i2s_in, 0, timekeeper, 0);   // Left in → TimeKeeper
AudioConnection patchCord2(i2s_in, 1, timekeeper, 1);   // Right in → TimeKeeper
AudioConnection patchCord3(timekeeper, 0, choke, 0);    // TimeKeeper → Choke (L)
AudioConnection patchCord4(timekeeper, 1, choke, 1);    // TimeKeeper → Choke (R)
AudioConnection patchCord5(choke, 0, i2s_out, 0);       // Choke → Left out
AudioConnection patchCord6(choke, 1, i2s_out, 1);       // Choke → Right out

// Custom SGTL5000 codec driver
SGTL5000 codec;

// ========== THREAD ENTRY POINTS ==========

/**
 * I/O Thread: MIDI parsing and event handling
 *
 * PRIORITY: High (responsive to MIDI input)
 * STACK: 2048 bytes (enough for MIDI library + handlers)
 *
 * WHY SEPARATE THREAD?
 * - Decouples MIDI latency from app logic
 * - If app thread stalls (Serial.print, etc.), MIDI keeps flowing
 * - Can tune time slice for optimal responsiveness
 */
void ioThreadEntry() {
    MidiIO::threadLoop();  // Never returns
}

/**
 * Choke I/O Thread: Neokey button polling
 *
 * PRIORITY: High (responsive to button presses)
 * STACK: 2048 bytes (enough for I2C library + Seesaw)
 *
 * WHY SEPARATE THREAD?
 * - Decouples I2C latency from app logic and MIDI
 * - Hybrid interrupt approach: Fast response, low CPU
 * - Matches architecture pattern (I/O threads for external inputs)
 */
void chokeThreadEntry() {
    ChokeIO::threadLoop();  // Never returns
}

/**
 * Display I/O Thread: OLED display updates
 *
 * PRIORITY: Lower (visual feedback, not time-critical)
 * STACK: 2048 bytes (enough for I2C library + Adafruit GFX)
 *
 * WHY SEPARATE THREAD?
 * - Display updates are slow (~20-30ms for full screen)
 * - Decouples display latency from audio/MIDI/choke
 * - Command queue allows async updates
 * - Future menu system won't block other threads
 */
void displayThreadEntry() {
    DisplayIO::threadLoop();  // Never returns
}

/**
 * App Thread: Application logic and UI
 *
 * PRIORITY: Normal (can afford to be slower)
 * STACK: 3072 bytes (room for future UI, displays, etc.)
 *
 * WHY SEPARATE THREAD?
 * - App logic can be bursty (UI updates, calculations)
 * - Isolation: App bugs won't crash MIDI I/O or Choke I/O
 * - Easier to reason about: Clear separation of concerns
 */
void appThreadEntry() {
    AppLogic::threadLoop();  // Never returns
}

// ========== SETUP ==========

void setup() {
    // ========== SERIAL DEBUG ==========
    Serial.begin(115200);

    // Wait up to 4 seconds for Serial Monitor (optional)
    // WHY? If programming with debugger, we want to see early messages
    // while (!Serial && millis() < 4000);

    // Print crash report if available (from previous run)
    // DEBUGGING AID: If Teensy crashed, this tells us why
    if (CrashReport) {
        Serial.print(CrashReport);
    }

    Serial.println("=== MicroLoop Initializing ===");

    // ========== AUDIO SETUP ==========
    /**
     * Audio Memory: Number of 128-sample blocks
     *
     * WHY 12 BLOCKS?
     * - Each connection needs 1 block
     * - We have 2 connections (L + R)
     * - Audio Library recommends 2-3x minimum for headroom
     * - 12 blocks = 6x headroom (safe for future expansion)
     * - Memory: 12 × 128 samples × 4 bytes = 6 KB
     *
     * TOO FEW: Clicks, pops, dropouts
     * TOO MANY: Wasted RAM (Teensy 4.1 has plenty though)
     */
    AudioMemory(12);

    /**
     * Initialize codec
     *
     * WHAT IT DOES:
     * - Configures I2C communication
     * - Sets up I2S (44.1kHz, 16-bit, slave mode)
     * - Powers up analog/digital blocks
     * - Unmutes headphone/line outputs
     *
     * WHY CUSTOM DRIVER?
     * - Audio Library's driver is bloated (~10KB)
     * - We only need basic functionality
     * - Better understanding of hardware
     * - Easier to add custom features later
     */
    if (!codec.enable()) {
        Serial.println("ERROR: Codec init failed!");
        while (1) {
            // Blink LED rapidly to indicate error
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            delay(100);
        }
    }
    Serial.println("Audio: OK");

    // ========== TIMEKEEPER SETUP ==========
    /**
     * Initialize timing system
     *
     * WHAT IT DOES:
     * - Reset sample counter to 0
     * - Reset beat/bar counters to 0
     * - Set default tempo (120 BPM)
     * - Set transport state to STOPPED
     *
     * IMPORTANT: Must initialize before MIDI (so transport events can sync)
     */
    TimeKeeper::begin();
    Serial.println("TimeKeeper: OK");

    // ========== MIDI SETUP ==========
    /**
     * Initialize MIDI I/O
     *
     * WHAT IT DOES:
     * - Configures Serial8 @ 31250 baud (MIDI standard)
     * - Registers clock/transport handlers
     * - Prepares SPSC queues
     */
    MidiIO::begin();
    Serial.println("MIDI: OK (DIN on Serial8)");

    // ========== APP LOGIC SETUP ==========
    /**
     * Initialize application
     *
     * WHAT IT DOES:
     * - Configure LED pin
     * - Initialize beat tracking state
     * - Set up displays/UI (future)
     */
    AppLogic::begin();
    Serial.println("App Logic: OK");

    // ========== CHOKE I/O SETUP ==========
    /**
     * Initialize Neokey 1x4 button input
     *
     * WHAT IT DOES:
     * - Initializes Wire2 (I2C bus 2: SDA2=pin 25, SCL2=pin 24)
     * - Initializes Seesaw I2C communication
     * - Configures key 0 for interrupt-on-change
     * - Sets up INT pin on Teensy (pin 33)
     * - Sets initial LED state (green = unmuted)
     */
    if (!ChokeIO::begin()) {
        Serial.println("ERROR: Choke I/O init failed!");
        while (1) {
            // Blink LED rapidly to indicate error
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            delay(100);
        }
    }
    Serial.println("Choke I/O: OK (Neokey on I2C 0x30 / Wire2)");

    // ========== DISPLAY SETUP ==========
    /**
     * Initialize OLED Display
     *
     * WHAT IT DOES:
     * - Initializes Wire1 (I2C bus 1: SDA1=pin 17, SCL1=pin 16)
     * - Initializes SSD1306 display (I2C 0x3C)
     * - Clears display
     * - Shows default bitmap
     *
     * NOTE: Display initialization failure is non-fatal (optional peripheral)
     */
    if (!DisplayIO::begin()) {
        Serial.println("WARNING: Display init failed (will continue without display)");
        // Continue anyway - display is optional for basic functionality
    } else {
        Serial.println("Display: OK (SSD1306 on I2C 0x3C / Wire1)");
    }

    // ========== THREAD CREATION ==========
    /**
     * TeensyThreads: Cooperative multithreading
     *
     * SCHEDULING:
     * - Preemptive: Context switch every time slice (default 10ms)
     * - Round-robin: All threads have equal priority
     * - Audio ISR has highest priority (pre-empts threads)
     *
     * STACK SIZES:
     * - I/O: 2048 bytes (MIDI parser + handlers)
     * - App: 3072 bytes (future UI, calculations)
     * - Too small: Stack overflow (crashes)
     * - Too large: Wasted RAM
     *
     * HOW TO SIZE?
     * 1. Start conservative (2-3KB)
     * 2. Monitor stack usage (future: stack watermarking)
     * 3. If overflow, increase by 1KB and repeat
     */
    int ioThreadId = threads.addThread(ioThreadEntry, 2048);
    int chokeThreadId = threads.addThread(chokeThreadEntry, 2048);
    int displayThreadId = threads.addThread(displayThreadEntry, 2048);
    int appThreadId = threads.addThread(appThreadEntry, 3072);

    if (ioThreadId < 0 || chokeThreadId < 0 || displayThreadId < 0 || appThreadId < 0) {
        Serial.println("ERROR: Thread creation failed!");
        while (1);  // Halt
    }

    /**
     * THREAD TIME SLICING (optional tuning)
     *
     * Default: 10ms per thread (100Hz context switches)
     *
     * TRADEOFF: Responsiveness vs context switch overhead
     * - Smaller slice: More responsive, more overhead
     * - Larger slice: Less overhead, chunkier execution
     *
     * For MIDI @ 120 BPM:
     * - Clock ticks every ~20ms
     * - 10ms slice = check 2x per tick (good enough)
     *
     * WHEN TO TUNE?
     * - If you see MIDI jitter: Reduce I/O slice (more responsive)
     * - If CPU usage high: Increase slices (less switching)
     * - If UI feels laggy: Reduce app slice
     *
     * Example (currently disabled):
     */
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

// ========== MAIN LOOP ==========

void loop() {
    /**
     * Main loop: Low-priority housekeeping and debug commands
     *
     * WHY NOT EMPTY?
     * - Serial commands for debugging (trace dump, etc.)
     * - Future: Watchdog, memory monitoring, etc.
     *
     * All real-time work happens in threads (I/O, App) and ISRs (Audio)
     */

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
