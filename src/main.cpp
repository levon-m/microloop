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

// ========== AUDIO GRAPH ==========
/**
 * Simple stereo passthrough: I2S Input → I2S Output
 *
 * WHY THIS TOPOLOGY?
 * - Minimal latency: Direct connection, no processing
 * - Proves audio path works (can hear input at output)
 * - Future: Insert effects/loopers between input and output
 *
 * LATENCY:
 * - Audio Library block size: 128 samples
 * - At 44.1kHz: 128/44100 ≈ 2.9ms per block
 * - Total latency: ~6ms (input buffer + output buffer)
 */
AudioInputI2S i2s_in;
AudioOutputI2S i2s_out;
AudioConnection patchCord1(i2s_in, 0, i2s_out, 0);  // Left channel
AudioConnection patchCord2(i2s_in, 1, i2s_out, 1);  // Right channel

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
 * App Thread: Application logic and UI
 *
 * PRIORITY: Normal (can afford to be slower)
 * STACK: 3072 bytes (room for future UI, displays, etc.)
 *
 * WHY SEPARATE THREAD?
 * - App logic can be bursty (UI updates, calculations)
 * - Isolation: App bugs won't crash MIDI I/O
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
    Serial.println("MIDI: OK (DIN on Serial8, RX=pin34, TX=pin35)");

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
    int appThreadId = threads.addThread(appThreadEntry, 3072);

    if (ioThreadId < 0 || appThreadId < 0) {
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
}

// ========== MAIN LOOP ==========

void loop() {
    /**
     * Main loop is EMPTY
     *
     * WHY?
     * - All work happens in threads (I/O, App)
     * - Audio runs in ISR (completely independent)
     * - Main loop has nothing to do
     *
     * ALTERNATIVE DESIGNS:
     * 1. Run app logic here (blocking, no concurrency)
     * 2. Use FreeRTOS (more complex, overkill for this project)
     * 3. Hand-rolled scheduler (reinventing the wheel)
     *
     * TeensyThreads is perfect middle ground:
     * - Simple API (addThread, yield, delay)
     * - Lightweight (minimal overhead)
     * - Integrates with Arduino ecosystem
     */

    // Nothing to do - threads handle everything
    // We could add low-priority housekeeping here if needed:
    // - Memory leak detection
    // - Watchdog feeding
    // - Over-temperature monitoring
    // - etc.
}
