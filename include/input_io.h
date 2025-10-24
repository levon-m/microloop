#pragma once

#include <Arduino.h>
#include "command.h"

namespace InputIO {
    bool begin();

    void threadLoop();

    bool popCommand(Command& outCmd);

    void setLED(EffectID effectID, bool enabled);

    bool isKeyPressed(uint8_t keyIndex);
}