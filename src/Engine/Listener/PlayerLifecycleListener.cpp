#include "Listener/PlayerLifecycleListener.hpp"

#include "Core/Runtime.hpp"
#include "Core/World/WorldScreen.hpp"
#include "Threads/Kingscraft.hpp"

namespace kc {

    void PlayerLifecycleListener::onPlayerDeath(PlayerDeathEvent& event) {
        (void)event;
        // Runs on the GameLogic thread; the WorldScreen is always active while
        // the player can die.
        if (auto* worldScreen = dynamic_cast<WorldScreen*>(
                Runtime::get().kingscraft->getScreenManager().getCurrent())) {
            worldScreen->onPlayerDeath();
        }
    }

    void PlayerLifecycleListener::onPlayerRespawn(PlayerRespawnEvent& event) {
        (void)event;
        if (auto* worldScreen = dynamic_cast<WorldScreen*>(
                Runtime::get().kingscraft->getScreenManager().getCurrent())) {
            worldScreen->onPlayerRespawn();
        }
    }

}