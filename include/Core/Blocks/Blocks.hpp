#pragma once

#include "Core/Registry.hpp"
#include "Core/RegistryKey.hpp"
#include "Block.hpp"

namespace lve {

class Blocks {
public:
    inline static const RegistryKey<Block> AIR = Registry<Block>::getRegistry().createKey();
    inline static const RegistryKey<Block> GRASS_BLOCK = Registry<Block>::getRegistry().createKey();
    inline static const RegistryKey<Block> STONE = Registry<Block>::getRegistry().createKey();
    inline static const RegistryKey<Block> DIRT = Registry<Block>::getRegistry().createKey();

    static void registerBlocks();
};

} // namespace lve
