#include "Core/World/Biomes/RiverBiome.hpp"

#include "Core/Blocks/Blocks.hpp"

namespace kc {
    SurfaceLayers RiverBiome::resolveSurface(
        const BiomeChunkContext& chunk,
        int /*worldX*/, int /*worldZ*/, int worldTopY,
        float dither, const Climate& /*climate*/,
        const ClimateWeights& /*weights*/) const
    {
        const int sea = chunk.seaLevel;

        // The bank: the same grass over dirt the surrounding land biome would
        // have painted, so a river through a meadow keeps the meadow right up
        // to the water.
        if (worldTopY > sea + shoreBand) {
            return {Blocks::GRASS_BLOCK, Blocks::DIRT, 2};
        }

        // The lip and the shallows: sand, noise-free, the same sand the ocean
        // floor gets. The shoreline should read as one clean beach, not as the
        // patch mix starting half a block under the surface.
        if (worldTopY >= sea - 1) {
            return {Blocks::SAND, Blocks::SAND, 2};
        }

        // The bed, deeper: patches keyed on the same surface noise the land
        // biomes use for soil depth (firstOctave -6, so blobs of ~16..64
        // blocks, not speckle). The cuts sit on that noise's measured
        // quantiles -- p25 -0.13, p75 +0.34, documented on surfaceStoneDepth in
        // Biome.hpp -- so roughly a quarter of the bed is gravel-bottomed, a
        // quarter sand-bottomed, and the middle stays the sand-over-gravel mix
        // a real river deposits. Dirt only shows up in the deep quiet middle:
        // the silted-out part of a slow run.
        if (dither >= 0.34f) {
            return {Blocks::SAND, Blocks::SAND, 2};
        }
        if (dither <= -0.13f) {
            return {Blocks::GRAVEL, Blocks::GRAVEL, 2};
        }
        if (worldTopY <= sea - 4) {
            return {Blocks::DIRT, Blocks::DIRT, 2};
        }
        return {Blocks::SAND, Blocks::GRAVEL, 2};
    }
}
