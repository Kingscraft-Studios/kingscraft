#pragma once

#include "Core/Attributes.hpp"
#include "Core/World/TerrainGenSettings.hpp"
#include <cstdint>
#include <glm/glm.hpp>

namespace kc {

    // Version knobs recorded in world.kcw. There is no migration pipeline yet,
    // so each constant simply documents what this session produced. Readers
    // gate on WORLD_FORMAT_VERSION and refuse anything else; the generator and
    // block-registry knobs exist so an old world can be detected (and later
    // migrated) instead of silently misread.
    constexpr uint32_t WORLD_FORMAT_VERSION = 1;      // line layout of world.kcw
    constexpr uint32_t GENERATOR_ID = 1;              // which generator made the terrain
    constexpr uint32_t GENERATOR_VERSION = 1;         // version of that generator
    constexpr uint32_t BLOCK_REGISTRY_VERSION = 1;    // block table worlds are saved against

    // Everything about the world and the player that must survive a restart.
    // world.kcw stores this as text directives; unknown directives are ignored
    // on read, so adding a field later only needs a new tag, not a format bump.
    struct WorldMetadata {
        TerrainGenSettings settings;         // generator config: seed + noise params (height shaping now lives on Biomes)
        uint32_t generatorId = GENERATOR_ID;
        uint32_t generatorVersion = GENERATOR_VERSION;
        uint32_t blockRegistryVersion = BLOCK_REGISTRY_VERSION;
        uint64_t worldTime = 0;               // ticks since world creation

        glm::vec3 spawnPos{67.5f, 15.0f - Attributes::EYE_HEIGHT, 67.5f};  // respawn point (body coords)
        glm::vec3 playerPos{67.5f, 15.0f - Attributes::EYE_HEIGHT, 67.5f}; // last player body position
        float yaw = 0.0f;                     // last camera facing
        float pitch = -35.0f;
    };

} // namespace kc