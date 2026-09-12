#include "Listener/WorldRegistryReloadListener.hpp"

#include "Core/Runtime.hpp"
#include "Core/World/WorldScreen.hpp"
#include "Threads/Kingscraft.hpp"

namespace kc {

    void WorldRegistryReloadListener::onRegistryReloadPost(RegistryReloadPostEvent&) {
        // Fired on the GameLogic thread (the renderer sent the message that
        // fired this event), after renderer-side offsets are patched — safe to
        // remesh against the fresh block textures.
        Runtime::get().kingscraft->getWorld().remeshAllChunks();

        if (auto* worldScreen = dynamic_cast<WorldScreen*>(
                Runtime::get().kingscraft->getScreenManager().getCurrent())) {
            worldScreen->refreshHotbar();
        }
    }

}