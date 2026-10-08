#pragma once
#include "Biome.hpp"

namespace kc {
    // The river corridor: the label the carve earns.
    //
    // Unlike every other biome here this one does not read the climate at all.
    // The split that matters in a river is not hot/cold or wet/dry, it is "under
    // water or not", and that is a height the generator already knows: worldTopY
    // arrives as the BUILT (carved) top, so comparing it against seaLevel says
    // everything.
    //
    //   above sea + shoreBand  the bank -> the same grass over dirt the
    //                           surrounding land biome would have painted, so a
    //                           river through a meadow keeps the meadow right up
    //                           to the water
    //   sea - 1 .. sea + 1     the lip and the shallows -> one clean sand beach,
    //                           matching the ocean's sand at the waterline
    //   below that             the bed -> sand / gravel / dirt patches keyed on
    //                           the surface noise (see RiverBiome.cpp), the way
    //                           a real river sorts its bottom
    //
    // The soil depth is a fixed 2 rather than the noise-driven 1/2/3 the land
    // biomes use: river bed material is deposited, not weathered, and a noisy
    // DEPTH under a 1-block water column would show as height stripes through
    // the shallows. (The patch choice above is noisy in X/Z instead -- blobs,
    // not layers.)
    class RiverBiome : public Biome {
    public:
        // How far above the waterline the sand shore still reaches. One block:
        // the column whose top sits at seaLevel is dry (the fill starts at
        // top + 1) and the one above it is the step out of the water, so both
        // are shore; anything wider starts eating into the bank's grass.
        static constexpr int shoreBand = 1;

        SurfaceLayers resolveSurface(
            const BiomeChunkContext& chunk,
            int worldX, int worldZ, int worldTopY,
            float dither, const Climate& climate,
            const ClimateWeights& weights) const override;
    };
}
