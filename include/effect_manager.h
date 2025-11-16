#pragma once

#include "audio_effect_base.h"
#include "command.h"
#include <stdint.h>

class EffectManager {
public:
    static constexpr uint8_t MAX_EFFECTS = 4;

    static bool registerEffect(EffectID id, AudioEffectBase* effect);

    static bool executeCommand(const Command& cmd);

    static AudioEffectBase* getEffect(EffectID id);

    //static uint32_t getEnabledEffectsMask();

    //static const char* getEffectName(EffectID id);

    static uint8_t getNumEffects() { return s_numEffects; }

private:
    struct EffectEntry {
        EffectID id;                // Effect identifier
        AudioEffectBase* effect;    // Non-owning pointer to effect object

        // Default constructor (for static array initialization)
        EffectEntry() : id(EffectID::NONE), effect(nullptr) {}
    };

    static EffectEntry s_effects[MAX_EFFECTS];

    static uint8_t s_numEffects;
};
