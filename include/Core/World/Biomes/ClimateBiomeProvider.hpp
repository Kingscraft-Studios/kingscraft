#pragma once

#include "Core/World/Biomes/Biome.hpp"
#include "Core/World/Biomes/Biomes.hpp"
#include "Core/World/Biomes/BiomeProvider.hpp"
#include "Core/World/Generation/HomelandsNoise.hpp"

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
    // getBiome() resolves it to the argmax. That argmax is authoritative: it
    // selects the surface material via Biome::resolveSurface and is what the
    // player-position query reports. Nothing downstream may re-derive a biome
    // from the climate fields independently, or the label and the material it
    // paints can disagree.
    //
    // MOUNTAINS is decided by the column's own height, not by the climate fields
    // alone, and that is a correction rather than a preference. The climate route
    // could not work: the erosion and continentalness ramps below saturate over
    // much of the map, so 99% of all columns standing above Y=100 scored a
    // mountain weight of exactly zero and were therefore mathematically unable to
    // be named mountains. A 200x200-block massif ended up labelled grasslands.
    // Retuning the erosion ramp was measured and does not fix it -- the tallest
    // grasslands column stays at Y=141, because relief (not erosion) is what
    // decides height at that scale. So the height term below is the signal that
    // actually matches the terrain, and the climate terms are kept only as a floor
    // so a rugged inland basin is still called mountains below the height band.
    class ClimateBiomeProvider final : public BiomeProvider {
    public:
        explicit ClimateBiomeProvider(const HomelandsNoise& noise) : noise_(noise) {}

        // Climate-only resolution. Cannot return OCEAN and cannot use the height
        // term, so it is only correct for queries that have no column to measure.
        static RegistryKey<Biome> biomeFor(const kc::Climate& c) {
            return biomeFor(c, unknownSurfaceY, unknownSurfaceY);
        }

        // Authoritative resolution.
        //
        // `seaLevel` is only compared when `surfaceY` is known; passing
        // unknownSurfaceY for both gives the climate-only answer. A submerged
        // column is OCEAN before any weight is considered, because "underwater" is
        // a statement about height that no combination of the four climate weights
        // can express.
        static RegistryKey<Biome> biomeFor(const kc::Climate& c, int surfaceY, int seaLevel) {
            if (surfaceY != unknownSurfaceY && surfaceY < seaLevel) return Biomes::OCEAN;
            if (c.mountain >= std::max({c.desert, c.grassland, c.forest})) return Biomes::MOUNTAINS;
            if (c.forest >= c.grassland && c.forest >= c.desert) return Biomes::FOREST;
            if (c.grassland >= c.desert) return Biomes::GRASSLANDS;
            return Biomes::DESERT;
        }

        RegistryKey<Biome> getBiome(int worldX, int worldZ) const override {
            return biomeFor(climateAt(worldX, worldZ));
        }

        RegistryKey<Biome> getBiomeForColumn(int worldX, int worldZ, int surfaceY,
                                            int seaLevel) const override {
            return biomeFor(climateAt(worldX, worldZ, surfaceY), surfaceY, seaLevel);
        }

        void applySettings(const TerrainGenSettings&) override {
            // The climate comes from the HomelandsNoise the generator owns, which
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
            return climateWeightsFrom(climateAt(worldX, worldZ));
        }

        // Ramp thresholds, in raw NormalNoise output units.
        //
        // A ramp is only as good as its two endpoints: `lo` is the noise value
        // that must mean "0" and `hi` the one that must mean "1", and `ramp()`
        // clamps outside that window. So the endpoints have to be placed against
        // the distribution the noise actually produces, not against the declared
        // range in overworld.json (which is [-2, 2] for every climate noise).
        //
        // Measured over 718848 columns spanning X[-1056,-225] Z[848,1711] of the
        // seed-1337 world:
        //
        //   field      p5      p50     p95      min     max
        //   temperature 0.095  0.264   0.362   -       0.373
        //   humidity   -0.404 -0.118   0.280   -0.519  0.474
        //   continentalness -0.038 0.462 0.787   -       -
        //   erosion      0.084  0.358   0.496   -       0.516
        //   ridges      -0.389  0.093   0.634   -       0.827
        //
        // wetLo/Hi and hotLo/Hi are fitted to bracket those humidity and
        // temperature ranges. The previous pair (wet [-0.30,0.60], hot
        // [0.05,0.50]) put the far end of both ramps outside the data, so `wet`
        // and `hot` could never reach 1.0 and forest was pinned near 16% instead
        // of its 25% target. Fitted by coordinate descent against the four land
        // shares over that column set.
        static constexpr float wetLo = -0.34f;  // vegetation -> forest
        static constexpr float wetHi = 0.51f;
        static constexpr float hotLo = 0.14f;   // temperature -> desert
        static constexpr float hotHi = 0.42f;
        // These two still saturate -- ruggedHi pins 35% of the map to "flat" and
        // inlandHi pins 57% to "fully inland" -- and that is left in place on
        // purpose. They only ever contribute the floor of the mountain weight now
        // (see climateAt), so their saturation no longer decides any label; they
        // are what still recognises a rugged inland basin that sits below the
        // height band. Widening them would change the mountain share without
        // changing which columns are tall, which is the opposite of what is wanted.
        static constexpr float inlandLo = -0.10f; // continentalness -> mountains
        static constexpr float inlandHi = 0.45f;
        static constexpr float ruggedLo = -0.15f; // erosion: high = flat, low = rugged
        static constexpr float ruggedHi = 0.40f;
        static constexpr float mountainBias = 1.25f;
        // Rugged ground that is NOT inland still reads as mountains. The ridge
        // ramp is placed high on the ridge distribution -- over a 16k-sample sweep
        // that is p90 0.43, p99 0.75 -- so only the strongest ridges qualify.
        // Measured biome shares over broad sweeps vary with the measurement
        // model; treat numbers in comments here as informational, not invariant
        // guarantees.
        static constexpr float ridgeLo = 0.55f;
        static constexpr float ridgeHi = 1.00f;
        static constexpr float ruggedInlandBias = 0.90f;

        // The height band that turns a column into mountains, in world Y.
        //
        // This is the term that carries the label. Both endpoints sit inside the
        // range the terrain actually reaches (plains ~63-80, this region's peaks
        // to Y=141), and the 26-block width is deliberate: a narrow band would
        // draw a dead-straight biome line across every slope, which is the same
        // class of artefact the old Y-based rock cutoff had.
        //
        // The band tops out at 105 rather than at the local maximum so ground
        // above it stops being "the mountains" and becomes alpine; vanilla draws
        // the equivalent line with its windswept/stony biome family instead of one
        // class. Over the measured column set this leaves 99.9% of everything
        // above Y=100 named mountains and caps the tallest grasslands column at
        // Y=100. Checked on an unrelated region (3000,3000): 100% and Y=93.
        static constexpr float mountainYLo = 79.0f;
        static constexpr float mountainYHi = 105.0f;
        // How mountainous a column must be before its top cell counts as bare
        // rock. Well below the 0.5 that the argmax needs to *name* this a
        // mountains biome, so the rock line is forgiving and does not slice
        // across slopes that are only partly mountainous.
        static constexpr float treelineBiome = 0.30f;

        static float ramp(float v, float lo, float hi) {
            float t = (v - lo) / (hi - lo);
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            return t * t * (3.0f - 2.0f * t);
        }

        kc::Climate climateAt(int worldX, int worldZ, int surfaceY = unknownSurfaceY) const {
            kc::Climate c;
            c.temperature = noise_.sampleTemperature(worldX, worldZ);
            c.humidity = noise_.sampleVegetation(worldX, worldZ);
            c.continentalness = noise_.sampleContinentalness(worldX, worldZ);
            c.erosion = noise_.sampleErosion(worldX, worldZ);
            c.ridges = noise_.sampleRidges(worldX, worldZ);

            const float wet = ramp(c.humidity, wetLo, wetHi);
            const float hot = ramp(c.temperature, hotLo, hotHi);
            const float dry = 1.0f - wet;
            const float rugged = 1.0f - ramp(c.erosion, ruggedLo, ruggedHi);
            const float inland = ramp(c.continentalness, inlandLo, inlandHi);
            // Rugged and strongly ridged: shattered ground regardless of where it
            // sits. Blended in only in proportion to how NOT inland it is, so it
            // fills the gap the inland-only rule left instead of competing with it.
            const float ruggedRidge = rugged * ramp(c.ridges, ridgeLo, ridgeHi);

            // Sand needs heat AND dryness, mirroring the desert weight.
            c.forest = wet;
            c.desert = hot * dry;
            c.grassland = (1.0f - hot) * dry;
            // Climate floor: an inland basin that erosion calls rugged reads as
            // mountains even where the ground is low. Kept, but no longer decisive
            // on its own -- the height term below is what actually tracks the
            // terrain, so a saturated erosion ramp can no longer mislabel a massif.
            float mountain = mountainBias *
                             (inland * rugged +
                              (1.0f - inland) * ruggedInlandBias * ruggedRidge);
            if (surfaceY != unknownSurfaceY) {
                mountain += ramp(static_cast<float>(surfaceY), mountainYLo, mountainYHi);
            }
            c.mountain = std::min(1.0f, mountain);
            return c;
        }

        private:
        const HomelandsNoise& noise_;
    };

}
