/**
 * effect_manager.h - Central registry and command dispatcher for audio effects
 *
 * PURPOSE:
 * Provides a single point of control for all audio effects. Manages effect
 * registration, command routing, and state queries. Decouples application logic
 * from specific effect implementations.
 *
 * DESIGN:
 * - Static class (single global registry, no instances needed)
 * - Fixed-size array registry (no heap allocation)
 * - Linear search (fast for small N, cache-friendly)
 * - Non-owning pointers (effects are global objects, never deleted)
 *
 * ARCHITECTURE:
 *   Button Press → Command → EffectManager::executeCommand() → AudioEffectBase
 *                                        ↓
 *                              [Choke, Delay, Reverb, ...]
 *
 * USAGE:
 *   // Setup (in main.cpp)
 *   AudioEffectChoke choke;
 *   AudioEffectDelay delay;
 *   EffectManager::registerEffect(EffectID::CHOKE, &choke);
 *   EffectManager::registerEffect(EffectID::DELAY, &delay);
 *
 *   // Runtime (in app logic)
 *   Command cmd{CommandType::EFFECT_TOGGLE, EffectID::CHOKE};
 *   if (EffectManager::executeCommand(cmd)) {
 *       Serial.println("Choke toggled");
 *   }
 *
 *   // Query state (for UI)
 *   uint32_t mask = EffectManager::getEnabledEffectsMask();
 *   bool chokeActive = (mask & (1 << static_cast<uint8_t>(EffectID::CHOKE))) != 0;
 *
 * THREAD SAFETY:
 * - Registry populated in setup() (before threads start) → no races
 * - Lookups are read-only after startup → no locking needed
 * - Effect state updates use atomics (handled by effects themselves)
 *
 * PERFORMANCE:
 * - Registration: O(N) check for duplicates, O(1) insert
 * - Command execution: O(N) lookup + O(1) dispatch (N ≤ 8, very fast)
 * - State query: O(N) iteration (N ≤ 8, ~50 cycles total)
 */

#pragma once

#include "audio_effect_base.h"
#include "command.h"
#include <stdint.h>

/**
 * EffectManager - Static class for effect registration and control
 */
class EffectManager {
public:
    /**
     * Maximum number of effects
     *
     * Why 8?
     * - Reasonable limit for live performance system
     * - Small enough for fast linear search
     * - Registry storage: 8 × 8 bytes = 64 bytes RAM
     * - Unlikely to hit limit (typical: 3-5 effects)
     */
    static constexpr uint8_t MAX_EFFECTS = 8;

    // ========================================================================
    // REGISTRATION (Called during setup)
    // ========================================================================

    /**
     * Register an effect in the global registry
     *
     * WHEN: Called in setup(), before threads start
     * WHERE: main.cpp, after effect objects created
     *
     * @param id Unique effect ID (e.g., EffectID::CHOKE)
     * @param effect Pointer to effect instance (must remain valid forever)
     * @return true if registered successfully, false if:
     *         - Registry is full (MAX_EFFECTS reached)
     *         - ID already registered (duplicate)
     *         - effect pointer is null
     *
     * Example:
     *   AudioEffectChoke choke;  // Global object
     *   if (!EffectManager::registerEffect(EffectID::CHOKE, &choke)) {
     *       Serial.println("ERROR: Failed to register choke effect");
     *       while(1);  // Fatal error (halt)
     *   }
     *
     * Thread safety: Not thread-safe (call only from setup)
     * Real-time safety: Not applicable (called before real-time operation)
     */
    static bool registerEffect(EffectID id, AudioEffectBase* effect);

    // ========================================================================
    // COMMAND DISPATCH (Called from App thread)
    // ========================================================================

    /**
     * Execute a command (routes to appropriate effect)
     *
     * WHEN: Called from App thread, when command popped from queue
     * WHERE: app_logic.cpp, in command processing loop
     *
     * @param cmd Command to execute (contains type and target effect)
     * @return true if executed successfully, false if:
     *         - Effect not found (target not registered)
     *         - Unknown command type
     *
     * Supported command types:
     * - EFFECT_TOGGLE: Call effect->toggle()
     * - EFFECT_ENABLE: Call effect->enable()
     * - EFFECT_DISABLE: Call effect->disable()
     * - EFFECT_SET_PARAM: Call effect->setParameter(cmd.param1, cmd.value)
     *
     * Example:
     *   Command cmd{CommandType::EFFECT_TOGGLE, EffectID::CHOKE};
     *   if (EffectManager::executeCommand(cmd)) {
     *       AudioEffectBase* effect = EffectManager::getEffect(EffectID::CHOKE);
     *       Serial.print(effect->getName());
     *       Serial.println(effect->isEnabled() ? " ON" : " OFF");
     *   } else {
     *       Serial.println("Command failed!");
     *   }
     *
     * Thread safety: Thread-safe (called from App thread only)
     * Real-time safety: Safe (no blocking, bounded execution time)
     */
    static bool executeCommand(const Command& cmd);

    // ========================================================================
    // QUERY INTERFACE (For UI, debugging, state inspection)
    // ========================================================================

    /**
     * Get effect by ID (for direct access)
     *
     * WHEN: When you need to call effect methods directly
     * WHERE: Serial command handlers, UI code
     *
     * @param id Effect ID to look up
     * @return Pointer to effect, or nullptr if not registered
     *
     * Example:
     *   AudioEffectBase* effect = EffectManager::getEffect(EffectID::CHOKE);
     *   if (effect) {
     *       Serial.print("Choke state: ");
     *       Serial.println(effect->isEnabled() ? "ON" : "OFF");
     *   }
     *
     * Thread safety: Thread-safe (read-only after setup)
     * Real-time safety: Safe (O(N) lookup, N ≤ 8)
     */
    static AudioEffectBase* getEffect(EffectID id);

    /**
     * Get bitmask of enabled effects
     *
     * WHEN: UI needs to display which effects are active
     * WHERE: Display update code, serial status output
     *
     * @return Bitmask where bit N = 1 if EffectID N is enabled
     *
     * Example:
     *   uint32_t mask = EffectManager::getEnabledEffectsMask();
     *   bool chokeActive = (mask & (1 << static_cast<uint8_t>(EffectID::CHOKE))) != 0;
     *   bool delayActive = (mask & (1 << static_cast<uint8_t>(EffectID::DELAY))) != 0;
     *
     * Thread safety: Thread-safe (queries atomic state in effects)
     * Real-time safety: Safe (O(N) iteration, N ≤ 8)
     */
    static uint32_t getEnabledEffectsMask();

    /**
     * Get effect name by ID
     *
     * WHEN: Debugging, serial output, UI display
     * WHERE: Anywhere you need human-readable effect name
     *
     * @param id Effect ID to query
     * @return Effect name (e.g., "Choke"), or "Unknown" if not registered
     *
     * Example:
     *   const char* name = EffectManager::getEffectName(EffectID::CHOKE);
     *   Serial.print("Effect: ");
     *   Serial.println(name);  // "Effect: Choke"
     *
     * Thread safety: Thread-safe (read-only after setup)
     * Real-time safety: Safe (O(N) lookup, N ≤ 8)
     */
    static const char* getEffectName(EffectID id);

    /**
     * Get number of registered effects
     *
     * WHEN: Debugging, verification that effects registered correctly
     * WHERE: Serial status output, test code
     *
     * @return Number of effects in registry (0 to MAX_EFFECTS)
     *
     * Example:
     *   Serial.print("Registered effects: ");
     *   Serial.println(EffectManager::getNumEffects());
     *
     * Thread safety: Thread-safe (read-only after setup)
     * Real-time safety: Safe (simple counter read)
     */
    static uint8_t getNumEffects() { return s_numEffects; }

private:
    // ========================================================================
    // INTERNAL DATA STRUCTURES
    // ========================================================================

    /**
     * Effect registry entry
     *
     * Stores mapping: EffectID → AudioEffectBase pointer
     */
    struct EffectEntry {
        EffectID id;                // Effect identifier (e.g., CHOKE, DELAY)
        AudioEffectBase* effect;    // Non-owning pointer to effect object

        // Default constructor (for static array initialization)
        EffectEntry() : id(EffectID::NONE), effect(nullptr) {}
    };

    /**
     * Static registry storage
     *
     * Fixed-size array, no heap allocation
     * Memory: 8 entries × 8 bytes = 64 bytes RAM
     */
    static EffectEntry s_effects[MAX_EFFECTS];

    /**
     * Number of registered effects
     *
     * Range: 0 to MAX_EFFECTS
     * Used for bounds checking and iteration
     */
    static uint8_t s_numEffects;
};
