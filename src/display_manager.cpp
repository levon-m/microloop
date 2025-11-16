#include "display_manager.h"

void DisplayManager::initialize() {
    m_lastActivatedEffect = EffectID::NONE;
    m_menuShowing = false;
}

void DisplayManager::updateDisplay() {
    // Priority 1: Menu (if showing)
    if (m_menuShowing) {
        DisplayIO::showMenu(m_currentMenu);
        return;
    }

    // Priority 2+: Effects or default
    // Check if any effects are active (use priority logic)
    AudioEffectBase* freezeEffect = EffectManager::getEffect(EffectID::FREEZE);
    AudioEffectBase* chokeEffect = EffectManager::getEffect(EffectID::CHOKE);

    bool freezeActive = freezeEffect && freezeEffect->isEnabled();
    bool chokeActive = chokeEffect && chokeEffect->isEnabled();

    // Priority 2: Last activated effect wins
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

void DisplayManager::showMenu(const MenuDisplayData& menuData) {
    m_menuShowing = true;
    m_currentMenu = menuData;
}

void DisplayManager::hideMenu() {
    m_menuShowing = false;
}

bool DisplayManager::isMenuShowing() const {
    return m_menuShowing;
}