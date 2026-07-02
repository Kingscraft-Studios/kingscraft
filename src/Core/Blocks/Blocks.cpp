#include "Core/Blocks/Blocks.hpp"

#include "Core/Blocks/AirBlock.hpp"
#include "Core/Blocks/GrassBlock.hpp"
#include "Core/Blocks/StoneBlock.hpp"
#include "Core/Blocks/DirtBlock.hpp"
#include "Core/Resources/ModelParser.hpp"
#include "Bus/MessageBus.hpp"
#include "Threads/Logger.hpp"
#include "Util/LogUtils.hpp"

namespace lve {

    void Blocks::registerBlocks() {
        auto mailbox = std::make_shared<Mailbox>();
        MessageBus::Get().subscribe(ThreadName::Registration, mailbox);

        auto& r = Registry<Block>::getRegistry();

        r.reg(AIR, std::make_unique<AirBlock>());

        int pending = 0;

        auto onModel = [&](RegistryKey<Block> key, BlockModel model) {
            int quadsTotal = 0;
            for (auto& el : model.getElements())
                quadsTotal += static_cast<int>(el.quads.size());
            int texCount = static_cast<int>(model.getTextures().size());
            int rawBytes = texCount > 0 ? static_cast<int>(model.getTextures()[0].rawData.size()) : 0;

            auto msg = "Model loaded: " + std::to_string(model.getElements().size())
                + " elements, " + std::to_string(quadsTotal)
                + " quads, " + std::to_string(texCount)
                + " textures (" + std::to_string(rawBytes) + " raw bytes)";
            LogUtils::info(ThreadName::Registration, msg);

            if (key == GRASS_BLOCK)
                r.reg(GRASS_BLOCK, std::make_unique<GrassBlock>(std::move(model)));
            else if (key == STONE)
                r.reg(STONE, std::make_unique<StoneBlock>(std::move(model)));
            else if (key == DIRT)
                r.reg(DIRT, std::make_unique<DirtBlock>(std::move(model)));
            pending--;
        };

        ModelParser::loadAsync("resources/models/block/grass_block.json",
            [&onModel](BlockModel model) { onModel(Blocks::GRASS_BLOCK, std::move(model)); });
        pending++;
        ModelParser::loadAsync("resources/models/block/stone_block.json",
            [&onModel](BlockModel model) { onModel(Blocks::STONE, std::move(model)); });
        pending++;
        ModelParser::loadAsync("resources/models/block/dirt_block.json",
            [&onModel](BlockModel model) { onModel(Blocks::DIRT, std::move(model)); });
        pending++;

        while (pending > 0) {
            Message msg;
            if (mailbox->pop_for(msg, std::chrono::milliseconds(100))) {
                if (msg.payload) msg.payload();
            }
        }

        MessageBus::Get().unsubscribe(ThreadName::Registration);
    }

} // namespace lve
