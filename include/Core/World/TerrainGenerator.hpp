#pragma once

#include "Core/Blocks/Blocks.hpp"
#include "Core/World/ITerrainGenerator.hpp"
#include "FastNoiseLite.h"
#include <glm/glm.hpp>

namespace lve {

    class TerrainGenerator {
    public:
        TerrainGenerator(int seed = 1337) {
            noise_.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
            noise_.SetFrequency(0.01f);
            noise_.SetFractalType(FastNoiseLite::FractalType_FBm);
            noise_.SetFractalOctaves(4);
            noise_.SetFractalLacunarity(2.0f);
            noise_.SetFractalGain(0.5f);
            noise_.SetSeed(seed);
        }

        float getHeight(float x, float z) const {
            return noise_.GetNoise(x, z) * 10.0f;
        }

        static glm::vec3 getColor(float h) {
            float t = (h + 10.0f) / 20.0f;
            t = glm::clamp(t, 0.0f, 1.0f);

            glm::vec3 low{1.0f, 0.0f, 0.0f};
            glm::vec3 high{0.0f, 0.0f, 1.0f};

            return glm::mix(low, high, t);
        }

    private:
        FastNoiseLite noise_;
    };

    class DefaultTerrainGenerator : public ITerrainGenerator {
    public:
        explicit DefaultTerrainGenerator(int seed = 1337) : noise_(seed) {}

        std::vector<uint8_t> generateBlocks(
            int gridX, int gridZ, int chunkSize, int height) override
        {
            uint8_t grassId = static_cast<uint8_t>(Blocks::GRASS_BLOCK.getId());
            uint8_t dirtId = static_cast<uint8_t>(Blocks::DIRT.getId());
            uint8_t stoneId = static_cast<uint8_t>(Blocks::STONE.getId());

            std::vector<uint8_t> blockIds(static_cast<size_t>(chunkSize) * height * chunkSize, 0);

            float originX = static_cast<float>(gridX) * chunkSize;
            float originZ = static_cast<float>(gridZ) * chunkSize;

            for (int z = 0; z < chunkSize; ++z) {
                for (int x = 0; x < chunkSize; ++x) {
                    float wx = originX + x;
                    float wz = originZ + z;
                    float surface = 8.0f + noise_.getHeight(wx, wz);
                    int top = std::max(0, std::min(height - 1, static_cast<int>(surface)));

                    blockIds[static_cast<size_t>(top) * chunkSize * chunkSize + z * chunkSize + x] = grassId;
                    for (int dy = 1; dy <= 2 && top - dy >= 0; ++dy)
                        blockIds[static_cast<size_t>(top - dy) * chunkSize * chunkSize + z * chunkSize + x] = dirtId;
                    for (int y = top - 3; y >= 0; --y)
                        blockIds[static_cast<size_t>(y) * chunkSize * chunkSize + z * chunkSize + x] = stoneId;
                }
            }

            return blockIds;
        }

    private:
        TerrainGenerator noise_;
    };

} // namespace lve
