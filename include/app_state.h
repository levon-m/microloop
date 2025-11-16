/**
 * app_state.h - Application state management
 *
 * PURPOSE:
 * Centralized state holder for the application layer. Provides a clear,
 * explicit state machine for application modes and tracks current UI context.
 *
 * DESIGN:
 * - Simple enum-based state machine (no complex FSM library needed)
 * - Single source of truth for "what mode is the app in?"
 * - Lightweight (2 bytes total)
 * - Thread-safe for single consumer (AppLogic thread only)
 *
 * USAGE:
 *   AppState state;
 *   state.setMode(AppMode::EDITING_PARAM);
 *   if (state.getMode() == AppMode::NORMAL) { ... }
 */

#pragma once

#include "effect_manager.h"  // For EffectID
#include <stdint.h>

/**
 * Application modes - high-level state machine
 *
 * These represent the major modes the application can be in, which
 * determines how input is interpreted and what is displayed.
 */
enum class AppMode : uint8_t {
    NORMAL = 0,        // Default mode: effects active, display shows last effect
    EDITING_PARAM = 1, // Encoder touched: display shows parameter being adjusted
    // Future modes:
    // MENU_NAVIGATION = 2,  // Navigating menu system
    // RECORDING = 3,        // Recording a loop
    // PATTERN_EDIT = 4,     // Editing step pattern
};

/**
 * Application state holder
 *
 * Tracks the current mode and context for the application layer.
 * All state is intentionally simple (no complex data structures)
 * to keep the state machine easy to reason about.
 */
class AppState {
public:
    /**
     * Initialize state to default values
     */
    AppState()
        : m_mode(AppMode::NORMAL),
          m_activeEffect(EffectID::NONE) {}

    /**
     * Get current application mode
     */
    AppMode getMode() const {
        return m_mode;
    }

    /**
     * Set application mode
     *
     * @param mode New mode to enter
     */
    void setMode(AppMode mode) {
        m_mode = mode;
    }

    /**
     * Get currently active effect (for display priority)
     *
     * @return EffectID of last activated effect, or NONE
     */
    EffectID getActiveEffect() const {
        return m_activeEffect;
    }

    /**
     * Set currently active effect
     *
     * @param effectID Effect to mark as active
     */
    void setActiveEffect(EffectID effectID) {
        m_activeEffect = effectID;
    }

    /**
     * Check if in normal mode (convenience helper)
     */
    bool isNormalMode() const {
        return m_mode == AppMode::NORMAL;
    }

    /**
     * Check if editing parameters (convenience helper)
     */
    bool isEditingMode() const {
        return m_mode == AppMode::EDITING_PARAM;
    }

private:
    AppMode m_mode;          // Current application mode (1 byte)
    EffectID m_activeEffect; // Last activated effect for display priority (1 byte)
};
