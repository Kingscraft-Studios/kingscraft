#include "Core/World/Biomes/BiomeProvider.hpp"

#include "Core/World/Biomes/Biomes.hpp"

namespace kc {
    DefaultBiomeProvider::DefaultBiomeProvider() {
        configure(TerrainGenSettings{}.seed); // matches the world's default seed
    }

    void DefaultBiomeProvider::configure(int seed) {
        std::lock_guard<std::mutex> lock(noiseMutex_);
        // Band map: clean, large regions (Minecraft-style continents).
        noise_.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
        noise_.SetFrequency(0.004f);
        noise_.SetFractalType(FastNoiseLite::FractalType_FBm);
        noise_.SetFractalOctaves(2);
        noise_.SetFractalLacunarity(2.0f);
        noise_.SetFractalGain(0.5f);
        noise_.SetSeed(seed);
        // Domain warp: bends the coordinate space so borders weave and snake
        // instead of forming round blobs. Own seed keeps it distinct.
        warp_.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2);
        warp_.SetFrequency(0.006f);
        warp_.SetDomainWarpAmp(200.0f);
        warp_.SetSeed(seed + 7777);
    }

    void DefaultBiomeProvider::applySettings(const TerrainGenSettings& settings) {
        configure(settings.seed);
    }

    RegistryKey<Biome> DefaultBiomeProvider::getBiome(int worldX, int worldZ) const {
        float v;
        {
            std::lock_guard<std::mutex> lock(noiseMutex_);
            float jx = static_cast<float>(worldX);
            float jz = static_cast<float>(worldZ);
            warp_.DomainWarp(jx, jz);
            v = noise_.GetNoise(jx, jz);
        }

        if (v < -0.35f) return Biomes::DESERT;
        if (v < 0.05f) return Biomes::GRASSLANDS;
        if (v < 0.40f) return Biomes::FOREST;
        return Biomes::MOUNTAINS;
    }
}