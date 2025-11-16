#pragma once

#include <Arduino.h>
#include "effect_quantization.h"  // For Quantization enum

namespace AppLogic {
    void begin();

    void threadLoop();

    Quantization getGlobalQuantization();

    void setGlobalQuantization(Quantization quant);
}
