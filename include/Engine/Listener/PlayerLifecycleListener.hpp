#pragma once

#include "Event/Events/PlayerDeathEvent.hpp"
#include "Event/Events/PlayerRespawnEvent.hpp"
#include "Event/KCEventHandler.hpp"
#include "Event/Listener.hpp"
#include "Event/detail/EventRegistrar.hpp"

namespace kc {

    // Drives the death/respawn UI transition: this listener is the GameLogic
    // side of "something happened to the player", while the actual cursor +
    // overlay toggling lives on the WorldScreen (reached via the live screen).
    class PlayerLifecycleListener : public Listener {
    public:
        // Must be public: EventManager's generated dispatch lambda calls the
        // kcOnEvent helper from outside the class.
        KC_EVENT_HANDLER(PlayerDeathEvent, onPlayerDeath, ThreadName::GameLogic)
        KC_EVENT_HANDLER(PlayerRespawnEvent, onPlayerRespawn, ThreadName::GameLogic)

    private:
        void onPlayerDeath(PlayerDeathEvent& event);
        void onPlayerRespawn(PlayerRespawnEvent& event);
    };

}