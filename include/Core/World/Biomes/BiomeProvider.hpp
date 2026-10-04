#pragma once

#include "Core/RegistryKey.hpp"
#include "Core/World/Biomes/Biomes.hpp"
#include "Core/World/TerrainGenSettings.hpp"
#include "FastNoiseLite.h"
#include <cmath>
#include <limits>
#include <mutex>

namespace kc {

    class Biome;

    // Sentinel for "this query has no column height to offer".
    //
    // Biome selection now depends on how high the ground actually is, because the
    // climate fields on their own cannot identify a mountain (see
    // ClimateBiomeProvider). Queries that never touch terrain -- a bare climate
    // lookup, a spawn search that has not settled on a column yet -- pass this and
    // get the climate-only answer, with the height-driven labels omitted rather
    // than guessed from a stale or invented height.
    inline constexpr int unknownSurfaceY = std::numeric_limits<int>::min();

    // Continuous membership weights for the 4-biome climate model. All entries
    // are smooth functions of the climate fields, so blending the terrain with
    // these weights is C0-continuous (no sheer cliffs at biome borders).
    struct ClimateWeights {
        float desert = 0.0f;    // hot & dry
        float grassland = 0.0f; // temperate remainder
        float forest = 0.0f;    // wet
        float mountain = 0.0f;  // inland & rugged (overlay)
    };

    // Raw climate fields, in NormalNoise output units.
    struct Climate {
        float temperature = 0.0f;
        float humidity = 0.0f; // vanilla calls this "vegetation"
        float continentalness = 0.0f;
        float erosion = 0.0f;
        float ridges = 0.0f;
        float desert = 0.0f;
        float grassland = 0.0f;
        float forest = 0.0f;
        float mountain = 0.0f;
    };

    // Decides which Biome exists at a world X/Z coordinate, based on a
    // Minecraft-style climate plane (temperature x humidity) plus a
    // continentals x erosion filter for mountains.
    class BiomeProvider {
    public:
        virtual ~BiomeProvider() = default;
        virtual RegistryKey<Biome> getBiome(int worldX, int worldZ) const = 0;

        // Height-aware form, for callers that have just measured (or are about to
        // measure) the column. `surfaceY` is the world Y of the column's top solid
        // block, or unknownSurfaceY if that is not known yet.
        //
        // Providers that have a height model must override this; the default just
        // forwards to the climate-only answer so a provider without one keeps its
        // previous behaviour instead of silently claiming labels it cannot justify.
        virtual RegistryKey<Biome> getBiomeForColumn(int worldX, int worldZ, int surfaceY,
                                                     int seaLevel) const {
            (void)surfaceY;
            (void)seaLevel;
            return getBiome(worldX, worldZ);
        }

        // Re-seed from an updated settings struct (e.g. a world.kcw load).
        // Default no-op for providers with no seed dependency.
        virtual void applySettings(const TerrainGenSettings&) {}

        // The four placement-only climate fields, normalized to [0,1]. They pick
        // the biome (via getClimateWeights) but never shape terrain height.
        virtual float getTemperature(int worldX, int worldZ) const { return 0.0f; }
        virtual float getHumidity(int worldX, int worldZ) const { return 0.0f; }
        virtual float getContinentalness(int worldX, int worldZ) const { return 0.0f; }
        virtual float getErosion(int worldX, int worldZ) const { return 0.0f; }

        // Continuous biome membership weights used to cross-fade terrain shapes
        // at biome borders. Default zeroes for non-noise providers.
        virtual ClimateWeights getClimateWeights(int worldX, int worldZ) const { return {}; }
    };

    inline ClimateWeights climateWeightsFrom(const Climate& c) {
        ClimateWeights w;
        w.forest = c.forest;
        w.grassland = c.grassland;
        w.desert = c.desert;
        w.mountain = c.mountain;
        return w;
    }

}
