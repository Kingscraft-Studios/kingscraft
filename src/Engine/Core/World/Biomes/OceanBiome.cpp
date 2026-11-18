#include "Core/World/Biomes/OceanBiome.hpp"

#include "Core/Blocks/Blocks.hpp"

namespace kc {
    // The seabed. Sand over sand, two deep; Biome::postGenerateColumn writes the
    // stone filler below that. This is the same result the global
    // `worldTopY < seaLevel` branch in Biome::resolveSurface used to produce, so
    // the generated world is unchanged -- only the ownership moved to the biome
    // the label already pointed at.
    //
    // Always sand, and the biome owns every sea-covered column, corridor or
    // not. The river keeps carving out under the sea for the junction's relief
    // -- the label does not follow it there: anything whose natural ground sits
    // below the waterline reads ocean, so the floor of the estuary trough is
    // sand here too. Painting the river's patch mix here (an early corridor
    // gate did) leaked dirt and gravel onto the ocean floor wherever a contour
    // crossed the sea, while the shallow river beside it read as clean sand --
    // chunk_probe asserts an ocean floor is sand as the regression guard.
    SurfaceLayers OceanBiome::resolveSurface(
        const BiomeChunkContext& /*chunk*/,
        int /*worldX*/, int /*worldZ*/, int /*worldTopY*/,
        float /*dither*/, const Climate& /*climate*/,
        const ClimateWeights& /*weights*/) const
    {
        return {Blocks::SAND, Blocks::SAND, 2};
    }
}