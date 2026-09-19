#pragma once

#include "Core/RegistryKey.hpp"
#include "Core/World/TerrainGenSettings.hpp"
#include "FastNoiseLite.h"
#include <mutex>

namespace kc {

    class Biome;

    // Decides which Biome exists at a world X/Z coordinate. The default
    // implementation maps a low-frequency noise (seeded by the world seed)
    // onto noise bands, giving contiguous regions with hard borders.
    class BiomeProvider {
    public:
        virtual ~BiomeProvider() = default;
        virtual RegistryKey<Biome> getBiome(int worldX, int worldZ) const = 0;
        // Re-seed from an updated settings struct (e.g. a world.kcw load).
        // Default no-op for providers with no seed dependency.
        virtual void applySettings(const TerrainGenSettings&) {}
    };

    // v1 selection: one low-frequency Perlin (FBm, 2 octaves) split into
    // Desert / Grasslands / Forest / Mountains bands. Deterministic per seed;
    // re-seeded via applySettings whenever the world seed resolves.
    class DefaultBiomeProvider final : public BiomeProvider {
    public:
        DefaultBiomeProvider();

        RegistryKey<Biome> getBiome(int worldX, int worldZ) const override;
        void applySettings(const TerrainGenSettings& settings) override;

    private:
        void configure(int seed);
        mutable std::mutex noiseMutex_;
        FastNoiseLite noise_; // band map: which biome region
        FastNoiseLite warp_;  // domain warp: bends region borders naturally
    };

}