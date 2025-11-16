#include "display_manager.h"

void DisplayManager::initialize() {
    m_lastActivatedEffect = EffectID::NONE;
}

void DisplayManager::updateDisplay() {
    // Check if any effects are active (use priority logic)
    AudioEffectBase* freezeEffect = EffectManager::getEffect(EffectID::FREEZE);
    AudioEffectBase* chokeEffect = EffectManager::getEffect(EffectID::CHOKE);

    bool freezeActive = freezeEffect && freezeEffect->isEnabled();
    bool chokeActive = chokeEffect && chokeEffect->isEnabled();

    // Priority: Last activated effect wins
    if (m_lastActivatedEffect == EffectID::FREEZE && freezeActive) {
        DisplayIO::showBitmap(BitmapID::FREEZE_ACTIVE);
    } else if (m_lastActivatedEffect == EffectID::CHOKE && chokeActive) {
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

void DisplayManager::setLastActivatedEffect(EffectID effectID) {
    m_lastActivatedEffect = effectID;
}

EffectID DisplayManager::getLastActivatedEffect() const {
    return m_lastActivatedEffect;
}