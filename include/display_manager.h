/**
 * display_manager.h - Display state management
 *
 * PURPOSE:
 * Manages display state and determines what to show on the OLED based on
 * effect states and user interactions. Decouples display logic from effect
 * logic.
 *
 * DESIGN:
 * - Singleton pattern (single global instance)
 * - Priority-based display: Last activated effect takes precedence
 * - Stateful: Remembers which effect was last activated
 *
 * USAGE:
 *   DisplayManager::instance().updateDisplay();
 *   DisplayManager::instance().setLastActivatedEffect(EffectID::CHOKE);
 */

#pragma once

#include "effect_manager.h"
#include "display_io.h"

/**
 * Display state manager
 *
 * Tracks display state and determines what bitmap to show based on
 * active effects and priority rules.
 */
class DisplayManager {
public:
    /**
     * Get singleton instance
     */
    static DisplayManager& instance() {
        static DisplayManager s_instance;
        return s_instance;
    }

    /**
     * Initialize display state
     * Call once during setup
     */
    void initialize();

    /**
     * Update display based on current effect states
     *
     * Priority logic:
     * 1. Last activated effect (if still active)
     * 2. Any active effect
     * 3. Default/idle screen
     */
    void updateDisplay();

    /**
     * Set which effect was last activated (for display priority)
     *
     * @param effectID Effect to mark as last activated
     */
    void setLastActivatedEffect(EffectID effectID);

    /**
     * Get which effect was last activated
     *
     * @return EffectID of last activated effect, or NONE
     */
    EffectID getLastActivatedEffect() const;

private:
    // Private constructor (singleton pattern)
    DisplayManager() : m_lastActivatedEffect(EffectID::NONE) {}

    // Delete copy constructor and assignment (singleton)
    DisplayManager(const DisplayManager&) = delete;
    DisplayManager& operator=(const DisplayManager&) = delete;

    EffectID m_lastActivatedEffect;  // Last activated effect for priority tracking
};
