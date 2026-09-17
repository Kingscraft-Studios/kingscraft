#include "Listener/PlayerBlockInteractionListener.hpp"

#include "Core/Blocks/Blocks.hpp"
#include "Core/Runtime.hpp"
#include "Core/World/Physics/CollisionSystem.hpp"
#include "Threads/Kingscraft.hpp"

namespace kc {

    void PlayerBlockInteractionListener::onBlockBreak(BlockBreakEvent& event) {
        World& world = Runtime::get().kingscraft->getWorld();
        world.setBlock(event.getX(), event.getY(), event.getZ(), Blocks::AIR);
        world.remeshDirtyChunks();
    }

    void PlayerBlockInteractionListener::onBlockPlace(BlockPlaceEvent& event) {
        World& world = Runtime::get().kingscraft->getWorld();

        const int x = event.getX();
        const int y = event.getY();
        const int z = event.getZ();
        if (y < 0 || y >= world.getHeight()) return;

        const RegistryKey<Block>& key = event.getBlock();
        if (!key) return;
        if (key == Blocks::AIR) return;

        const Block& block = key;
        if (world.getPlayerController().getBodyAABB().overlaps(
                CollisionSystem::blockAABBAt(block, x, y, z))) {
            return;
        }

        world.setBlock(x, y, z, block);
        world.remeshDirtyChunks();
    }

}