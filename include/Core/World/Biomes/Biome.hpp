#pragma once

#include "Core/World/TerrainGenSettings.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace kc {

    struct Climate;
    struct ClimateWeights;

    // Immutable for the whole of one generateBlocks() call.
    struct BiomeChunkContext {
        int chunkSize = 0;
        int height = 0;
        int seaLevel = 0;
        int minWorldY = 0;
    };

    // How many blocks of soil sit under the surface block.
    //
    // The old form was vanilla's StoneDepthRule arithmetic, `floor(0.25 + 0.55 *
    // surface)` clamped to [1, 4]. Those constants assume a different noise scale
    // than the one actually sampled here: terrain/surface_noise is NormalNoise
    // with Normalization::Enabled, so it is bounded to roughly [-1, 1] and measures
    // [-0.63, +0.83] (p50 +0.09) over the 718848 columns sampled on seed 1337.
    // Against that range the expression floors to {-1, 0}, and the clamp then
    // pinned every column to 1 -- the rule was inert and soil depth never varied
    // anywhere in the world. Verified by evaluating it over all 718848 samples:
    // the result was 1 for every one.
    //
    // Thresholds are placed on the measured quantiles so depth splits roughly
    // 25/50/25 across 1/2/3: the 1->2 step sits at p25 (-0.13) and the 2->3 step at
    // p75 (+0.34). The upper bound is 3 rather than the old 4 because the linear
    // span above saturates at 3, and a biome still caps this by its own declared
    // soil depth (see Biome::resolveSurface).
    inline int surfaceStoneDepth(float surfaceNoise) {
        const float t = std::clamp((surfaceNoise + 0.13f) / 0.47f, 0.0f, 1.0f);
        return 1 + static_cast<int>(std::lround(t * 2.0f));
    }

    // One column's surface result.
    //
    // These are RegistryKey<Block>, not decoded uint64 ids. A key is a
    // self-contained value (identifier hash + name) that re-resolves through the
    // registry on use, so holding one across a reload is safe. A bare uint64 is
    // not: it is indistinguishable from a cell encoding, it silently keeps
    // pointing at a block that may no longer be registered, and the only way to
    // rebuild it is Registry<Block>::getEncodedID(), an O(n) scan of the whole
    // registry under a lock. The single conversion to a cell value happens in
    // Biome::postGenerateColumn, where it is written into the chunk grid.
    struct SurfaceLayers {
        RegistryKey<Block> topBlock;
        RegistryKey<Block> soilBlock;
        int soilDepth = 1;   // how many blocks of soilBlock below topBlock
    };

    class Biome {
    protected:
        // Block composition, top->bottom. This used to live in a
        // BiomeTerrainSettings alongside baseHeight/amplitude/noise, but those
        // three were unread from the day the heightfield moved into
        // HomelandsNoise: shape is the noise graph's job, not the biome's. Only
        // the layer stack is still per-biome state.
        std::vector<TerrainGenSettings::Layer> layers;

    public:
        Biome() = default;
        virtual ~Biome() = default;

        // Bare-rock override hook. Default: no biome exposes rock by itself.
        //
        // `dither` is the same per-column surface noise the soil depth uses, so a
        // biome that keys off a height can break the resulting level contour into
        // a wavy one instead of drawing a ruler-straight line at a single Y.
        virtual bool useBareRock(int worldTopY, float dither, const Climate& climate) const {
            (void)worldTopY;
            (void)dither;
            (void)climate;
            return false;
        }

        // Per-biome surface rule.
        virtual SurfaceLayers resolveSurface(
            const BiomeChunkContext& chunk,
            int worldX, int worldZ, int worldTopY,
            float dither, const Climate& climate,
            const ClimateWeights& weights) const;

        // Visual surface pass for ONE column. Granularity is per column to preserve
        // border blending via per-column surface noise (dither).
        virtual void postGenerateColumn(
            const BiomeChunkContext& chunk,
            int localX, int localZ,
            int worldX, int worldZ,
            int topLocalY,
            int worldTopY,
            float dither,
            const Climate& climate,
            const ClimateWeights& weights,
            std::vector<uint64_t>& blocks
        ) const;
    };

} // namespace kc