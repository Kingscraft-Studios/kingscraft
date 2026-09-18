#pragma once

#include "Core/Blocks/Block.hpp"
#include "Core/RegistryKey.hpp"
#include <vector>

namespace kc {

    // Plain value struct: the generator configuration for a world. Carried by
    // WorldMetadata (so it persists in world.kcw) and passed into the terrain
    // generator at construction; the generator can also be re-seeded later via
    // TerrainGenerator::setSettings (e.g. after an async world.kcw load).
    //
    // Deliberately does NOT include Blocks.hpp here: that header pulls in the
    // message bus (via MessageBus for async model loads), which would create an
    // include cycle through WorldMetadata -> IO templates -> MessageBus. The
    // default layer set is built in the .cpp instead.
    struct TerrainGenSettings {
        struct Layer {
            RegistryKey<Block> block;
            int depth; // layers thick; 0 = fill to bottom
        };

        int seed = 1337;
        float frequency = 0.01f;
        float amplitude = 10.0f;
        float baseHeight = 8.0f;
        int octaves = 4;
        float lacunarity = 2.0f;
        float gain = 0.5f;

        // Layers (ordered top -> bottom from surface height), defaulted in the
        // .cpp using the canonical Blocks:: registry keys.
        std::vector<Layer> layers;

        TerrainGenSettings();
    };

} // namespace kc