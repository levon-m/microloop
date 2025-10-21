#pragma once

#include "effect_manager.h"
#include "display_io.h"

/**
 * Display Manager Module
 *
 * Manages display priority and state transitions based on active effects.
 * Eliminates code duplication for display update logic.
 *
 * DESIGN:
 * - Priority system: Last activated effect takes precedence
 * - Automatic fallback to default when no effects active
 * - Centralized display update logic
 *
 * PRIORITY RULES:
 * 1. If last activated effect is still active, show it
 * 2. Otherwise, show any other active effect
 * 3. If no effects active, show default
 */

namespace DisplayManager {

/**
 * Update display based on current effect states
 * Automatically shows highest-priority active effect or default.
 */
void updateDisplay();

/**
 * Set the last activated effect (for priority tracking)
 */
void setLastActivatedEffect(EffectID effectID);

/**
 * Get the last activated effect
 */
EffectID getLastActivatedEffect();

/**
 * Initialize display manager
 */
void initialize();

}  // namespace DisplayManager
