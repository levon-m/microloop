#include "display_manager.h"

namespace DisplayManager {

// Last activated effect (for priority tracking)
static EffectID lastActivatedEffect = EffectID::NONE;

// ========== DISPLAY PRIORITY LOGIC ==========

void updateDisplay() {
    // Check if any effects are active (use priority logic)
    AudioEffectBase* freezeEffect = EffectManager::getEffect(EffectID::FREEZE);
    AudioEffectBase* chokeEffect = EffectManager::getEffect(EffectID::CHOKE);

    bool freezeActive = freezeEffect && freezeEffect->isEnabled();
    bool chokeActive = chokeEffect && chokeEffect->isEnabled();

    // Priority: Last activated effect wins
    if (lastActivatedEffect == EffectID::FREEZE && freezeActive) {
        DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);
    } else if (lastActivatedEffect == EffectID::CHOKE && chokeActive) {
        DisplayIO::showChoke();
    } else if (freezeActive) {
        // Freeze is active but not last activated (show it anyway)
        DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);
    } else if (chokeActive) {
        // Choke is active but not last activated (show it anyway)
        DisplayIO::showChoke();
    } else {
        // No effects active - show default
        DisplayIO::showDefault();
    }
}

void setLastActivatedEffect(EffectID effectID) {
    lastActivatedEffect = effectID;
}

EffectID getLastActivatedEffect() {
    return lastActivatedEffect;
}

void initialize() {
    lastActivatedEffect = EffectID::NONE;
}

}  // namespace DisplayManager
