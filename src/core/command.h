/**
 * command.h - Command system for decoupling input from DSP effects
 *
 * PURPOSE:
 * Provides a generic command abstraction layer that decouples input handling
 * (buttons, MIDI, etc.) from audio effect control. Commands are POD structs
 * that can be safely passed through lock-free queues.
 *
 * DESIGN:
 * - POD (Plain Old Data): Safe for lock-free SPSC queues
 * - Compact: 8 bytes (cache-friendly)
 * - Type-safe: enum class prevents mixing types
 * - Extensible: Generic parameter slots for future features
 *
 * USAGE:
 *   // Toggle an effect
 *   Command cmd{CommandType::EFFECT_TOGGLE, EffectID::CHOKE};
 *   commandQueue.push(cmd);
 *
 *   // Set effect parameter
 *   Command cmd{CommandType::EFFECT_SET_PARAM, EffectID::DELAY};
 *   cmd.param1 = 0;  // Parameter index (0 = delay time)
 *   cmd.value = 22050;  // Value (22050 samples = 0.5s @ 44.1kHz)
 *   commandQueue.push(cmd);
 *
 * THREAD SAFETY:
 * - Commands are immutable after creation (const-correct)
 * - POD type allows safe cross-thread communication
 * - No pointers, no heap allocation
 */

#pragma once

#include <stdint.h>
#include <type_traits>

/**
 * Command Types - What action to perform
 *
 * Design: uint8_t enum for compact storage (1 byte)
 */
enum class CommandType : uint8_t {
    NONE = 0,           // No-op command (used for disabled buttons)

    // Effect control commands
    EFFECT_TOGGLE = 1,   // Toggle effect on/off (read current state, flip it)
    EFFECT_ENABLE = 2,   // Force enable effect (regardless of current state)
    EFFECT_DISABLE = 3,  // Force disable effect (regardless of current state)
    EFFECT_SET_PARAM = 4, // Set effect parameter (uses param1 as index, value as data)

    // Future: Transport control (MIDI START/STOP/CONTINUE)
    // TRANSPORT_PLAY = 10,
    // TRANSPORT_STOP = 11,
    // TRANSPORT_CONTINUE = 12,

    // Future: Loop control
    // LOOP_RECORD = 20,
    // LOOP_OVERDUB = 21,
    // LOOP_CLEAR = 22,

    // Future: Sample control
    // SAMPLE_TRIGGER = 30,
    // SAMPLE_STOP = 31,
};

/**
 * Effect IDs - Which effect to control
 *
 * Design: uint8_t enum for compact storage (1 byte)
 * Max: 255 effects (more than enough for this project)
 */
enum class EffectID : uint8_t {
    NONE = 0,       // No effect (used for NONE commands)
    STUTTER = 1,    // Audio stutter effect (capture and loop playback)
    FREEZE = 2,     // Audio freeze effect (momentary - loops captured buffer)
    CHOKE = 3,      // Audio mute effect (momentary or toggle)
    FUNC = 4        // Function modifier button (no standalone effect)
};

/**
 * Command - Generic command structure
 *
 * SIZE: 8 bytes (2 enums + 2 param bytes + 4 value bytes)
 * ALIGNMENT: 4-byte aligned (efficient on ARM Cortex-M7)
 * POD: Yes (trivially copyable, no constructor/destructor side effects)
 *
 * FIELDS:
 * - type: What action to perform (TOGGLE, ENABLE, DISABLE, SET_PARAM, etc.)
 * - targetEffect: Which effect to control (CHOKE, DELAY, REVERB, etc.)
 * - param1: Generic parameter slot 1 (usage depends on command type)
 * - param2: Generic parameter slot 2 (usage depends on command type)
 * - value: Generic 32-bit value (e.g., delay time in samples, gain in percent)
 *
 * PARAMETER USAGE EXAMPLES:
 *
 * EFFECT_TOGGLE, EFFECT_ENABLE, EFFECT_DISABLE:
 *   - param1/param2/value: unused (set to 0)
 *
 * EFFECT_SET_PARAM:
 *   - param1: Parameter index (which parameter to set)
 *   - param2: Reserved (future use)
 *   - value: Parameter value (units depend on effect and parameter)
 *
 * Example: Set delay time to 0.5 seconds
 *   Command{EFFECT_SET_PARAM, DELAY, 0, 0, 22050}
 *   // param1=0 means "delay time", value=22050 samples (0.5s @ 44.1kHz)
 *
 * Example: Set reverb mix to 50%
 *   Command{EFFECT_SET_PARAM, REVERB, 1, 0, 50}
 *   // param1=1 means "mix level", value=50 (50%)
 */
struct Command {
    CommandType type;       // What action (1 byte)
    EffectID targetEffect;  // Which effect (1 byte)
    uint8_t param1;         // Generic parameter slot 1
    uint8_t param2;         // Generic parameter slot 2
    uint32_t value;         // Generic value (delay time, gain, etc.)

    /**
     * Default constructor - creates NONE command
     */
    constexpr Command()
        : type(CommandType::NONE),
          targetEffect(EffectID::NONE),
          param1(0),
          param2(0),
          value(0) {}

    /**
     * Simple constructor - for commands without parameters
     *
     * @param t Command type
     * @param e Target effect
     */
    constexpr Command(CommandType t, EffectID e)
        : type(t),
          targetEffect(e),
          param1(0),
          param2(0),
          value(0) {}

    /**
     * Full constructor - for commands with value
     *
     * @param t Command type
     * @param e Target effect
     * @param v Value (optional, default 0)
     */
    constexpr Command(CommandType t, EffectID e, uint32_t v)
        : type(t),
          targetEffect(e),
          param1(0),
          param2(0),
          value(v) {}

    /**
     * Parameter constructor - for SET_PARAM commands
     *
     * @param t Command type (should be EFFECT_SET_PARAM)
     * @param e Target effect
     * @param p1 Parameter index
     * @param v Parameter value
     */
    constexpr Command(CommandType t, EffectID e, uint8_t p1, uint32_t v)
        : type(t),
          targetEffect(e),
          param1(p1),
          param2(0),
          value(v) {}
};

// ============================================================================
// COMPILE-TIME CHECKS (ensures Command is safe for lock-free queues)
// ============================================================================

/**
 * Verify Command is POD (Plain Old Data)
 *
 * Required for:
 * - Lock-free SPSC queue (no constructors/destructors)
 * - Memcpy-safe (bitwise copy is valid)
 * - Cross-thread communication (no hidden state)
 */
static_assert(std::is_trivially_copyable<Command>::value,
              "Command must be trivially copyable (POD requirement for lock-free queues)");

/**
 * Verify Command size
 *
 * Expected: 8 bytes
 * - CommandType: 1 byte
 * - EffectID: 1 byte
 * - param1: 1 byte
 * - param2: 1 byte
 * - value: 4 bytes
 * Total: 8 bytes (powers-of-2, cache-friendly)
 */
static_assert(sizeof(Command) == 8,
              "Command should be 8 bytes (2 enums + 2 params + 4 value)");

/**
 * Verify enums are 1 byte
 *
 * Ensures compact storage and predictable layout
 */
static_assert(sizeof(CommandType) == 1, "CommandType should be 1 byte");
static_assert(sizeof(EffectID) == 1, "EffectID should be 1 byte");
