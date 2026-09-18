#pragma once

#include "Core/Blocks/Blocks.hpp"
#include "Core/World/ITerrainGenerator.hpp"
#include "Core/World/TerrainGenSettings.hpp"
#include "FastNoiseLite.h"
#include <glm/glm.hpp>

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

        float getHeight(float x, float z) const {
            return noise_.GetNoise(x, z) * settings_.amplitude;
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
        DefaultTerrainGenerator(const TerrainGenSettings& settings) : noise_(settings), settings_(settings) {}

        // Re-seed after an async world.kcw load resolves.
        void applySettings(const TerrainGenSettings& settings) override {
            noise_.setSettings(settings);
            settings_ = settings;
        }

        std::vector<uint64_t> generateBlocks(
            int gridX, int gridZ, int chunkSize, int height) override
        {
            auto& cfg = settings_;

            std::vector<uint64_t> blockIds(static_cast<size_t>(chunkSize) * height * chunkSize, 0);

            float originX = static_cast<float>(gridX) * chunkSize;
            float originZ = static_cast<float>(gridZ) * chunkSize;

            for (int z = 0; z < chunkSize; ++z) {
                for (int x = 0; x < chunkSize; ++x) {
                    float wx = originX + x;
                    float wz = originZ + z;
                    float surface = cfg.baseHeight + noise_.getHeight(wx, wz);
                    int top = std::max(0, std::min(height - 1, static_cast<int>(surface)));

                    int y = top;
                    for (auto& layer : cfg.layers) {
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
        TerrainGenerator noise_;
        TerrainGenSettings settings_;
    };

} // namespace kc
