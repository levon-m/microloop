#pragma once

#include <Arduino.h>
#include "EffectQuantization.h"  // For Quantization enum

namespace App {
    void begin();

    void threadLoop();

    Quantization getGlobalQuantization();

    void setGlobalQuantization(Quantization quant);
}
