#include "Core/World/Biomes/Biome.hpp"

#include "Core/Blocks/Blocks.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace kc {

SurfaceLayers Biome::resolveSurface(
    const BiomeChunkContext& chunk,
    int worldX, int worldZ, int worldTopY,
    float dither, const Climate& climate,
    const ClimateWeights& weights) const {

    if (useBareRock(worldTopY, dither, climate)) {
        return {Blocks::STONE, Blocks::STONE, 1};
    }

    // Default to stone rather than air: a biome that ships no layers should come
    // out as bare rock, not punch a hole in the world.
    SurfaceLayers out{Blocks::STONE, Blocks::STONE, 1};
    if (!layers.empty()) {
        out.topBlock = layers[0].block;
        if (layers.size() > 1) {
            out.soilBlock = layers[1].block;
            const int declared = layers[1].depth;
            const int noisy = surfaceStoneDepth(dither);
            out.soilDepth = declared > 0 ? std::min(noisy, declared) : noisy;
        }
    }
    return out;
}

void Biome::postGenerateColumn(const BiomeChunkContext& chunk, int localX, int localZ, int worldX, int worldZ, int topLocalY, int worldTopY, float dither,
                                const Climate& climate, const ClimateWeights& weights, std::vector<uint64_t>& blocks) const {

    const int chunkSize = chunk.chunkSize;

    if (topLocalY < 0) {
        return;
    }

    const SurfaceLayers surf = resolveSurface(chunk, worldX, worldZ, worldTopY, dither, climate, weights);
    // The one place a key becomes a cell value. Everything above this line deals
    // in keys, so nothing upstream can hold an id the registry no longer owns.
    const uint64_t topBlock = surf.topBlock.getEncoded();
    const uint64_t soilBlock = surf.soilBlock.getEncoded();
    const int soil = surf.soilDepth;
    const uint64_t stone = Blocks::STONE.getEncoded();

    for (int y = 0; y <= topLocalY; ++y) {
        uint64_t block = stone;
        if (y == topLocalY) {
            block = topBlock;
        } else if (y > topLocalY - 1 - soil) {
            block = soilBlock;
        }
        blocks[(static_cast<size_t>(y) * chunkSize + localZ) * chunkSize + localX] = block;
    }
}

} // namespace kc