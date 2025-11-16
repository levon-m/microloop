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
     * 1. Menu screen (if menu is active)
     * 2. Last activated effect (if still active)
     * 3. Any active effect
     * 4. Default/idle screen
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

    /**
     * Show menu screen (takes priority over effect displays)
     *
     * @param menuData Menu information to display
     */
    void showMenu(const MenuDisplayData& menuData);

    /**
     * Hide menu and return to effect/idle display
     */
    void hideMenu();

    /**
     * Check if menu is currently showing
     *
     * @return true if menu is active
     */
    bool isMenuShowing() const;

private:
    // Private constructor (singleton pattern)
    DisplayManager() : m_lastActivatedEffect(EffectID::NONE), m_menuShowing(false) {}

    // Delete copy constructor and assignment (singleton)
    DisplayManager(const DisplayManager&) = delete;
    DisplayManager& operator=(const DisplayManager&) = delete;

    EffectID m_lastActivatedEffect;  // Last activated effect for priority tracking
    bool m_menuShowing;              // True if menu is currently showing
    MenuDisplayData m_currentMenu;   // Current menu data
};
