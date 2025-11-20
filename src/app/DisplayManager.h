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
 * - Priority-based display: Audio chain order determines priority (CHOKE > FREEZE > STUTTER)
 * - Stateful: Tracks menu state and manages display transitions
 *
 * USAGE:
 *   DisplayManager::instance().updateDisplay();
 */

#pragma once

#include "EffectManager.h"
#include "OledIO.h"

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
     * Priority logic (effects always override menu):
     * 1. CHOKE effect (if active) - highest effect priority
     * 2. FREEZE effect (if active) - middle priority
     * 3. STUTTER effect (if active) - lowest effect priority
     * 4. Menu screen (if menu is active and no effects active)
     * 5. Default/idle screen
     */
    void updateDisplay();

    /**
     * Set which effect was last activated
     *
     * NOTE: This method is deprecated and no longer used for display priority.
     * Display priority is now based on fixed audio chain order.
     * Kept for backward compatibility.
     *
     * @param effectID Effect to mark as last activated
     */
    void setLastActivatedEffect(EffectID effectID);

    /**
     * Get which effect was last activated
     *
     * NOTE: This method is deprecated and no longer used for display priority.
     * Kept for backward compatibility.
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
