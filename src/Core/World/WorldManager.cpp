#include "Core/World/WorldManager.hpp"

#include <cmath>
#include <exception>
#include <string>
#include <utility>

#include "Bus/MessageBus.hpp"
#include "Core/Attributes.hpp"
#include "IO/Templates/WorldMetadataTemplate.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Threads/IO.hpp"
#include "Util/LogUtils.hpp"

namespace kc {
    WorldManager::~WorldManager() {
        destroyWorld();
        lifetimeToken.reset();
    }

    bool WorldManager::createWorld(CreateCallback callback) {
        if (world || creating) {
            LogUtils::warn(ThreadName::GameLogic, "WorldManager: world already exists or creation is in progress");
            return false;
        }

        creating = true;

        const std::uint64_t requestId = ++requestGeneration;
        const std::weak_ptr lifetime = lifetimeToken;

        MessageBus::Get().request<WorldMetadataTemplate::LoadOutcome>(ThreadName::Engine, []() {
            return IO::Get().getBuiltinTemplates().getWorldMetadataTemplate().load();
        }, ThreadName::GameLogic, [this, lifetime, requestId, callback = std::move(callback)] (WorldMetadataTemplate::LoadOutcome outcome) mutable {

            // Manager may have been destroyed while the request was pending.
            if (lifetime.expired())
                return;

            // Ignore cancelled or outdated creation requests.
            if (requestId != requestGeneration)
                return;

            creating = false;

            bool success = false;

            try {
                WorldMetadata metadata = std::move(outcome.metadata);

                const bool metadataValid =
                    outcome.result == WorldMetadataTemplate::LoadResult::Ok;

                const bool persistMetadata =
                    outcome.result != WorldMetadataTemplate::LoadResult::Invalid;

                // The generator starts with the actual world's settings.
                auto newTerrainGenerator = std::make_unique<DefaultTerrainGenerator>(metadata.settings);

                // Only new or invalid metadata uses the default spawn.
                // Existing worlds retain their saved spawn and player position.
                if (!metadataValid) {
                    constexpr int spawnSearchRadius = 1536;

                    const int originX = static_cast<int>(std::floor(metadata.spawnPos.x));

                    const int originZ = static_cast<int>(std::floor(metadata.spawnPos.z));

                    int landX = 0;
                    int landY = 0;
                    int landZ = 0;

                    if (newTerrainGenerator->findSpawnColumn(originX, originZ, spawnSearchRadius, landX, landY, landZ)) {

                        const glm::vec3 spawnPosition(static_cast<float>(landX) + 0.5f, static_cast<float>(landY) + Attributes::EYE_HEIGHT,
                            static_cast<float>(landZ) + 0.5f);

                        metadata.spawnPos = spawnPosition;
                        metadata.playerPos = spawnPosition;

                        LogUtils::info(ThreadName::GameLogic, "WorldManager: new spawn placed on land at (" + std::to_string(landX) + ", " +
                                std::to_string(landY) + ", " + std::to_string(landZ) + ")");
                    } else {
                        LogUtils::warn(ThreadName::GameLogic, "WorldManager: no dry land found within " +
                                std::to_string(spawnSearchRadius) + " blocks; keeping configured spawn");
                    }
                }

                // Construct the world only after metadata is fully resolved.
                auto newWorld = std::make_unique<World>(*newTerrainGenerator, RendererSettings::get().chunkSize, RendererSettings::get().worldHeight,
                    metadata, persistMetadata, !metadataValid);

                // Publish ownership only after construction succeeds.
                // World must be destroyed before its generator.
                this->terrainGenerator = std::move(newTerrainGenerator);
                this->world = std::move(newWorld);

                LogUtils::info(ThreadName::GameLogic,"WorldManager: world session created");

                success = true;

            } catch (const std::exception& e) {
                LogUtils::error(ThreadName::GameLogic,"WorldManager: failed to create world: " + std::string(e.what()));
            }

            if (callback) {
                callback(success);
            }
        });

        return true;
    }

    void WorldManager::destroyWorld() {
        // Invalidates any pending creation callback.
        ++requestGeneration;
        creating = false;

        if (!world) {
            terrainGenerator.reset();
            return;
        }

        // World destructor stops and joins its workers and stages its saves.
        world.reset();

        // World stores a reference to this generator.
        terrainGenerator.reset();

        LogUtils::info(ThreadName::GameLogic, "WorldManager: world session destroyed");
    }
}
