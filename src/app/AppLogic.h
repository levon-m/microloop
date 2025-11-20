#pragma once

#include <Arduino.h>
#include "EffectQuantization.h"  // For Quantization enum

namespace AppLogic {
    void begin();

    void threadLoop();

    Quantization getGlobalQuantization();

    void setGlobalQuantization(Quantization quant);
}
