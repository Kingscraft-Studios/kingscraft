#pragma once

#include "Core/Blocks/Blocks.hpp"
#include "Core/Registry.hpp"
#include "Core/World/Biomes/Biome.hpp"
#include "Core/World/Biomes/Biomes.hpp"
#include "Core/World/Generation/HomelandsNoise.hpp"
#include "Core/World/Biomes/ClimateBiomeProvider.hpp"
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

    // Vanilla-shaped generator for the Homelands.
    //
    // The heightfield comes from HomelandsNoise, a node-for-node transcription of
    // Minecraft 26.3's NoiseRouterData overworld router with the cave, vein and
    // structure terms removed (see HomelandsNoise.hpp). Two properties of that
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
        // HomelandsNoise the heightfield uses, so the biome map and the
        // heightfield cannot drift apart.
        const BiomeProvider& getBiomeProvider() const { return *biomes_; }

        // World Y of the top solid block in this column, or unknownSurfaceY for
        // a column with no solid cell at all.
        //
        // Exposed because biome selection is height-aware, so any caller asking
        // "which biome is here?" has to offer a height or it gets the weaker
        // climate-only answer. Costs one density scan (~10-20 samples), the same
        // one generateBlocks() already does per column.
        int getSurfaceY(int worldX, int worldZ) const {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            const int topLimit = HomelandsNoise::height - 1;
            const int local = columnTop(worldX, worldZ, topLimit, settings_.solidThreshold);
            return local < 0 ? unknownSurfaceY : local + HomelandsNoise::minY;
        }

        // The biome the terrain at this column was actually built with. Preferred
        // over BiomeProvider::getBiomeForColumn directly so the caller cannot
        // pair a height from one moment with settings from another.
        RegistryKey<Biome> getBiomeAt(int worldX, int worldZ) const {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            const int topLimit = HomelandsNoise::height - 1;
            const int local = columnTop(worldX, worldZ, topLimit, settings_.solidThreshold);
            const int surfaceY = local < 0 ? unknownSurfaceY : local + HomelandsNoise::minY;
            const int sea = std::clamp(settings_.seaLevel, HomelandsNoise::minY,
                                       HomelandsNoise::maxY - 1);
            return ClimateBiomeProvider::biomeFor(
                biomes_->climateAt(worldX, worldZ, surfaceY), surfaceY, sea);
        }

        std::vector<uint64_t> generateBlocks(int gridX, int gridZ, int chunkSize, int height) override {
            // Shared, not exclusive. Several chunk-generation threads call this
            // at once, and the whole reason this lock exists is to stop
            // applySettings() from swapping the noise out from under a
            // generation. Nothing here writes: settings_, noise_ and biomes_
            // are only read, DensityGraph's memo cache is thread_local (see
            // DensityGraph.hpp), and the block registry is read under its own
            // shared_mutex. So readers do not need to exclude each other.
            std::shared_lock<std::shared_mutex> lock(mutex_);

            // Local y 0 is the bottom of the world, so a world Y of `sea` sits at
            // local y = sea - minY. minY/maxY are already world Y bounds, so
            // the clamp range is [minY, maxY - 1]; the water table itself is
            // just a fill, so clamping it into the world is enough.
            const int sea = std::clamp(settings_.seaLevel, HomelandsNoise::minY,
                                       HomelandsNoise::maxY - 1);
            const int seaLocal = sea - HomelandsNoise::minY;
            // Terrain is not capped at sea level: anything above it is simply
            // land, and that is how mountains get above the water.
            const int topLimit = height - 1;

            // Layout must match Chunk::getBlock:
            //   blocks[(y * chunkSize + z) * chunkSize + x]
            std::vector<uint64_t> blocks(
                static_cast<size_t>(chunkSize) * height * chunkSize, 0);

            // Cell encodings for the two blocks this function writes outside the
            // biome path. RegistryKey::getEncoded() is the registry's own map key,
            // which is exactly what Block::getEncodedId() reverse-maps to, so this
            // is the same value the old resolveBlocks() produced -- without the
            // getShared() round trip and without its O(n) getEncodedID() scan, and
            // without collapsing to 0 (air) for any block that had not finished
            // registering. That fallback was the real hazard: block registration
            // is asynchronous, so a chunk generated during startup became solid air
            // and was then written to the region file that way.
            const uint64_t stoneId = Blocks::STONE.getEncoded();
            const uint64_t waterId = Blocks::WATER.getEncoded();
            const float solid = settings_.solidThreshold;

            BiomeChunkContext ctx;
            ctx.chunkSize = chunkSize;
            ctx.height = height;
            ctx.seaLevel = sea;
            ctx.minWorldY = HomelandsNoise::minY;

            // Biome cache to avoid repeated getShared calls
            struct BiomeSlot {
                uint64_t encoded = 0;
                std::shared_ptr<Biome> ref;
            };
            BiomeSlot biomeSlots[8];
            int biomeSlotCount = 0;
            const auto resolveBiome = [&](const RegistryKey<Biome>& key) -> const Biome* {
                for (int i = 0; i < biomeSlotCount; ++i) {
                    if (biomeSlots[i].encoded == key.getEncoded()) {
                        return biomeSlots[i].ref.get();
                    }
                }
                auto ref = Registry<Biome>::getRegistry().getShared(key.getEncoded());
                if (!ref) return nullptr;
                if (biomeSlotCount < 8) {
                    biomeSlots[biomeSlotCount++] = BiomeSlot{key.getEncoded(), ref};
                    return ref.get();
                }
                return ref.get();
            };
            const auto fillPlainStone = [&](int x, int z, int top, uint64_t stoneId) {
                for (int y = 0; y <= top; ++y) {
                    blocks[(static_cast<size_t>(y) * chunkSize + z) * chunkSize + x] = stoneId;
                }
            };

            // Column-major on purpose
            for (int x = 0; x < chunkSize; ++x) {
                const int wx = gridX * chunkSize + x;
                for (int z = 0; z < chunkSize; ++z) {
                    const int wz = gridZ * chunkSize + z;

                    const int top = columnTop(wx, wz, topLimit, solid);
                    if (top >= 0) {
                        const float surfaceNoise = noise_->sampleSurfaceNoise(wx, wz);
                        // Biome selection needs the height we just measured: it is
                        // what decides "mountains", and "below sea level" is what
                        // decides "ocean". Passing unknownSurfaceY here would
                        // quietly fall back to the climate-only answer and
                        // reintroduce the mislabelling this fixes.
                        const int surfaceY = top + HomelandsNoise::minY;
                        const kc::Climate climate = biomes_->climateAt(wx, wz, surfaceY);
                        const auto biomeKey = ClimateBiomeProvider::biomeFor(climate, surfaceY, sea);
                        if (const Biome* biome = resolveBiome(biomeKey)) {
                            biome->postGenerateColumn(ctx, x, z, wx, wz, top, top + HomelandsNoise::minY, surfaceNoise, climate, ClimateWeights{climate.desert, climate.grassland, climate.forest, climate.mountain}, blocks);
                        } else {
                            fillPlainStone(x, z, top, stoneId);
                        }
                    }

                    // Aquifer fill: air at or below sea level is water.
                    for (int y = std::max(0, top + 1); y <= seaLocal && y < height; ++y) {
                        blocks[(static_cast<size_t>(y) * chunkSize + z) * chunkSize + x] = waterId;
                    }
                }
            }
            return blocks;
        }

        // How far above the waterline a column has to sit before the spawn scan
        // will accept it. Two blocks of freeboard keeps the player out of the
        // shallows where waves and any future water motion would reach them.
        static constexpr int spawnDryMargin = 2;

        // How far the surface is allowed to differ between a spawn candidate and
        // the blocks right beside it. One block is enough to walk off; anything
        // more means a cliff or a spire, which is what used to drop the player
        // into a hole the moment they moved.
        static constexpr int spawnPadStepTolerance = 1;

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

            const int sea = std::clamp(settings_.seaLevel, HomelandsNoise::minY,
                                       HomelandsNoise::maxY - 1);
            const float solid = settings_.solidThreshold;
            const int topLimit = HomelandsNoise::height - 1;
            const int step = 16; // one chunk

            // World Y of the top solid block, or a value below the world when the
            // column is open water all the way down.
            const auto surfaceY = [&](int wx, int wz) {
                const int local = columnTop(wx, wz, topLimit, solid);
                return local < 0 ? HomelandsNoise::minY - 1
                                 : local + HomelandsNoise::minY;
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
                    const bool dry = ny > sea + spawnDryMargin;
                    const bool level = std::abs(ny - centreY) <= spawnPadStepTolerance;
                    if (!dry || !level) return false;
                    if (i == 3) {   // sides are fine, now the corners
                        for (int j = 4; j < 8; ++j) {
                            const int cy = surfaceY(wx + offsets[j][0], wz + offsets[j][1]);
                            if (cy <= sea + spawnDryMargin
                                || std::abs(cy - centreY) > spawnPadStepTolerance) {
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
                        if (centreY <= sea + spawnDryMargin) continue;
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
        void reseed(const TerrainGenSettings& settings) {
            settings_ = settings;
            // The shape dials are baked into the spline knots here, so they only
            // need reading when the generator is rebuilt -- not per column.
            noise_ = std::make_unique<HomelandsNoise>(settings.seed, settings.shape);
            biomes_ = std::make_unique<ClimateBiomeProvider>(*noise_);
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
        // scanMargin is now only a performance hint -- correctness comes from the
        // bisection fallback -- so its exact value no longer has to be a bound.
        static constexpr int scanMargin = 16;

        int columnTop(int wx, int wz, int topLimit, float solid) const {
            const auto isSolid = [&](int localY) {
                return noise_->sampleFinalDensity(wx, localY + HomelandsNoise::minY, wz) > solid;
            };

            const int surface = static_cast<int>(std::floor(noise_->sampleChunkSurfaceLevel(wx, wz)));
            const int start = std::clamp(surface - HomelandsNoise::minY + scanMargin, 0, topLimit);

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

        // Visual surface decisions moved to Biome::postGenerateColumn.
        // The ocean label moved too: a submerged column is now Biome::OCEAN via
        // ClimateBiomeProvider::biomeFor, which picks ocean on depth before any
        // climate term is consulted, and OceanBiome::resolveSurface owns the
        // seabed material.
        //
        // What stays here is the water *fill* above, because it is a density
        // property rather than a surface one. It deliberately does not consult
        // the biome: Vanilla's overworld declares no aquifer_density_functions,
        // so the sea-level fallback aquifer fills every non-solid cell at or
        // below sea level regardless of which biome won the argmax. Coupling it
        // to the biome would be actively wrong here, because the climate-only
        // ClimateBiomeProvider::biomeFor overload -- the one used whenever
        // surfaceY is unknown -- never returns OCEAN, and would leave dry holes
        // wherever a caller took that path.

        TerrainGenSettings settings_;
        std::unique_ptr<HomelandsNoise> noise_;
        std::unique_ptr<ClimateBiomeProvider> biomes_;
        // A reader-writer lock, not a plain mutex. Generation is the long pole
        // (tens of milliseconds per chunk) and it only reads, so shared_lock
        // lets the chunk threads run concurrently while applySettings() still
        // gets exclusive access to swap the terrain out safely.
        // Mutable because the biome/surface queries are const: they only read
        // settings_ and noise_, but they still have to join the same lock so they
        // cannot observe a half-applied reseed.
        mutable std::shared_mutex mutex_;
    };

} // namespace kc
