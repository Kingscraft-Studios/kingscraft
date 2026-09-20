#pragma once

#include "Core/Blocks/Blocks.hpp"
#include "Core/Registry.hpp"
#include "Core/World/Biomes/Biome.hpp"
#include "Core/World/Biomes/BiomeProvider.hpp"
#include "Core/World/Biomes/Biomes.hpp"
#include "Core/World/ITerrainGenerator.hpp"
#include "Core/World/TerrainGenSettings.hpp"
#include "FastNoiseLite.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace kc {

    class TerrainGenerator {
    public:
        TerrainGenerator(const TerrainGenSettings& settings) : settings_(settings) {
            applySettings(settings_);
        }

        // Re-seed and re-tune the underlying noise from an updated settings
        // struct (e.g. a world.kcw load that resolves after construction).
        void setSettings(const TerrainGenSettings& settings) {
            settings_ = settings;
            applySettings(settings_);
        }

        // Raw noise scaled by the caller-supplied amplitude (per-biome).
        float getHeight(float x, float z, float amplitude) const {
            return noise_.GetNoise(x, z) * amplitude;
        }

        static glm::vec3 getColor(float h) {
            float t = (h + 10.0f) / 20.0f;
            t = glm::clamp(t, 0.0f, 1.0f);

            glm::vec3 low{1.0f, 0.0f, 0.0f};
            glm::vec3 high{0.0f, 0.0f, 1.0f};

            return glm::mix(low, high, t);
        }

    private:
        void applySettings(const TerrainGenSettings& settings) {
            noise_.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
            noise_.SetFrequency(settings.frequency);
            noise_.SetFractalType(FastNoiseLite::FractalType_FBm);
            noise_.SetFractalOctaves(settings.octaves);
            noise_.SetFractalLacunarity(settings.lacunarity);
            noise_.SetFractalGain(settings.gain);
            noise_.SetSeed(settings.seed);
        }

        FastNoiseLite noise_;
        TerrainGenSettings settings_;
    };

    class DefaultTerrainGenerator : public ITerrainGenerator {
    public:
        // Height budget: y = 0 is the void floor and y = SEA_LEVEL is the
        // future sea level (no water block yet). Land sits above sea level;
        // low valley floors dip just below it so later oceans/rivers have
        // cells to fill.
        static constexpr int SEA_LEVEL = 32;

        DefaultTerrainGenerator(const TerrainGenSettings& settings, BiomeProvider& provider)
            : noise_(settings), settings_(settings), biomeProvider_(provider) {}

        // Re-seed after an async world.kcw load resolves. The provider shares
        // the world seed so biome selection stays in sync; cached per-biome
        // noises are invalidated (they embed the old seed).
        void applySettings(const TerrainGenSettings& settings) override {
            noise_.setSettings(settings);
            settings_ = settings;
            biomeProvider_.applySettings(settings);
            std::lock_guard<std::mutex> lock(biomeNoisesMutex_);
            biomeNoises_.clear();
        }

        std::vector<uint64_t> generateBlocks(
            int gridX, int gridZ, int chunkSize, int height) override
        {
            std::vector<uint64_t> blockIds(static_cast<size_t>(chunkSize) * height * chunkSize, 0);

            for (int z = 0; z < chunkSize; ++z) {
                for (int x = 0; x < chunkSize; ++x) {
                    int wx = gridX * chunkSize + x;
                    int wz = gridZ * chunkSize + z;

                    // Falls back to the world default when the lookup fails
                    // (e.g. the biome registry is mid-reload).
                    auto biome = biomeProvider_.getBiome(wx, wz);
                    const BiomeTerrainSettings& bts = biome ? biome->getTerrainSettings() : worldTerrain_;

                    float surface;
                    if (biome) {
                        surface = sampleBiomeHeight(biome.getEncoded(), bts, wx, wz);
                    } else {
                        surface = bts.baseHeight + noise_.getHeight(static_cast<float>(wx), static_cast<float>(wz), bts.amplitude * worldTerrain_.noise.noiseScale);
                    }
                    int top = std::max(0, std::min(height - 1, static_cast<int>(surface)));
                    int y = top;
                    const auto& layers = bts.layers;
                    for (const auto& layer : layers) {
                        // Block may have been removed from the registry by a
                        // reload; treat it as air instead of dereferencing null.
                        if (!layer.block) continue;
                        uint64_t blockId = static_cast<uint64_t>(layer.block->getEncodedId());
                        if (layer.depth > 0) {
                            for (int dy = 0; dy < layer.depth && y - dy >= 0; ++dy)
                                blockIds[static_cast<size_t>(y - dy) * chunkSize * chunkSize + z * chunkSize + x] = blockId;
                            y -= layer.depth;
                        } else {
                            for (; y >= 0; --y)
                                blockIds[static_cast<size_t>(y) * chunkSize * chunkSize + z * chunkSize + x] = blockId;
                        }
                    }
                }
            }

            return blockIds;
        }

    private:
        // Height for the climate model. The provider's continuous membership
        // weights (functions of temperature/humidity/continentalness/erosion)
        // cross-fade the four biomes' TerrainGenSettings, so adjacent biomes
        // blend seamlessly and no sheer cliff forms. Placement fields never
        // feed height directly -- only the selected biomes' settings do.
        //
        // Valley floors are capped to 20% of the hills' range after the mix, so
        // plains/desert valleys dip only slightly below baseHeight (around sea
        // level) instead of carving deep pits (there is no water block yet to
        // fill them).
        float sampleBiomeHeight(uint64_t /*biomeEncoded*/, const BiomeTerrainSettings& /*bts*/, int wx, int wz) {
            const auto& reg = Registry<Biome>::getRegistry();
            const auto* bD = reg.get(Biomes::DESERT.getEncoded());
            const auto* bG = reg.get(Biomes::GRASSLANDS.getEncoded());
            const auto* bF = reg.get(Biomes::FOREST.getEncoded());
            const auto* bM = reg.get(Biomes::MOUNTAINS.getEncoded());
            if (!bD || !bG || !bF || !bM) { // registry mid-reload: fall back to the default shape
                return worldTerrain_.baseHeight
                     + noise_.getHeight(static_cast<float>(wx), static_cast<float>(wz),
                                        worldTerrain_.amplitude * worldTerrain_.noise.noiseScale);
            }

            const ClimateWeights w = biomeProvider_.getClimateWeights(wx, wz);
            const float x = static_cast<float>(wx);
            const float z = static_cast<float>(wz);

            const auto& sD = bD->getTerrainSettings();
            const auto& sG = bG->getTerrainSettings();
            const auto& sF = bF->getTerrainSettings();
            const auto& sM = bM->getTerrainSettings();

            const float hD = terrainShape(sD, Biomes::DESERT.getEncoded(), x, z);
            const float hG = terrainShape(sG, Biomes::GRASSLANDS.getEncoded(), x, z);
            const float hF = terrainShape(sF, Biomes::FOREST.getEncoded(), x, z);

            const float climateSum =
                std::max(w.desert + w.grassland + w.forest, 1e-6f);
            float climate = (w.desert * hD + w.grassland * hG + w.forest * hF) / climateSum;
            float climateBase =
                (w.desert * sD.baseHeight + w.grassland * sG.baseHeight + w.forest * sF.baseHeight) / climateSum;

            const float hM = terrainShape(sM, Biomes::MOUNTAINS.getEncoded(), x, z);
            float height = glm::mix(climate, hM, w.mountain);
            float base = glm::mix(climateBase, sM.baseHeight, w.mountain);

            float detail = height - base;
            if (detail < 0.0f) height = base + detail * 0.2f; // soften valleys
            return height;
        }

        // One biome's height function: its own noise, scaled by its noiseScale
        // then its amplitude, added to its baseHeight. Mountains use the same
        // rule (shape comes from the biome's TerrainGenSettings, not climate).
        float terrainShape(const BiomeTerrainSettings& s, uint64_t biomeEncoded, float x, float z) const {
            const float d = biomeNoise(biomeEncoded, s).GetNoise(x, z);
            return s.baseHeight + s.amplitude * (d * s.noise.noiseScale);
        }

        // Lazily builds one noise per biome key. The key is the stable FNV-1a
        // hash of the biome name, so entries survive registry reloads and are
        // shared by all chunk threads. Only the map needs synchronising: the
        // noise object itself is safe for concurrent GetNoise reads.
        const FastNoiseLite& biomeNoise(uint64_t biomeEncoded, const BiomeTerrainSettings& bts) const {
            {
                std::lock_guard<std::mutex> lock(biomeNoisesMutex_);
                auto it = biomeNoises_.find(biomeEncoded);
                if (it != biomeNoises_.end())
                    return it->second;
            }

            FastNoiseLite noise;
            noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
            noise.SetFrequency(bts.noise.frequency);
            noise.SetFractalType(FastNoiseLite::FractalType_FBm);
            noise.SetFractalOctaves(bts.noise.octaves);
            noise.SetFractalLacunarity(bts.noise.lacunarity);
            noise.SetFractalGain(bts.noise.gain);
            noise.SetSeed(settings_.seed + bts.noise.seedOffset);

            std::lock_guard<std::mutex> lock(biomeNoisesMutex_);
            return biomeNoises_.try_emplace(biomeEncoded, std::move(noise)).first->second;
        }

        TerrainGenerator noise_;
        TerrainGenSettings settings_;
        BiomeProvider& biomeProvider_;
        mutable std::unordered_map<uint64_t, FastNoiseLite> biomeNoises_;
        mutable std::mutex biomeNoisesMutex_;
        // v1 default terrain (Grasslands matches these values).
        BiomeTerrainSettings worldTerrain_{
            34.0f, 10.0f,
            {0.01f, 4, 2.0f, 0.5f, 0, 3.0f},
            {{ {Blocks::GRASS_BLOCK, 1}, {Blocks::DIRT, 2}, {Blocks::STONE, 0} }}
        };
    };

} // namespace kc