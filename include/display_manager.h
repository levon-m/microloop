#pragma once

#include "effect_manager.h"
#include "display_io.h"

namespace DisplayManager {

void updateDisplay();

void setLastActivatedEffect(EffectID effectID);

EffectID getLastActivatedEffect();

void initialize();

}