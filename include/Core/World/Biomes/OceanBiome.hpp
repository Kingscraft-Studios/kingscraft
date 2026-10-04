#pragma once
#include "Biome.hpp"

namespace kc {
    // The label for "below the waterline".
    //
    // Every submerged column used to be claimed by a land biome, because the
    // climate weights have no way to express "underwater": over the measured
    // region all 27173 submerged columns were labelled, 24396 of them
    // grasslands. Vanilla settles it the same way -- ocean is chosen on depth
    // before any climate parameter is consulted.
    //
    // The surface rule needs an override rather than a `layers` config: this is
    // the one biome whose material is not "topsoil over stone filler". The
    // seabed is sand over sand, two deep, and Biome::resolveSurface only honours
    // layers[1].depth as a soil depth -- layers[0].depth is ignored -- so a
    // {SAND, 2}, {SANDSTONE, 0} config could not have expressed this even if the
    // generic path had reached it. It never did: a global `worldTopY < seaLevel`
    // branch in Biome::resolveSurface shadowed it, which left this biome
    // contributing nothing but a name.
    class OceanBiome : public Biome {
    public:
        SurfaceLayers resolveSurface(
            const BiomeChunkContext& chunk,
            int worldX, int worldZ, int worldTopY,
            float dither, const Climate& climate,
            const ClimateWeights& weights) const override;
    };
}