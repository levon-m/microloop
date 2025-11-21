#pragma once

#include <Arduino.h>
#include "Command.h"

namespace NeokeyInput {
    bool begin();

    void threadLoop();

    bool popCommand(Command& outCmd);

    void setLED(EffectID effectID, bool enabled);

    bool isKeyPressed(uint8_t keyIndex);
}