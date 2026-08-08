#include "Core/Blocks/Blocks.hpp"

#include "Core/Blocks/AirBlock.hpp"
#include "Core/Blocks/GrassBlock.hpp"
#include "Core/Blocks/StoneBlock.hpp"
#include "Core/Blocks/DirtBlock.hpp"
#include "Threads/Logger.hpp"
#include "Util/LogUtils.hpp"

namespace lve {

    void Blocks::logModelInfo(const BlockModel& model) {
        int quadsTotal = 0;
        for (auto& el : model.getElements())
            quadsTotal += static_cast<int>(el.quads.size());
        int texCount = static_cast<int>(model.getTextures().size());
        int rawBytes = texCount > 0
            ? static_cast<int>(model.getTextures()[0].rawData.size()) : 0;

        auto msg = "Model loaded: "
            + std::to_string(model.getElements().size()) + " elements, "
            + std::to_string(quadsTotal) + " quads, "
            + std::to_string(texCount) + " textures ("
            + std::to_string(rawBytes) + " raw bytes)";
        LogUtils::info(ThreadName::Registration, msg);
    }

    void Blocks::registerBlocks(int& pending) {
        auto& registry = Registry<Block>::getRegistry();

        auto air = std::make_unique<AirBlock>();
        registry.add(AIR, std::move(air));

        loadBlock<GrassBlock>(
            GRASS_BLOCK,
            "resources/models/block/grass_block.json",
            pending,
            registry);

        loadBlock<StoneBlock>(
            STONE,
            "resources/models/block/stone_block.json",
            pending,
            registry);

        loadBlock<DirtBlock>(
            DIRT,
            "resources/models/block/dirt_block.json",
            pending,
            registry);
    }

} // namespace lve