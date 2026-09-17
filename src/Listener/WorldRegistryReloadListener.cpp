#include "Listener/WorldRegistryReloadListener.hpp"

#include "Core/Runtime.hpp"
#include "Core/World/WorldScreen.hpp"
#include "Threads/Kingscraft.hpp"

namespace kc {

    void WorldRegistryReloadListener::onRegistryReload(RegistryReloadEvent&) {
        // Routed to the GameLogic thread by the dispatcher, after the renderer
        // listener (Highest priority) finished rebuilding — safe to remesh
        // against the fresh block textures.
        Runtime::get().kingscraft->getWorld().remeshAllChunks();

        if (auto* worldScreen = dynamic_cast<WorldScreen*>(
                Runtime::get().kingscraft->getScreenManager().getCurrent())) {
            worldScreen->refreshHotbar();
        }
    }

}