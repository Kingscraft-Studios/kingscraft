#include "Core/World/Biomes/BiomeProvider.hpp"

#include "Core/World/Biomes/Biomes.hpp"
#include <algorithm>

namespace kc {

    static float clamp01_(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    void DefaultBiomeProvider::Field::configure(
        float freq, int octaves, float gain, int seed,
        float warpFreq, float warpAmp, int warpSeed) {
        noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
        noise.SetFrequency(freq);
        noise.SetFractalType(FastNoiseLite::FractalType_FBm);
        noise.SetFractalOctaves(octaves);
        noise.SetFractalLacunarity(2.0f);
        noise.SetFractalGain(gain);
        noise.SetSeed(seed);

        warp.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2);
        warp.SetFrequency(warpFreq);
        warp.SetDomainWarpAmp(warpAmp);
        warp.SetSeed(warpSeed);
    }

    float DefaultBiomeProvider::Field::sample(float wx, float wz) const {
        warp.DomainWarp(wx, wz);
        float v = clamp01_((noise.GetNoise(wx, wz) + 1.0f) * 0.5f);
        return clamp01_(0.5f + (v - 0.5f) * FIELD_STRETCH);
    }

    float DefaultBiomeProvider::ramp(float v, float lo, float hi) {
        float t = (v - lo) / (hi - lo);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return t * t * (3.0f - 2.0f * t);
    }

    DefaultBiomeProvider::DefaultBiomeProvider() {
        configure(TerrainGenSettings{}.seed); // matches the world's default seed
    }

    void DefaultBiomeProvider::configure(int seed) {
        std::lock_guard<std::mutex> lock(noiseMutex_);
        // Continent-scale fields, each on its own seed offset and warp so the
        // four axes evolve independently.
        temperature_.configure(0.0022f, 3, 0.5f, seed + 11, 0.0035f, 180.0f, seed + 911);
        humidity_.configure(0.0026f, 3, 0.5f, seed + 22, 0.0040f, 180.0f, seed + 922);
        continentalness_.configure(0.0025f, 3, 0.5f, seed + 33, 0.0045f, 220.0f, seed + 933);
        erosion_.configure(0.0035f, 4, 0.5f, seed + 44, 0.0040f, 180.0f, seed + 944);
    }

    void DefaultBiomeProvider::applySettings(const TerrainGenSettings& settings) {
        configure(settings.seed);
    }

    DefaultBiomeProvider::ClimateSample DefaultBiomeProvider::sampleFields(int worldX, int worldZ) const {
        std::lock_guard<std::mutex> lock(noiseMutex_);
        const float x = static_cast<float>(worldX);
        const float z = static_cast<float>(worldZ);
        return { temperature_.sample(x, z), humidity_.sample(x, z),
                 continentalness_.sample(x, z), erosion_.sample(x, z) };
    }

    float DefaultBiomeProvider::getTemperature(int worldX, int worldZ) const {
        return sampleFields(worldX, worldZ).temperature;
    }

    float DefaultBiomeProvider::getHumidity(int worldX, int worldZ) const {
        return sampleFields(worldX, worldZ).humidity;
    }

    float DefaultBiomeProvider::getContinentalness(int worldX, int worldZ) const {
        return sampleFields(worldX, worldZ).continentalness;
    }

    float DefaultBiomeProvider::getErosion(int worldX, int worldZ) const {
        return sampleFields(worldX, worldZ).erosion;
    }

    ClimateWeights DefaultBiomeProvider::getClimateWeights(int worldX, int worldZ) const {
        const ClimateSample s = sampleFields(worldX, worldZ);

        const float wet = ramp(s.humidity, RAMP_WET_LO, RAMP_WET_HI);
        const float hot = ramp(s.temperature, RAMP_HOT_LO, RAMP_HOT_HI);
        const float dry = 1.0f - wet;

        const float rugged = 1.0f - ramp(s.erosion, RAMP_RUGGED_LO, RAMP_RUGGED_HI);
        const float inland = ramp(s.continentalness, RAMP_INLAND_LO, RAMP_INLAND_HI);

        ClimateWeights w;
        w.forest = wet;
        w.desert = hot * dry;
        w.grassland = (1.0f - hot) * dry;
        w.mountain = std::min(1.0f, MOUNTAIN_BIAS * inland * rugged);
        return w;
    }

    RegistryKey<Biome> DefaultBiomeProvider::getBiome(int worldX, int worldZ) const {
        const ClimateWeights w = getClimateWeights(worldX, worldZ);

        const float maxClimate = std::max(std::max(w.desert, w.grassland), w.forest);
        if (w.mountain >= maxClimate) return Biomes::MOUNTAINS;
        if (w.forest >= w.grassland && w.forest >= w.desert) return Biomes::FOREST;
        if (w.grassland >= w.desert) return Biomes::GRASSLANDS;
        return Biomes::DESERT;
    }

} // namespace kc