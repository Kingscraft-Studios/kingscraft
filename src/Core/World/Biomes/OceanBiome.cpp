#include "Core/World/Biomes/OceanBiome.hpp"

#include "Core/Blocks/Blocks.hpp"

namespace kc {
    // The seabed. Sand over sand, two deep; Biome::postGenerateColumn writes the
    // stone filler below that. This is the same result the global
    // `worldTopY < seaLevel` branch in Biome::resolveSurface used to produce, so
    // the generated world is unchanged -- only the ownership moved to the biome
    // the label already pointed at.
    SurfaceLayers OceanBiome::resolveSurface(
        const BiomeChunkContext& /*chunk*/,
        int /*worldX*/, int /*worldZ*/, int /*worldTopY*/,
        float /*dither*/, const Climate& /*climate*/,
        const ClimateWeights& /*weights*/) const
    {
        return {Blocks::SAND, Blocks::SAND, 2};
    }
}