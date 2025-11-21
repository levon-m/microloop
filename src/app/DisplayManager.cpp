#include "DisplayManager.h"

void DisplayManager::initialize() {
    m_lastActivatedEffect = EffectID::NONE;
    m_menuShowing = false;
}

void DisplayManager::updateDisplay() {
    // Priority 1: CHOKE effect (highest priority - last in audio chain)
    IEffectAudio* chokeEffect = EffectManager::getEffect(EffectID::CHOKE);
    if (chokeEffect && chokeEffect->isEnabled()) {
        Ssd1306Display::showChoke();
        return;
    }

    // Priority 2: FREEZE effect (middle priority)
    IEffectAudio* freezeEffect = EffectManager::getEffect(EffectID::FREEZE);
    if (freezeEffect && freezeEffect->isEnabled()) {
        Ssd1306Display::showBitmap(BitmapID::FREEZE_ACTIVE);
        return;
    }

    // Priority 3: STUTTER effect (lowest effect priority - first in audio chain)
    IEffectAudio* stutterEffect = EffectManager::getEffect(EffectID::STUTTER);
    if (stutterEffect && stutterEffect->isEnabled()) {
        Ssd1306Display::showBitmap(BitmapID::STUTTER_ACTIVE);
        return;
    }

    // Priority 4: Menu (if showing and no effects active)
    if (m_menuShowing) {
        Ssd1306Display::showMenu(m_currentMenu);
        return;
    }

    // Priority 5: Default/idle (no effects active, no menu)
    Ssd1306Display::showDefault();
}

void DisplayManager::setLastActivatedEffect(EffectID effectID) {
    m_lastActivatedEffect = effectID;
}

EffectID DisplayManager::getLastActivatedEffect() const {
    return m_lastActivatedEffect;
}

void DisplayManager::showMenu(const MenuDisplayData& menuData) {
    m_menuShowing = true;
    m_currentMenu = menuData;
    updateDisplay();
}

void DisplayManager::hideMenu() {
    m_menuShowing = false;
    updateDisplay();
}

bool DisplayManager::isMenuShowing() const {
    return m_menuShowing;
}