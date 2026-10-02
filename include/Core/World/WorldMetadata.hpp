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
    //
    // v2: the world grew to Minecraft's vertical extent (minY -64, height 384,
    // sea level 63) and the terrain became a vanilla density-function pipeline.
    // Region overlays from a v1 world are 100 blocks tall and would be read as
    // if their rows described the bottom of a much taller column, so v1 worlds
    // must not be loaded — delete world/ and start again.
    //
    // generator v3: the terrain shape stopped being hard-coded. The spline knots
    // that decided how rugged the wilderness is and how much the inland lowlands
    // roll moved out of OverworldNoise and into TerrainShape (persisted in
    // world.kcw as #RB/#LP/#LH/#LT). The defaults are the same numbers vanilla
    // uses, so the shape is unchanged until a dial is edited — but an edited dial
    // makes an unedited chunk regenerate differently from a region overlay that
    // was saved against the old shape, so worlds wanting the new look should
    // start fresh. GENERATOR_VERSION is recorded but not yet enforced on load;
    // validation is a known gap.
    constexpr uint32_t WORLD_FORMAT_VERSION = 2;      // line layout of world.kcw
    constexpr uint32_t GENERATOR_ID = 1;              // which generator made the terrain
    constexpr uint32_t GENERATOR_VERSION = 3;         // version of that generator
    constexpr uint32_t BLOCK_REGISTRY_VERSION = 1;    // block table worlds are saved against

    // Everything about the world and the player that must survive a restart.
    // world.kcw stores this as text directives; unknown directives are ignored
    // on read, so adding a field later only needs a new tag, not a format bump.
    struct WorldMetadata {
        TerrainGenSettings settings;         // generator config: seed, sea level, terrain shape dials
        uint32_t generatorId = GENERATOR_ID;
        uint32_t generatorVersion = GENERATOR_VERSION;
        uint32_t blockRegistryVersion = BLOCK_REGISTRY_VERSION;
        uint64_t worldTime = 0;               // ticks since world creation

        // Default spawn sits on a sea-level beach rather than at the old y=15,
        // which is now deep underground. World::tick snaps this onto the real
        // surface as soon as the column is streamed in.
        glm::vec3 spawnPos{67.5f, 64.0f - Attributes::EYE_HEIGHT, 67.5f};
        glm::vec3 playerPos{67.5f, 64.0f - Attributes::EYE_HEIGHT, 67.5f};
        float yaw = 0.0f;                     // last camera facing
        float pitch = -35.0f;
    };

} // namespace kc