#pragma once

#include "Core/Blocks/Blocks.hpp"
#include "Core/Registry.hpp"
#include "Core/World/Biomes/Biome.hpp"
#include "Core/World/Biomes/Biomes.hpp"
#include "Core/World/Generation/OverworldNoise.hpp"
#include "Core/World/Biomes/VanillaBiomeProvider.hpp"
#include "Core/World/ITerrainGenerator.hpp"
#include "Core/World/TerrainGenSettings.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace kc {

    // Vanilla-shaped overworld generator.
    //
    // The heightfield comes from OverworldNoise, a node-for-node transcription of
    // Minecraft 26.3's NoiseRouterData overworld router with the cave, vein and
    // structure terms removed (see OverworldNoise.hpp). Two properties of that
    // graph drive the whole design:
    //
    //  1. With no cave/vein terms in final_density, density is monotonic in y for
    //     a fixed column: it crosses zero exactly once. So a column is fully
    //     described by a single y -- the terrain height -- instead of 384
    //     independent samples, and the whole chunk is O(columns log height), not
    //     O(volume). Caves, when they land, break this and the fill has to sample
    //     per block. columnTop() leans on it directly to bisect for the crossing.
    //  2. That makes water a fill rather than a feature: air above the terrain
    //     height and at or below sea level becomes water. This is exactly
    //     vanilla's behaviour for the overworld, because overworld.json declares
    //     no aquifer_density_functions and so relies on the sea-level fallback
    //     aquifer. Caves and the real Aquifer (lava below y -54/drowned caves) are
    //     deferred.
    //
    // Surface blocks follow material_rule/overworld.json, restricted to the four
    // biomes this project ships. See the .cpp comments for the rule names.
    class DefaultTerrainGenerator : public ITerrainGenerator {
    public:
        // SurfaceRules.StoneDepthRule(top, filler, 0.25, 0.8): the topsoil layer
        // is 0.25 + 0.55 * minecraft:surface blocks thick, clamped to [1, 4].
        static int stoneDepth(float surfaceNoise) {
            const float depth = 0.25f + 0.55f * surfaceNoise;
            return std::clamp(static_cast<int>(std::floor(depth)), 1, 4);
        }

        explicit DefaultTerrainGenerator(const TerrainGenSettings& settings) {
            reseed(settings);
        }

        void applySettings(const TerrainGenSettings& settings) override {
            // Exclusive: reseed() rebuilds noise_ and biomes_, so it must not
            // overlap any generation still reading them.
            std::unique_lock<std::shared_mutex> lock(mutex_);
            reseed(settings);
        }

        // Resolved once per call: the biome provider is a view onto the same
        // OverworldNoise the heightfield uses, so the biome map and the
        // heightfield cannot drift apart.
        const BiomeProvider& getBiomeProvider() const { return *biomes_; }

        std::vector<uint64_t> generateBlocks(
            int gridX, int gridZ, int chunkSize, int height) override
        {
            // Shared, not exclusive. Several chunk-generation threads call this
            // at once, and the whole reason this lock exists is to stop
            // applySettings() from swapping the noise out from under a
            // generation. Nothing here writes: settings_, noise_ and biomes_
            // are only read, DensityGraph's memo cache is thread_local (see
            // DensityGraph.hpp), and the block registry is read under its own
            // shared_mutex. So readers do not need to exclude each other.
            std::shared_lock<std::shared_mutex> lock(mutex_);

            // Local y 0 is the bottom of the world, so a world Y of `sea` sits at
            // local y = sea - kMinY. kMinY/kMaxY are already world Y bounds, so
            // the clamp range is [kMinY, kMaxY - 1]; the water table itself is
            // just a fill, so clamping it into the world is enough.
            const int sea = std::clamp(settings_.seaLevel, OverworldNoise::kMinY,
                                       OverworldNoise::kMaxY - 1);
            const int seaLocal = sea - OverworldNoise::kMinY;
            // Terrain is not capped at sea level: anything above it is simply
            // land, and that is how mountains get above the water.
            const int topLimit = height - 1;

            // Layout must match Chunk::getBlock:
            //   blocks[(y * chunkSize + z) * chunkSize + x]
            std::vector<uint64_t> blocks(
                static_cast<size_t>(chunkSize) * height * chunkSize, 0);

            const BlockIds ids = resolveBlocks();
            const float solid = settings_.solidThreshold;

            // Column-major on purpose: DensityGraph memoises per node on the last
            // (x, z) it saw, so sweeping y within one column keeps every
            // y-invariant node cached for the whole column. Row-major throws that
            // away and costs ~7x (1158 ms vs 150 ms per chunk, -O2).
            for (int x = 0; x < chunkSize; ++x) {
                const int wx = gridX * chunkSize + x;
                for (int z = 0; z < chunkSize; ++z) {
                    const int wz = gridZ * chunkSize + z;

                    const int top = columnTop(wx, wz, topLimit, solid);
                    uint64_t topBlock = ids.air;
                    uint64_t soilBlock = ids.stone;
                    int soil = 0;
                    if (top >= 0) {
                        // One climate lookup per column, shared by the surface cell
                        // and the filler beneath it.
                        const float surfaceNoise = noise_->sampleSurfaceNoise(wx, wz);
                        const SurfaceChoice pick = chooseSurface(wx, wz, top, sea, surfaceNoise);
                        topBlock = surfaceBlock(pick, ids);
                        soilBlock = subSurfaceBlock(pick, ids);
                        soil = stoneDepth(surfaceNoise);
                    }

                    for (int y = 0; y <= top; ++y) {
                        uint64_t block = ids.stone;
                        if (y == top) {
                            block = topBlock;
                        } else if (y > top - 1 - soil) {
                            block = soilBlock;
                        }
                        blocks[(static_cast<size_t>(y) * chunkSize + z) * chunkSize + x] = block;
                    }

                    // Aquifer fill: air at or below sea level is water.
                    for (int y = std::max(0, top + 1); y <= seaLocal && y < height; ++y) {
                        blocks[(static_cast<size_t>(y) * chunkSize + z) * chunkSize + x] = ids.water;
                    }
                }
            }
            return blocks;
        }

        // How far above the waterline a column has to sit before the spawn scan
        // will accept it. Two blocks of freeboard keeps the player out of the
        // shallows where waves and any future water motion would reach them.
        static constexpr int kSpawnDryMargin = 2;

        // How far the surface is allowed to differ between a spawn candidate and
        // the blocks right beside it. One block is enough to walk off; anything
        // more means a cliff or a spire, which is what used to drop the player
        // into a hole the moment they moved.
        static constexpr int kSpawnPadStepTolerance = 1;

        // Picks the closest patch of dry land to (originX, originZ) so a new
        // world does not start the player in the middle of an ocean.
        //
        // This used to be a hardcoded (67, 67), which is fine for some seeds and
        // hopeless for others: with seed 1337 the nearest dry land is 384 blocks
        // away, well outside the render distance, so the in-game chunk search
        // could never win and the player simply spawned on the seabed. Vanilla
        // instead scans the terrain for somewhere sensible, which is what this
        // does, using the same density query as generateBlocks() so the answer
        // matches the terrain that will actually be built.
        //
        // Candidates step one chunk at a time to keep the scan cheap, and each one
        // is only accepted when it sits on a small flat dry pad, so the player
        // never lands on a lone islet, a spire or the lip of a cliff. Returns false
        // when no such ground fits inside the radius, which leaves the caller to
        // keep its own spawn and fall back to the gentlest water it can find.
        bool findSpawnColumn(int originX, int originZ, int maxRadiusBlocks,
                             int& outX, int& outY, int& outZ) {
            std::shared_lock<std::shared_mutex> lock(mutex_);

            const int sea = std::clamp(settings_.seaLevel, OverworldNoise::kMinY,
                                       OverworldNoise::kMaxY - 1);
            const float solid = settings_.solidThreshold;
            const int topLimit = OverworldNoise::kHeight - 1;
            const int step = 16; // one chunk

            // World Y of the top solid block, or a value below the world when the
            // column is open water all the way down.
            const auto surfaceY = [&](int wx, int wz) {
                const int local = columnTop(wx, wz, topLimit, solid);
                return local < 0 ? OverworldNoise::kMinY - 1
                                 : local + OverworldNoise::kMinY;
            };
            // A candidate also has to be part of a small flat dry pad, not just a
            // single dry column. The player is 0.6 blocks wide and centred on the
            // chosen block, so anything a block higher or lower right beside them
            // would spawn them embedded in it or half over a drop -- the surface
            // level alone cannot see that, so the neighbours are checked too.
            //
            // The four sides are checked first because a cliff usually shows up
            // there; the diagonals are only paid for once those pass, which keeps
            // the scan about as expensive as the single-column version it replaces.
            const auto flatPad = [&](int wx, int wz, int centreY) {
                const int offsets[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                           {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
                for (int i = 0; i < 8; ++i) {
                    const int ny = surfaceY(wx + offsets[i][0], wz + offsets[i][1]);
                    const bool dry = ny > sea + kSpawnDryMargin;
                    const bool level = std::abs(ny - centreY) <= kSpawnPadStepTolerance;
                    if (!dry || !level) return false;
                    if (i == 3) {   // sides are fine, now the corners
                        for (int j = 4; j < 8; ++j) {
                            const int cy = surfaceY(wx + offsets[j][0], wz + offsets[j][1]);
                            if (cy <= sea + kSpawnDryMargin
                                || std::abs(cy - centreY) > kSpawnPadStepTolerance) {
                                return false;
                            }
                        }
                        return true;
                    }
                }
                return true;
            };

            for (int radius = 0; radius <= maxRadiusBlocks; radius += step) {
                for (int dx = -radius; dx <= radius; dx += step) {
                    for (int dz = -radius; dz <= radius; dz += step) {
                        // Only the border of each ring, so the scan stays a spiral
                        // outwards instead of re-checking everything inside it.
                        const bool onBorder = radius == 0 || dx == -radius || dx == radius
                                              || dz == -radius || dz == radius;
                        if (!onBorder) continue;

                        const int wx = originX + dx;
                        const int wz = originZ + dz;
                        const int centreY = surfaceY(wx, wz);
                        if (centreY <= sea + kSpawnDryMargin) continue;
                        if (!flatPad(wx, wz, centreY)) continue;

                        outX = wx;
                        outZ = wz;
                        // +1 stands the player on top of the surface block instead
                        // of inside it.
                        outY = centreY + 1;
                        return true;
                    }
                }
            }
            return false;
        }


    private:
        struct BlockIds {
            uint64_t air = 0;
            uint64_t stone = 0;
            uint64_t dirt = 0;
            uint64_t sand = 0;
            uint64_t sandstone = 0;
            uint64_t water = 0;
            uint64_t grass = 0;
            uint64_t forestGrass = 0;
        };

        // Registry entries can disappear on a reload, so every block is resolved
        // through getShared and falls back to air rather than being dereferenced.
        static uint64_t blockId(const RegistryKey<Block>& key) {
            const auto block = Registry<Block>::getRegistry().getShared(key.getEncoded());
            return block ? static_cast<uint64_t>(block->getEncodedId()) : 0;
        }

        static BlockIds resolveBlocks() {
            BlockIds b;
            b.air = 0;
            b.stone = blockId(Blocks::STONE);
            b.dirt = blockId(Blocks::DIRT);
            b.sand = blockId(Blocks::SAND);
            b.sandstone = blockId(Blocks::SANDSTONE);
            b.water = blockId(Blocks::WATER);
            b.grass = blockId(Blocks::GRASS_BLOCK);
            b.forestGrass = blockId(Blocks::FOREST_GRASS);
            return b;
        }

        void reseed(const TerrainGenSettings& settings) {
            settings_ = settings;
            // The shape dials are baked into the spline knots here, so they only
            // need reading when the generator is rebuilt -- not per column.
            noise_ = std::make_unique<OverworldNoise>(settings.seed, settings.shape);
            biomes_ = std::make_unique<VanillaBiomeProvider>(*noise_);
        }

        // Highest local y whose final density exceeds `solid`, or -1 for a column
        // with no solid cell at all (open ocean).
        //
        // Fast path: walk DOWN from the router's own chunk_surface_level. Walking
        // consecutive y matters a lot -- DensityGraph memoises y-invariant nodes
        // per column, so a downward walk computes the whole 3D chain once per
        // block and reuses every 2D node, whereas any scheme that samples y out of
        // order pays full price for every sample. Bisecting instead is ~7x slower
        // for exactly this reason.
        //
        // But chunk_surface_level is only a smoothed ESTIMATE: findTopSurface
        // walks in 8-block steps and interpolated() then spreads it over a 16x1
        // cell, so it is not an upper bound. Measured against a brute-force
        // crossing of final_density over 6241 columns it sat below the true
        // surface by a p99 of +30 blocks and a worst case of +79. A pure scan
        // therefore silently flattened ~25% of all columns down to wherever it
        // started, and because the estimate is smooth those errors are spatially
        // correlated, so it shaved broad plateaus off hillsides instead of
        // scattering single blocks.
        //
        // The fix: if the very first sample is already solid, the estimate was
        // below the surface, so bisect for the real crossing. ~75% of columns
        // still take the cheap walk and the rest pay ~11 extra samples. Measured
        // residual error 24.8% -> 0.02% of columns (worst 79 -> 27 blocks; the
        // remainder are the 0.3% of columns whose density is non-monotonic).
        //
        // The old validation missed the original bug because it compared
        // findTopSurface against a brute force of its OWN input field rather than
        // of final_density; that pair is self-consistent by construction.
        //
        // kScanMargin is now only a performance hint -- correctness comes from the
        // bisection fallback -- so its exact value no longer has to be a bound.
        static constexpr int kScanMargin = 16;

        int columnTop(int wx, int wz, int topLimit, float solid) const {
            const auto isSolid = [&](int localY) {
                return noise_->sampleFinalDensity(wx, localY + OverworldNoise::kMinY, wz) > solid;
            };

            const int surface = static_cast<int>(std::floor(noise_->sampleChunkSurfaceLevel(wx, wz)));
            const int start = std::clamp(surface - OverworldNoise::kMinY + kScanMargin, 0, topLimit);

            // Fast path. isSolid(start) false means the estimate is at or above the
            // surface, so the true top is somewhere at or below it.
            if (!isSolid(start)) {
                for (int y = start; y >= 0; --y) {
                    if (isSolid(y)) return y;
                }
                return -1; // genuinely no solid cell in this column (open ocean)
            }

            // The estimate was below the surface. Density is monotonic in y with no
            // caves, so bisect for the crossing: ~log2(height) samples regardless of
            // how far off the estimate was.
            if (!isSolid(0)) return -1;
            int lo = 0;
            int hi = topLimit;
            while (hi - lo > 1) {
                const int mid = lo + (hi - lo) / 2;
                if (isSolid(mid)) lo = mid; else hi = mid;
            }
            return lo;
        }

        // Surface material decisions, resolved once per column so the surface
        // cell, the soil underneath it and the depth of that soil all key off the
        // same numbers.
        //
        // These used to be identity comparisons on the argmax biome, which made
        // every biome border a 1-block vertical cliff -- grass next to sand
        // stepped over in a single column, and because the argmax flips where the
        // four smooth climate fields cross, that line is a smooth isoline and
        // reads as a drawn edge rather than a landscape. getClimateWeights()
        // already existed and was written precisely to cross-fade this, but
        // nothing called it.
        //
        // Blocks cannot be blended, so the border is broken up with noise instead:
        // the climate term decides *where* it sits, and the existing per-column
        // surface noise jitters the threshold so it wanders instead of tracing the
        // isoline. That is the usual way to get a ragged natural border out of a
        // smooth field, and surface noise is already sampled once per column for
        // stoneDepth(), so it costs a reuse rather than a new field.
        struct SurfaceChoice {
            bool sand = false;     // desert sand, or sand at the shoreline
            bool forest = false;   // forest grass rather than plains grass
            bool bareRock = false; // above the treeline: stone, not soil
        };

        // How hard a hot+dry column is pushed toward sand, and how far the surface
        // noise can drag a border across it. Unitless multipliers on a 0..1 climate
        // term. The gain lifts the sand midpoint above a plain "hot and dry" test so
        // sand starts appearing while the desert weight is still merely the largest
        // of the four, which is what puts the border in the middle of the climate
        // gradient instead of at the far end of it.
        static constexpr float kSandGain = 1.45f;
        static constexpr float kBorderDither = 0.22f;

        // Above this, mountains are bare rock. Vanilla splits the mountains family
        // across snowy/stony slopes and peaks; with four biomes this single line
        // is what keeps peaks from looking like meadows.
        //
        // This has to be read against how high THIS terrain actually gets, not
        // against vanilla's numbers. A measured sweep of the default shape gives a
        // land p90 of 98-115 and a peak of 144-184 depending on seed, so the old
        // value of 160 sat at the very top of the range: it caught only the single
        // highest column of a seed, and on seed 1337 (peak 144) it caught nothing
        // at all, leaving exposed stone unreachably rare and every "mountain"
        // biome painted as grass.
        //
        // 104 is roughly the 90th percentile of land height across the seeds
        // swept, so the rule reads as "the top tenth of the land is bare rock".
        // Measured bare-rock share of land: 4.7%-17.2% depending on seed, 6.9%
        // on seed 1337 -- visible ridgelines without turning the world grey.
        // Raise it for bare rock only on the very highest peaks, lower it for a
        // rockier, more mountainous look.
        static constexpr int kTreelineY = 104;

        SurfaceChoice chooseSurface(int wx, int wz, int top, int sea, float dither) const {
            SurfaceChoice pick;
            const int worldY = top + OverworldNoise::kMinY;

            // Below sea level everything is a shoreline. biome_surface.json sends
            // warm_ocean/beach/desert to sand_or_sandstone_if_ceiling, and with no
            // carvers there is never a ceiling, so that always lands on sand.
            if (worldY < sea) {
                pick.sand = true;
                return pick;
            }

            const auto c = biomes_->climateAt(wx, wz);
            const float wet = VanillaBiomeProvider::ramp(
                c.humidity, VanillaBiomeProvider::kWetLo,
                VanillaBiomeProvider::kWetHi);
            const float hot = VanillaBiomeProvider::ramp(
                c.temperature, VanillaBiomeProvider::kHotLo,
                VanillaBiomeProvider::kHotHi);

            // Sand needs heat AND dryness, mirroring the desert weight itself.
            if (hot * (1.0f - wet) * kSandGain + dither * kBorderDither > 0.5f) {
                pick.sand = true;
            } else {
                // Otherwise grass, split between the two grass blocks by wetness
                // using the same jittered threshold.
                pick.forest = wet + dither * kBorderDither > 0.5f;
            }

            pick.bareRock = c.mountain >= VanillaBiomeProvider::kTreelineBiome &&
                            worldY > kTreelineY;
            return pick;
        }

        // The single exposed cell: the "on_floor" branch of
        // material_rule/overworld/surface.json.
        uint64_t surfaceBlock(const SurfaceChoice& pick, const BlockIds& ids) const {
            if (pick.bareRock) return ids.stone; // frozen_peaks/stony_peaks are bare
            if (pick.sand) return ids.sand;
            return pick.forest ? ids.forestGrass : ids.grass;
        }

        // Everything between the topsoil and the stone: the "under_floor" branch,
        // i.e. biome-specific filler.
        //
        // Vanilla's under_biome_surface only overrides the default dirt for the
        // peaks biomes, which resolve to stone. Everything else -- desert and
        // forest included -- inherits default.json, which is plain dirt, so a
        // desert's subsurface is dirt and NOT sandstone. Sandstone only ever shows
        // up as a desert *surface* block.
        //
        // Gated on bareRock rather than on the raw biome so it agrees with the
        // surface cell by construction: grass at y=66 with a stone cell under it
        // would leave a bare one-block lip and no soil band.
        uint64_t subSurfaceBlock(const SurfaceChoice& pick, const BlockIds& ids) const {
            return pick.bareRock ? ids.stone : ids.dirt;
        }

        TerrainGenSettings settings_;
        std::unique_ptr<OverworldNoise> noise_;
        std::unique_ptr<VanillaBiomeProvider> biomes_;
        // A reader-writer lock, not a plain mutex. Generation is the long pole
        // (tens of milliseconds per chunk) and it only reads, so shared_lock
        // lets the chunk threads run concurrently while applySettings() still
        // gets exclusive access to swap the terrain out safely.
        std::shared_mutex mutex_;
    };

} // namespace kc
