#pragma once

#include "Core/RegistryKey.hpp"
#include "Core/World/Biomes/Biomes.hpp"
#include "Core/World/TerrainGenSettings.hpp"
#include "FastNoiseLite.h"
#include <cmath>
#include <mutex>

namespace kc {

    class Biome;

    // Continuous membership weights for the 4-biome climate model. All entries
    // are smooth functions of the climate fields, so blending the terrain with
    // these weights is C0-continuous (no sheer cliffs at biome borders).
    struct ClimateWeights {
        float desert = 0.0f;    // hot & dry
        float grassland = 0.0f; // temperate remainder
        float forest = 0.0f;    // wet
        float mountain = 0.0f;  // inland & rugged (overlay)
    };

    // Decides which Biome exists at a world X/Z coordinate, based on a
    // Minecraft-style climate plane (temperature x humidity) plus a
    // continentalness x erosion filter for mountains.
    class BiomeProvider {
    public:
        virtual ~BiomeProvider() = default;
        virtual RegistryKey<Biome> getBiome(int worldX, int worldZ) const = 0;
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

    // Climate placement: temperature + humidity form the Axis-aligned climate
    // plane (Forest = wet, Desert = hot & dry, Grasslands = the temperate
    // remainder); Mountains gate on continentalness (ocean -> coast -> inland)
    // and erosion (high erosion = smooth, low erosion = rugged). Deterministic
    // per seed; re-seeded via applySettings whenever the world seed resolves.
    class DefaultBiomeProvider final : public BiomeProvider {
    public:
        DefaultBiomeProvider();

        RegistryKey<Biome> getBiome(int worldX, int worldZ) const override;
        void applySettings(const TerrainGenSettings& settings) override;

        float getTemperature(int worldX, int worldZ) const override;
        float getHumidity(int worldX, int worldZ) const override;
        float getContinentalness(int worldX, int worldZ) const override;
        float getErosion(int worldX, int worldZ) const override;
        ClimateWeights getClimateWeights(int worldX, int worldZ) const override;

        // Axis-aligned ramp thresholds, all in the [0,1] field space.
        static constexpr float RAMP_WET_LO    = 0.40f; // humidity -> forest
        static constexpr float RAMP_WET_HI    = 0.72f;
        static constexpr float RAMP_HOT_LO    = 0.50f; // temperature -> desert
        static constexpr float RAMP_HOT_HI    = 0.80f;
        static constexpr float RAMP_INLAND_LO = 0.34f; // continentalness -> mountains
        static constexpr float RAMP_INLAND_HI = 0.58f;
        static constexpr float RAMP_RUGGED_LO = 0.30f; // erosion (high = smooth)
        static constexpr float RAMP_RUGGED_HI = 0.60f;
        // Scales the mountain membership so rugged inland zones win the argmax;
        // values stay in [0,1] and remain continuous in world space.
        static constexpr float MOUNTAIN_BIAS  = 1.30f;
        // Contrast stretch applied to every raw noise field: FNL Perlin hugs
        // ~0.5, so the normalized values are pulled to the full [0,1] range.
        static constexpr float FIELD_STRETCH  = 2.40f;

        // smoothstep ramp helper shared by placement and the generator.
        static float ramp(float v, float lo, float hi);

    private:
        struct ClimateSample {
            float temperature;
            float humidity;
            float continentalness;
            float erosion;
        };

        struct Field {
            FastNoiseLite noise; // the climate field
            FastNoiseLite warp;  // domain warp: bends the field naturally
            void configure(float freq, int octaves, float gain, int seed,
                           float warpFreq, float warpAmp, int warpSeed);
            float sample(float wx, float wz) const;
        };

        void configure(int seed);
        ClimateSample sampleFields(int worldX, int worldZ) const;

        mutable std::mutex noiseMutex_;
        Field temperature_;
        Field humidity_;
        Field continentalness_;
        Field erosion_;
    };

}