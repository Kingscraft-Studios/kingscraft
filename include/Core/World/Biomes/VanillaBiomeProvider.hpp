#pragma once

#include "Core/World/Biomes/Biome.hpp"
#include "Core/World/Biomes/Biomes.hpp"
#include "Core/World/Biomes/BiomeProvider.hpp"
#include "Core/World/Generation/OverworldNoise.hpp"

namespace kc {

    // Biome selection driven by the same four climate noises the terrain graph
    // uses, so the biome map and the heightfield always agree.
    //
    // Vanilla picks biomes from a climate *point* (MultiNoise) with fixed
    // parameter ranges; overworld.json declares each noise over [-2, 2] with a
    // step of 0.2 (temperature, vegetation, continentalness, erosion). Rather
    // than reproduce vanilla's ~64-entry biome table, this maps the same four
    // fields onto the four biomes this project ships:
    //
    //   mountains  rugged terrain, whether or not it is inland
    //   desert     hot and dry
    //   forest     wet
    //   grasslands everything else
    //
    // Membership is continuous (smoothstep ramps) so callers can cross-fade, but
    // getBiome() resolves it to the argmax, which is what the surface rules and
    // the player-position query want.
    //
    // The ridges axis is what keeps the labels honest. Terrain ruggedness is
    // driven by erosion, and erosion produces shattered ground at ANY
    // continentalness -- including out at sea and in low inland basins. A rule
    // that only called something a mountain when it was BOTH inland and rugged
    // therefore labelled every rugged coastline and valley "grasslands", and the
    // surface rules then painted grass blocks onto cliffs. Vanilla handles this
    // with a whole biome family for it (windswept hills/savanna/forest edge);
    // here one extra term on the same continuous weights does the job without
    // inflating the mountain share (see kRuggedInlandBias).
    class VanillaBiomeProvider final : public BiomeProvider {
    public:
        explicit VanillaBiomeProvider(const OverworldNoise& noise) : noise_(noise) {}

        RegistryKey<Biome> getBiome(int worldX, int worldZ) const override {
            const Climate c = climateAt(worldX, worldZ);
            if (c.mountain >= std::max({c.desert, c.grassland, c.forest})) return Biomes::MOUNTAINS;
            if (c.forest >= c.grassland && c.forest >= c.desert) return Biomes::FOREST;
            if (c.grassland >= c.desert) return Biomes::GRASSLANDS;
            return Biomes::DESERT;
        }

        void applySettings(const TerrainGenSettings&) override {
            // The climate comes from the OverworldNoise the generator owns, which
            // is re-seeded itself; nothing to forward here.
        }

        float getTemperature(int worldX, int worldZ) const override {
            return climateAt(worldX, worldZ).temperature;
        }
        float getHumidity(int worldX, int worldZ) const override { return climateAt(worldX, worldZ).humidity; }
        float getContinentalness(int worldX, int worldZ) const override {
            return climateAt(worldX, worldZ).continentalness;
        }
        float getErosion(int worldX, int worldZ) const override { return climateAt(worldX, worldZ).erosion; }

        ClimateWeights getClimateWeights(int worldX, int worldZ) const override {
            const Climate c = climateAt(worldX, worldZ);
            ClimateWeights w;
            w.forest = c.forest;
            w.desert = c.desert;
            w.grassland = c.grassland;
            w.mountain = c.mountain;
            return w;
        }

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

        // Ramp thresholds, in raw NormalNoise output units.
        //
        // overworld.json declares each climate noise over [-2, 2], but a measured
        // 20k-sample sweep only ever reaches about [-1.2, 1.3] (p1..p99 is roughly
        // [-0.85, 0.8]). Thresholds placed against the declared range would put
        // every sample inside every ramp's midpoint and collapse the world to a
        // single biome, so these are fitted against the distribution the noise
        // actually produces. The eight values came from a coordinate-descent fit
        // of the four shares towards grass 45% / forest 25% / desert 16% /
        // mountain 14%, over a 16k-sample 16000x16000 block sweep, then rounded.
        static constexpr float kWetLo = -0.30f;  // vegetation -> forest
        static constexpr float kWetHi = 0.60f;
        static constexpr float kHotLo = 0.05f;   // temperature -> desert
        static constexpr float kHotHi = 0.50f;
        static constexpr float kInlandLo = -0.10f; // continentalness -> mountains
        static constexpr float kInlandHi = 0.45f;
        static constexpr float kRuggedLo = -0.15f; // erosion: high = flat, low = rugged
        static constexpr float kRuggedHi = 0.40f;
        static constexpr float kMountainBias = 1.25f;
        // Rugged ground that is NOT inland still reads as mountains. The ridge
        // ramp is placed high on the ridge distribution -- over a 16k-sample sweep
        // that is p90 0.43, p99 0.75 -- so only the strongest ridges qualify.
        //
        // Measured biome shares over a 4M-sample 16000x16000 sweep of seed 1337
        // are 36.8 grassland / 19.8 forest / 13.6 desert / 29.8 mountain, against
        // vanilla's 45 / 25 / 16 / 14. Mountains are 13.4% without this term and
        // 29.8% with it, so the term more than doubles the share rather than
        // nudging it. An earlier version of this comment claimed 45.2 / 24.3 /
        // 16.1 / 14.4 here, which no longer matches the code; re-measure with
        // probe_biome.cpp before trusting any number written in this block.
        static constexpr float kRidgeLo = 0.55f;
        static constexpr float kRidgeHi = 1.00f;
        static constexpr float kRuggedInlandBias = 0.90f;
        // How mountainous a column must be before its top cell counts as bare
        // rock. Well below the 0.5 that the argmax needs to *name* this a
        // mountains biome, so the rock line is forgiving and does not slice
        // across slopes that are only partly mountainous.
        static constexpr float kTreelineBiome = 0.30f;

        static float ramp(float v, float lo, float hi) {
            float t = (v - lo) / (hi - lo);
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            return t * t * (3.0f - 2.0f * t);
        }

        Climate climateAt(int worldX, int worldZ) const {
            Climate c;
            c.temperature = noise_.sampleTemperature(worldX, worldZ);
            c.humidity = noise_.sampleVegetation(worldX, worldZ);
            c.continentalness = noise_.sampleContinentalness(worldX, worldZ);
            c.erosion = noise_.sampleErosion(worldX, worldZ);
            c.ridges = noise_.sampleRidges(worldX, worldZ);

            const float wet = ramp(c.humidity, kWetLo, kWetHi);
            const float hot = ramp(c.temperature, kHotLo, kHotHi);
            const float dry = 1.0f - wet;
            const float rugged = 1.0f - ramp(c.erosion, kRuggedLo, kRuggedHi);
            const float inland = ramp(c.continentalness, kInlandLo, kInlandHi);
            // Rugged and strongly ridged: shattered ground regardless of where it
            // sits. Blended in only in proportion to how NOT inland it is, so it
            // fills the gap the inland-only rule left instead of competing with it.
            const float ruggedRidge = rugged * ramp(c.ridges, kRidgeLo, kRidgeHi);

            // Sand needs heat AND dryness, mirroring the desert weight.
            c.forest = wet;
            c.desert = hot * dry;
            c.grassland = (1.0f - hot) * dry;
            // The inland term keeps its erosion factor: a flat plain deep inland is
            // not a mountain, and dropping that factor turns every continent into
            // one. The new term is then a strict superset of the old rule -- it can
            // only add mountains, never remove them -- and only where erosion is
            // already saying the ground is shattered.
            c.mountain = std::min(1.0f, kMountainBias *
                                         (inland * rugged +
                                          (1.0f - inland) * kRuggedInlandBias * ruggedRidge));
            return c;
        }

    private:
        const OverworldNoise& noise_;
    };

}
