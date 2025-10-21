/**
 * audio_effect_base.h - Abstract base class for all audio effects
 *
 * PURPOSE:
 * Provides a common interface for controlling audio effects (enable/disable,
 * parameter setting, state queries). Enables polymorphic effect management
 * via EffectManager without hardcoded switch statements.
 *
 * DESIGN:
 * - Inherits from AudioStream (Teensy Audio Library)
 * - Pure virtual control methods (must be implemented by derived classes)
 * - Virtual parameter methods (optional, default = no-op)
 * - Audio processing (update()) is NOT virtual (each effect implements directly)
 *
 * PERFORMANCE:
 * - Virtual calls only from App thread (not audio ISR)
 * - Overhead: ~10-20 CPU cycles per call (negligible)
 * - Audio processing remains non-virtual (no vtable lookup)
 *
 * USAGE:
 *   class AudioEffectDelay : public AudioEffectBase {
 *   public:
 *       AudioEffectDelay() : AudioEffectBase(2) {}  // Stereo (2 inputs)
 *
 *       void enable() override { m_enabled.store(true); }
 *       void disable() override { m_enabled.store(false); }
 *       void toggle() override { m_enabled.store(!m_enabled.load()); }
 *       bool isEnabled() const override { return m_enabled.load(); }
 *       const char* getName() const override { return "Delay"; }
 *
 *       void update() override {
 *           // Audio processing (called by audio ISR)
 *           // NOT a virtual call (compiler resolves at compile time)
 *       }
 *
 *   private:
 *       std::atomic<bool> m_enabled;
 *   };
 *
 * THREAD SAFETY:
 * - Control methods (enable/disable/toggle) are thread-safe
 * - Derived classes MUST use atomics for state shared with audio ISR
 * - Parameter methods are called from App thread only
 *
 * CRITICAL: Audio Processing (update())
 * - Each derived class implements update() directly
 * - Compiler resolves to concrete implementation (no virtual dispatch)
 * - AudioStream calls update() on the concrete type, not through vtable
 * - This preserves real-time performance in audio ISR
 */

#pragma once

#include <Audio.h>

/**
 * AudioEffectBase - Abstract interface for audio effects
 *
 * All effects inherit from this class and AudioStream.
 * Provides standard control interface (enable/disable/parameters).
 * Audio processing (update()) is implemented directly by each effect.
 */
class AudioEffectBase : public AudioStream {
public:
    /**
     * Constructor
     *
     * @param numInputs Number of input channels (1 = mono, 2 = stereo)
     *
     * Note: Derived classes pass this to AudioStream constructor
     * Example: AudioEffectChoke() : AudioEffectBase(2) {}  // Stereo
     */
    AudioEffectBase(uint8_t numInputs)
        : AudioStream(numInputs, inputQueueArray) {}

    /**
     * Virtual destructor (required for polymorphic deletion)
     *
     * Note: We never actually delete effects (they're global static objects),
     * but good practice for abstract base classes.
     */
    virtual ~AudioEffectBase() = default;

    // ========================================================================
    // CONTROL INTERFACE (Pure Virtual - Must Implement)
    // ========================================================================

    /**
     * Enable effect
     *
     * Thread-safe: Callable from any thread
     * Real-time safe: No blocking, no allocation
     *
     * Implementation: Derived classes typically set atomic bool to true
     */
    virtual void enable() = 0;

    /**
     * Disable effect
     *
     * Thread-safe: Callable from any thread
     * Real-time safe: No blocking, no allocation
     *
     * Implementation: Derived classes typically set atomic bool to false
     */
    virtual void disable() = 0;

    /**
     * Toggle effect on/off
     *
     * Thread-safe: Callable from any thread
     * Real-time safe: No blocking, no allocation
     *
     * Implementation: Derived classes read current state, flip it
     */
    virtual void toggle() = 0;

    /**
     * Check if effect is enabled
     *
     * Thread-safe: Callable from any thread
     * Real-time safe: No blocking, no allocation
     *
     * @return true if effect is currently enabled (processing audio)
     *
     * Implementation: Derived classes typically read atomic bool
     */
    virtual bool isEnabled() const = 0;

    /**
     * Get effect name (for debugging, UI, serial output)
     *
     * @return Human-readable effect name (e.g., "Choke", "Delay", "Reverb")
     *
     * Implementation: Return string literal (stored in flash, not RAM)
     */
    virtual const char* getName() const = 0;

    // ========================================================================
    // PARAMETER INTERFACE (Virtual - Optional Override)
    // ========================================================================

    /**
     * Set effect parameter
     *
     * Thread-safe: Callable from App thread
     * Real-time safe: Depends on derived class implementation
     *
     * @param paramIndex Which parameter to set (effect-specific)
     * @param value Parameter value (units depend on effect and parameter)
     *
     * Examples:
     *   Delay: paramIndex=0 (delay time), value=22050 (samples)
     *   Reverb: paramIndex=1 (mix level), value=50 (percent)
     *   Gain: paramIndex=0 (volume), value=75 (percent)
     *
     * Default implementation: No-op (effects without parameters ignore)
     *
     * Note: If effect has parameters, override this method
     */
    virtual void setParameter(uint8_t paramIndex, float value) {
        // Default: no parameters
        (void)paramIndex;  // Suppress unused warning
        (void)value;
    }

    /**
     * Get effect parameter (for UI display, debugging)
     *
     * Thread-safe: Callable from App thread
     * Real-time safe: Depends on derived class implementation
     *
     * @param paramIndex Which parameter to get (effect-specific)
     * @return Current parameter value (units depend on effect and parameter)
     *
     * Default implementation: Returns 0.0 (effects without parameters)
     *
     * Note: If effect has parameters, override this method
     */
    virtual float getParameter(uint8_t paramIndex) const {
        // Default: no parameters
        (void)paramIndex;  // Suppress unused warning
        return 0.0f;
    }

    // ========================================================================
    // AUDIO PROCESSING (NOT Virtual - Implemented Directly)
    // ========================================================================

    /**
     * Audio processing callback (called by audio ISR every 128 samples)
     *
     * CRITICAL: This method is NOT virtual!
     * - Each derived class implements update() directly
     * - Compiler resolves to concrete implementation at compile time
     * - No vtable lookup overhead in audio path
     *
     * WHY NOT VIRTUAL?
     * - Audio ISR is the hottest code path (called every 2.9ms)
     * - Virtual call overhead: ~10-20 cycles (acceptable in App thread)
     * - In audio ISR: Every cycle matters, minimize overhead
     * - AudioStream framework calls update() on concrete type (not through base pointer)
     *
     * Implementation in derived classes:
     *   virtual void update() override {
     *       audio_block_t* block = receiveWritable(0);
     *       if (!block) return;
     *
     *       // Process audio samples
     *       if (isEnabled()) {
     *           // Apply effect
     *       }
     *
     *       transmit(block, 0);
     *       release(block);
     *   }
     *
     * Note: Declared here for documentation, but each effect implements directly
     */
    // virtual void update() override = 0;  // NOT declared (AudioStream handles this)

protected:
    /**
     * Input queue storage (required by AudioStream)
     *
     * Size: 2 (stereo effects have 2 inputs)
     * For mono effects, only slot 0 is used
     *
     * Note: AudioStream needs this array for receiveReadOnly()/receiveWritable()
     */
    audio_block_t* inputQueueArray[2];
};
