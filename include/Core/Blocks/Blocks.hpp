#pragma once

#include "Core/Registry.hpp"
#include "Core/RegistryKey.hpp"
#include "Core/Resources/BlockModel.hpp"
#include "Block.hpp"
#include "Bus/MessageBus.hpp"

namespace kc {

class Blocks {
public:
    inline static const RegistryKey<Block> AIR = Registry<Block>::getRegistry().createKey();
    inline static const RegistryKey<Block> GRASS_BLOCK = Registry<Block>::getRegistry().createKey();
    inline static const RegistryKey<Block> STONE = Registry<Block>::getRegistry().createKey();
    inline static const RegistryKey<Block> DIRT = Registry<Block>::getRegistry().createKey();

    static void registerBlocks(int& pending);

private:
    static void logModelInfo(const BlockModel& model);

    template<typename T>
    static void loadBlock(
        const RegistryKey<Block>& key,
        std::string_view path,
        int& pending,
        Registry<Block>& registry) {
        pending++;

        MessageBus::Get().request<BlockModel>(ThreadName::Engine, [path = std::string(path)]() {
            return IO::Get().getBuiltinTemplates().getModelTemplate().load(path);
        }, ThreadName::Registry, [&key, &pending, &registry](BlockModel model) {
            logModelInfo(model);
            registry.add(key, std::make_unique<T>(std::move(model)));
            pending--;
        });
    }
};

} // namespace kc