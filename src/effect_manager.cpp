/**
 * effect_manager.cpp - Implementation of effect registry and command dispatcher
 */

#include "effect_manager.h"
#include <Arduino.h>  // For Serial debug output

// ============================================================================
// STATIC STORAGE
// ============================================================================

/**
 * Effect registry storage
 *
 * Static array, initialized to NONE/nullptr
 * Memory: 8 entries × 8 bytes = 64 bytes RAM
 */
EffectManager::EffectEntry EffectManager::s_effects[MAX_EFFECTS] = {};

/**
 * Number of registered effects
 *
 * Initialized to 0 (no effects registered yet)
 */
uint8_t EffectManager::s_numEffects = 0;

// ============================================================================
// REGISTRATION
// ============================================================================

bool EffectManager::registerEffect(EffectID id, AudioEffectBase* effect) {
    // Validate inputs
    if (effect == nullptr) {
        Serial.println("ERROR: EffectManager::registerEffect() - effect is null");
        return false;
    }

    if (id == EffectID::NONE) {
        Serial.println("ERROR: EffectManager::registerEffect() - cannot register NONE");
        return false;
    }

    // Check if registry is full
    if (s_numEffects >= MAX_EFFECTS) {
        Serial.print("ERROR: EffectManager::registerEffect() - registry full (max ");
        Serial.print(MAX_EFFECTS);
        Serial.println(" effects)");
        return false;
    }

    // Check for duplicate ID
    for (uint8_t i = 0; i < s_numEffects; i++) {
        if (s_effects[i].id == id) {
            Serial.print("ERROR: EffectManager::registerEffect() - ID ");
            Serial.print(static_cast<uint8_t>(id));
            Serial.println(" already registered");
            return false;
        }
    }

    // Add to registry
    s_effects[s_numEffects].id = id;
    s_effects[s_numEffects].effect = effect;
    s_numEffects++;

    // Success - log registration
    Serial.print("EffectManager: Registered effect '");
    Serial.print(effect->getName());
    Serial.print("' (ID ");
    Serial.print(static_cast<uint8_t>(id));
    Serial.print(", total: ");
    Serial.print(s_numEffects);
    Serial.println(")");

    return true;
}

// ============================================================================
// COMMAND DISPATCH
// ============================================================================

bool EffectManager::executeCommand(const Command& cmd) {
    // Special case: NONE command is a no-op (used for disabled buttons)
    if (cmd.type == CommandType::NONE) {
        return true;  // Not an error, just do nothing
    }

    // Look up target effect
    AudioEffectBase* effect = getEffect(cmd.targetEffect);
    if (effect == nullptr) {
        // Effect not found - log error
        Serial.print("ERROR: EffectManager::executeCommand() - effect ID ");
        Serial.print(static_cast<uint8_t>(cmd.targetEffect));
        Serial.println(" not registered");
        return false;
    }

    // Dispatch command based on type
    switch (cmd.type) {
        case CommandType::EFFECT_TOGGLE:
            effect->toggle();
            return true;

        case CommandType::EFFECT_ENABLE:
            effect->enable();
            return true;

        case CommandType::EFFECT_DISABLE:
            effect->disable();
            return true;

        case CommandType::EFFECT_SET_PARAM:
            // Convert uint32_t value to float (parameter methods use float)
            effect->setParameter(cmd.param1, static_cast<float>(cmd.value));
            return true;

        default:
            // Unknown command type
            Serial.print("ERROR: EffectManager::executeCommand() - unknown command type ");
            Serial.println(static_cast<uint8_t>(cmd.type));
            return false;
    }
}

// ============================================================================
// QUERY INTERFACE
// ============================================================================

AudioEffectBase* EffectManager::getEffect(EffectID id) {
    // Linear search (fast for small N, cache-friendly)
    for (uint8_t i = 0; i < s_numEffects; i++) {
        if (s_effects[i].id == id) {
            return s_effects[i].effect;
        }
    }

    // Not found
    return nullptr;
}

uint32_t EffectManager::getEnabledEffectsMask() {
    uint32_t mask = 0;

    // Iterate all registered effects
    for (uint8_t i = 0; i < s_numEffects; i++) {
        // If effect is enabled, set its bit in the mask
        if (s_effects[i].effect->isEnabled()) {
            // Bit position = effect ID
            uint8_t bitPos = static_cast<uint8_t>(s_effects[i].id);
            mask |= (1U << bitPos);
        }
    }

    return mask;
}

const char* EffectManager::getEffectName(EffectID id) {
    AudioEffectBase* effect = getEffect(id);
    if (effect != nullptr) {
        return effect->getName();
    }

    // Effect not found - return generic "Unknown"
    return "Unknown";
}
