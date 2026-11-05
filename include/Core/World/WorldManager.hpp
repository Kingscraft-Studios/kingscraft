#pragma once
#include <cstdint>
#include <functional>
#include <memory>

#include "TerrainGenerator.hpp"
#include "World.hpp"

namespace kc {

    class WorldManager {
    public:
        using CreateCallback = std::function<void(bool)>;

        WorldManager() = default;
        ~WorldManager();

        WorldManager(const WorldManager&) = delete;
        WorldManager& operator=(const WorldManager&) = delete;

        // Starts asynchronous world creation.
        // Returns false if a world exists or creation is already in progress.
        bool createWorld(CreateCallback callback = {});

        // Cancels pending creation and destroys the active world if any.
        void destroyWorld();

        bool hasWorld() const { return static_cast<bool>(world); }
        bool isCreating() const { return creating; }

        World* getWorld() { return world.get(); }
        const World* getWorld() const { return world.get(); }

        DefaultTerrainGenerator* getTerrainGenerator() {
            return terrainGenerator.get();
        }
    private:
        std::unique_ptr<DefaultTerrainGenerator> terrainGenerator;
        std::unique_ptr<World> world;

        bool creating = false;
        std::uint64_t requestGeneration = 0;

        // Prevents a pending MessageBus callback from accessing a destroyed manager.
        std::shared_ptr<void> lifetimeToken = std::make_shared<int>(0);
    };
}
