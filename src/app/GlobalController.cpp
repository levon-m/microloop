#include "GlobalController.h"
#include "DisplayManager.h"
#include "EncoderHandler.h"
#include <Arduino.h>

GlobalController::GlobalController()
    : m_currentParameter(Parameter::QUANTIZATION) {
}

const char* GlobalController::parameterName(Parameter param) {
    switch (param) {
        case Parameter::QUANTIZATION: return "Quantization";
        // Future parameters:
        // case Parameter::MASTER_VOLUME: return "Master Volume";
        // case Parameter::TEMPO_MULTIPLIER: return "Tempo Multiplier";
        // case Parameter::SWING: return "Swing";
        default: return "Unknown";
    }
}

// ========== HELPER FUNCTIONS ==========

/**
 * Clamp index to valid range
 */
static int8_t clampIndex(int8_t value, int8_t minValue, int8_t maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

// ========== ENCODER BINDING ==========

void GlobalController::bindToEncoder(EncoderHandler::Handler& encoder,
                                     AnyEncoderTouchedFn anyTouchedExcept) {
    // Button press: Cycle between global parameters
    encoder.onButtonPress([this]() {
        // For now, only one parameter (QUANTIZATION), but ready for future expansion
        Parameter current = m_currentParameter;

        // Cycle through parameters (when more are added)
        switch (current) {
            case Parameter::QUANTIZATION:
                // When we add more parameters, cycle to the next one:
                // m_currentParameter = Parameter::MASTER_VOLUME;
                // For now, just stay on QUANTIZATION
                m_currentParameter = Parameter::QUANTIZATION;
                Serial.println("Global Parameter: QUANTIZATION");
                break;
            // Future parameters:
            // case Parameter::MASTER_VOLUME:
            //     m_currentParameter = Parameter::TEMPO_MULTIPLIER;
            //     Serial.println("Global Parameter: TEMPO_MULTIPLIER");
            //     break;
            // etc.
        }
        // Display update handled by onDisplayUpdate callback
    });

    // Value change: Adjust current parameter
    encoder.onValueChange([this](int8_t delta) {
        Parameter param = m_currentParameter;

        if (param == Parameter::QUANTIZATION) {
            // Adjust global quantization (QUANT_32 → QUANT_16 → QUANT_8 → QUANT_4)
            int8_t currentIndex = static_cast<int8_t>(EffectQuantization::getGlobalQuantization());
            int8_t newIndex = clampIndex(currentIndex + delta, 0, 3);

            if (newIndex != currentIndex) {
                Quantization newQuant = static_cast<Quantization>(newIndex);
                EffectQuantization::setGlobalQuantization(newQuant);
                Serial.print("Global Quantization: ");
                Serial.println(EffectQuantization::quantizationName(newQuant));

                MenuDisplayData menuData;
                menuData.topText = "GLOBAL->Quantization";
                menuData.middleText = EffectQuantization::quantizationName(newQuant);
                menuData.numOptions = 4;
                menuData.selectedIndex = newIndex;
                DisplayManager::instance().showMenu(menuData);
            }
        }
        // Future parameters:
        // else if (param == Parameter::MASTER_VOLUME) {
        //     // Adjust master volume (0-100)
        //     // Implementation here
        // }
        // etc.
    });

    // Display update: Show current parameter or return to effect display
    encoder.onDisplayUpdate([this, &encoder, anyTouchedExcept](bool isTouched) {
        if (isTouched) {
            Parameter param = m_currentParameter;

            if (param == Parameter::QUANTIZATION) {
                Quantization quant = EffectQuantization::getGlobalQuantization();
                MenuDisplayData menuData;
                menuData.topText = "GLOBAL->Quantization";
                menuData.middleText = EffectQuantization::quantizationName(quant);
                menuData.numOptions = 4;
                menuData.selectedIndex = static_cast<uint8_t>(quant);
                DisplayManager::instance().showMenu(menuData);
            }
            // Future parameters:
            // else if (param == Parameter::MASTER_VOLUME) {
            //     // Show master volume display
            // }
            // etc.
        } else {
            // Cooldown expired - only hide menu if NO other encoders are touched
            if (!anyTouchedExcept(&encoder)) {
                DisplayManager::instance().hideMenu();
            }
        }
    });
}
